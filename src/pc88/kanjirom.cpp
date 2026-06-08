// ---------------------------------------------------------------------------
//	M88 - PC-88 Emulator.
//	Copyright (C) cisc 1998, 1999.
// ---------------------------------------------------------------------------
//  Š¿Žš ROM
// ---------------------------------------------------------------------------
//	$Id: kanjirom.cpp,v 1.6 2000/02/29 12:29:52 cisc Exp $

#include "headers.h"
#include "file.h"
#include "pc88/kanjirom.h"
#ifdef M88_LINUX_PORT
#include "path.h"
#include "rom_log.h"
#endif

using namespace PC8801;

// ---------------------------------------------------------------------------
//	\’z/Á–Å
// ---------------------------------------------------------------------------

KanjiROM::KanjiROM(const ID& id) : Device(id)
{
	image = 0;
	adr = 0;
}

KanjiROM::~KanjiROM()
{
	delete[] image;
}

// ---------------------------------------------------------------------------
//	‰Šú‰»
//
bool KanjiROM::Init(const char* filename)
{
	if (!image)
		image = new uint8[0x20000];
	if (!image)
		return false;
	memset(image, 0xff, 0x20000);

#ifdef M88_LINUX_PORT
	char path[MAX_PATH];
	M88RomPath(path, sizeof(path), filename);
	FileIO file(path, FileIO::readonly);
#else
	FileIO file(filename, FileIO::readonly);
#endif
	if (file.GetFlags() & FileIO::open)
	{
		file.Seek(0, FileIO::begin);
		file.Read(image, 0x20000);
#ifdef M88_LINUX_PORT
		M88RomLogLoaded(path, "kanji ROM 128K");
#endif
	}
#ifdef M88_LINUX_PORT
	else {
		M88RomLogSkipped(path, "not found, using 0xFF fill");
	}
#endif
	return true;
}

// ---------------------------------------------------------------------------
//	I/O
//
void IOCALL KanjiROM::SetL(uint, uint d)
{
	adr = (adr & ~0xff) | d;
}

void IOCALL KanjiROM::SetH(uint, uint d)
{
	adr = (adr & 0xff) | (d * 0x100);
}

uint IOCALL KanjiROM::ReadL(uint)
{
	return image[adr * 2 + 1];
}

uint IOCALL KanjiROM::ReadH(uint)
{
	return image[adr * 2];
}

// ---------------------------------------------------------------------------
//	device description
//
const Device::Descriptor KanjiROM::descriptor =
{
	KanjiROM::indef, KanjiROM::outdef
};

const Device::OutFuncPtr KanjiROM::outdef[] =
{
	STATIC_CAST(Device::OutFuncPtr, &SetL),
	STATIC_CAST(Device::OutFuncPtr, &SetH),
};

const Device::InFuncPtr KanjiROM::indef[]  =
{
	STATIC_CAST(Device::InFuncPtr, &ReadL),
	STATIC_CAST(Device::InFuncPtr, &ReadH),
};
