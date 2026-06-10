// ---------------------------------------------------------------------------
//	M88 - PC88 emulator
//	Copyright (c) cisc 1998. 1999.
// ---------------------------------------------------------------------------
//	PC88 Keyboard Interface Emulation for Win32/106 key (Rev. 3)
// ---------------------------------------------------------------------------
//	$Id: WinKeyIF.cpp,v 1.8 2000/02/04 01:50:00 cisc Exp $

#ifdef M88_LINUX_PORT
#include "../linux_compat/headers.h"
#else
#include "headers.h"
#endif
#include "WinKeyIF.h"
#include "pc88/config.h"
#include "misc.h"

#ifdef M88_LINUX_PORT
#include "../linux/pc88_key_fixup.h"
#include "../linux/half_kana_ime.h"
#include "winkeys.h"
#include <cstring>
#else
#include "messages.h"
#endif

//#define LOGNAME "keyif"
#include "diag.h"

using namespace PC8801;

#ifdef M88_LINUX_PORT
namespace {

struct FixupPending {
  uint8_t host_vk = 0;
  uint8_t guest_vk = 0;
  bool mask_host_shift = false;
  bool guest_shift = false;
  bool swallow = false;
};

constexpr int kMaxFixupPending = 16;
FixupPending g_fixup_pending[kMaxFixupPending];
int g_fixup_pending_count = 0;

bool IsTypistShiftVk(uint vk) {
  return vk == VK_LSHIFT || vk == VK_RSHIFT;
}

void PushFixupPending(uint8_t host_vk, uint8_t guest_vk, bool mask, bool guest_shift,
                      bool swallow) {
  if (g_fixup_pending_count >= kMaxFixupPending) {
    return;
  }
  g_fixup_pending[g_fixup_pending_count++] = {host_vk, guest_vk, mask, guest_shift, swallow};
}

bool PopFixupPending(uint8_t host_vk, FixupPending* out) {
  for (int i = g_fixup_pending_count - 1; i >= 0; --i) {
    if (g_fixup_pending[i].host_vk == host_vk) {
      if (out) {
        *out = g_fixup_pending[i];
      }
      g_fixup_pending[i] = g_fixup_pending[--g_fixup_pending_count];
      return true;
    }
  }
  return false;
}

void ClearFixupPending() { g_fixup_pending_count = 0; }

void SyncKeyboardArray(uint8* keyboard, const uint8* keystate) {
  for (int i = 0; i < 256; ++i) {
    const uint8 lock = keyboard[i] & 0x01u;
    keyboard[i] = (keystate[i] || keystate[i | 0x100]) ? 0x80u : 0u;
    keyboard[i] |= lock;
  }
}

}  // namespace

bool WinKeyIF::HostShiftHeld() const {
  return host_shift_refs_ > 0 || keystate[VK_LSHIFT] || keystate[VK_RSHIFT];
}

bool WinKeyIF::ImeInjectionActive() const {
  return HalfKanaIme::InjectBusy() || HalfKanaIme::SessionActive();
}

void WinKeyIF::ClearImeKanaLockUnlessUser(uint vk) {
  if (ImeInjectionActive() || user_kana_lock_ || vk == VK_SCROLL) {
    return;
  }
  if (keyboard[VK_SCROLL] & 0x01u) {
    keyboard[VK_SCROLL] &= ~0x01u;
  }
}

void WinKeyIF::SetKanaLock(bool on) {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
#endif
  keystate[VK_SCROLL] = 0;
  if (on) {
    keyboard[VK_SCROLL] |= 0x01u;
    user_kana_lock_ = true;
  } else {
    keyboard[VK_SCROLL] &= ~0x01u;
    user_kana_lock_ = false;
  }
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::EnableHalfWidthKana() {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
#endif
  keystate[0xf4] = 3;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::InjectKeyDown(uint vkcode, uint32 keydata) {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
  if (host_keytype_ == Config::AT101 && (vkcode & 0xffu) == VK_MENU) {
    return;
  }
  keydata |= M88_KEYDATA_FIXUP_MASK;
#endif
  KeyDownImpl(vkcode, keydata);
}

void WinKeyIF::InjectKeyUp(uint vkcode, uint32 keydata) {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
  if (host_keytype_ == Config::AT101 && (vkcode & 0xffu) == VK_MENU) {
    return;
  }
#endif
  KeyUpImpl(vkcode, keydata);
}

void WinKeyIF::ToggleMatrixLock(uint vk) {
  if (vk != VK_SCROLL && vk != VK_CAPITAL) {
    return;
  }
  if (keystate[vk] || keystate[vk | 0x100]) {
    return;
  }
  keyboard[vk] ^= 0x01u;
  if (vk == VK_SCROLL) {
    user_kana_lock_ = (keyboard[VK_SCROLL] & 0x01u) != 0;
  }
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::ClearHostModifiers() {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
#endif
  keystate[VK_SHIFT] = keystate[VK_LSHIFT] = keystate[VK_RSHIFT] = 0;
  keystate[VK_CONTROL] = keystate[VK_MENU] = 0;
  host_shift_refs_ = 0;
  SyncKeyboardArray(keyboard, keystate);
}

void WinKeyIF::ClearGraphIfAt101Host() {
  if (host_keytype_ != Config::AT101) {
    return;
  }
  keystate[VK_MENU] = 0;
  keystate[VK_MENU | 0x100] = 0;
  keyboard[VK_MENU] &= ~0x80u;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::ClearImeLayerKeys() {
  keystate[VK_SHIFT] = keystate[VK_LSHIFT] = keystate[VK_RSHIFT] = 0;
  keystate[VK_CONTROL] = 0;
  host_shift_refs_ = 0;
  keystate[VK_MENU] = 0;
  keystate[VK_MENU | 0x100] = 0;
  keyboard[VK_MENU] &= ~0x80u;
}

void WinKeyIF::MaintainImeInjectState() {
  CriticalSection::Lock lock(key_mutex_);
  ClearImeLayerKeys();
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::InjectImeKeyDown(uint vkcode, uint32 keydata) {
  CriticalSection::Lock lock(key_mutex_);
  ClearImeLayerKeys();
  if (HalfKanaIme::SessionActive()) {
    keystate[0xf4] = 3;
  } else {
    keystate[0xf4] = 0;
  }
  keyboard[VK_SCROLL] &= ~0x01u;
  user_kana_lock_ = false;
  // Momentary カナ (101 host has no kana key; matrix chart kana layer).
  keystate[VK_SCROLL] = 1;
  if (keydata & M88_KEYDATA_GUEST_SHIFT) {
    keystate[VK_SHIFT] = 1;
    keystate[VK_LSHIFT] = 1;
  } else if (keydata & M88_KEYDATA_FH_SHIFT) {
    keystate[VK_SHIFT] = 1;
  }
  const uint vk = vkcode & 0xffu;
  const uint keyindex =
      vk | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[keyindex] = 1;
  if (vk == VK_RETURN) {
    keystate[VK_RETURN] = 1;
    keystate[VK_RETURN | 0x100] = 1;
  }
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::InjectImeKeyUp(uint vkcode, uint32 keydata) {
  CriticalSection::Lock lock(key_mutex_);
  const uint vk = vkcode & 0xffu;
  const uint keyindex =
      vk | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[keyindex] = 0;
  if (vk == VK_RETURN) {
    keystate[VK_RETURN] = 0;
    keystate[VK_RETURN | 0x100] = 0;
  }
  keystate[VK_SCROLL] = 0;
  if (HalfKanaIme::SessionActive()) {
    keystate[0xf4] = 3;
  } else {
    keystate[0xf4] = 0;
  }
  ClearImeLayerKeys();
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::InjectImeLockKeyDown(uint vkcode, uint32 keydata) {
  CriticalSection::Lock lock(key_mutex_);
  ClearImeLayerKeys();
  keystate[0xf4] = 0;
  keystate[VK_SCROLL] = 0;
  keyboard[VK_SCROLL] |= 0x01u;
  const uint vk = vkcode & 0xffu;
  const uint keyindex =
      vk | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[keyindex] = 1;
  if (vk == VK_RETURN) {
    keystate[VK_RETURN] = 1;
    keystate[VK_RETURN | 0x100] = 1;
  }
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::InjectImeLockKeyUp(uint vkcode, uint32 keydata) {
  CriticalSection::Lock lock(key_mutex_);
  const uint vk = vkcode & 0xffu;
  const uint keyindex =
      vk | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[keyindex] = 0;
  if (vk == VK_RETURN) {
    keystate[VK_RETURN] = 0;
    keystate[VK_RETURN | 0x100] = 0;
  }
  if (!user_kana_lock_) {
    keyboard[VK_SCROLL] &= ~0x01u;
  }
  keystate[0xf4] = 0;
  ClearImeLayerKeys();
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::PulseHalfWidthKana() {
  CriticalSection::Lock lock(key_mutex_);
  keystate[0xf4] = 3;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::FlushGuestKeys() {
#ifdef M88_LINUX_PORT
  CriticalSection::Lock lock(key_mutex_);
#endif
  memset(keystate, 0, 512);
  host_shift_refs_ = 0;
  if (!HalfKanaIme::InjectBusy() && !user_kana_lock_) {
    keyboard[VK_SCROLL] &= ~0x01u;
  }
  SyncKeyboardArray(keyboard, keystate);
  ClearFixupPending();
  for (int i = 0; i < 16; ++i) {
    keyport[i] = -1;
  }
}

void WinKeyIF::InvalidateKeyports() {
  for (int i = 0; i < 16; ++i) {
    keyport[i] = -1;
  }
}

void WinKeyIF::PushImeKeyTable() {
  CriticalSection::Lock lock(key_mutex_);
  if (ime_saved_keytable_) {
    return;
  }
  static Key ime_table[16 * 8][8];
  const Key* src = keytable;
  if (host_keytype_ == Config::AT101) {
    src = KeyTable101[0];
  } else if (host_keytype_ == Config::AT106 || host_keytype_ == Config::PC98) {
    src = KeyTable106[0];
  }
  memcpy(ime_table, src, sizeof(ime_table));
  // 101 host: no カナ key — momentary via keystate; LOCK via keyboard bit 0x01.
  constexpr int kKanaCol = 8 * 8 + 5;
  ime_table[kKanaCol][0] = Key{VK_SCROLL, none};
  ime_table[kKanaCol][1] = Key{static_cast<uint8>(VK_SCROLL), KeyFlags::lock};
  ime_table[kKanaCol][2] = Key{0, 0};
  ime_saved_keytable_ = keytable;
  keytable = ime_table[0];
  InvalidateKeyports();
}

void WinKeyIF::PopImeKeyTable() {
  CriticalSection::Lock lock(key_mutex_);
  if (!ime_saved_keytable_) {
    return;
  }
  keytable = ime_saved_keytable_;
  ime_saved_keytable_ = nullptr;
  InvalidateKeyports();
}

uint8_t WinKeyIF::MatrixVk(uint8_t port, uint8_t col) const {
  if (!keytable || col > 7) {
    return 0;
  }
  const Key* key = keytable + (port & 0x0fu) * 64 + col * 8;
  return key->k;
}

bool WinKeyIF::HostShiftDown() const {
  return HostShiftHeld();
}

void WinKeyIF::ClearShiftKeystate() {
  keystate[VK_SHIFT] = 0;
  keystate[VK_LSHIFT] = 0;
  keystate[VK_RSHIFT] = 0;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::RestoreShiftKeystate() {
  if (host_shift_refs_ <= 0) {
    // guest_shift leaves row08 VK_SHIFT set; drop it when host Shift is gone.
    ClearShiftKeystate();
    return;
  }
  keystate[VK_LSHIFT] = 1;
  keystate[VK_SHIFT] = 1;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::ApplyGuestShiftChordDown(uint vk, uint32 keydata) {
  ClearShiftKeystate();
  const uint keyindex = (vk & 0xffu) | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[VK_LSHIFT] = 1;
  keystate[VK_SHIFT] = 1;
  keystate[keyindex] = 1;
  if ((vk & 0xffu) == VK_RETURN) {
    keystate[VK_RETURN] = 1;
    keystate[VK_RETURN | 0x100] = 1;
  }
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

void WinKeyIF::ApplyGuestShiftChordUp(uint vk, uint32 keydata) {
  const uint keyindex = (vk & 0xffu) | ((keydata & M88_KEYDATA_EXTENDED) ? 0x100u : 0u);
  keystate[keyindex] = 0;
  if ((vk & 0xffu) == VK_RETURN) {
    keystate[VK_RETURN] = 0;
    keystate[VK_RETURN | 0x100] = 0;
  }
  if (keytable == KeyTable106[0] || keytable == KeyTable101[0]) {
    switch (keyindex) {
      case VK_NUMPAD8:
      case VK_UP:
        keystate[VK_NUMPAD8] = keystate[VK_UP] = 0;
        break;
      case VK_NUMPAD9:
      case VK_PRIOR:
        keystate[VK_NUMPAD9] = keystate[VK_PRIOR] = 0;
        break;
      default:
        break;
    }
  }
  keystate[VK_LSHIFT] = 0;
  keystate[VK_RSHIFT] = 0;
  keystate[VK_SHIFT] = 0;
  SyncKeyboardArray(keyboard, keystate);
  InvalidateKeyports();
}

bool WinKeyIF::ApplyLinuxKeyFixupDown(uint vk, uint32 keydata) {
  const bool shift = HostShiftDown() || ((keydata & M88_KEYDATA_HOST_SHIFT) != 0);
  if (IsTypistShiftVk(vk)) {
    const uint idx = vk & 0xffu;
    if (!keystate[idx] && !keystate[idx | 0x100]) {
      ++host_shift_refs_;
    }
    return false;
  }

  Pc88KeyFixup::KeyMap mapped{};
  if (!Pc88KeyFixup::MapKey(host_keytype_, vk, shift, &mapped)) {
    return false;
  }

  const bool mask_shift = mapped.mask_host_shift || mapped.guest_shift;
  PushFixupPending(static_cast<uint8_t>(vk), mapped.vk, mask_shift, mapped.guest_shift,
                   mapped.swallow);
  if (mapped.swallow) {
    if (mask_shift) {
      ClearShiftKeystate();
    }
    return true;
  }
  if (mapped.guest_shift) {
    ApplyGuestShiftChordDown(mapped.vk, keydata);
    return true;
  }
  if (mask_shift) {
    ClearShiftKeystate();
  }
  const uint32 impl_keydata =
      mask_shift ? ((keydata & ~M88_KEYDATA_HOST_SHIFT) | M88_KEYDATA_FIXUP_MASK)
                 : keydata;
  KeyDownImpl(mapped.vk, impl_keydata);
  return true;
}

bool WinKeyIF::ApplyLinuxKeyFixupUp(uint vk, uint32 keydata) {
  if (IsTypistShiftVk(vk)) {
    if (host_shift_refs_ > 0) {
      --host_shift_refs_;
    }
    return false;
  }

  FixupPending pending{};
  const bool popped = PopFixupPending(static_cast<uint8_t>(vk), &pending);
  if (!popped) {
    // Host Shift may already be released on KeyUp; match the unshifted rule so a
    // remapped guest key (e.g. OEM_MINUS) is not left stuck down.
    Pc88KeyFixup::KeyMap mapped{};
    if (!Pc88KeyFixup::MapKey(host_keytype_, vk, false, &mapped)) {
      const bool shift =
          HostShiftDown() || ((keydata & M88_KEYDATA_HOST_SHIFT) != 0);
      if (!Pc88KeyFixup::MapKey(host_keytype_, vk, shift, &mapped)) {
        return false;
      }
    }
    pending.guest_vk = mapped.vk;
    pending.mask_host_shift = mapped.mask_host_shift || mapped.guest_shift;
    pending.guest_shift = mapped.guest_shift;
    pending.swallow = mapped.swallow;
  }

  if (pending.guest_shift) {
    ApplyGuestShiftChordUp(pending.guest_vk, keydata);
    if (pending.mask_host_shift) {
      RestoreShiftKeystate();
    }
    return true;
  }

  if (!pending.swallow) {
    KeyUpImpl(pending.guest_vk, keydata);
  }
  if (pending.mask_host_shift) {
    RestoreShiftKeystate();
  }
  return true;
}
#endif

// ---------------------------------------------------------------------------
//	Construct/Destruct
//
WinKeyIF::WinKeyIF()
: Device(0)
{
	hwnd = 0;
	hevent = 0;
	for (int i=0; i<16; i++)
	{
		keyport[i] = -1;
	}
	memset(keyboard, 0, 256);
	memset(keystate, 0, 512);
	usearrow = false;

	disable = false;
}

WinKeyIF::~WinKeyIF()
{
#ifndef M88_LINUX_PORT
	if (hevent)
		CloseHandle(hevent);
#endif
}

// ---------------------------------------------------------------------------
//	初期化
//
#ifdef M88_LINUX_PORT
bool WinKeyIF::Init()
{
	active = true;
	disable = false;
	keytable = KeyTable101[0];
	return true;
}
#else
bool WinKeyIF::Init(HWND hwndmsg)
{
	hwnd = hwndmsg;
	hevent = CreateEvent(0, 0, 0, 0);
	keytable = KeyTable106[0];
	return hevent != 0;
}
#endif

// ---------------------------------------------------------------------------
//	リセット（というか、BASIC モードの変更）
//
void IOCALL WinKeyIF::Reset(uint, uint)
{
	pc80mode = (basicmode & 2) != 0; 
}

// ---------------------------------------------------------------------------
//	設定反映
//
void WinKeyIF::ApplyConfig(const Config* config)
{
	usearrow = 0 != (config->flags & Config::usearrowfor10);
	basicmode = config->basicmode;

#ifdef M88_LINUX_PORT
	// Host keyboard layout selects the key table (same as Win32 M88).
	host_keytype_ = static_cast<Config::KeyType>(config->keytype);
	if (host_keytype_ == Config::PC98) {
		host_keytype_ = Config::AT106;
	}
	switch (host_keytype_) {
	case Config::AT101:
		keytable = KeyTable101[0];
		Pc88KeyFixup::SetGuestKeyboard(Config::AT101);
		break;
	case Config::AT106:
	default:
		keytable = KeyTable106[0];
		Pc88KeyFixup::SetGuestKeyboard(Config::AT106);
		break;
	}
	Pc88KeyFixup::SetHostKeyboard(host_keytype_);
#else
	switch (config->keytype)
	{
	case Config::PC98:
		keytable = KeyTable98[0];
		break;

	case Config::AT101:
		keytable = KeyTable101[0];
		break;

	case Config::AT106:
	default:
		keytable = KeyTable106[0];
		break;
	}
#endif
}

#ifdef M88_LINUX_PORT
void WinKeyIF::KeyDownImpl(uint vkcode, uint32 keydata)
#else
static void KeyDownBody(PC8801::WinKeyIF* self, uint vkcode, uint32 keydata)
#endif
{
#ifdef M88_LINUX_PORT
	WinKeyIF* self = this;
#endif
	if (self->keytable == KeyTable106[0] || self->keytable == KeyTable101[0])
	{
		if (vkcode == 0xf3 || vkcode == 0xf4)
		{
			self->keystate[0xf4] = 3;
			return;
		}
	}
	const uint vk = vkcode & 0xff;
	if (vk == VK_SCROLL || vk == VK_CAPITAL) {
		ToggleMatrixLock(vk);
	}
	uint keyindex = (vkcode & 0xff) | ((keydata & (1<<24)) ? 0x100 : 0);
	LOG2("KeyDown  = %.2x %.3x\n", vkcode, keyindex);
	self->keystate[keyindex] = 1;
	if (vk == VK_SCROLL || vk == VK_CAPITAL) {
		SyncKeyboardArray(self->keyboard, self->keystate);
		self->InvalidateKeyports();
		return;
	}
#ifdef M88_LINUX_PORT
	// Row 0e typist shift; row 08 VK_SHIFT (shifted symbols / FH shift layer).
	if (vk == VK_LSHIFT || vk == VK_RSHIFT || vk == VK_SHIFT) {
		self->keystate[VK_SHIFT] = 1;
	}
	if (!(keydata & M88_KEYDATA_FIXUP_MASK)) {
		if (keydata & M88_KEYDATA_HOST_SHIFT) {
			self->keystate[VK_LSHIFT] = 1;
			self->keystate[VK_SHIFT] = 1;
		}
		if (self->host_shift_refs_ > 0) {
			self->keystate[VK_LSHIFT] = 1;
			self->keystate[VK_SHIFT] = 1;
		}
	}
	if (!ImeInjectionActive()) {
		ClearImeKanaLockUnlessUser(vk);
	}
	if (ImeInjectionActive()) {
		ClearImeLayerKeys();
	} else {
		ClearGraphIfAt101Host();
	}
#endif
	if ((vkcode & 0xff) == VK_RETURN) {
		self->keystate[VK_RETURN] = 1;
		self->keystate[VK_RETURN | 0x100] = 1;
	}
#ifdef M88_LINUX_PORT
	SyncKeyboardArray(self->keyboard, self->keystate);
	self->InvalidateKeyports();
#endif
}

 // ---------------------------------------------------------------------------
//	WM_KEYDOWN
//
void WinKeyIF::KeyDown(uint vkcode, uint32 keydata)
{
#ifdef M88_LINUX_PORT
	CriticalSection::Lock lock(key_mutex_);
	if (HalfKanaIme::SessionActive()) {
		return;
	}
	if (keytable == KeyTable106[0] || keytable == KeyTable101[0])
	{
		if (vkcode == 0xf3 || vkcode == 0xf4)
		{
			keystate[0xf4] = 3;
			return;
		}
	}
	uint vk = vkcode & 0xff;
	if (host_keytype_ == Config::AT101 && vk == VK_MENU) {
		return;
	}
	if (host_keytype_ == Config::AT101 && vk == VK_SHIFT) {
		vk = VK_LSHIFT;
		vkcode = (vkcode & ~0xffu) | vk;
	}
	const bool ime_path = ImeInjectionActive();
	if (!ime_path && ApplyLinuxKeyFixupDown(vk, keydata)) {
		return;
	}
	KeyDownImpl(vkcode, keydata);
#else
	KeyDownBody(this, vkcode, keydata);
#endif
}

#ifdef M88_LINUX_PORT
void WinKeyIF::KeyUpImpl(uint vkcode, uint32 keydata)
#else
static void KeyUpBody(PC8801::WinKeyIF* self, uint vkcode, uint32 keydata)
#endif
{
#ifdef M88_LINUX_PORT
	WinKeyIF* self = this;
#endif
	uint keyindex = (vkcode & 0xff) | (keydata & (1<<24) ? 0x100 : 0);
	self->keystate[keyindex] = 0;
	LOG2("KeyUp   = %.2x %.3x\n", vkcode, keyindex);
	const uint vk_up = vkcode & 0xff;
	if (vk_up == VK_CAPITAL || vk_up == VK_SCROLL) {
#ifdef M88_LINUX_PORT
		ClearGraphIfAt101Host();
		SyncKeyboardArray(self->keyboard, self->keystate);
		self->InvalidateKeyports();
#endif
		return;
	}
	if ((vkcode & 0xff) == VK_RETURN) {
		self->keystate[VK_RETURN] = 0;
		self->keystate[VK_RETURN | 0x100] = 0;
	}
	
	// SHIFT + テンキーによる押しっぱなし現象対策
	
	if (self->keytable == KeyTable106[0] || self->keytable == KeyTable101[0])
	{
		switch (keyindex)
		{
		case VK_NUMPAD0: case VK_INSERT:
			self->keystate[VK_NUMPAD0] = self->keystate[VK_INSERT] = 0;
			break;
		case VK_NUMPAD1: case VK_END:
			self->keystate[VK_NUMPAD1] = self->keystate[VK_END] = 0;
			break;
		case VK_NUMPAD2: case VK_DOWN:
			self->keystate[VK_NUMPAD2] = self->keystate[VK_DOWN] = 0;
			break;
		case VK_NUMPAD3: case VK_NEXT:
			self->keystate[VK_NUMPAD3] = self->keystate[VK_NEXT] = 0;
			break;
		case VK_NUMPAD4: case VK_LEFT:
			self->keystate[VK_NUMPAD4] = self->keystate[VK_LEFT] = 0;
			break;
		case VK_NUMPAD5: case VK_CLEAR:
			self->keystate[VK_NUMPAD5] = self->keystate[VK_CLEAR] = 0;
			break;
		case VK_NUMPAD6: case VK_RIGHT:
			self->keystate[VK_NUMPAD6] = self->keystate[VK_RIGHT] = 0;
			break;
		case VK_NUMPAD7: case VK_HOME:
			self->keystate[VK_NUMPAD7] = self->keystate[VK_HOME] = 0;
			break;
		case VK_NUMPAD8: case VK_UP:
			self->keystate[VK_NUMPAD8] = self->keystate[VK_UP] = 0;
			break;
		case VK_NUMPAD9: case VK_PRIOR:
			self->keystate[VK_NUMPAD9] = self->keystate[VK_PRIOR] = 0;
			break;
		}
	}
	if (vk_up == VK_LSHIFT || vk_up == VK_RSHIFT || vk_up == VK_SHIFT) {
		if (!self->keystate[VK_LSHIFT] && !self->keystate[VK_RSHIFT]) {
			self->keystate[VK_SHIFT] = 0;
		}
	}
#ifdef M88_LINUX_PORT
	if (self->host_shift_refs_ > 0) {
		self->keystate[VK_LSHIFT] = 1;
		self->keystate[VK_SHIFT] = 1;
	}
	ClearGraphIfAt101Host();
	SyncKeyboardArray(self->keyboard, self->keystate);
	self->InvalidateKeyports();
#endif
}

// ---------------------------------------------------------------------------
//	WM_KEYUP
//
void WinKeyIF::KeyUp(uint vkcode, uint32 keydata)
{
#ifdef M88_LINUX_PORT
	CriticalSection::Lock lock(key_mutex_);
	if (HalfKanaIme::SessionActive()) {
		return;
	}
	uint vk = vkcode & 0xff;
	if (host_keytype_ == Config::AT101 && vk == VK_MENU) {
		return;
	}
	if (host_keytype_ == Config::AT101 && vk == VK_SHIFT) {
		vkcode = (vkcode & ~0xffu) | VK_LSHIFT;
		vk = VK_LSHIFT;
	}
	const bool ime_path = ImeInjectionActive();
	if (!ime_path && ApplyLinuxKeyFixupUp(vk, keydata)) {
		return;
	}
	KeyUpImpl(vkcode, keydata);
#else
	KeyUpBody(this, vkcode, keydata);
#endif
}

// ---------------------------------------------------------------------------
//	Key
//	keyboard によるキーチェックは反応が鈍いかも知れず
//
uint WinKeyIF::GetKey(const Key* key)
{
	uint i;

	for (i=0; i<8 && key->k; i++, key++)
	{
		switch (key->f)
		{
		case lock:
			if (keyboard[key->k] & 0x01)
				return 0;
			break;

		case keyb:
			if (keyboard[key->k] & 0x80)
				return 0;
			break;
		
		case nex:
			if (keystate[key->k])
				return 0;
			break;

		case ext:
			if (keystate[key->k | 0x100])
				return 0;
			break;

		case arrowten:
			if (usearrow && (keyboard[key->k] & 0x80))
				return 0;
			break;

		case noarrowten:
			if (!usearrow && (keyboard[key->k] & 0x80))
				return 0;
			break;

		case noarrowtenex:
			if (!usearrow && keystate[key->k | 0x100])
				return 0;
			break;

		case pc80sft:
			if (pc80mode && ((keyboard[VK_DOWN] & 0x80) || (keyboard[VK_LEFT] & 0x80)))
				return 0;
			break;

		case pc80key:
			if (pc80mode && (keyboard[key->k] & 0x80))
				return 0;
			break;

		default:
			if (keystate[key->k] | keystate[key->k | 0x100]) // & 0x80)
				return 0;
			break;
		}
	}
	return 1;
}

// ---------------------------------------------------------------------------
//	VSync 処理
//
void IOCALL WinKeyIF::VSync(uint,uint d)
{
	if (d && active)
	{
#ifdef M88_LINUX_PORT
		CriticalSection::Lock lock(key_mutex_);
#endif
#ifndef M88_LINUX_PORT
		if (hwnd)
		{
			PostMessage(hwnd, WM_M88_SENDKEYSTATE, 
				reinterpret_cast<DWORD>(keyboard), (DWORD) hevent);
			WaitForSingleObject(hevent, 10);
		}
#endif

		if (keytable == KeyTable106[0] || keytable == KeyTable101[0])
		{
			keystate[0xf4] = Max(keystate[0xf4] - 1, 0);
		}
		for (int i=0; i<16; i++)
		{
			keyport[i] = -1;
		}
	}
}

void WinKeyIF::Activate(bool yes)
{
#ifdef M88_LINUX_PORT
	CriticalSection::Lock lock(key_mutex_);
#endif
	active = yes;
	if (active)
	{
		memset(keystate, 0, 512);
		for (int i=0; i<16; i++)
		{
			keyport[i] = -1;
		}
	}
}

void WinKeyIF::Disable(bool yes)
{
	disable = yes;
}

// ---------------------------------------------------------------------------
//	キー入力
//
uint IOCALL WinKeyIF::In(uint port)
{
#ifdef M88_LINUX_PORT
	CriticalSection::Lock lock(key_mutex_);
#endif
	port &= 0x0f;
	
	if (active)
	{
		int r = keyport[port];
		if (r == -1)
		{
			const Key* key = keytable + port * 64 + 56;
			r=0;
			for (int i=0; i<8; i++)
			{
				r = r * 2 + GetKey(key);
				key -= 8;
			}
			keyport[port] = r;
			if (port == 0x0d)
			{	
				LOG3("In(13)   = %.2x %.2x %.2x\n", r, keystate[0xf4], keystate[0x1f4]);
			}
		}
		return uint8(r);
	}
	else
		return 0xff;
}

// ---------------------------------------------------------------------------
//	キー対応表
//	ひとつのキーに書けるエントリ数は８個まで。
//	８個未満の場合は最後に TERM を付けること。
//	
//	KEYF の f は次のどれか。
//	nex		WM_KEYxxx の extended フラグが 0 のキーのみ
//	ext		WM_KEYxxx の extended フラグが 1 のキーのみ
//	lock	ロック機能を持つキー (別に CAPS LOCK やカナのように物理的
//								  ロック機能を持っている必要は無いはず)
//	arrowten 方向キーをテンキーに対応させる場合のみ
//

#define KEY(k)     { k, 0 }
#define KEYF(k,f)  { k, f }
#define TERM       { 0, 0 }

// ---------------------------------------------------------------------------
//	キー対応表 for 日本語 106 キーボード
//
const WinKeyIF::Key WinKeyIF::KeyTable106[16 * 8][8] =
{
	// 00
	{ KEY(VK_NUMPAD0), KEYF(VK_INSERT, nex), TERM, },	// num 0
	{ KEY(VK_NUMPAD1), KEYF(VK_END,	   nex), TERM, },	// num 1
	{ KEY(VK_NUMPAD2), KEYF(VK_DOWN,   nex), KEYF(VK_DOWN, arrowten), TERM, },
	{ KEY(VK_NUMPAD3), KEYF(VK_NEXT,   nex), TERM, },	// num 3
	{ KEY(VK_NUMPAD4), KEYF(VK_LEFT,   nex), KEYF(VK_LEFT, arrowten), TERM, },
	{ KEY(VK_NUMPAD5), KEYF(VK_CLEAR,  nex), TERM, },	// num 5
	{ KEY(VK_NUMPAD6), KEYF(VK_RIGHT,  nex), KEYF(VK_RIGHT,arrowten), TERM, },
	{ KEY(VK_NUMPAD7), KEYF(VK_HOME,   nex), TERM, },	// num 7
	
	// 01
	{ KEY(VK_NUMPAD8), KEYF(VK_UP,     nex), KEYF(VK_UP  , arrowten), TERM, },
	{ KEY(VK_NUMPAD9), KEYF(VK_PRIOR,  nex), TERM, },	// num 9
	{ KEY(VK_MULTIPLY), TERM, },						// num *
	{ KEY(VK_ADD),		TERM, },						// num +
	{ KEY(0x92),		TERM, },						// num =
	{ KEY(VK_SEPARATOR), KEYF(VK_DELETE, nex), TERM, },	// num ,
	{ KEY(VK_DECIMAL),	TERM, },						// num .
	{ KEY(VK_RETURN),	TERM, },						// RET

	// 02
	{ KEY(0xc0),TERM },	// @
	{ KEY('A'),	TERM }, // A
	{ KEY('B'),	TERM }, // B
	{ KEY('C'),	TERM }, // C
	{ KEY('D'),	TERM }, // D
	{ KEY('E'),	TERM }, // E
	{ KEY('F'),	TERM }, // F
	{ KEY('G'),	TERM }, // G

	// 03
	{ KEY('H'),	TERM }, // H
	{ KEY('I'),	TERM }, // I
	{ KEY('J'),	TERM }, // J
	{ KEY('K'),	TERM }, // K
	{ KEY('L'),	TERM }, // L
	{ KEY('M'),	TERM }, // M
	{ KEY('N'),	TERM }, // N
	{ KEY('O'),	TERM }, // O

	// 04
	{ KEY('P'),	TERM }, // P
	{ KEY('Q'),	TERM }, // Q
	{ KEY('R'),	TERM }, // R
	{ KEY('S'),	TERM }, // S
	{ KEY('T'),	TERM }, // T
	{ KEY('U'),	TERM }, // U
	{ KEY('V'),	TERM }, // V
	{ KEY('W'),	TERM }, // W

	// 05
	{ KEY('X'),	TERM }, // X
	{ KEY('Y'),	TERM }, // Y
	{ KEY('Z'),	TERM }, // Z
	{ KEY(0xdb),TERM }, // [
	{ KEY(0xdc),TERM }, // \ 
	{ KEY(0xdd),TERM }, // ]
	{ KEY(0xde),TERM }, // ^
	{ KEY(0xbd),TERM }, // -

	// 06
	{ KEY('0'),	TERM }, // 0
	{ KEY('1'),	TERM }, // 1
	{ KEY('2'),	TERM }, // 2
	{ KEY('3'),	TERM }, // 3
	{ KEY('4'),	TERM }, // 4
	{ KEY('5'),	TERM }, // 5
	{ KEY('6'),	TERM }, // 6
	{ KEY('7'),	TERM }, // 7

	// 07
	{ KEY('8'),	TERM }, // 8
	{ KEY('9'),	TERM }, // 9
	{ KEY(0xba),TERM }, // :
	{ KEY(0xbb),TERM }, // ;
	{ KEY(0xbc),TERM }, // ,
	{ KEY(0xbe),TERM }, // .
	{ KEY(0xbf),TERM }, // /
	{ KEY(0xe2),TERM }, // _

	// 08
	{ KEYF(VK_HOME, ext),	TERM }, // CLR
	{ KEYF(VK_UP, noarrowtenex),	KEYF(VK_DOWN, pc80key), TERM }, // ↑
	{ KEYF(VK_RIGHT, noarrowtenex),	KEYF(VK_LEFT, pc80key), TERM }, // →
	{ KEY(VK_BACK),	KEYF(VK_INSERT, ext), KEYF(VK_DELETE, ext), TERM }, // BS
	{ KEY(VK_MENU),			TERM }, // GRPH
	{ KEYF(VK_SCROLL, lock),TERM }, // カナ
	{ KEY(VK_SHIFT), KEY(VK_F6), KEY(VK_F7), KEY(VK_F8), KEY(VK_F9), KEY(VK_F10), KEYF(VK_INSERT, ext), KEYF(1, pc80sft) }, // SHIFT
	{ KEY(VK_CONTROL),		TERM }, // CTRL

	// 09
	{ KEY(VK_F11),KEY(VK_PAUSE), TERM }, // STOP
	{ KEY(VK_F1), KEY(VK_F6),	TERM }, // F1
	{ KEY(VK_F2), KEY(VK_F7),	TERM }, // F2
	{ KEY(VK_F3), KEY(VK_F8),	TERM }, // F3
	{ KEY(VK_F4), KEY(VK_F9),	TERM }, // F4
	{ KEY(VK_F5), KEY(VK_F10),	TERM }, // F5
	{ KEY(VK_SPACE), KEY(VK_CONVERT), KEY(VK_NONCONVERT), TERM }, // SPACE
	{ KEY(VK_ESCAPE),	TERM }, // ESC

	// 0a
	{ KEY(VK_TAB),			TERM }, // TAB
	{ KEYF(VK_DOWN, noarrowtenex),	TERM }, // ↓
	{ KEYF(VK_LEFT, noarrowtenex),	TERM }, // ←
	{ KEYF(VK_END, ext), KEY(VK_HELP), TERM }, // HELP
	{ KEY(VK_F12), TERM }, // COPY
	{ KEY(0x6d),			TERM }, // -
	{ KEY(0x6f),			TERM }, // /
	{ KEYF(VK_CAPITAL, lock), TERM }, // CAPS LOCK

	// 0b
	{ KEYF(VK_NEXT, ext),	TERM }, // ROLL DOWN
	{ KEYF(VK_PRIOR, ext),	TERM }, // ROLL UP
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },

	// 0c
	{ KEY(VK_F6), TERM },			// F6
	{ KEY(VK_F7), TERM },			// F7
	{ KEY(VK_F8), TERM },			// F8
	{ KEY(VK_F9), TERM },			// F9
	{ KEY(VK_F10), TERM },			// F10
	{ KEY(VK_BACK), TERM },			// BS
	{ KEYF(VK_INSERT, ext), TERM },	// INS
	{ KEYF(VK_DELETE, ext), TERM },	// DEL

	// 0d
	{ KEY(VK_CONVERT), TERM },		// 変換
	{ KEY(VK_NONCONVERT), KEY(VK_ACCEPT), TERM }, // 決定
	{ TERM },						// PC
	{ KEY(0xf4), TERM },			// 全角
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },

	// 0e
	{ KEYF(VK_RETURN, nex), TERM },		// RET FK
	{ KEYF(VK_RETURN, ext), TERM },		// RET 10
	{ KEY(VK_LSHIFT), TERM },		// SHIFT L
	{ KEY(VK_RSHIFT), TERM },		// SHIFT R
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },

	// 0f
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
};


// ---------------------------------------------------------------------------
//	キー対応表 for 101 キーボード
//
const WinKeyIF::Key WinKeyIF::KeyTable101[16 * 8][8] =
{
	// 00
	{ KEY(VK_NUMPAD0), KEYF(VK_INSERT, nex), TERM, },	// num 0
	{ KEY(VK_NUMPAD1), KEYF(VK_END,	   nex), TERM, },	// num 1
	{ KEY(VK_NUMPAD2), KEYF(VK_DOWN,   nex), KEYF(VK_DOWN, arrowten), TERM, },
	{ KEY(VK_NUMPAD3), KEYF(VK_NEXT,   nex), TERM, },	// num 3
	{ KEY(VK_NUMPAD4), KEYF(VK_LEFT,   nex), KEYF(VK_LEFT, arrowten), TERM, },
	{ KEY(VK_NUMPAD5), KEYF(VK_CLEAR,  nex), TERM, },	// num 5
	{ KEY(VK_NUMPAD6), KEYF(VK_RIGHT,  nex), KEYF(VK_RIGHT,arrowten), TERM, },
	{ KEY(VK_NUMPAD7), KEYF(VK_HOME,   nex), TERM, },	// num 7
	
	// 01
	{ KEY(VK_NUMPAD8), KEYF(VK_UP,     nex), KEYF(VK_UP  , arrowten), TERM, },
	{ KEY(VK_NUMPAD9), KEYF(VK_PRIOR,  nex), TERM, },	// num 9
	{ KEY(VK_MULTIPLY), TERM, },						// num *
	{ KEY(VK_ADD),		TERM, },						// num +
	{ KEY(0x92),		TERM, },						// num =
	{ KEY(VK_SEPARATOR), KEYF(VK_DELETE, nex), TERM, },	// num ,
	{ KEY(VK_DECIMAL),	TERM, },						// num .
	{ KEY(VK_RETURN),	TERM, },						// RET

	// 02
	{ KEY(0xdb),TERM },	// @
	{ KEY('A'),	TERM }, // A
	{ KEY('B'),	TERM }, // B
	{ KEY('C'),	TERM }, // C
	{ KEY('D'),	TERM }, // D
	{ KEY('E'),	TERM }, // E
	{ KEY('F'),	TERM }, // F
	{ KEY('G'),	TERM }, // G

	// 03
	{ KEY('H'),	TERM }, // H
	{ KEY('I'),	TERM }, // I
	{ KEY('J'),	TERM }, // J
	{ KEY('K'),	TERM }, // K
	{ KEY('L'),	TERM }, // L
	{ KEY('M'),	TERM }, // M
	{ KEY('N'),	TERM }, // N
	{ KEY('O'),	TERM }, // O

	// 04
	{ KEY('P'),	TERM }, // P
	{ KEY('Q'),	TERM }, // Q
	{ KEY('R'),	TERM }, // R
	{ KEY('S'),	TERM }, // S
	{ KEY('T'),	TERM }, // T
	{ KEY('U'),	TERM }, // U
	{ KEY('V'),	TERM }, // V
	{ KEY('W'),	TERM }, // W

	// 05
	{ KEY('X'),	TERM }, // X
	{ KEY('Y'),	TERM }, // Y
	{ KEY('Z'),	TERM }, // Z
	{ KEY(0xdd),TERM }, // [ ok
	{ KEY(0xdc),TERM }, // \ ok
	{ KEY(0xc0),TERM }, // ]
	{ KEY(0xbb),TERM }, // ^ ok
	{ KEY(0xbd),TERM }, // - ok

	// 06
	{ KEY('0'),	TERM }, // 0
	{ KEY('1'),	TERM }, // 1
	{ KEY('2'),	TERM }, // 2
	{ KEY('3'),	TERM }, // 3
	{ KEY('4'),	TERM }, // 4
	{ KEY('5'),	TERM }, // 5
	{ KEY('6'),	TERM }, // 6
	{ KEY('7'),	TERM }, // 7

	// 07
	{ KEY('8'),	TERM }, // 8
	{ KEY('9'),	TERM }, // 9
	{ KEY(0xba),TERM }, // :
	{ KEY(0xde),TERM }, // ;
	{ KEY(0xbc),TERM }, // ,
	{ KEY(0xbe),TERM }, // .
	{ KEY(0xbf),TERM }, // /
	{ KEY(0xe2),TERM }, // _

	// 08
	{ KEYF(VK_HOME, ext),	TERM }, // CLR
	{ KEYF(VK_UP, noarrowtenex),	KEYF(VK_DOWN, pc80key), TERM }, // ↑
	{ KEYF(VK_RIGHT, noarrowtenex),	KEYF(VK_LEFT, pc80key), TERM }, // →
	{ KEY(VK_BACK), KEYF(VK_DELETE, ext), TERM }, // BS
	{ KEY(VK_MENU),			TERM }, // GRPH
	{ KEYF(VK_SCROLL, lock),TERM }, // カナ
	{ KEY(VK_SHIFT), KEY(VK_F6), KEY(VK_F7), KEY(VK_F8), KEY(VK_F9), KEY(VK_F10), KEYF(1, pc80sft) }, // SHIFT
	{ KEY(VK_CONTROL),		TERM }, // CTRL

	// 09
	{ KEY(VK_F11),KEY(VK_PAUSE), TERM }, // STOP
	{ KEY(VK_F1), KEY(VK_F6),	TERM }, // F1
	{ KEY(VK_F2), KEY(VK_F7),	TERM }, // F2
	{ KEY(VK_F3), KEY(VK_F8),	TERM }, // F3
	{ KEY(VK_F4), KEY(VK_F9),	TERM }, // F4
	{ KEY(VK_F5), KEY(VK_F10),	TERM }, // F5
	{ KEY(VK_SPACE), KEY(VK_CONVERT), KEY(VK_NONCONVERT), TERM }, // SPACE
	{ KEY(VK_ESCAPE),	TERM }, // ESC

	// 0a
	{ KEY(VK_TAB),			TERM }, // TAB
	{ KEYF(VK_DOWN, noarrowtenex),	TERM }, // ↓
	{ KEYF(VK_LEFT, noarrowtenex),	TERM }, // ←
	{ KEYF(VK_END, ext), KEY(VK_HELP), TERM }, // HELP
	{ KEY(VK_F12), TERM }, // COPY
	{ KEY(0x6d),			TERM }, // -
	{ KEY(0x6f),			TERM }, // /
	{ KEYF(VK_CAPITAL, lock), TERM }, // CAPS LOCK

	// 0b
	{ KEYF(VK_NEXT, ext),	TERM }, // ROLL DOWN
	{ KEYF(VK_PRIOR, ext),	TERM }, // ROLL UP
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },

	// 0c
	{ KEY(VK_F6), TERM },			// F6
	{ KEY(VK_F7), TERM },			// F7
	{ KEY(VK_F8), TERM },			// F8
	{ KEY(VK_F9), TERM },			// F9
	{ KEY(VK_F10), TERM },			// F10
	{ KEY(VK_BACK), TERM },			// BS
	{ KEYF(VK_INSERT, ext), TERM },	// INS
	{ KEYF(VK_DELETE, ext), TERM },	// DEL

	// 0d
	{ KEY(VK_CONVERT), TERM },		// 変換
	{ KEY(VK_NONCONVERT), KEY(VK_ACCEPT), TERM }, // 決定
	{ TERM },						// PC
	{ KEY(0xf4), TERM },			// 全角
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },

	// 0e
	{ KEYF(VK_RETURN, nex), TERM },		// RET FK
	{ KEYF(VK_RETURN, ext), TERM },		// RET 10
	{ KEY(VK_LSHIFT), TERM },		// SHIFT L
	{ KEY(VK_RSHIFT), TERM },		// SHIFT R
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },

	// 0f
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
};

// ---------------------------------------------------------------------------
//	キー対応表 for 9801 key
//
const WinKeyIF::Key WinKeyIF::KeyTable98[16 * 8][8] =
{
	// 00
	{ KEY(VK_NUMPAD0), TERM, },		// num 0
	{ KEY(VK_NUMPAD1), TERM, },		// num 1
	{ KEY(VK_NUMPAD2), KEYF(VK_DOWN, arrowten), TERM, },		// num 2
	{ KEY(VK_NUMPAD3), TERM, },		// num 3
	{ KEY(VK_NUMPAD4), KEYF(VK_LEFT, arrowten), TERM, },		// num 4
	{ KEY(VK_NUMPAD5), TERM, },		// num 5
	{ KEY(VK_NUMPAD6), KEYF(VK_RIGHT,arrowten), TERM, },		// num 6
	{ KEY(VK_NUMPAD7), TERM, },		// num 7

	// 01
	{ KEY(VK_NUMPAD8), KEYF(VK_UP  , arrowten), TERM, },		// num 8
	{ KEY(VK_NUMPAD9), TERM, },		// num 9
	{ KEY(VK_MULTIPLY), TERM, },	// num *
	{ KEY(VK_ADD),		TERM, },	// num +
	{ KEY(0x92),		TERM, },	// num =
	{ KEY(VK_SEPARATOR), TERM, },	// num ,
	{ KEY(VK_DECIMAL),	TERM, },	// num .
	{ KEY(VK_RETURN),	TERM, },	// RET

	// 02
	{ KEY(0xc0),TERM },	// @
	{ KEY('A'),	TERM }, // A
	{ KEY('B'),	TERM }, // B
	{ KEY('C'),	TERM }, // C
	{ KEY('D'),	TERM }, // D
	{ KEY('E'),	TERM }, // E
	{ KEY('F'),	TERM }, // F
	{ KEY('G'),	TERM }, // G

	// 03
	{ KEY('H'),	TERM }, // H
	{ KEY('I'),	TERM }, // I
	{ KEY('J'),	TERM }, // J
	{ KEY('K'),	TERM }, // K
	{ KEY('L'),	TERM }, // L
	{ KEY('M'),	TERM }, // M
	{ KEY('N'),	TERM }, // N
	{ KEY('O'),	TERM }, // O

	// 04
	{ KEY('P'),	TERM }, // P
	{ KEY('Q'),	TERM }, // Q
	{ KEY('R'),	TERM }, // R
	{ KEY('S'),	TERM }, // S
	{ KEY('T'),	TERM }, // T
	{ KEY('U'),	TERM }, // U
	{ KEY('V'),	TERM }, // V
	{ KEY('W'),	TERM }, // W

	// 05
	{ KEY('X'),	TERM }, // X
	{ KEY('Y'),	TERM }, // Y
	{ KEY('Z'),	TERM }, // Z
	{ KEY(0xdb),TERM }, // [
	{ KEY(0xdc),TERM }, // \ 
	{ KEY(0xdd),TERM }, // ]
	{ KEY(0xde),TERM }, // ^
	{ KEY(0xbd),TERM }, // -

	// 06
	{ KEY('0'),	TERM }, // 0
	{ KEY('1'),	TERM }, // 1
	{ KEY('2'),	TERM }, // 2
	{ KEY('3'),	TERM }, // 3
	{ KEY('4'),	TERM }, // 4
	{ KEY('5'),	TERM }, // 5
	{ KEY('6'),	TERM }, // 6
	{ KEY('7'),	TERM }, // 7

	// 07
	{ KEY('8'),	TERM }, // 8
	{ KEY('9'),	TERM }, // 9
	{ KEY(0xba),TERM }, // :
	{ KEY(0xbb),TERM }, // ;
	{ KEY(0xbc),TERM }, // ,
	{ KEY(0xbe),TERM }, // .
	{ KEY(0xbf),TERM }, // /
	{ KEY(0xdf),TERM }, // _

	// 08
	{ KEY(VK_HOME),		TERM }, // CLR
	{ KEYF(VK_UP, noarrowten),		TERM }, // ↑
	{ KEYF(VK_RIGHT, noarrowten),	TERM }, // →
	{ KEY(VK_BACK),		TERM }, // BS
	{ KEY(VK_MENU),		TERM }, // GRPH
	{ KEYF(0x15, lock),	TERM }, // カナ
	{ KEY(VK_SHIFT), KEY(VK_F6), KEY(VK_F7), KEY(VK_F8), KEY(VK_F9), KEY(VK_F10), KEYF(0, pc80sft) }, // SHIFT
	{ KEY(VK_CONTROL),		TERM }, // CTRL

	// 09
	{ KEY(VK_PAUSE),KEY(VK_F11),KEY(VK_F15),TERM }, // STOP
	{ KEY(VK_F1),	KEY(VK_F6),	TERM }, // F1
	{ KEY(VK_F2),	KEY(VK_F7),	TERM }, // F2
	{ KEY(VK_F3),	KEY(VK_F8),	TERM }, // F3
	{ KEY(VK_F4),	KEY(VK_F9),	TERM }, // F4
	{ KEY(VK_F5),	KEY(VK_F10),TERM }, // F5
	{ KEY(VK_SPACE), KEY(0x1d), KEY(0x19), TERM }, // SPACE
	{ KEY(VK_ESCAPE),	TERM }, // ESC

	// 0a
	{ KEY(VK_TAB),		TERM }, // TAB
	{ KEYF(VK_DOWN, noarrowten),		TERM }, // ↓
	{ KEYF(VK_LEFT, noarrowten),		TERM }, // ←
	{ KEY(VK_END),		TERM }, // HELP
	{ KEY(VK_F14),	KEY(VK_F12),	TERM }, // COPY
	{ KEY(0x6d),		TERM }, // -
	{ KEY(0x6f),		TERM }, // /
	{ KEYF(VK_CAPITAL, lock), TERM }, // CAPS LOCK

	// 0b
	{ KEY(VK_NEXT),		TERM }, // ROLL DOWN
	{ KEY(VK_PRIOR),	TERM }, // ROLL UP
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },
	{ TERM, },

	// 0c
	{ KEY(VK_F1), TERM }, // F1
	{ KEY(VK_F2), TERM }, // F2
	{ KEY(VK_F3), TERM }, // F3
	{ KEY(VK_F4), TERM }, // F4
	{ KEY(VK_F5), TERM }, // F5
	{ KEY(VK_BACK), TERM },	// BS
	{ KEY(VK_INSERT), TERM }, // INS
	{ KEY(VK_DELETE), TERM }, // DEL

	// 0d
	{ KEY(VK_F6),	TERM }, // F6
	{ KEY(VK_F7),	TERM }, // F7
	{ KEY(VK_F8),	TERM }, // F8
	{ KEY(VK_F9),	TERM }, // F9
	{ KEY(VK_F10),	TERM }, // F10
	{ KEY(0x1d),	TERM }, // 変換
	{ KEY(0x19),	TERM }, // 決定
	{ KEY(VK_SPACE), TERM }, // SPACE

	// 0e
	{ KEY(VK_RETURN), TERM }, // RET FK
	{ KEY(VK_RETURN), TERM }, // RET 10
	{ KEY(VK_LSHIFT), TERM }, // SHIFT L
	{ KEY(VK_RSHIFT), TERM }, // SHIFT R
	{ TERM }, // PC
	{ TERM }, // 全角
	{ TERM },
	{ TERM },

	// 0f
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
	{ TERM },
};

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor WinKeyIF::descriptor = { indef, outdef };

const Device::OutFuncPtr WinKeyIF::outdef[] = 
{
	STATIC_CAST(Device::OutFuncPtr, &Reset),
	STATIC_CAST(Device::OutFuncPtr, &VSync),
};

const Device::InFuncPtr WinKeyIF::indef[] = 
{
	STATIC_CAST(Device::InFuncPtr, &In),
};
