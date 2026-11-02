// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "MouseLook.h"
#include "MouseLookGames.h"

#include "Host.h"
#include "IconsFontAwesome.h"
#include "Input/InputManager.h"
#include "SIO/Pad/Pad.h"
#include "SIO/Pad/PadBase.h"
#include "SIO/Pad/PadDualshock2.h"
#include "VMManager.h"

#include "common/Console.h"
#include "common/SettingsInterface.h"
#include "common/StringUtil.h"

#include "fmt/format.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <optional>
#include <string>
#include <utility>

// All entry points run on the CPU thread: binding reloads, PollSources() (which feeds pointer movement
// and wheel), Qt mouse button events (forwarded with Host::RunOnCPUThread), hotkeys and the per-frame Update().
// Needs proper testing on all hosts.

namespace MouseLook
{
	namespace
	{
		struct Settings
		{
			bool enabled = false;
			int sensitivity = 20;
			bool invert = false;
			bool menu_mode = true;
			MenuButton menu_confirm = MenuButton::Cross;
			MenuButton menu_back = MenuButton::Circle;
		};

		enum class ButtonOwner : u8
		{
			None,
			Bindings, // press went to the normal input bindings, so the release must too
			Menu, // press was turned into a menu confirm/back
		};

		// Menu navigation: each wheel notch becomes a short D-pad press, queued so fast scrolling isn't lost.
		static constexpr u32 MENU_PRESS_FRAMES = 3;
		static constexpr u32 MENU_GAP_FRAMES = 2;
		static constexpr s32 MENU_MAX_QUEUED_STEPS = 6;
	} // namespace

	static const GameDriver* s_driver = nullptr;
	static Settings s_settings;
	static std::atomic_bool s_session_enabled{true}; // flipped by the hotkey
	static float s_accum_dx = 0.0f;
	static float s_accum_dy = 0.0f;
	static Context s_last_context = Context::Busy;

	static s32 s_menu_queued_steps = 0; // negative = up, positive = down
	static s32 s_menu_held_bind = -1;
	static u32 s_menu_frames_left = 0;
	static std::array<ButtonOwner, InputManager::MAX_POINTER_BUTTONS> s_button_owner = {};
	static std::array<u32, InputManager::MAX_POINTER_BUTTONS> s_button_menu_bind = {};

	static bool IsActive()
	{
		return s_driver && s_settings.enabled && s_session_enabled.load(std::memory_order_relaxed);
	}

	static bool CanTouchGame()
	{
		return IsActive() && VMManager::GetState() == VMState::Running && VMManager::Internal::HasBootedELF();
	}

	static bool HasDualShock2()
	{
		const PadBase* pad = Pad::GetPad(0, 0);
		return pad && pad->GetType() == Pad::ControllerType::DualShock2;
	}

	static std::optional<MenuButton> ParseMenuButton(std::string_view str)
	{
		if (StringUtil::compareNoCase(str, "Cross"))
			return MenuButton::Cross;
		if (StringUtil::compareNoCase(str, "Circle"))
			return MenuButton::Circle;
		if (StringUtil::compareNoCase(str, "Triangle"))
			return MenuButton::Triangle;
		if (StringUtil::compareNoCase(str, "Square"))
			return MenuButton::Square;
		return std::nullopt;
	}

	static u32 MenuButtonToBind(MenuButton button)
	{
		switch (button)
		{
			case MenuButton::Circle:
				return PadDualshock2::Inputs::PAD_CIRCLE;
			case MenuButton::Triangle:
				return PadDualshock2::Inputs::PAD_TRIANGLE;
			case MenuButton::Square:
				return PadDualshock2::Inputs::PAD_SQUARE;
			case MenuButton::Cross:
			default:
				return PadDualshock2::Inputs::PAD_CROSS;
		}
	}

	static void ReleaseMenuInputs()
	{
		if (s_menu_held_bind >= 0)
		{
			Pad::SetControllerState(0, static_cast<u32>(s_menu_held_bind), 0.0f);
			s_menu_held_bind = -1;
		}
		s_menu_queued_steps = 0;
		s_menu_frames_left = 0;

		for (u32 i = 0; i < s_button_owner.size(); i++)
		{
			if (s_button_owner[i] == ButtonOwner::Menu)
				Pad::SetControllerState(0, s_button_menu_bind[i], 0.0f);
			s_button_owner[i] = ButtonOwner::None;
		}
	}

	static void UpdateMenuNavigation()
	{
		if (s_menu_frames_left > 0)
		{
			s_menu_frames_left--;
			if (s_menu_frames_left == 0 && s_menu_held_bind >= 0)
			{
				// End of the press: release, then wait a couple of frames so the game sees a clean edge.
				Pad::SetControllerState(0, static_cast<u32>(s_menu_held_bind), 0.0f);
				s_menu_held_bind = -1;
				s_menu_frames_left = MENU_GAP_FRAMES;
			}
			return;
		}

		if (s_menu_queued_steps == 0)
			return;

		const bool up = (s_menu_queued_steps < 0);
		s_menu_held_bind = up ? PadDualshock2::Inputs::PAD_UP : PadDualshock2::Inputs::PAD_DOWN;
		s_menu_queued_steps += up ? 1 : -1;
		s_menu_frames_left = MENU_PRESS_FRAMES;
		Pad::SetControllerState(0, static_cast<u32>(s_menu_held_bind), 1.0f);
	}
} // namespace MouseLook

void MouseLook::LoadSettings(const SettingsInterface& si)
{
	const GameDriver* const old_driver = s_driver;
	const std::string serial = VMManager::HasValidVM() ? VMManager::GetDiscSerial() : std::string();
	s_driver = serial.empty() ? nullptr : FindGameDriver(serial);

	s_settings.enabled = si.GetBoolValue("MouseLook", "Enabled", false);
	s_settings.sensitivity = std::clamp(si.GetIntValue("MouseLook", "Sensitivity", 20), 1, 200);
	s_settings.invert = si.GetBoolValue("MouseLook", "InvertY", false);
	s_settings.menu_mode = si.GetBoolValue("MouseLook", "MenuMode", true);
	s_settings.menu_confirm = s_driver ? s_driver->default_confirm : MenuButton::Cross;
	s_settings.menu_back = s_driver ? s_driver->default_back : MenuButton::Circle;
	if (const std::optional<MenuButton> b = ParseMenuButton(si.GetStringValue("MouseLook", "MenuConfirm", "")))
		s_settings.menu_confirm = b.value();
	if (const std::optional<MenuButton> b = ParseMenuButton(si.GetStringValue("MouseLook", "MenuBack", "")))
		s_settings.menu_back = b.value();

	if (s_driver != old_driver)
	{
		ReleaseMenuInputs();
		s_accum_dx = 0.0f;
		s_accum_dy = 0.0f;
		s_last_context = Context::Busy;
		if (s_driver && s_driver->reset)
			s_driver->reset();

		if (s_driver && s_settings.enabled)
		{
			Console.WriteLn(fmt::format("MouseLook: active for {} ({}), sensitivity {}.", s_driver->name, s_driver->serial,
				s_settings.sensitivity));
			Host::AddIconOSDMessage("MouseLook", ICON_FA_COMPUTER_MOUSE,
				fmt::format("Mouse look active for {}.", s_driver->name), Host::OSD_INFO_DURATION);
		}
	}
}

bool MouseLook::WantsRelativeMouse()
{
	return IsActive() && VMManager::HasValidVM();
}

void MouseLook::AddPointerDelta(u32 axis, float delta)
{
	// Ignore movement while paused so the camera doesn't jump on resume.
	if (!IsActive() || VMManager::GetState() != VMState::Running)
		return;

	if (axis == 0)
		s_accum_dx += delta;
	else if (axis == 1)
		s_accum_dy += delta;
}

bool MouseLook::HandleWheel(float delta)
{
	if (delta == 0.0f || !CanTouchGame() || !s_settings.menu_mode || s_last_context != Context::Menu || !HasDualShock2())
		return false;

	s_menu_queued_steps = std::clamp(s_menu_queued_steps + ((delta > 0.0f) ? -1 : 1), -MENU_MAX_QUEUED_STEPS,
		MENU_MAX_QUEUED_STEPS);
	return true;
}

bool MouseLook::HandlePointerButton(u32 button, float value)
{
	if (button >= s_button_owner.size())
		return false;

	const bool pressed = (value > 0.0f);
	if (!pressed)
	{
		// Releases always go wherever the press went, so nothing gets stuck down.
		const ButtonOwner owner = std::exchange(s_button_owner[button], ButtonOwner::None);
		if (owner == ButtonOwner::Menu)
		{
			Pad::SetControllerState(0, s_button_menu_bind[button], 0.0f);
			return true;
		}
		return false;
	}

	if (s_button_owner[button] != ButtonOwner::None)
		return s_button_owner[button] == ButtonOwner::Menu; // repeated press (e.g. double-click), keep the owner

	const bool menu = (button <= 1) && CanTouchGame() && s_settings.menu_mode && s_last_context == Context::Menu &&
	                  HasDualShock2();
	if (!menu)
	{
		s_button_owner[button] = ButtonOwner::Bindings;
		return false;
	}

	s_button_owner[button] = ButtonOwner::Menu;
	s_button_menu_bind[button] = MenuButtonToBind((button == 0) ? s_settings.menu_confirm : s_settings.menu_back);
	Pad::SetControllerState(0, s_button_menu_bind[button], 1.0f);
	return true;
}

void MouseLook::Update()
{
	if (!CanTouchGame())
	{
		s_accum_dx = 0.0f;
		s_accum_dy = 0.0f;
		if (s_menu_held_bind >= 0 || s_menu_queued_steps != 0)
			ReleaseMenuInputs();
		return;
	}

	const LookInput in = {std::exchange(s_accum_dx, 0.0f), std::exchange(s_accum_dy, 0.0f), s_settings.sensitivity,
		s_settings.invert};

	const Context context = s_driver->context ? s_driver->context() : Context::Gameplay;
	if (context != Context::Menu && s_last_context == Context::Menu)
	{
		// Left the menu: drop any queued navigation so it doesn't leak into gameplay.
		if (s_menu_held_bind >= 0)
		{
			Pad::SetControllerState(0, static_cast<u32>(s_menu_held_bind), 0.0f);
			s_menu_held_bind = -1;
		}
		s_menu_queued_steps = 0;
		s_menu_frames_left = 0;
	}
	s_last_context = context;

	if (s_driver->every_frame)
		s_driver->every_frame();

	if (in.dx != 0.0f || in.dy != 0.0f)
	{
		if (context == Context::Gameplay)
			s_driver->look(in);
		else if (context == Context::Menu && s_driver->menu_cursor)
			s_driver->menu_cursor(in);
	}

	if (context == Context::Menu && s_settings.menu_mode && HasDualShock2())
		UpdateMenuNavigation();
}

void MouseLook::Toggle()
{
	if (!s_driver || !s_settings.enabled)
	{
		Host::AddIconOSDMessage("MouseLook", ICON_FA_COMPUTER_MOUSE,
			"Mouse look is not available for this game (unsupported, or [MouseLook] Enabled is off).",
			Host::OSD_INFO_DURATION);
		return;
	}

	const bool enabled = !s_session_enabled.load(std::memory_order_relaxed);
	s_session_enabled.store(enabled, std::memory_order_relaxed);
	if (!enabled)
		ReleaseMenuInputs();
	s_accum_dx = 0.0f;
	s_accum_dy = 0.0f;

	InputManager::UpdateHostMouseMode();
	Host::AddIconOSDMessage("MouseLook", ICON_FA_COMPUTER_MOUSE,
		enabled ? "Mouse look enabled." : "Mouse look disabled.", Host::OSD_QUICK_DURATION);
}
