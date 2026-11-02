// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

// Per-game mouse look drivers.
//
// Memory addresses and camera math are ported from Mouse Injector for Dolphin/PCSX2
// (Copyright (C) 2019-2020 Carnivorous and contributors, GPLv2-or-later,
// https://github.com/garungorp/MouseInjectorDolphinDuck). The external tool polled at 1000 Hz;
// here the same math runs once per frame with the summed mouse movement, which is equivalent
// for these drivers because the injected deltas are linear in mouse movement.
//
// Addresses are offsets into EE main RAM, exactly as in Mouse Injector. Out-of-range accesses
// read as 0 and are not written, matching the original tool.
//
// Needs proper testing: each driver was verified against the original source, not in-game.

#include "MouseLookGames.h"

#include "Memory.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace MouseLook
{
	static constexpr float PI = 3.14159265f;
	static constexpr float TAU = 6.2831853f;

	// ---------------------------------------------------------------------------------------
	// EE RAM helpers, mirroring Mouse Injector's PS2_MEM_* functions.
	// "Word" accessors are byte-swapped (big-endian view), everything else is native.
	// ---------------------------------------------------------------------------------------

	static bool InRange(u32 addr, u32 size)
	{
		return (addr <= 0x1FFFFFFu) && (addr + size <= Ps2MemSize::MainRam);
	}

	static u32 ReadUInt(u32 addr)
	{
		if (!InRange(addr, 4))
			return 0;
		u32 v;
		std::memcpy(&v, &eeMem->Main[addr], sizeof(v));
		return v;
	}

	static u32 ReadWord(u32 addr)
	{
		const u32 v = ReadUInt(addr);
		return (v >> 24) | ((v >> 8) & 0xFF00u) | ((v << 8) & 0xFF0000u) | (v << 24);
	}

	static u32 ReadPointer(u32 addr)
	{
		return ReadUInt(addr);
	}

	static u32 ReadUInt16(u32 addr)
	{
		if (!InRange(addr, 2))
			return 0;
		u16 v;
		std::memcpy(&v, &eeMem->Main[addr], sizeof(v));
		return v;
	}

	static float ReadFloat(u32 addr)
	{
		if (!InRange(addr, 4))
			return 0.0f;
		float v;
		std::memcpy(&v, &eeMem->Main[addr], sizeof(v));
		return v;
	}

	static void WriteUInt(u32 addr, u32 value)
	{
		if (InRange(addr, 4))
			std::memcpy(&eeMem->Main[addr], &value, sizeof(value));
	}

	static void WriteFloat(u32 addr, float value)
	{
		if (InRange(addr, 4))
			std::memcpy(&eeMem->Main[addr], &value, sizeof(value));
	}

	static float ClampFloat(float value, float min, float max)
	{
		return std::clamp(value, min, max);
	}

	// =======================================================================================
	// Armored Core 2 (USA) - SLUS-20014
	// =======================================================================================
	namespace AC2
	{
		static constexpr u32 ROTY = 0x2BB920;
		static constexpr u32 ROTX = 0x2BB954;
		static constexpr u32 IS_PAUSED = 0x2B6900;
		static constexpr u32 IS_PAUSED_TRUE = 0xFF010000;
		static constexpr u32 IS_IN_GAME_CUTSCENE = 0x2B68EC;
		static constexpr u32 IS_MAP_DISPLAYED = 0x1C7D624;
		static constexpr u32 IS_NOT_IN_MENU = 0x2D4D00;

		static Context GetContext()
		{
			if (!ReadUInt(IS_NOT_IN_MENU))
				return Context::Menu;
			if (ReadWord(IS_PAUSED) == IS_PAUSED_TRUE)
				return Context::Menu;
			if (ReadWord(IS_IN_GAME_CUTSCENE))
				return Context::Busy;
			if (ReadUInt(IS_MAP_DISPLAYED))
				return Context::Busy;
			return Context::Gameplay;
		}

		static void Look(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 40.0f;
			const float scale = 400.0f;

			float rotX = ReadFloat(ROTX);
			float rotY = ReadFloat(ROTY);

			rotX += in.dx * looksensitivity / scale;
			rotY -= (in.invert ? -in.dy : in.dy) * looksensitivity / scale;

			rotY = ClampFloat(rotY, -1.17809999f, 1.17809999f);

			while (rotX > PI)
				rotX -= TAU;
			while (rotX < -PI)
				rotX += TAU;

			WriteFloat(ROTX, rotX);
			WriteFloat(ROTY, rotY);
		}
	} // namespace AC2

	// =======================================================================================
	// 007: Agent Under Fire (USA) - SLUS-20265
	// =======================================================================================
	namespace AUF
	{
		static constexpr float TOTAL_ANGLE_UNSET = -99.0f;

		static constexpr u32 ONFOOT_CAMY = 0x005064D0;
		static constexpr u32 ONFOOT_CAMX = 0x005064D4;
		static constexpr u32 ONFOOT_SANITY = 0x005064D8;
		static constexpr u32 ONFOOT_SANITY_VALUE = 0x0;

		static constexpr u32 CARGUN_CAMX_SIN = 0x003CD6C0;
		static constexpr u32 CARGUN_CAMX_COS = 0x003CD6C8;
		static constexpr u32 CARGUN_CAMY = 0x003CD858;
		static constexpr u32 CARGUN_SANITY_1 = 0x3CD670;
		static constexpr u32 CARGUN_SANITY_1_VALUE = 0x4D420000;
		static constexpr u32 CARGUN_SANITY_2 = 0x3CD674;

		static constexpr u32 TANKGUN_CAMX_SIN = 0x3BD600;
		static constexpr u32 TANKGUN_CAMX_COS = 0x3BD608;
		static constexpr u32 TANKGUN_CAMY = 0x3BD798;
		static constexpr u32 TANKGUN_SANITY_1 = 0x3BD5B0;
		static constexpr u32 TANKGUN_SANITY_1_VALUE = 0x4D420000;
		static constexpr u32 TANKGUN_SANITY_2 = 0x3BD5B4;
		static constexpr u32 TANKGUN_SANITY_2_VALUE = 0xA0030000;

		static constexpr u32 AIMLOCK_BASE = 0x11D9A0; // requires sanity check as it blips values
		static constexpr u32 AIMLOCK_SANITY = 0x4;
		static constexpr u32 AIMLOCK_SANITY_VALUE = 0xB8BD3F00;
		static constexpr u32 AIMLOCK = 0x7B8;

		static constexpr u32 FOV = 0x385330;

		static u32 s_aim_lock_base = 0;
		static float s_car_gun_total_angle = TOTAL_ANGLE_UNSET;
		static float s_tank_gun_total_angle = TOTAL_ANGLE_UNSET;

		static void Reset()
		{
			s_aim_lock_base = 0;
			s_car_gun_total_angle = TOTAL_ANGLE_UNSET;
			s_tank_gun_total_angle = TOTAL_ANGLE_UNSET;
		}

		// Disables the game's aim lock so the mouse has full control. Runs every frame, as in Mouse Injector.
		static void EveryFrame()
		{
			if (s_aim_lock_base == 0)
				s_aim_lock_base = ReadPointer(AIMLOCK_BASE);

			if (ReadWord(s_aim_lock_base + AIMLOCK_SANITY) == AIMLOCK_SANITY_VALUE)
				WriteUInt(s_aim_lock_base + AIMLOCK, 0x0);
			else
				s_aim_lock_base = ReadPointer(AIMLOCK_BASE);
		}

		static void TurretLook(const LookInput& in, float looksensitivity, u32 sin_addr, u32 cos_addr, u32 camy_addr,
			float& total_angle)
		{
			float camY = ReadFloat(camy_addr);
			const float camXSin = ReadFloat(sin_addr);
			const float camXCos = ReadFloat(cos_addr);

			const float angle = std::atan(camXSin / camXCos);
			if (total_angle == TOTAL_ANGLE_UNSET)
				total_angle = angle;

			total_angle += in.dx * looksensitivity / 100.0f;
			while (total_angle > TAU)
				total_angle -= TAU;
			while (total_angle < -TAU)
				total_angle += TAU;

			WriteFloat(sin_addr, std::sin(total_angle));
			WriteFloat(cos_addr, std::cos(total_angle));

			camY += (in.invert ? -in.dy : in.dy) * looksensitivity / 600.0f;
			WriteFloat(camy_addr, camY);
		}

		static void Look(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 180.0f;

			if (ReadWord(TANKGUN_SANITY_1) == TANKGUN_SANITY_1_VALUE && ReadWord(TANKGUN_SANITY_2) == TANKGUN_SANITY_2_VALUE)
			{
				TurretLook(in, looksensitivity, TANKGUN_CAMX_SIN, TANKGUN_CAMX_COS, TANKGUN_CAMY, s_tank_gun_total_angle);
			}
			else if (ReadWord(CARGUN_SANITY_1) == CARGUN_SANITY_1_VALUE && ReadWord(CARGUN_SANITY_2) != 0x0)
			{
				TurretLook(in, looksensitivity, CARGUN_CAMX_SIN, CARGUN_CAMX_COS, CARGUN_CAMY, s_car_gun_total_angle);
			}
			else if (ReadWord(ONFOOT_SANITY) == ONFOOT_SANITY_VALUE)
			{
				s_tank_gun_total_angle = TOTAL_ANGLE_UNSET;
				s_car_gun_total_angle = TOTAL_ANGLE_UNSET;

				const float fov = ReadFloat(FOV);
				float camX = ReadFloat(ONFOOT_CAMX);
				float camY = ReadFloat(ONFOOT_CAMY);

				// The original driver ignores invert on foot; honour it here for consistency.
				camX -= in.dx * looksensitivity / 2.0f * (fov / 60.0f);
				camY += (in.invert ? -in.dy : in.dy) * looksensitivity / 2.0f * (fov / 60.0f);

				WriteFloat(ONFOOT_CAMX, camX);
				WriteFloat(ONFOOT_CAMY, camY);
			}
		}
	} // namespace AUF

	// =======================================================================================
	// Dirge of Cerberus: Final Fantasy VII (USA) - SLUS-21419
	// =======================================================================================
	namespace DOC
	{
		static constexpr u32 CAM_BASE_PTR = 0x4ED554;
		static constexpr u32 CAM_BASE_SANITY_1_VALUE = 0xE8F94900;
		static constexpr u32 CAM_BASE_SANITY_2_VALUE = 0xE0391300;
		static constexpr u32 CAM_BASE_SANITY_1 = 0x50;
		static constexpr u32 CAM_BASE_SANITY_2 = 0x54;
		static constexpr u32 FOV = 0x170;
		static constexpr u32 CAMY = 0x174;
		static constexpr u32 CAMX = 0x178;

		static constexpr u32 CURSORX = 0x46ACA0;
		static constexpr u32 CURSORY = 0x46ACA4;

		static constexpr u32 IS_BUSY = 0x47CC94;
		static constexpr u32 IS_NOT_PAUSED = 0x4F0738;

		static u32 s_cam_base = 0;

		static void Reset()
		{
			s_cam_base = 0;
		}

		static bool DetectCamBase()
		{
			if (ReadWord(s_cam_base + CAM_BASE_SANITY_1) == CAM_BASE_SANITY_1_VALUE &&
				ReadWord(s_cam_base + CAM_BASE_SANITY_2) == CAM_BASE_SANITY_2_VALUE)
			{
				return true;
			}

			const u32 temp = ReadUInt(CAM_BASE_PTR);
			if (temp && ReadWord(temp + CAM_BASE_SANITY_1) == CAM_BASE_SANITY_1_VALUE &&
				ReadWord(temp + CAM_BASE_SANITY_2) == CAM_BASE_SANITY_2_VALUE)
			{
				s_cam_base = temp;
				return true;
			}

			return false;
		}

		static Context GetContext()
		{
			if (!ReadUInt(IS_NOT_PAUSED))
				return Context::Menu;
			if (ReadUInt(IS_BUSY))
				return Context::Busy;
			if (!DetectCamBase())
				return Context::Busy;
			return Context::Gameplay;
		}

		// The pause menu has its own cursor; drive it directly with the mouse.
		static void MenuCursor(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 40.0f;

			float cursorX = ReadFloat(CURSORX);
			float cursorY = ReadFloat(CURSORY);

			cursorX += in.dx * looksensitivity;
			cursorY += (in.invert ? -in.dy : in.dy) * looksensitivity;

			cursorX = ClampFloat(cursorX, 8.0f, 504.0f);
			cursorY = ClampFloat(cursorY, 8.0f, 440.0f);

			WriteFloat(CURSORX, cursorX);
			WriteFloat(CURSORY, cursorY);
		}

		static void Look(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 40.0f;
			const float scale = 200.0f;
			const float fov = ReadFloat(s_cam_base + FOV) / 73.5f;

			float camX = ReadFloat(s_cam_base + CAMX);
			camX += in.dx * looksensitivity * fov / scale;
			WriteFloat(s_cam_base + CAMX, camX);

			float camY = ReadFloat(s_cam_base + CAMY);
			camY -= (in.invert ? -in.dy : in.dy) * looksensitivity * fov / scale;
			camY = ClampFloat(camY, -0.9424778819f, 0.87266469f);
			WriteFloat(s_cam_base + CAMY, camY);
		}
	} // namespace DOC

	// =======================================================================================
	// Eternal Ring (USA) - SLUS-20015
	// =======================================================================================
	namespace ER
	{
		static constexpr u32 CAMY = 0x1FF350;
		static constexpr u32 CAMX = 0x1FF354;
		static constexpr u32 IS_BUSY = 0x1FFAA4; // doors, cutscenes, item pickup, messages, conversation
		static constexpr u32 IS_PAUSED = 0x202B74;

		static Context GetContext()
		{
			if (ReadUInt16(IS_BUSY))
				return Context::Busy;
			if (ReadUInt16(IS_PAUSED))
				return Context::Menu;
			return Context::Gameplay;
		}

		static void Look(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 40.0f;
			const float scale = 500.0f;

			float camX = ReadFloat(CAMX);
			float camY = ReadFloat(CAMY);

			camX += in.dx * looksensitivity / scale;
			camY -= (in.invert ? -in.dy : in.dy) * looksensitivity / scale;

			WriteFloat(CAMX, camX);
			WriteFloat(CAMY, camY);
		}
	} // namespace ER

	// =======================================================================================
	// King's Field IV: The Ancient City (USA) - SLUS-20318
	// =======================================================================================
	namespace KF4
	{
		static constexpr u32 CAMY = 0x413F90;
		static constexpr u32 CAMX = 0x413F94;
		static constexpr u32 IS_NOT_CONVERSING = 0x38CC50;
		static constexpr u32 IS_BUSY = 0x38CC80;
		static constexpr u32 IS_PAUSED = 0x5FB01C;

		static Context GetContext()
		{
			if (ReadUInt16(IS_NOT_CONVERSING) != 257) // talking to NPCs
				return Context::Busy;
			if (ReadUInt16(IS_BUSY)) // picking up / using items, reading messages
				return Context::Busy;
			if (ReadUInt16(IS_PAUSED)) // pause and status menus
				return Context::Menu;
			return Context::Gameplay;
		}

		static void Look(const LookInput& in)
		{
			const float looksensitivity = static_cast<float>(in.sensitivity) / 14000.0f;

			float camX = ReadFloat(CAMX);
			float camY = ReadFloat(CAMY);

			camX += in.dx * looksensitivity;
			camY -= (in.invert ? -in.dy : in.dy) * looksensitivity;

			// Mouse Injector TODO carried over: pitch is not clamped.
			WriteFloat(CAMX, camX);
			WriteFloat(CAMY, camY);
		}
	} // namespace KF4

	// =======================================================================================
	// Driver table
	// =======================================================================================

	// Menu confirm/back defaults are a best guess (Japanese-developed releases of this era often kept
	// Circle = confirm); they can be overridden per game with MenuConfirm/MenuBack. Needs proper testing.
	static constexpr GameDriver s_drivers[] = {
		{"SLUS-20014", "Armored Core 2", MenuButton::Circle, MenuButton::Cross,
			nullptr, &AC2::GetContext, nullptr, &AC2::Look, nullptr},
		{"SLUS-20265", "007: Agent Under Fire", MenuButton::Cross, MenuButton::Triangle,
			&AUF::Reset, nullptr, &AUF::EveryFrame, &AUF::Look, nullptr},
		{"SLUS-21419", "Dirge of Cerberus: Final Fantasy VII", MenuButton::Cross, MenuButton::Circle,
			&DOC::Reset, &DOC::GetContext, nullptr, &DOC::Look, &DOC::MenuCursor},
		{"SLUS-20015", "Eternal Ring", MenuButton::Circle, MenuButton::Cross,
			nullptr, &ER::GetContext, nullptr, &ER::Look, nullptr},
		{"SLUS-20318", "King's Field IV: The Ancient City", MenuButton::Circle, MenuButton::Cross,
			nullptr, &KF4::GetContext, nullptr, &KF4::Look, nullptr},
	};

	const GameDriver* FindGameDriver(std::string_view serial)
	{
		for (const GameDriver& driver : s_drivers)
		{
			if (serial == driver.serial)
				return &driver;
		}
		return nullptr;
	}
} // namespace MouseLook
