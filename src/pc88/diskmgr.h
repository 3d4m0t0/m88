// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: diskmgr.h,v 1.8 1999/06/19 14:06:22 cisc Exp $

#pragma once

#include "floppy.h"
#include "file.h"
#include "fdu.h"
#include "critsect.h"

namespace D88
{
	struct ImageHeader
	{
		char title[17];
		uint8 reserved[9];
		uint8 readonly;
		uint8 disktype;
		uint32 disksize;
		uint32 trackptr[164];
	};

	struct SectorHeader
	{
		FloppyDisk::IDR id;
		uint16 sectors;
		uint8 density;
		uint8 deleted;
		uint8 status;
		uint8 reserved[5];
		uint16 length;
	};
}

// ---------------------------------------------------------------------------

class DiskImageHolder
{
public:
	enum
	{
		max_disks = 64,
	};

public:
	DiskImageHolder();
	~DiskImageHolder();

	bool Open(const char* filename, bool readonly, bool create);
	bool Connect(const char* filename);
	bool Disconnect();

	const char* GetTitle(int index);
	FileIO* GetDisk(int index);
	uint GetNumDisks() { return ndisks; }
	bool SetDiskSize(int index, int newsize);
	bool IsReadOnly() { return readonly; }
	uint IsOpen() { return ref > 0; }
	bool AddDisk(const char* title, uint type);

private:
	struct DiskInfo
	{
		char title[20];
		int32 pos;
		int32 size;
	};
	bool ReadHeaders();
	void Close();
	bool IsValidHeader(D88::ImageHeader&);
	
	FileIO fio;
	int ndisks;
	int ref;
	bool readonly;
	DiskInfo disks[max_disks];
	char diskname[MAX_PATH];
};

// Combine one or more .d88 files into a single multi-disk image.
// Each input file may already contain multiple disks; all are appended in order.
// Returns the number of disks written via disks_written (optional).
bool CombineDiskImages(const char* output_path, const char* const* input_paths,
                       int num_inputs, int* disks_written = nullptr);

struct MultiDiskSlot {
  enum class Kind { Empty, FromFile, Blank };
  Kind kind = Kind::Empty;
  char file_path[MAX_PATH];
  int file_disk_index = 0;
  char title[17];
  uint blank_type = 1;  // 0=2D, 1=2DD, 2=2HD
};

namespace D88 {
struct DiskCatalogInfo {
  int32 pos;
  int32 size;
  char title[20];
};
}  // namespace D88

bool ReadDiskImageCatalog(const char* path, D88::DiskCatalogInfo* entries,
                          int max_entries, int* num_disks);
bool WriteMultiDiskImage(const char* output_path, const MultiDiskSlot* slots,
                         int slot_count, int* disks_written = nullptr);

// ---------------------------------------------------------------------------

class DiskManager
{
public:
	enum
	{
		max_drives = 2,
	};

public:
	DiskManager();
	~DiskManager();
	bool Init();

	bool Mount(uint drive, const char* diskname, bool readonly, int index, bool create);
	bool Unmount(uint drive);
	const char* GetImageTitle(uint dr, uint index);
	uint GetNumDisks(uint dr);
	int GetCurrentDisk(uint dr); 
	bool AddDisk(uint dr, const char* title, uint type);
	bool IsImageOpen(const char* filename);
	bool FormatDisk(uint dr);

	void Update();

	void Modified(int drive=-1, int track=-1);
	CriticalSection& GetCS() { return cs; }

	PC8801::FDU* GetFDU(int dr) { return dr < max_drives ? &drive[dr].fdu : 0; }

private:
	struct Drive
	{
		FloppyDisk disk;
		PC8801::FDU fdu;
		DiskImageHolder* holder;
		int index;
		bool sizechanged;

		uint32 trackpos[168];
		int tracksize[168];
		bool modified[168];
	};

	bool ReadDiskImage(FileIO* fio, Drive* drive);
	bool ReadDiskImageRaw(FileIO* fio, Drive* drive);
	bool WriteDiskImage(FileIO* fio, Drive* drive);
	bool WriteTrackImage(FileIO* fio, Drive* drive, int track);
	uint GetDiskImageSize(Drive* drive);
	void UpdateDrive(Drive* drive);

	DiskImageHolder holder[max_drives];
	Drive drive[max_drives];

	CriticalSection cs;
};

