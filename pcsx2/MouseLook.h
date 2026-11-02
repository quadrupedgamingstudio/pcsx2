// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

class SettingsInterface;

/// Native per-game mouse look.
///
/// For supported games, raw mouse movement is written straight into the game's camera
/// variables once per frame, giving 1:1 aiming instead of emulating a capped analog stick.
/// The per-game memory addresses and camera math are ported from Mouse Injector
/// (https://github.com/garungorp/MouseInjectorDolphinDuck, GPLv2-or-later).
///
/// Settings live in the [MouseLook] section and can be set per game:
///   Enabled      (bool,  default false)  - turn the feature on
///   Sensitivity  (int,   default 20)     - same scale as Mouse Injector (1-200)
///   InvertY      (bool,  default false)
///   MenuMode     (bool,  default true)   - in menus: wheel = D-pad up/down, left click = confirm, right click = back
///   MenuConfirm  (Cross/Circle/Triangle/Square, default per game)
///   MenuBack     (Cross/Circle/Triangle/Square, default per game)
namespace MouseLook
{
	/// Picks the driver for the running game and reads settings. Called whenever input bindings reload.
	void LoadSettings(const SettingsInterface& si);

	/// True when mouse look wants the host cursor captured in relative mode.
	bool WantsRelativeMouse();

	/// Raw pointer movement from the host, in host pixels. Axis 0 = X, 1 = Y.
	void AddPointerDelta(u32 axis, float delta);

	/// Mouse wheel movement. Returns true if mouse look consumed it (menu navigation).
	bool HandleWheel(float delta);

	/// Mouse button press/release. Returns true if mouse look consumed it (menu confirm/back).
	bool HandlePointerButton(u32 button, float value);

	/// Applies accumulated movement to game memory. Called once per frame on the CPU thread.
	void Update();

	/// Hotkey: enable/disable mouse look for the current session.
	void Toggle();
} // namespace MouseLook
