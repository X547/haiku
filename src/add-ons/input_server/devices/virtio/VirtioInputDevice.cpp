/*
 * Copyright 2021, Haiku, Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include "VirtioInputDevice.h"
#include "WaylandKeycodes.h"

#include <virtio_input_driver.h>
#include <virtio_defs.h>

#include <stdio.h>
#include <string.h>

#include <Application.h>
#include <String.h>


//#define TRACE_VIRTIO_INPUT_DEVICE
#ifdef TRACE_VIRTIO_INPUT_DEVICE
#       define TRACE(x...) debug_printf("virtio_input_device: " x)
#else
#       define TRACE(x...) ;
#endif
#define ERROR(x...) debug_printf("virtio_input_device: " x)
#define CALLED() TRACE("CALLED %s\n", __PRETTY_FUNCTION__)


enum {
	kWatcherThreadPriority = B_FIRST_REAL_TIME_PRIORITY + 4,
};


template<typename Type>
inline static void SetBit(Type &val, int bit) {val |= Type(1) << bit;}

template<typename Type>
inline static void ClearBit(Type &val, int bit) {val &= ~(Type(1) << bit);}

template<typename Type>
inline static void InvertBit(Type &val, int bit) {val ^= Type(1) << bit;}

template<typename Type>
inline static void SetBitTo(Type &val, int bit, bool isSet) {
	val ^= ((isSet? -1: 0) ^ val) & (Type(1) << bit);}

template<typename Type>
inline static bool IsBitSet(Type val, int bit) {
	return (val & (Type(1) << bit)) != 0;}


#ifdef TRACE_VIRTIO_INPUT_DEVICE
static void WriteInputPacket(const VirtioInputPacket &pkt)
{
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			TRACE("syn");
			break;
		case kVirtioInputEvKey:
			TRACE("key, ");
			switch (pkt.code) {
				case kVirtioInputBtnLeft:
					TRACE("left");
					break;
				case kVirtioInputBtnRight:
					TRACE("middle");
					break;
				case kVirtioInputBtnMiddle:
					TRACE("right");
					break;
				case kVirtioInputBtnGearDown:
					TRACE("gearDown");
					break;
				case kVirtioInputBtnGearUp:
					TRACE("gearUp");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRel:
			TRACE("rel, ");
			switch (pkt.code) {
				case kVirtioInputRelX:
					TRACE("relX");
					break;
				case kVirtioInputRelY:
					TRACE("relY");
					break;
				case kVirtioInputRelZ:
					TRACE("relZ");
					break;
				case kVirtioInputRelWheel:
					TRACE("relWheel");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvAbs:
			TRACE("abs, ");
			switch (pkt.code) {
				case kVirtioInputAbsX:
					TRACE("absX");
					break;
				case kVirtioInputAbsY:
					TRACE("absY");
					break;
				case kVirtioInputAbsZ:
					TRACE("absZ");
					break;
				default:
					TRACE("%d", pkt.code);
			}
			break;
		case kVirtioInputEvRep:
			TRACE("rep");
			break;
		default:
			TRACE("?(%d)", pkt.type);
	}
	switch (pkt.type) {
		case kVirtioInputEvSyn:
			break;
		case kVirtioInputEvKey:
			TRACE(", ");
			if (pkt.value == 0) {
				TRACE("up");
			} else if (pkt.value == 1) {
				TRACE("down");
			} else {
				TRACE("%d", pkt.value);
			}
			break;
		default:
			TRACE(", ");
			TRACE("%d", pkt.value);
	}
}
#endif /* TRACE_VIRTIO_INPUT_DEVICE */



class KeycodeTable {
private:
	uint8 fWlToHaikuMap[256] {};

public:
	KeycodeTable()
	{
		for (uint32 i = 0; i < 256; i++) {
			fWlToHaikuMap[FromHaikuKeyCode(i)] = i;
		}
	}

	uint32_t FromHaikuKeyCode(uint32 haikuKey)
	{
		uint32_t wlKey;
		switch (haikuKey) {
			case 0x01: wlKey = KEY_ESC; break;
			case 0x02: wlKey = KEY_F1; break;
			case 0x03: wlKey = KEY_F2; break;
			case 0x04: wlKey = KEY_F3; break;
			case 0x05: wlKey = KEY_F4; break;
			case 0x06: wlKey = KEY_F5; break;
			case 0x07: wlKey = KEY_F6; break;
			case 0x08: wlKey = KEY_F7; break;
			case 0x09: wlKey = KEY_F8; break;
			case 0x0a: wlKey = KEY_F9; break;
			case 0x0b: wlKey = KEY_F10; break;
			case 0x0c: wlKey = KEY_F11; break;
			case 0x0d: wlKey = KEY_F12; break;
			case 0x0e: wlKey = KEY_SYSRQ; break;
			case 0x0f: wlKey = KEY_SCROLLLOCK; break;
			case 0x10: wlKey = KEY_PAUSE; break;
			case 0x11: wlKey = KEY_GRAVE; break;
			case 0x12: wlKey = KEY_1; break;
			case 0x13: wlKey = KEY_2; break;
			case 0x14: wlKey = KEY_3; break;
			case 0x15: wlKey = KEY_4; break;
			case 0x16: wlKey = KEY_5; break;
			case 0x17: wlKey = KEY_6; break;
			case 0x18: wlKey = KEY_7; break;
			case 0x19: wlKey = KEY_8; break;
			case 0x1a: wlKey = KEY_9; break;
			case 0x1b: wlKey = KEY_0; break;
			case 0x1c: wlKey = KEY_MINUS; break;
			case 0x1d: wlKey = KEY_EQUAL; break;
			case 0x1e: wlKey = KEY_BACKSPACE; break;
			case 0x1f: wlKey = KEY_INSERT; break;
			case 0x20: wlKey = KEY_HOME; break;
			case 0x21: wlKey = KEY_PAGEUP; break;
			case 0x22: wlKey = KEY_NUMLOCK; break;
			case 0x23: wlKey = KEY_KPSLASH; break;
			case 0x24: wlKey = KEY_KPASTERISK; break;
			case 0x25: wlKey = KEY_KPMINUS; break;
			case 0x26: wlKey = KEY_TAB; break;
			case 0x27: wlKey = KEY_Q; break;
			case 0x28: wlKey = KEY_W; break;
			case 0x29: wlKey = KEY_E; break;
			case 0x2a: wlKey = KEY_R; break;
			case 0x2b: wlKey = KEY_T; break;
			case 0x2c: wlKey = KEY_Y; break;
			case 0x2d: wlKey = KEY_U; break;
			case 0x2e: wlKey = KEY_I; break;
			case 0x2f: wlKey = KEY_O; break;
			case 0x30: wlKey = KEY_P; break;
			case 0x31: wlKey = KEY_LEFTBRACE; break;
			case 0x32: wlKey = KEY_RIGHTBRACE; break;
			case 0x33: wlKey = KEY_BACKSLASH; break;
			case 0x34: wlKey = KEY_DELETE; break;
			case 0x35: wlKey = KEY_END; break;
			case 0x36: wlKey = KEY_PAGEDOWN; break;
			case 0x37: wlKey = KEY_KP7; break;
			case 0x38: wlKey = KEY_KP8; break;
			case 0x39: wlKey = KEY_KP9; break;
			case 0x3a: wlKey = KEY_KPPLUS; break;
			case 0x3b: wlKey = KEY_CAPSLOCK; break;
			case 0x3c: wlKey = KEY_A; break;
			case 0x3d: wlKey = KEY_S; break;
			case 0x3e: wlKey = KEY_D; break;
			case 0x3f: wlKey = KEY_F; break;
			case 0x40: wlKey = KEY_G; break;
			case 0x41: wlKey = KEY_H; break;
			case 0x42: wlKey = KEY_J; break;
			case 0x43: wlKey = KEY_K; break;
			case 0x44: wlKey = KEY_L; break;
			case 0x45: wlKey = KEY_SEMICOLON; break;
			case 0x46: wlKey = KEY_APOSTROPHE; break;
			case 0x47: wlKey = KEY_ENTER; break;
			case 0x48: wlKey = KEY_KP4; break;
			case 0x49: wlKey = KEY_KP5; break;
			case 0x4a: wlKey = KEY_KP6; break;
			case 0x4b: wlKey = KEY_LEFTSHIFT; break;
			case 0x4c: wlKey = KEY_Z; break;
			case 0x4d: wlKey = KEY_X; break;
			case 0x4e: wlKey = KEY_C; break;
			case 0x4f: wlKey = KEY_V; break;
			case 0x50: wlKey = KEY_B; break;
			case 0x51: wlKey = KEY_N; break;
			case 0x52: wlKey = KEY_M; break;
			case 0x53: wlKey = KEY_COMMA; break;
			case 0x54: wlKey = KEY_DOT; break;
			case 0x55: wlKey = KEY_SLASH; break;
			case 0x56: wlKey = KEY_RIGHTSHIFT; break;
			case 0x57: wlKey = KEY_UP; break;
			case 0x58: wlKey = KEY_KP1; break;
			case 0x59: wlKey = KEY_KP2; break;
			case 0x5a: wlKey = KEY_KP3; break;
			case 0x5b: wlKey = KEY_KPENTER; break;
			case 0x5c: wlKey = KEY_LEFTCTRL; break;
			case 0x5d: wlKey = KEY_LEFTALT; break;
			case 0x5e: wlKey = KEY_SPACE; break;
			case 0x5f: wlKey = KEY_RIGHTALT; break;
			case 0x60: wlKey = KEY_RIGHTCTRL; break;
			case 0x61: wlKey = KEY_LEFT; break;
			case 0x62: wlKey = KEY_DOWN; break;
			case 0x63: wlKey = KEY_RIGHT; break;
			case 0x64: wlKey = KEY_KP0; break;
			case 0x65: wlKey = KEY_KPDOT; break;
			case 0x66: wlKey = KEY_LEFTMETA; break;
			case 0x67: wlKey = KEY_RIGHTMETA; break;
			case 0x68: wlKey = KEY_COMPOSE; break;
			case 0x69: wlKey = KEY_102ND; break;
			case 0x6a: wlKey = KEY_YEN; break;
			case 0x6b: wlKey = KEY_RO; break;

			default:
				//fprintf(stderr, "[!] unknown key: %#x\n", haikuKey);
				wlKey = 0;
		}
		return wlKey;
	}

	uint32 ToHaikuKeycode(uint32_t wlKey)
	{
		if (wlKey >= B_COUNT_OF(fWlToHaikuMap))
			return 0;

		return fWlToHaikuMap[wlKey];
	}

} gKeycodeTable;


//#pragma mark VirtioInputDevice


VirtioInputDevice::VirtioInputDevice()
{
}

VirtioInputDevice::~VirtioInputDevice()
{
}


status_t
VirtioInputDevice::InitCheck()
{
	static input_device_ref *devices[3];
	input_device_ref **devicesEnd = devices;

	FileDescriptorCloser fd;

	// TODO: dynamically scan and detect device type

	ObjectDeleter<VirtioInputHandler> tablet(
		new TabletHandler(this, "VirtIO tablet"));
	fd.SetTo(open("/dev/input/virtio/1/raw", O_RDWR));
	if (fd.IsSet()) {
		tablet->SetFd(fd.Detach());
		*devicesEnd++ = tablet->Ref();
		tablet.Detach();
	} else {
		TRACE("Unable to detect tablet device!");
	}

	ObjectDeleter<VirtioInputHandler> keyboard(
		new KeyboardHandler(this, "VirtIO keyboard"));
	fd.SetTo(open("/dev/input/virtio/0/raw", O_RDWR));
	if (fd.IsSet()) {
		keyboard->SetFd(fd.Detach());
		*devicesEnd++ = keyboard->Ref();
		keyboard.Detach();
	} else {
		TRACE("Unable to detect keyboard device!");
	}

	*devicesEnd = NULL;

	RegisterDevices(devices);
	return B_OK;
}


status_t
VirtioInputDevice::Start(const char* name, void* cookie)
{
	return ((VirtioInputHandler*)cookie)->Start();
}


status_t
VirtioInputDevice::Stop(const char* name, void* cookie)
{
	return ((VirtioInputHandler*)cookie)->Stop();
}


status_t
VirtioInputDevice::Control(const char* name, void* cookie, uint32 command,
	BMessage* message)
{
	return ((VirtioInputHandler*)cookie)->Control(command, message);
}


//#pragma mark VirtioInputHandler


VirtioInputHandler::VirtioInputHandler(VirtioInputDevice* dev, const char* name,
	input_device_type type)
	:
	fDev(dev),
	fWatcherThread(B_ERROR),
	fRun(false)
{
	fRef.name = (char*)name; // NOTE: name should be constant data
	fRef.type = type;
	fRef.cookie = this;
}


VirtioInputHandler::~VirtioInputHandler()
{

}


void
VirtioInputHandler::SetFd(int fd)
{
	fDeviceFd.SetTo(fd);
}


status_t
VirtioInputHandler::Start()
{
	char threadName[B_OS_NAME_LENGTH];
	snprintf(threadName, B_OS_NAME_LENGTH, "%s watcher", fRef.name);

	if (fWatcherThread < 0) {
		fWatcherThread = spawn_thread(Watcher, threadName,
			kWatcherThreadPriority, this);

		if (fWatcherThread < B_OK)
			return fWatcherThread;

		fRun = true;
		resume_thread(fWatcherThread);
	}
	return B_OK;
}


status_t
VirtioInputHandler::Stop()
{
	// TODO: Use condition variable to sync access? suspend_thread
	// avoids a race condition so it doesn't exit before wait_for_thread

	if (fWatcherThread >= B_OK) {
		// ioctl(fDeviceFd.Get(), virtioInputCancelIO, NULL, 0);
		suspend_thread(fWatcherThread);
		fRun = false;
		status_t res;
		wait_for_thread(fWatcherThread, &res);
		fWatcherThread = B_ERROR;
	}
	return B_OK;
}


status_t
VirtioInputHandler::Control(uint32 command, BMessage* message)
{
	return B_OK;
}


int32
VirtioInputHandler::Watcher(void *arg)
{
	VirtioInputHandler &handler = *((VirtioInputHandler*)arg);
	handler.Reset();
	while (handler.fRun) {
		VirtioInputPacket pkt;
		status_t res = ioctl(handler.fDeviceFd.Get(), virtioInputRead, &pkt,
			sizeof(pkt));
		// if (res == B_CANCELED) return B_OK;
		if (res < B_OK)
			continue;
		handler.PacketReceived(pkt);
	}
	return B_OK;
}


//#pragma mark KeyboardHandler


KeyboardHandler::KeyboardHandler(VirtioInputDevice* dev, const char* name)
	:
	VirtioInputHandler(dev, name, B_KEYBOARD_DEVICE),
	fRepeatThread(-1),
	fRepeatThreadSem(-1)
{
	TRACE("+KeyboardHandler()\n");
	{
		// TODO: Similar to B_KEY_MAP_CHANGED below?
		key_map *keyMap = NULL;
		char *chars = NULL;
		get_key_map(&keyMap, &chars);
		fKeyMap.SetTo(keyMap);
		fChars.SetTo(chars);
	}
	TRACE("  fKeymap: %p\n", fKeyMap.Get());
	TRACE("  fChars: %p\n", fChars.Get());
	get_key_repeat_delay(&fRepeatDelay);
	get_key_repeat_rate (&fRepeatRate);
	TRACE("  fRepeatDelay: %" B_PRIdBIGTIME "\n", fRepeatDelay);
	TRACE("  fRepeatRate: % " B_PRId32 "\n", fRepeatRate);

	if (fRepeatRate < 1)
		fRepeatRate = 1;
}


KeyboardHandler::~KeyboardHandler()
{
	_StopRepeating();
}


void
KeyboardHandler::Reset()
{
	memset(&fNewState, 0, sizeof(KeyboardState));
	memcpy(&fState, &fNewState, sizeof(KeyboardState));
	_StopRepeating();
}


status_t
KeyboardHandler::Control(uint32 command, BMessage* message)
{
	switch (command) {
		case B_KEY_MAP_CHANGED: {
			key_map *keyMap = NULL;
			char *chars = NULL;
			get_key_map(&keyMap, &chars);
			if (keyMap == NULL || chars == NULL)
				return B_NO_MEMORY;
			fKeyMap.SetTo(keyMap);
			fChars.SetTo(chars);
			return B_OK;
		}
		case B_KEY_REPEAT_DELAY_CHANGED:
			get_key_repeat_delay(&fRepeatDelay);
			TRACE("  fRepeatDelay: %" B_PRIdBIGTIME "\n", fRepeatDelay);
			return B_OK;
		case B_KEY_REPEAT_RATE_CHANGED:
			get_key_repeat_rate(&fRepeatRate);
			TRACE("  fRepeatRate: %" B_PRId32 "\n", fRepeatRate);
			if (fRepeatRate < 1) fRepeatRate = 1;
			return B_OK;
	}
	return VirtioInputHandler::Control(command, message);
}


void
KeyboardHandler::PacketReceived(const VirtioInputPacket &pkt)
{
#ifdef TRACE_VIRTIO_INPUT_DEVICE
	TRACE("keyboard: ");
	WriteInputPacket(pkt);
	TRACE("\n");
#endif
	switch (pkt.type) {
		case kVirtioInputEvKey: {
			uint32 haikuKey = gKeycodeTable.ToHaikuKeycode(pkt.code);
			if (haikuKey < 256)
				SetBitTo(fNewState.keys[haikuKey / 8], haikuKey % 8, pkt.value != 0);
			break;
		}
		case kVirtioInputEvSyn: {
			fState.when = system_time();
			_StateChanged();
		}
	}
}


bool
KeyboardHandler::_IsKeyPressed(const KeyboardState &state, uint32 key)
{
	return key < 256 && IsBitSet(state.keys[key / 8], key % 8);
}


void
KeyboardHandler::_KeyString(uint32 code, char *str, size_t len)
{
	char *ch;
	switch (fNewState.modifiers & (
		B_SHIFT_KEY | B_CONTROL_KEY | B_OPTION_KEY | B_CAPS_LOCK)) {
		case B_OPTION_KEY | B_CAPS_LOCK | B_SHIFT_KEY:
			ch = fChars.Get() + fKeyMap->option_caps_shift_map[code];
			break;
		case B_OPTION_KEY | B_CAPS_LOCK:
			ch = fChars.Get() + fKeyMap->option_caps_map[code];
			break;
		case B_OPTION_KEY | B_SHIFT_KEY:
			ch = fChars.Get() + fKeyMap->option_shift_map[code];
			break;
		case B_OPTION_KEY:
			ch = fChars.Get() + fKeyMap->option_map[code];
			break;
		case B_CAPS_LOCK  | B_SHIFT_KEY:
			ch = fChars.Get() + fKeyMap->caps_shift_map[code];
			break;
		case B_CAPS_LOCK:
			ch = fChars.Get() + fKeyMap->caps_map[code];
			break;
		case B_SHIFT_KEY:
			ch = fChars.Get() + fKeyMap->shift_map[code];
			break;
		default:
			if ((fNewState.modifiers & B_CONTROL_KEY) != 0)
				ch = fChars.Get() + fKeyMap->control_map[code];
			else
				ch = fChars.Get() + fKeyMap->normal_map[code];
	}
	if (len > 0) {
		uint32 i;
		for (i = 0; (i < (uint32)ch[0]) && (i < len - 1); i++)
			str[i] = ch[i + 1];
		str[i] = '\0';
	}
}


void
KeyboardHandler::_StartRepeating(BMessage* msg)
{
	if (fRepeatThread >= B_OK)
		_StopRepeating();

	fRepeatMsg = *msg;
	fRepeatThread = spawn_thread(_RepeatThread, "repeat thread",
		B_REAL_TIME_DISPLAY_PRIORITY + 4, this);
	fRepeatThreadSem = create_sem(0, "repeat thread sem");
	if (fRepeatThread >= B_OK)
		resume_thread(fRepeatThread);
}


void
KeyboardHandler::_StopRepeating()
{
	if (fRepeatThread >= B_OK) {
		status_t res;
		release_sem(fRepeatThreadSem);
		wait_for_thread(fRepeatThread, &res);
		fRepeatThread = -1;
		delete_sem(fRepeatThreadSem);
		fRepeatThreadSem = -1;
	}
}


status_t
KeyboardHandler::_RepeatThread(void *arg)
{
	status_t res;
	KeyboardHandler *h = (KeyboardHandler*)arg;

	res = acquire_sem_etc(h->fRepeatThreadSem, 1, B_RELATIVE_TIMEOUT,
		h->fRepeatDelay);
	if (res >= B_OK)
		return B_OK;

	while (true) {
		int32 count;

		h->fRepeatMsg.ReplaceInt64("when", system_time());
		h->fRepeatMsg.FindInt32("be:key_repeat", &count);
		h->fRepeatMsg.ReplaceInt32("be:key_repeat", count + 1);

		ObjectDeleter<BMessage> msg(new(std::nothrow) BMessage(h->fRepeatMsg));
		if (msg.IsSet() && h->Device()->EnqueueMessage(msg.Get()) >= B_OK)
			msg.Detach();

		res = acquire_sem_etc(h->fRepeatThreadSem, 1, B_RELATIVE_TIMEOUT,
			(bigtime_t)10000000 / h->fRepeatRate);
		if (res >= B_OK)
			return B_OK;
	}
}


void
KeyboardHandler::_StateChanged()
{
	uint32 i, j;

	fNewState.modifiers = fState.modifiers
		& (B_CAPS_LOCK | B_SCROLL_LOCK | B_NUM_LOCK);
	if (_IsKeyPressed(fNewState, fKeyMap->left_shift_key))
		fNewState.modifiers |= B_SHIFT_KEY   | B_LEFT_SHIFT_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->right_shift_key))
		fNewState.modifiers |= B_SHIFT_KEY   | B_RIGHT_SHIFT_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->left_command_key))
		fNewState.modifiers |= B_COMMAND_KEY | B_LEFT_COMMAND_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->right_command_key))
		fNewState.modifiers |= B_COMMAND_KEY | B_RIGHT_COMMAND_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->left_control_key))
		fNewState.modifiers |= B_CONTROL_KEY | B_LEFT_CONTROL_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->right_control_key))
		fNewState.modifiers |= B_CONTROL_KEY | B_RIGHT_CONTROL_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->caps_key))
		fNewState.modifiers ^= B_CAPS_LOCK;
	if (_IsKeyPressed(fNewState, fKeyMap->scroll_key))
		fNewState.modifiers ^= B_SCROLL_LOCK;
	if (_IsKeyPressed(fNewState, fKeyMap->num_key))
		fNewState.modifiers ^= B_NUM_LOCK;
	if (_IsKeyPressed(fNewState, fKeyMap->left_option_key))
		fNewState.modifiers |= B_OPTION_KEY  | B_LEFT_OPTION_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->right_option_key))
		fNewState.modifiers |= B_OPTION_KEY  | B_RIGHT_OPTION_KEY;
	if (_IsKeyPressed(fNewState, fKeyMap->menu_key))
		fNewState.modifiers |= B_MENU_KEY;

	if (fState.modifiers != fNewState.modifiers) {
		ObjectDeleter<BMessage> msg(
			new(std::nothrow) BMessage(B_MODIFIERS_CHANGED));
		if (msg.IsSet()) {
			msg->AddInt64("when", system_time());
			msg->AddInt32("modifiers", fNewState.modifiers);
			msg->AddInt32("be:old_modifiers", fState.modifiers);
			msg->AddData("states", B_UINT8_TYPE, fNewState.keys, 16);

			if (Device()->EnqueueMessage(msg.Get()) >= B_OK) {
				msg.Detach();
				fState.modifiers = fNewState.modifiers;
			}
		}
	}


	uint8 diff[16];
	char rawCh;
	char str[5];

	for (i = 0; i < 16; ++i)
		diff[i] = fState.keys[i] ^ fNewState.keys[i];

	for (i = 0; i < 128; ++i) {
		if (diff[i/8] & (1 << (i % 8))) {
			ObjectDeleter<BMessage> msg(new(std::nothrow) BMessage());
			if (msg.IsSet()) {
				_KeyString(i, str, sizeof(str));

				msg->AddInt64("when", system_time());
				msg->AddInt32("key", i);
				msg->AddInt32("modifiers", fNewState.modifiers);
				msg->AddData("states", B_UINT8_TYPE, fNewState.keys, 16);

				if (str[0] != '\0') {
					if (fChars.Get()[fKeyMap->normal_map[i]] != 0)
						rawCh = fChars.Get()[fKeyMap->normal_map[i] + 1];
					else
						rawCh = str[0];

					for (j = 0; str[j] != '\0'; ++j)
						msg->AddInt8("byte", str[j]);

					msg->AddString("bytes", str);
					msg->AddInt32("raw_char", rawCh);
				}

				if (fNewState.keys[i / 8] & (1 << (i % 8))) {
					if (str[0] != '\0')
						msg->what = B_KEY_DOWN;
					else
						msg->what = B_UNMAPPED_KEY_DOWN;

					msg->AddInt32("be:key_repeat", 1);
					_StartRepeating(msg.Get());
				} else {
					if (str[0] != '\0')
						msg->what = B_KEY_UP;
					else
						msg->what = B_UNMAPPED_KEY_UP;

					_StopRepeating();
				}

				if (Device()->EnqueueMessage(msg.Get()) >= B_OK) {
					msg.Detach();
					for (j = 0; j < 16; ++j)
						fState.keys[j] = fNewState.keys[j];
				}
			}
		}
	}
}


//#pragma mark TabletHandler


TabletHandler::TabletHandler(VirtioInputDevice* dev, const char* name)
	:
	VirtioInputHandler(dev, name, B_POINTING_DEVICE)
{
}


void
TabletHandler::Reset()
{
	memset(&fNewState, 0, sizeof(TabletState));
	fNewState.x = 0.5f;
	fNewState.y = 0.5f;
	memcpy(&fState, &fNewState, sizeof(TabletState));
	fLastClick = -1;
	fLastClickBtn = -1;

	get_click_speed(&fClickSpeed);
	TRACE("  fClickSpeed: %" B_PRIdBIGTIME "\n", fClickSpeed);
}


status_t
TabletHandler::Control(uint32 command, BMessage* message)
{
	switch (command) {
		case B_CLICK_SPEED_CHANGED: {
			get_click_speed(&fClickSpeed);
			TRACE("  fClickSpeed: %" B_PRIdBIGTIME "\n", fClickSpeed);
			return B_OK;
		}
	}
	return VirtioInputHandler::Control(command, message);
}


void
TabletHandler::PacketReceived(const VirtioInputPacket &pkt)
{
	switch (pkt.type) {
		case kVirtioInputEvAbs: {
			switch (pkt.code) {
				case kVirtioInputAbsX:
					fNewState.x = float(pkt.value) / 32768.0f;
					break;
				case kVirtioInputAbsY:
					fNewState.y = float(pkt.value) / 32768.0f;
					break;
			}
			break;
		}
		case kVirtioInputEvRel: {
			switch (pkt.code) {
				case kVirtioInputRelWheel:
					fNewState.wheelY -= pkt.value;
					break;
			}
			break;
		}
		case kVirtioInputEvKey: {
			switch (pkt.code) {
				case kVirtioInputBtnLeft:
					SetBitTo(fNewState.buttons, 0, pkt.value != 0);
					break;
				case kVirtioInputBtnRight:
					SetBitTo(fNewState.buttons, 1, pkt.value != 0);
					break;
				case kVirtioInputBtnMiddle:
					SetBitTo(fNewState.buttons, 2, pkt.value != 0);
					break;
			}
			break;
		}
		case kVirtioInputEvSyn: {
			fState.when = system_time();

			// update pos
			if (fState.x != fNewState.x || fState.y != fNewState.y
				|| fState.pressure != fNewState.pressure) {
				fState.x = fNewState.x;
				fState.y = fNewState.y;
				fState.pressure = fNewState.pressure;
				ObjectDeleter<BMessage> msg(
					new(std::nothrow) BMessage(B_MOUSE_MOVED));
				if (!msg.IsSet() || !_FillMessage(*msg.Get(), fState))
					return;

				if (Device()->EnqueueMessage(msg.Get()) >= B_OK)
					msg.Detach();
			}

			// update buttons
			for (int i = 0; i < 32; i++) {
				if ((IsBitSet(fState.buttons, i)
					!= IsBitSet(fNewState.buttons, i))) {
					InvertBit(fState.buttons, i);

					// TODO: new B_MOUSE_DOWN for every button clicked together?
					// should be refactored to look like other input drivers.

					ObjectDeleter<BMessage> msg(new(std::nothrow) BMessage());
					if (!msg.IsSet() || !_FillMessage(*msg.Get(), fState))
						return;

					if (IsBitSet(fState.buttons, i)) {
						msg->what = B_MOUSE_DOWN;
						if (i == fLastClickBtn
							&& fState.when - fLastClick <= fClickSpeed)
							fState.clicks++;
						else
							fState.clicks = 1;
						fLastClickBtn = i;
						fLastClick = fState.when;
						msg->AddInt32("clicks", fState.clicks);
					} else
						msg->what = B_MOUSE_UP;

					if (Device()->EnqueueMessage(msg.Get()) >= B_OK)
						msg.Detach();
				}
			}

			// update wheel
			if (fState.wheelX != fNewState.wheelX
				|| fState.wheelY != fNewState.wheelY) {
				ObjectDeleter<BMessage> msg(
					new(std::nothrow) BMessage(B_MOUSE_WHEEL_CHANGED));
				if (!msg.IsSet()
					|| msg->AddInt64("when", fState.when) < B_OK
					|| msg->AddFloat("be:wheel_delta_x",
						fNewState.wheelX - fState.wheelX) < B_OK
					|| msg->AddFloat("be:wheel_delta_y",
						fNewState.wheelY - fState.wheelY) < B_OK) {
					return;
				}

				fState.wheelX = fNewState.wheelX;
				fState.wheelY = fNewState.wheelY;
				if (Device()->EnqueueMessage(msg.Get()) >= B_OK)
					msg.Detach();
			}
			break;
		}
	}
}


bool
TabletHandler::_FillMessage(BMessage &msg, const TabletState &s)
{
	if (msg.AddInt64("when", s.when) < B_OK
		|| msg.AddInt32("buttons", s.buttons) < B_OK
		|| msg.AddFloat("x", s.x) < B_OK
		|| msg.AddFloat("y", s.y) < B_OK) {
		return false;
	}
	msg.AddFloat("be:tablet_x", s.x);
	msg.AddFloat("be:tablet_y", s.y);
	msg.AddFloat("be:tablet_pressure", s.pressure);
	return true;
}


//#pragma mark -


extern "C" BInputServerDevice*
instantiate_input_device()
{
	return new(std::nothrow) VirtioInputDevice();
}
