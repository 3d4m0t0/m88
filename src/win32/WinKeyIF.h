// ---------------------------------------------------------------------------
//	M88 - PC88 emulator
//	Copyright (c) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//	PC88 Keyboard Interface Emulation for Win32/106 key (Rev. 3)
// ---------------------------------------------------------------------------
//	$Id: WinKeyIF.h,v 1.3 1999/10/10 01:47:20 cisc Exp $

#pragma once

#include "device.h"
#ifdef M88_LINUX_PORT
#include "../linux_compat/critsect.h"
#include "pc88/config.h"
#else
#include "CritSect.h"
#endif

// ---------------------------------------------------------------------------
namespace PC8801
{

class Config;

class WinKeyIF : public Device
{
public:
	enum
	{
		reset = 0, vsync,
		in = 0,
	};

public:
	WinKeyIF();
	~WinKeyIF();
#ifdef M88_LINUX_PORT
	bool Init();
#else
	bool Init(HWND);
#endif
	void ApplyConfig(const Config* config);

	uint IOCALL In(uint port);
	void IOCALL VSync(uint, uint data);
	void IOCALL Reset(uint=0, uint=0);
	
	void Activate(bool);
	void Disable(bool);
	void KeyDown(uint, uint32);
	void KeyUp(uint, uint32);

#ifdef M88_LINUX_PORT
	void SetKanaLock(bool on);
	void ClearHostModifiers();
	void FlushGuestKeys();
#endif

	const Descriptor* IFCALL GetDesc() const { return &descriptor; }

private:
	enum KeyState
	{
		locked = 1,
		down   = 2,
		downex = 4,
	};
	enum KeyFlags
	{
		none = 0, lock, nex, ext, arrowten, keyb, noarrowten, noarrowtenex, 
		pc80sft, pc80key,
	};
	struct Key
	{
		uint8 k, f;
	};

	uint GetKey(const Key* key);

	static const Key KeyTable98[16 * 8][8];
	static const Key KeyTable106[16 * 8][8];
	static const Key KeyTable101[16 * 8][8];

	const Key* keytable;
	int keyboardtype;
#ifdef M88_LINUX_PORT
	Config::KeyType host_keytype_ = Config::AT101;
	int host_shift_refs_ = 0;
	void KeyDownImpl(uint vkcode, uint32 keydata);
	void KeyUpImpl(uint vkcode, uint32 keydata);
	void InvalidateKeyports();
	void ApplyGuestShiftChordDown(uint vk, uint32 keydata);
	void ApplyGuestShiftChordUp(uint vk, uint32 keydata);
	bool HostShiftDown() const;
	void ClearShiftKeystate();
	void RestoreShiftKeystate();
	bool ApplyLinuxKeyFixupDown(uint vk, uint32 keydata);
	bool ApplyLinuxKeyFixupUp(uint vk, uint32 keydata);
#endif
	bool active;
	bool disable;
	bool usearrow;
	bool pc80mode;
	HWND hwnd;
	HANDLE hevent;
	uint basicmode;
	int keyport[16];
	uint8 keyboard[256];
	uint8 keystate[512];

private:
	static const Descriptor descriptor;
	static const InFuncPtr  indef[];
	static const OutFuncPtr outdef[];
};

}

