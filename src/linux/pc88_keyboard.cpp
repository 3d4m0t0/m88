#include "headers.h"
#include "../win32/WinKeyIF.h"
#include "pc88/pc88.h"

using namespace PC8801;

bool PC88::ConnectKeyboard(WinKeyIF* keyb)
{
	static const IOBus::Connector c_keyb[] = {
		{ pres, IOBus::portout, WinKeyIF::reset },
		{ vrtc, IOBus::portout, WinKeyIF::vsync },
		{ 0x00, IOBus::portin, WinKeyIF::in },
		{ 0x01, IOBus::portin, WinKeyIF::in },
		{ 0x02, IOBus::portin, WinKeyIF::in },
		{ 0x03, IOBus::portin, WinKeyIF::in },
		{ 0x04, IOBus::portin, WinKeyIF::in },
		{ 0x05, IOBus::portin, WinKeyIF::in },
		{ 0x06, IOBus::portin, WinKeyIF::in },
		{ 0x07, IOBus::portin, WinKeyIF::in },
		{ 0x08, IOBus::portin, WinKeyIF::in },
		{ 0x09, IOBus::portin, WinKeyIF::in },
		{ 0x0a, IOBus::portin, WinKeyIF::in },
		{ 0x0b, IOBus::portin, WinKeyIF::in },
		{ 0x0c, IOBus::portin, WinKeyIF::in },
		{ 0x0d, IOBus::portin, WinKeyIF::in },
		{ 0x0e, IOBus::portin, WinKeyIF::in },
		{ 0x0f, IOBus::portin, WinKeyIF::in },
		{ 0, 0, 0 },
	};
	return keyb && bus1.Connect(keyb, c_keyb);
}
