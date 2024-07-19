/*  PCSX2 - PS2 Emulator for PCs
 *  Copyright (C) 2002-2020  PCSX2 Dev Team
 *
 *  PCSX2 is free software: you can redistribute it and/or modify it under the terms
 *  of the GNU Lesser General Public License as published by the Free Software Found-
 *  ation, either version 3 of the License, or (at your option) any later version.
 *
 *  PCSX2 is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
 *  without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR
 *  PURPOSE.  See the GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along with PCSX2.
 *  If not, see <http://www.gnu.org/licenses/>.
 */

#include "PrecompiledHeader.h"

#include "common/StringUtil.h"
#include "common/SettingsInterface.h"

#include "HostSettings.h"

#include "PAD/Host/Global.h"
#include "PAD/Host/Device.h"
#include "PAD/Host/PAD.h"
#include "PAD/Host/KeyStatus.h"
#include "PAD/Host/StateManagement.h"
#include "PAD/Host/Device.h"
#include "PAD/Host/InputManager.h"
#include "PAD/Host/Config.h"
#include "PAD/Host/SDLJoystick.h"

#ifdef SDL_BUILD
#include <SDL.h>
#endif

const u32 revision = 3;
const u32 build = 0; // increase that with each version
#define PAD_SAVE_STATE_VERSION ((revision << 8) | (build << 0))

PADconf g_conf;
KeyStatus g_key_status;
JoystickInfo* g_haptic_android = nullptr;
bool g_forcefeedback = false;

s32 PADinit()
{
	Pad::reset_all();

	query.reset();

	for (int & slot : slots) {
        slot = 0;
    }

	PADshutdown();

	return 0;
}

void PADshutdown()
{
	if(g_haptic_android != nullptr) {
		delete g_haptic_android;
		g_haptic_android = nullptr;
	}
}

s32 PADopen(const WindowInfo& wi)
{
	g_key_status.Init();
	EnumerateDevices();
	return 0;
}

void PADclose()
{
	device_manager.devices.clear();
}

s32 PADsetSlot(u8 port, u8 slot)
{
	port--;
	slot--;
	if (port > 1 || slot > 3)
	{
		return 0;
	}
	// Even if no pad there, record the slot, as it is the active slot regardless.
	slots[port] = slot;

	return 1;
}

s32 PADfreeze(FreezeAction p_mode, freezeData* p_data)
{
	if (!p_data) {
		return -1;
	}

	if (p_mode == FreezeAction::Size)
	{
		p_data->size = sizeof(PadFullFreezeData);
	}
	else if (p_mode == FreezeAction::Load)
	{
		auto* pdata = (PadFullFreezeData*)(p_data->data);

		Pad::stop_vibrate_all();

		if (p_data->size != sizeof(PadFullFreezeData) || pdata->version != PAD_SAVE_STATE_VERSION ||
			strncmp(pdata->format, "LinPad", sizeof(pdata->format))) {
			return 0;
		}

		query = pdata->query;
		if (pdata->query.slot < 4)
		{
			query = pdata->query;
		}

		// Tales of the Abyss - pad fix
		// - restore data for both ports
		for (int port = 0; port < 2; ++port) {
			for (int slot = 0; slot < 4; ++slot) {
				u8 mode = pdata->padData[port][slot].mode;

				if (mode != MODE_DIGITAL && mode != MODE_ANALOG && mode != MODE_DS2_NATIVE) {
					break;
				}

				memcpy(&pads[port][slot], &pdata->padData[port][slot], sizeof(PadFreezeData));
			}

			if (pdata->slot[port] < 4) {
				slots[port] = pdata->slot[port];
			}
		}
	}
	else if (p_mode == FreezeAction::Save)
	{
		if (p_data->size != sizeof(PadFullFreezeData)) {
			return 0;
		}

		auto* pdata = (PadFullFreezeData*)(p_data->data);

		// Tales of the Abyss - pad fix
		// - PCSX2 only saves port0 (save #1), then port1 (save #2)

		memset(pdata, 0, p_data->size);
		strncpy(pdata->format, "LinPad", sizeof(pdata->format));
		pdata->version = PAD_SAVE_STATE_VERSION;
		pdata->query = query;

		for (int port = 0; port < 2; ++port) {
			for (int slot = 0; slot < 4; ++slot) {
				pdata->padData[port][slot] = pads[port][slot];
			}

			pdata->slot[port] = slots[port];
		}
	}
	else
	{
		return -1;
	}

	return 0;
}

u8 PADstartPoll(int pad)
{
	return pad_start_poll(pad);
}

u8 PADpoll(u8 value)
{
	return pad_poll(value);
}

void PAD::PollDevices()
{
#ifdef SDL_BUILD
	// Take the opportunity to handle hot plugging here
	SDL_Event events;
	while (SDL_PollEvent(&events))
	{
		switch (events.type)
		{
			case SDL_CONTROLLERDEVICEADDED:
			case SDL_CONTROLLERDEVICEREMOVED:
				EnumerateDevices();
				break;
			default:
				break;
		}
	}
#endif

	device_manager.Update();
}

/// g_key_status.press but with proper handling for analog buttons
static void PressButton(u32 p_pad, u32 p_button, u32 p_range)
{
	// Analog controls.
	if (IsAnalogKey(p_button))
	{
		switch (p_button)
		{
			case PAD_R_LEFT:
			case PAD_R_UP:
			case PAD_L_LEFT:
			case PAD_L_UP:
				g_key_status.press(p_pad, p_button, -p_range);
				break;
			case PAD_R_RIGHT:
			case PAD_R_DOWN:
			case PAD_L_RIGHT:
			case PAD_L_DOWN:
				g_key_status.press(p_pad, p_button, p_range);
				break;
		}
	}
	else
	{
		g_key_status.press(p_pad, p_button);
	}
}

bool PAD::HandleHostInputEvent(const HostKeyEvent& event)
{
	switch (event.type)
	{
		case HostKeyEvent::Type::KeyPressed:
		case HostKeyEvent::Type::KeyReleased:
		{
			bool result = false;

			for (u32 cpad = 0; cpad < GAMEPAD_NUMBER; ++cpad) {
				const int button_index = get_keyboard_key(cpad, event.key);
				if (button_index < 0) {
					continue;
				}

				g_key_status.keyboard_state_acces(cpad);
				if (event.type == HostKeyEvent::Type::KeyPressed) {
					PressButton(cpad, button_index, event.range);
				} else {
					g_key_status.release(cpad, button_index);
				}
			}

			return result;
		}

		default: {
			return false;
		}
	}
}

void PAD::SetVibration(bool p_isvalue)
{
	g_forcefeedback = p_isvalue;
	for (auto & pad_option : g_conf.pad_options){
		pad_option.forcefeedback = g_forcefeedback;
	}
}

void PAD::LoadConfig(const SettingsInterface& si)
{
	g_conf.init();

	// load keyboard bindings
	for (u32 pad = 0; pad < GAMEPAD_NUMBER; ++pad)
    {
        set_keyboard_key(pad, 104, PAD_L2);
        set_keyboard_key(pad, 105, PAD_R2);
        set_keyboard_key(pad, 102, PAD_L1);
        set_keyboard_key(pad, 103, PAD_R1);
        set_keyboard_key(pad, 100, PAD_TRIANGLE);
        set_keyboard_key(pad, 97, PAD_CIRCLE);
        set_keyboard_key(pad, 96, PAD_CROSS);
        set_keyboard_key(pad, 99, PAD_SQUARE);
        set_keyboard_key(pad, 109, PAD_SELECT);
        set_keyboard_key(pad, 106, PAD_L3);
        set_keyboard_key(pad, 107, PAD_R3);
        set_keyboard_key(pad, 108, PAD_START);
        set_keyboard_key(pad, 19, PAD_UP);
        set_keyboard_key(pad, 22, PAD_RIGHT);
        set_keyboard_key(pad, 20, PAD_DOWN);
        set_keyboard_key(pad, 21, PAD_LEFT);
        set_keyboard_key(pad, 110, PAD_L_UP);
        set_keyboard_key(pad, 111, PAD_L_RIGHT);
        set_keyboard_key(pad, 112, PAD_L_DOWN);
        set_keyboard_key(pad, 113, PAD_L_LEFT);
        set_keyboard_key(pad, 120, PAD_R_UP);
        set_keyboard_key(pad, 121, PAD_R_RIGHT);
        set_keyboard_key(pad, 122, PAD_R_DOWN);
        set_keyboard_key(pad, 123, PAD_R_LEFT);

		const std::string section(StringUtil::StdStringFromFormat("Pad%u", pad));
		g_conf.set_joy_uid(pad, si.GetUIntValue(section.c_str(), "JoystickUID", 0u));
		g_conf.pad_options[pad].forcefeedback = g_forcefeedback; //si.GetBoolValue(section.c_str(), "ForceFeedback", true);
		g_conf.pad_options[pad].reverse_lx = si.GetBoolValue(section.c_str(), "ReverseLX", false);
		g_conf.pad_options[pad].reverse_ly = si.GetBoolValue(section.c_str(), "ReverseLY", false);
		g_conf.pad_options[pad].reverse_rx = si.GetBoolValue(section.c_str(), "ReverseRX", false);
		g_conf.pad_options[pad].reverse_ry = si.GetBoolValue(section.c_str(), "ReverseRY", false);
		g_conf.pad_options[pad].mouse_l = si.GetBoolValue(section.c_str(), "MouseL", false);
		g_conf.pad_options[pad].mouse_r = si.GetBoolValue(section.c_str(), "MouseR", false);
	}

	g_conf.set_sensibility(si.GetUIntValue("Pad", "MouseSensibility", 100));
	g_conf.set_ff_intensity(si.GetUIntValue("Pad", "FFIntensity", 0x7FFF));
}

static void SetKeyboardBinding(SettingsInterface& si, u32 port, const char* name, int binding)
{
	const std::string section(StringUtil::StdStringFromFormat("Pad%u", port));
	const std::string key(StringUtil::StdStringFromFormat("Button%d", binding));
	si.SetStringValue(section.c_str(), key.c_str(), name);
}

void PAD::SetDefaultConfig(SettingsInterface& si)
{
	SetKeyboardBinding(si, 0, "1", PAD_L2);
	SetKeyboardBinding(si, 0, "Q", PAD_R2);
	SetKeyboardBinding(si, 0, "E", PAD_L1);
	SetKeyboardBinding(si, 0, "3", PAD_R1);
	SetKeyboardBinding(si, 0, "I", PAD_TRIANGLE);
	SetKeyboardBinding(si, 0, "L", PAD_CIRCLE);
	SetKeyboardBinding(si, 0, "K", PAD_CROSS);
	SetKeyboardBinding(si, 0, "J", PAD_SQUARE);
	SetKeyboardBinding(si, 0, "Backspace", PAD_SELECT);
	SetKeyboardBinding(si, 0, "Return", PAD_START);
	SetKeyboardBinding(si, 0, "Up", PAD_UP);
	SetKeyboardBinding(si, 0, "Right", PAD_RIGHT);
	SetKeyboardBinding(si, 0, "Down", PAD_DOWN);
	SetKeyboardBinding(si, 0, "Left", PAD_LEFT);
	SetKeyboardBinding(si, 0, "W", PAD_L_UP);
	SetKeyboardBinding(si, 0, "D", PAD_L_RIGHT);
	SetKeyboardBinding(si, 0, "S", PAD_L_DOWN);
	SetKeyboardBinding(si, 0, "A", PAD_L_LEFT);
	SetKeyboardBinding(si, 0, "T", PAD_R_UP);
	SetKeyboardBinding(si, 0, "H", PAD_R_RIGHT);
	SetKeyboardBinding(si, 0, "G", PAD_R_DOWN);
	SetKeyboardBinding(si, 0, "F", PAD_R_LEFT);
}
