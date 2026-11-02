// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "common/Pcsx2Defs.h"

#include <string_view>

// Internal interface between MouseLook.cpp and the per-game drivers in MouseLookGames.cpp.
namespace MouseLook
{
	/// What the game is doing right now, as far as the driver can tell.
	enum class Context : u8
	{
		Gameplay, // mouse moves the camera
		Menu, // pause/garage/status menu: menu mode handles wheel and clicks
		Busy, // cutscene, dialogue, door animation...: mouse does nothing
	};

	enum class MenuButton : u8
	{
		Cross,
		Circle,
		Triangle,
		Square,
	};

	struct LookInput
	{
		float dx; // host mouse movement this frame (pixels), same units as Mouse Injector's xmouse
		float dy; // same as above for ymouse
		int sensitivity; // Mouse Injector scale, default 20
		bool invert; // Mouse Injector's invertpitch
	};

	struct GameDriver
	{
		const char* serial; // e.g. "SLUS-20014"
		const char* name;
		MenuButton default_confirm;
		MenuButton default_back;
		void (*reset)(); // clear cached pointers/state; may be null
		Context (*context)(); // null = always gameplay
		void (*every_frame)(); // runs every frame even with no mouse movement; may be null
		void (*look)(const LookInput& in); // camera injection
		void (*menu_cursor)(const LookInput&); // drives an in-game cursor while in a menu; may be null
	};

	const GameDriver* FindGameDriver(std::string_view serial);
} // namespace MouseLook
