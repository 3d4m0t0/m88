// ---------------------------------------------------------------------------
//	M88 - PC-8801 Emulator
//	Copyright (C) cisc 1999.
// ---------------------------------------------------------------------------
//	$Id: diskmgr.cpp,v 1.13 1999/11/26 10:13:46 cisc Exp $

#include "headers.h"
#include "diskmgr.h"
#include "status.h"
#include "misc.h"

#include <filesystem>
#include <string>
#include <vector>

using namespace D88;

namespace detail {

constexpr int kMaxCatalogDisks = DiskImageHolder::max_disks;
constexpr int32 kCopyBufferSize = 65536;

struct DiskCatalogEntry {
  int32 pos;
  int32 size;
};

bool IsValidImageHeader(const ImageHeader& ih)
{
  ImageHeader copy = ih;
  int i;
  if (copy.disktype == 0) {
    memset(&copy.trackptr[84], 0, 4 * 80);
  }

  for (i = 0; i < 25 && copy.title[i]; i++) {
  }
  if (i == 25) {
    return false;
  }

  if (copy.disksize > 4 * 1024 * 1024) {
    return false;
  }

  uint trackstart = sizeof(ImageHeader);
  for (int t = 0; t < 160; t++) {
    if (copy.trackptr[t] >= copy.disksize) {
      break;
    }
    if (copy.trackptr[t] && copy.trackptr[t] < trackstart) {
      trackstart = copy.trackptr[t];
    }
  }

  if (trackstart < 32 + 4 * 84) {
    return false;
  }

  return true;
}

bool ReadDiskCatalog(FileIO& fio, DiskCatalogEntry* entries, int* num_disks)
{
  if (!entries || !num_disks) {
    return false;
  }

  fio.SetLogicalOrigin(0);
  fio.Seek(0, FileIO::end);
  const int32 file_size = fio.Tellp();
  if (file_size <= 0) {
    return false;
  }

  fio.Seek(0, FileIO::begin);

  ImageHeader ih;
  int ndisks = 0;
  for (; ndisks < kMaxCatalogDisks; ndisks++) {
    DiskCatalogEntry& entry = entries[ndisks];
    entry.pos = fio.Tellp();
    if (entry.pos < 0 || entry.pos >= file_size) {
      break;
    }

    if (fio.Read(&ih, sizeof(ImageHeader)) < 256 + 16) {
      break;
    }

    if (memcmp(ih.title, "M88 RawDiskImage", 16)) {
      if (!IsValidImageHeader(ih)) {
        break;
      }
      entry.size = static_cast<int32>(ih.disksize);
      if (entry.pos + entry.size > file_size) {
        return false;
      }
      fio.Seek(entry.pos + entry.size, FileIO::begin);
    } else {
      if (ndisks != 0) {
        return false;
      }
      entry.size = file_size - entry.pos;
      fio.Seek(file_size, FileIO::begin);
    }
  }

  *num_disks = ndisks;
  return ndisks > 0;
}

bool CopyFileRegion(FileIO& src, int32 src_pos, int32 size, FileIO& dst)
{
  if (size <= 0) {
    return true;
  }

  std::vector<uint8> buffer(kCopyBufferSize);
  src.SetLogicalOrigin(0);
  if (!src.Seek(src_pos, FileIO::begin)) {
    return false;
  }

  int32 remain = size;
  while (remain > 0) {
    const int32 chunk = Min(remain, kCopyBufferSize);
    if (src.Read(buffer.data(), chunk) != chunk) {
      return false;
    }
    if (dst.Write(buffer.data(), chunk) != chunk) {
      return false;
    }
    remain -= chunk;
  }
  return true;
}

bool MakeBlankDiskBlob(const char* title, uint type, std::vector<uint8>& out)
{
  ImageHeader ih {};
  if (title) {
    strncpy(ih.title, title, 16);
  }
  ih.disktype = static_cast<uint8>(type * 0x10);
  ih.disksize = sizeof(ImageHeader);
  out.resize(sizeof(ImageHeader));
  memcpy(out.data(), &ih, sizeof(ImageHeader));
  return true;
}

bool PatchDiskBlobTitle(std::vector<uint8>& blob, const char* title)
{
  if (!title || !title[0] || blob.size() < sizeof(ImageHeader)) {
    return false;
  }
  ImageHeader* ih = reinterpret_cast<ImageHeader*>(blob.data());
  memset(ih->title, 0, sizeof(ih->title));
  strncpy(ih->title, title, 16);
  return true;
}

bool ExtractDiskBlob(const char* path, int disk_index, std::vector<uint8>& out)
{
  if (!path || !*path || disk_index < 0) {
    return false;
  }

  FileIO in;
  if (!in.Open(path, FileIO::readonly)) {
    return false;
  }

  DiskCatalogEntry catalog[kMaxCatalogDisks];
  int ndisks = 0;
  if (!ReadDiskCatalog(in, catalog, &ndisks) || disk_index >= ndisks) {
    return false;
  }

  out.resize(static_cast<size_t>(catalog[disk_index].size));
  in.SetLogicalOrigin(0);
  if (!in.Seek(catalog[disk_index].pos, FileIO::begin)) {
    return false;
  }
  if (in.Read(out.data(), catalog[disk_index].size) != catalog[disk_index].size) {
    return false;
  }
  return true;
}

bool WriteBlobFile(const char* output_path, const std::vector<std::vector<uint8>>& blobs)
{
  const std::filesystem::path out_path(output_path);
  const std::filesystem::path temp_path = out_path.string() + ".tmp";

  FileIO out;
  if (!out.Open(temp_path.string().c_str(), FileIO::create)) {
    return false;
  }

  for (const auto& blob : blobs) {
    if (blob.empty()) {
      continue;
    }
    if (out.Write(blob.data(), static_cast<int32>(blob.size())) !=
        static_cast<int32>(blob.size())) {
      out.Close();
      std::filesystem::remove(temp_path);
      return false;
    }
  }
  out.Close();

  std::error_code ec;
  std::filesystem::remove(out_path, ec);
  ec.clear();
  std::filesystem::rename(temp_path, out_path, ec);
  if (ec) {
    std::filesystem::remove(temp_path);
    return false;
  }
  return true;
}

}  // namespace detail

bool ReadDiskImageCatalog(const char* path, D88::DiskCatalogInfo* entries,
                          int max_entries, int* num_disks)
{
  if (!path || !*path || !entries || !num_disks || max_entries <= 0) {
    return false;
  }

  FileIO in;
  if (!in.Open(path, FileIO::readonly)) {
    return false;
  }

  detail::DiskCatalogEntry catalog[detail::kMaxCatalogDisks];
  int ndisks = 0;
  if (!detail::ReadDiskCatalog(in, catalog, &ndisks)) {
    return false;
  }

  const int count = Min(ndisks, max_entries);
  for (int i = 0; i < count; ++i) {
    entries[i].pos = catalog[i].pos;
    entries[i].size = catalog[i].size;
    entries[i].title[0] = '\0';

    in.SetLogicalOrigin(0);
    in.Seek(catalog[i].pos, FileIO::begin);
    ImageHeader ih {};
    if (in.Read(&ih, sizeof(ImageHeader)) == static_cast<int32>(sizeof(ImageHeader))) {
      if (memcmp(ih.title, "M88 RawDiskImage", 16) == 0) {
        strncpy(entries[i].title, "(no name)", sizeof(entries[i].title) - 1);
      } else {
        strncpy(entries[i].title, ih.title, 16);
      }
      entries[i].title[sizeof(entries[i].title) - 1] = '\0';
    }
  }

  *num_disks = count;
  return count > 0;
}

bool WriteMultiDiskImage(const char* output_path, const MultiDiskSlot* slots,
                         int slot_count, int* disks_written)
{
  if (disks_written) {
    *disks_written = 0;
  }
  if (!output_path || !*output_path || !slots || slot_count <= 0) {
    return false;
  }

  std::vector<std::vector<uint8>> blobs;
  blobs.reserve(static_cast<size_t>(slot_count));

  for (int i = 0; i < slot_count; ++i) {
    const MultiDiskSlot& slot = slots[i];
    if (slot.kind == MultiDiskSlot::Kind::Empty) {
      continue;
    }

    std::vector<uint8> blob;
    if (slot.kind == MultiDiskSlot::Kind::FromFile) {
      if (!detail::ExtractDiskBlob(slot.file_path, slot.file_disk_index, blob)) {
        statusdisplay.Show(80, 3000, "ディスクイメージの連結に失敗しました");
        return false;
      }
      if (slot.title[0]) {
        detail::PatchDiskBlobTitle(blob, slot.title);
      }
    } else if (slot.kind == MultiDiskSlot::Kind::Blank) {
      if (!detail::MakeBlankDiskBlob(slot.title, slot.blank_type, blob)) {
        return false;
      }
    } else {
      continue;
    }
    blobs.push_back(std::move(blob));
  }

  if (blobs.empty()) {
    return false;
  }

  if (!detail::WriteBlobFile(output_path, blobs)) {
    statusdisplay.Show(80, 3000, "?f?B?X?N?C???[?W??A??????s???????");
    return false;
  }

  if (disks_written) {
    *disks_written = static_cast<int>(blobs.size());
  }
  return true;
}

bool CombineDiskImages(const char* output_path, const char* const* input_paths,
                       int num_inputs, int* disks_written)
{
  if (disks_written) {
    *disks_written = 0;
  }
  if (!output_path || !*output_path || !input_paths || num_inputs <= 0) {
    return false;
  }

  FileIO out;
  if (!out.Open(output_path, FileIO::create)) {
    statusdisplay.Show(80, 3000, "?A????f?B?X?N?C???[?W????????????");
    return false;
  }

  detail::DiskCatalogEntry catalog[detail::kMaxCatalogDisks];
  int total_disks = 0;

  for (int i = 0; i < num_inputs; i++) {
    const char* path = input_paths[i];
    if (!path || !*path) {
      statusdisplay.Show(80, 3000, "????t?@?C?????????????");
      out.Close();
      return false;
    }

    FileIO in;
    if (!in.Open(path, FileIO::readonly)) {
      statusdisplay.Show(80, 3000, "?f?B?X?N?C???[?W???J???????");
      out.Close();
      return false;
    }

    int ndisks = 0;
    if (!detail::ReadDiskCatalog(in, catalog, &ndisks)) {
      statusdisplay.Show(90, 3000, "?C???[?W???????f?[?^???????????");
      in.Close();
      out.Close();
      return false;
    }

    for (int d = 0; d < ndisks; d++) {
      if (total_disks >= detail::kMaxCatalogDisks) {
        statusdisplay.Show(80, 3000, "?A???????f?B?X?N????????B???????");
        in.Close();
        out.Close();
        return false;
      }

      if (!detail::CopyFileRegion(in, catalog[d].pos, catalog[d].size, out)) {
        statusdisplay.Show(80, 3000, "?f?B?X?N?C???[?W??A??????s???????");
        in.Close();
        out.Close();
        return false;
      }
      total_disks++;
    }

    in.Close();
  }

  out.Close();
  if (disks_written) {
    *disks_written = total_disks;
  }
  return total_disks > 0;
}



// ---------------------------------------------------------------------------
//	?\?z?E?j??
//
DiskImageHolder::DiskImageHolder()
{
	ref = 0;
}

DiskImageHolder::~DiskImageHolder()
{
	Close();
}

// ---------------------------------------------------------------------------
//	?t?@?C?????J??
//
bool DiskImageHolder::Open(const char* filename, bool ro, bool create)
{
	// ????????????t?@?C????????????m?F
	if (Connect(filename))
		return true;
	
	if (ref > 0)
		return false;
	
	// ?t?@?C?????J??
	readonly = ro;
	
	if (readonly || !fio.Open(filename, 0))
	{
		if (fio.Open(filename, FileIO::readonly))
		{
			if (!readonly)
				statusdisplay.Show(100, 3000, "????p?t?@?C?????");
			readonly = true;
		}
		else
		{
			// ?V?????f?B?X?N?C???[?W?H
			if (!create || !fio.Open(filename, FileIO::create))
			{
				statusdisplay.Show(80, 3000, "?f?B?X?N?C???[?W???J???????");
				return false;
			}
		}
	}
	
	// ?t?@?C??????o?^
	strncpy(diskname, filename, MAX_PATH-1);
	diskname[MAX_PATH-1] = 0;

	if (!ReadHeaders())
		return false;
	
	ref = 1;
	return true;
}

// ---------------------------------------------------------------------------
//	?V?????f?B?X?N?C???[?W????????
//	type:	2D 0 / 2DD 1 / 2HD 2
//
bool DiskImageHolder::AddDisk(const char* title, uint type)
{
	if (ndisks >= max_disks)
		return false;

	int32 diskpos = 0;
	if (ndisks > 0)
	{
		diskpos = disks[ndisks-1].pos + disks[ndisks-1].size;
	}
	DiskInfo& disk = disks[ndisks++];
	disk.pos = diskpos;
	disk.size = sizeof(ImageHeader);

	ImageHeader ih;
	memset(&ih, 0, sizeof(ImageHeader));
	strncpy(ih.title, title, 16);
	ih.disktype = type * 0x10;
	ih.disksize = sizeof(ImageHeader);
	fio.SetLogicalOrigin(0);
	fio.Seek(diskpos, FileIO::begin);
	fio.Write(&ih, sizeof(ImageHeader));
	return true;
}

// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W??????
//
bool DiskImageHolder::ReadHeaders()
{
	fio.SetLogicalOrigin(0);
	fio.Seek(0, FileIO::end);
	if (fio.Tellp() == 0)
	{
		// new file
		ndisks = 0;
		return true;
	}
	
	fio.Seek(0, FileIO::begin);
	
	ImageHeader ih;
	for (ndisks = 0; ndisks < max_disks; ndisks++)
	{
		// ?w?b?_?[??????
		DiskInfo& disk = disks[ndisks];
		disk.pos = fio.Tellp();
		
		// 256+16 ?? Raw ?C???[?W?????T?C?Y
		if (fio.Read(&ih, sizeof(ImageHeader)) < 256+16)
			break;
		
		if (memcmp(ih.title, "M88 RawDiskImage", 16))
		{
			if (!IsValidHeader(ih))
			{
				statusdisplay.Show(90, 3000, "?C???[?W???????f?[?^???????????");
				break;
			}
			
			strncpy(disk.title, ih.title, 16);
			disk.title[16] = 0;
			disk.size = ih.disksize;
			fio.Seek(disk.pos + disk.size, FileIO::begin);
		}
		else
		{
			if (ndisks != 0)
			{
				statusdisplay.Show(80, 3000, "READER ?n?f?B?X?N?C???[?W??A??????????");
				return false;
			}

			strncpy(disk.title, "(no name)", 16);
			fio.Seek(0, FileIO::end);
			disk.size = fio.Tellp() - disk.pos;
		}
	}
	if (!ndisks)
		return false;

	return true;
}

// ---------------------------------------------------------------------------
//	?????
//
void DiskImageHolder::Close()
{
	fio.Close();
	ndisks = 0;
	diskname[0] = 0;
	ref = 0;
}

// ---------------------------------------------------------------------------
//	Connect
//
bool DiskImageHolder::Connect(const char* filename)
{
	// ????????????t?@?C????????????m?F
	if (!strnicmp(diskname, filename, MAX_PATH))
	{
		ref++;
		return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	Disconnect
//
bool DiskImageHolder::Disconnect()
{
	if (--ref <= 0)
		Close();
	return true;
}

// ---------------------------------------------------------------------------
//	?w?b?_?[???L????????????m?F
//	
bool DiskImageHolder::IsValidHeader(ImageHeader& ih)
{
	int i;
	// 2D ?C???[?W????]?v?????????????????????
	if (ih.disktype == 0)
		memset(&ih.trackptr[84], 0, 4*80);

	// ????: title ?? 25 ??????????????
	for (i=0; i<25 && ih.title[i]; i++)
		;
	if (i==25)
		return false;
	
	// ????: disksize <= 4M
	if (ih.disksize > 4 * 1024 * 1024)
		return false;

	// ????: trackptr[0-159] < disksize
	uint trackstart = sizeof(ImageHeader);
	for (int t=0; t<160; t++)
	{
		if (ih.trackptr[t] >= ih.disksize)
			break;
		if (ih.trackptr[t] && ih.trackptr[t] < trackstart)
			trackstart = uint(ih.trackptr[t]);
	}
	
	// ????: 32+4*84 <= trackstart
	if (trackstart < 32 + 4 * 84)
		return false;
	
	return true;
}

// ---------------------------------------------------------------------------
//	GetTitle
//
const char* DiskImageHolder::GetTitle(int index)
{
	if (index < ndisks)
		return disks[index].title;
	return 0;
}

// ---------------------------------------------------------------------------
//	GetDisk
//
FileIO* DiskImageHolder::GetDisk(int index)
{
	if (index < ndisks)
	{
		fio.SetLogicalOrigin(disks[index].pos);
		return &fio;
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	SetDiskSize
//
bool DiskImageHolder::SetDiskSize(int index, int newsize)
{
	int i;
	if (index >= ndisks)
		return false;

	int32 sizediff = newsize - disks[index].size;
	if (!sizediff)
		return true;

	// ?????????K?v?????f?[?^??T?C?Y???v?Z????
	int32 sizemove=0;
	for (i=index+1; i<ndisks; i++)
	{
		sizemove += disks[i].size;
	}

	fio.SetLogicalOrigin(0);
	if (sizemove)
	{
		int32 moveorg = disks[index+1].pos;
		uint8* data = new uint8[sizemove];
		if (!data)
			return false;

		fio.Seek(moveorg, FileIO::begin);
		fio.Read(data, sizemove);
		fio.Seek(moveorg + sizediff, FileIO::begin);
		fio.Write(data, sizemove);

		delete[] data;

		for (i=index+1; i<ndisks; i++)
			disks[i].pos += sizemove;
	}
	else
	{
		fio.Seek(disks[index].pos + newsize, FileIO::begin);
	}
	fio.SetEndOfFile();
	disks[index].size = newsize;
	return true;
}

// ---------------------------------------------------------------------------
//	?\?z?E?j??
//
DiskManager::DiskManager()
{
	for (int i=0; i<max_drives; i++)
	{
		drive[i].holder = 0;
		drive[i].index = -1;
		drive[i].fdu.Init(this, i);
	}
}

DiskManager::~DiskManager()
{
	for (int i=0; i<max_drives; i++)
		Unmount(i);
}

// ---------------------------------------------------------------------------
//	??????
//
bool DiskManager::Init()
{
	for (int i=0; i<max_drives; i++)
	{
		drive[i].holder = 0;
		if (!drive[i].fdu.Init(this, i))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W??????J??????????????m?F
//	arg:diskname	?f?B?X?N?C???[?W??t?@?C???l?[??
//	
bool DiskManager::IsImageOpen(const char* diskname)
{
	CriticalSection::Lock lock(cs);
	
	for (int i=0; i<max_drives; i++)
	{
		if (holder[i].Connect(diskname))
		{
			holder[i].Disconnect();
			return true;
		}
	}
	return false;
}

// ---------------------------------------------------------------------------
//	Mount
//	arg:dr			Mount ????h???C?u
//		diskname	?f?B?X?N?C???[?W??t?@?C???l?[??
//		readonly	????????
//		index		mount ????f?B?X?N?C???[?W???? (-1 == no disk)
//
bool DiskManager::Mount
(uint dr, const char* diskname, bool readonly, int index, bool create)
{
	int i;

	Unmount(dr);
	
	CriticalSection::Lock lock(cs);
	// ?f?B?X?N?C???[?W??????? hold ????????????????m?F
	DiskImageHolder* h = 0;
	for (i=0; i<max_drives; i++)
	{
		if (holder[i].Connect(diskname))
		{
			h = &holder[i];
			// ??????J???f?B?X?N??????J???????????????m?F????
			if (index >= 0)
			{
				for (uint d=0; d<max_drives; d++)
				{
					if ((d != dr) && (drive[d].holder == h) && (drive[d].index == index))
					{
						index = -1;		// no disk
						statusdisplay.Show(90, 3000, "????f?B?X?N??g?p?????");
						break;
					}
				}
			}
			break;
		}
	}
	if (!h)			// ?????? holder ?? hold ??????
	{
		for (i=0; i<max_drives; i++)
		{
			if (!holder[i].IsOpen())
			{
				h = &holder[i];
				break;
			}
		}
		if (!h || !h->Open(diskname, readonly, create))
		{
			if (h)
				h->Disconnect();
			return 0;
		}
	}

	if (!h->GetNumDisks())
		index = -1;

	FileIO* fio = 0;
	if (index >= 0)
	{
		fio = h->GetDisk(index);
		if (!fio)
		{
			h->Disconnect();
			return false;
		}
	}
	drive[dr].holder = h;
	drive[dr].index = index;
	drive[dr].sizechanged = false;
	
	if (fio)
	{
		fio->Seek(0, FileIO::begin);
		if (!ReadDiskImage(fio, &drive[dr]))
		{
			h->Disconnect();
			drive[dr].holder = 0;
			return false;
		}
		memset(drive[dr].modified, 0, 164);
		
		drive[dr].fdu.Mount(&drive[dr].disk);
	}
	return true;
}	

// ---------------------------------------------------------------------------
//	?f?B?X?N?????O??
//
bool DiskManager::Unmount(uint dr)
{
	CriticalSection::Lock lock(cs);
	
	bool ret = true;
	Drive& drv = drive[dr];
	drive[dr].fdu.Unmount();
	if (drv.holder)
	{
		if (drv.index >= 0)
		{
			for (int t=0; t<164; t++)
			{
				if (drv.modified[t])
				{
					uint32 disksize = GetDiskImageSize(&drv);
					if (!drv.holder->SetDiskSize(drv.index, disksize))
					{
						ret = false;
						break;
					}

					FileIO* fio = drv.holder->GetDisk(drv.index);
					ret = fio ? WriteDiskImage(fio, &drv) : false;
					break;
				}
			}
		}
		drv.holder->Disconnect();
		drv.holder = 0;
	}
	if (!ret)
		statusdisplay.Show(50, 3000, "?f?B?X?N??X?V????s???????");
	return ret;
}

// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W???????
//
bool DiskManager::ReadDiskImage(FileIO* fio, Drive* drive)
{
	uint t;
	ImageHeader ih;
	fio->Read(&ih, sizeof(ImageHeader));
	if (!memcmp(ih.title, "M88 RawDiskImage", 16))
		return ReadDiskImageRaw(fio, drive);
	
	// ?f?B?X?N??^?C?v?`?F?b?N
	FloppyDisk::DiskType type;
	uint hd = 0;
	switch (ih.disktype)
	{
	case 0x00:
		type = FloppyDisk::MD2D; 
		memset(&ih.trackptr[84], 0, 4*80);
		break;

	case 0x10: 
		type = FloppyDisk::MD2DD; 
		break;

	case 0x20: 
		type = FloppyDisk::MD2HD; 
		hd = FloppyDisk::highdensity; 
		break;

	default:
		statusdisplay.Show(90, 3000, "?T?|?[?g???????????f?B?A???");
		return false;
	}
	bool readonly = drive->holder->IsReadOnly() || ih.readonly;
	
	FloppyDisk& disk = drive->disk;
	if (!disk.Init(type, readonly))
	{
		statusdisplay.Show(70, 3000, "???p???????????????????????????");
		return false;
	}

	// ?????????????P
	for (t=0; t<disk.GetNumTracks(); t++)
	{
		if (ih.trackptr[t] >= ih.disksize)
			break;
	}
	if (t<164)
		memset(&ih.trackptr[t], 0, (164-t) * 4);
	if (t<(uint) Min(160, disk.GetNumTracks()))
		statusdisplay.Show(80, 3000, "?w?b?_?[???????f?[?^???????????");

	// trackptr ??????????
	uint trackstart = sizeof(ImageHeader);
	for (t=0; t<84; t++)
	{
		if (ih.trackptr[t] && ih.trackptr[t] < trackstart)
			trackstart = (uint) ih.trackptr[t];
	}
	if (trackstart < sizeof(ImageHeader))
		memset(((char*) &ih) + trackstart, 0, sizeof(ImageHeader)-trackstart);

	// trackptr ?f?[?^????
	for (t=0; t<164; t++)
	{
		drive->trackpos[t] = ih.trackptr[t];
		drive->tracksize[t] = 0;
	}
	for (t=0; t<168; t++)
	{
		disk.Seek(t);
		disk.FormatTrack(0, 0);
	}

	// ?e?g???b?N???????
	for (t=0; t<disk.GetNumTracks(); t++)
	{
		int cy = t >> 1;
		if (type == FloppyDisk::MD2D)
			cy *= 2;
		disk.Seek((cy * 2) + (t & 1));
		if (ih.trackptr[t])
		{
			fio->Seek(ih.trackptr[t], FileIO::begin);
			int sot = 0;
			int i = 0;
			SectorHeader sh;
			do
			{
				if (fio->Read(&sh, sizeof(sh)) != sizeof(sh))
					break;
				
				FloppyDisk::Sector* sec = disk.AddSector(sh.length);
				if (!sec)
					break;
				sec->id = sh.id;
				sec->size = sh.length;
				sec->flags = (sh.density ^ 0x40) | hd;
				if (sh.deleted == 0x10)
					sec->flags |= FloppyDisk::deleted;

				switch (sh.status)
				{
				case 0xa0: sec->flags |= FloppyDisk::idcrc;   break;
				case 0xb0: sec->flags |= FloppyDisk::datacrc; break;
				case 0xf0: sec->flags |= FloppyDisk::mam;     break;
				}
				if (fio->Read(sec->image, sh.length) != sh.length)
					break;
				sot += 0x10 + sh.length;
			} while (++i < sh.sectors);

			drive->tracksize[t] = sot;
		}
	}
	return true;
}

// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W (READER ?`??) ???????
//
bool DiskManager::ReadDiskImageRaw(FileIO* fio, Drive* drive)
{
	fio->Seek(16, FileIO::begin);

	bool readonly = drive->holder->IsReadOnly();
	
	FloppyDisk& disk = drive->disk;
	if (!disk.Init(FloppyDisk::MD2D, readonly))
	{
		statusdisplay.Show(70, 3000, "???p???????????????????????????");
		return false;
	}

	int t;
	for (t=0; t<164; t++)
	{
		drive->trackpos[t] = 0;
		drive->tracksize[t] = 0;
	}
	for (t=0; t<168; t++)
	{
		disk.Seek(t);
		disk.FormatTrack(0, 0);
	}

	// ?e?g???b?N???????
	uint8 buf[256];
	FloppyDisk::IDR id;
	id.n = 1;
	for (t=0; t<80; t++)
	{
		id.c = t / 2;
		id.h = t & 1;

		disk.Seek(id.c * 4 + id.h);
		disk.FormatTrack(0, 0);

		for (int r=1; r<=16; r++)
		{
			id.r = r;
			
			if (fio->Read(buf, 256) != 256)
				break;

			FloppyDisk::Sector* sec = disk.AddSector(256);
			if (!sec)
				break;
			sec->id = id;
			sec->size = 256;
			sec->flags = 0x40;
			memcpy(sec->image, buf,256);
		}
	}
	drive->sizechanged = false;

	return true;
}


// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W??T?C?Y???v?Z????
//
uint DiskManager::GetDiskImageSize(Drive* drv)
{
	uint disksize = sizeof(ImageHeader);

	for (int t=drv->disk.GetNumTracks()-1; t>=0; t--)
	{
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);

		FloppyDisk::Sector* sec;
		for (sec = drv->disk.GetFirstSector(tr); sec; sec=sec->next)
		{
			disksize += sec->size + sizeof(SectorHeader);
		}
	}
	return disksize;
}
	
// ---------------------------------------------------------------------------
//	?f?B?X?N?C???[?W??????o??
//	?K?v??????????????m???????????????
//
bool DiskManager::WriteDiskImage(FileIO* fio, Drive* drv)
{
	static const uint8 typetbl[3] = { 0x00, 0x10, 0x20 };
	int t;

	// Header ???
	ImageHeader ih;
	memset(&ih, 0, sizeof(ImageHeader));
	strcpy(ih.title, drv->holder->GetTitle(drv->index));
	
	ih.disktype = typetbl[drv->disk.GetType()];
	ih.readonly = drv->disk.IsReadOnly() ? 0x10 : 0;
	
	uint32 disksize = sizeof(ImageHeader);
	int ntracks = drv->disk.GetNumTracks();

	for (t=0; t<ntracks; t++)
	{
		int tracksize = 0;
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);

		FloppyDisk::Sector* sec;
		for (sec = drv->disk.GetFirstSector(tr); sec; sec=sec->next)
			tracksize += sec->size + sizeof(SectorHeader);

		ih.trackptr[t] = tracksize ? disksize : 0;
		disksize += tracksize;
	}
	for (; t<164; t++)
		ih.trackptr[t] = 0;
	
	ih.disksize = disksize;

	if (!fio->Seek(0, FileIO::begin))
		return false;
	if (fio->Write(&ih, sizeof(ImageHeader)) != sizeof(ImageHeader))
		return false;

	for (t=0; t<ntracks; t++)
	{
		int tr = (drv->disk.GetType() == FloppyDisk::MD2D) ? t & ~1 : t >> 1;
		tr = (tr << 1) | (t & 1);
		if (!WriteTrackImage(fio, drv, tr))
			return false;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	?g???b?N??????C???[?W??????
//
bool DiskManager::WriteTrackImage(FileIO* fio, Drive* drv, int t)
{
	SectorHeader sh;
	memset(&sh, 0, sizeof(SectorHeader));
	
	FloppyDisk::Sector* sec;
	int nsect = 0;
	for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
		nsect++;
	sh.sectors = nsect;
	
	for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
	{
		sh.id = sec->id;
		sh.density = (~sec->flags) & 0x40;
		sh.deleted = sec->flags & 1 ? 0x10 : 0;
		sh.length = sec->size;
		sh.status = 0;
		switch (sec->flags & 14)
		{
		case FloppyDisk::idcrc:		sh.status = 0xa0; break;
		case FloppyDisk::datacrc:	sh.status = 0xb0; break;
		case FloppyDisk::mam:		sh.status = 0xf0; break;
		}
		if (fio->Write(&sh, sizeof(SectorHeader)) != sizeof(SectorHeader))
			return false;
		if (uint(fio->Write(sec->image, sec->size)) != sec->size)
			return false;;
	}
	return true;
}

// ---------------------------------------------------------------------------
//	Unlock
//	Disk ??X??
//
void DiskManager::Modified(int dr, int tr)
{
	if (0 <= tr && tr < 164 && !drive[dr].disk.IsReadOnly())
	{
		drive[dr].modified[tr] = true;
	}
}

// ---------------------------------------------------------------------------
//	Update
//	?g???b?N???u????????X?V??????X??????????
//
void DiskManager::Update()
{
	for (int d=0; d<max_drives; d++)
		UpdateDrive(&drive[d]);
}

// ---------------------------------------------------------------------------
//	UpdateDrive
//
void DiskManager::UpdateDrive(Drive* drv)
{
	if (!drv->holder || drv->sizechanged)
		return;

	CriticalSection::Lock lock(cs);
	int t;
	for (t=0; t<164 && !drv->modified[t]; t++)
		;
	if (t < 164)
	{
		FileIO* fio = drv->holder->GetDisk(drv->index);
		if (fio)
		{
			for (; t<164; t++)
			{
				if (drv->modified[t])
				{
					FloppyDisk::Sector* sec;
					int tracksize = 0;
					
					for (sec = drv->disk.GetFirstSector(t); sec; sec=sec->next)
						tracksize += sec->size + sizeof(SectorHeader);
					
					if (tracksize <= drv->tracksize[t])
					{
						drv->modified[t] = false;
						fio->Seek(drv->trackpos[t], FileIO::begin);
						WriteTrackImage(fio, drv, t);
					}
					else
					{
						drv->sizechanged = true;
						break;
					}
				}
			}
		}
	}
}

// ---------------------------------------------------------------------------
//	?C???[?W?^?C?g?????
//
const char* DiskManager::GetImageTitle(uint dr, uint index)
{
	if (dr < max_drives && drive[dr].holder)
	{
		return drive[dr].holder->GetTitle(index);
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	?C???[?W??????
//
uint DiskManager::GetNumDisks(uint dr)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder)
			return drive[dr].holder->GetNumDisks();
	}
	return 0;
}

// ---------------------------------------------------------------------------
//	????I??????????f?B?X?N?????????
//
int DiskManager::GetCurrentDisk(uint dr)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder)
			return drive[dr].index;
	}
	return -1;
}

// ---------------------------------------------------------------------------
//	?f?B?X?N???
//	dr		???h???C?u
//	title	?f?B?X?N?^?C?g??
//	type	b1-0	?f?B?X?N????f?B?A?^?C?v
//					00 = 2D, 01 = 2DD, 10 = 2HD
//
bool DiskManager::AddDisk(uint dr, const char* title, uint type)
{
	if (dr < max_drives)
	{
		if (drive[dr].holder && drive[dr].holder->AddDisk(title, type))
			return true;
	}
	return false;
}

// ---------------------------------------------------------------------------
//	N88-BASIC ?W???t?H?[?}?b?g???|????
//	????????@??(^^;
//
bool DiskManager::FormatDisk(uint dr)
{
	if (!drive[dr].holder || drive[dr].disk.GetType() != FloppyDisk::MD2D)
		return false;
//	statusdisplay.Show(10, 5000, "Format drive : %d", dr);
	
	uint8* buf = new uint8[80*16*256];
	if (!buf)
		return false;

	// ?t?H?[?}?b?g
	memset(buf, 0xff, 80*16*256);
	// IPL
	buf[0] = 0xc9;
	// ID
	memset(&buf[0x25c00], 0, 256);
	buf[0x25c01] = 0xff;
	// FAT
	buf[0x25d4a] = 0xfe; buf[0x25d4b] = 0xfe;
	buf[0x25e4a] = 0xfe; buf[0x25e4b] = 0xfe;
	buf[0x25f4a] = 0xfe; buf[0x25f4b] = 0xfe;
	
	// ????????
	FloppyDisk& disk = drive[dr].disk;
	FloppyDisk::IDR id;
	id.n = 1;
	uint8* dest = buf;

	for (int t=0; t<80; t++)
	{
		id.c = t / 2, id.h = t & 1;

		disk.Seek(id.c * 4 + id.h);
		disk.FormatTrack(0, 0);

		for (int r=1; r<=16; r++)
		{
			id.r = r;

			FloppyDisk::Sector* sec = disk.AddSector(256);
			if (!sec)
				break;
			sec->id = id, sec->size = 256;
			sec->flags = 0x40;
			memcpy(sec->image, dest, 256);
			dest += 256;
		}
	}
	drive->sizechanged = true;
	drive->modified[0] = true;
	delete[] buf;
	return true;
}
