#include "screen_capture.h"

#include <algorithm>
#include <cstring>

namespace M88ScreenCapture {
namespace {

#pragma pack(push, 1)
struct BitmapFileHeader {
  uint16 type;
  uint32 size;
  uint16 reserved1;
  uint16 reserved2;
  uint32 off_bits;
};

struct BitmapInfoHeader {
  uint32 header_size;
  int32 width;
  int32 height;
  uint16 planes;
  uint16 bit_count;
  uint32 compression;
  uint32 size_image;
  int32 x_pels_per_meter;
  int32 y_pels_per_meter;
  uint32 clr_used;
  uint32 clr_important;
};

struct RgbQuad {
  uint8 blue;
  uint8 green;
  uint8 red;
  uint8 reserved;
};
#pragma pack(pop)

constexpr int kWidth = 640;
constexpr int kHeight = 400;

}  // namespace

size_t BuildBmp4(const uint8_t* framebuffer, int bytes_per_line,
                 const Draw::Palette palette[256], std::vector<uint8>* out) {
  if (!framebuffer || bytes_per_line < kWidth || !palette || !out) {
    return 0;
  }

  constexpr int kColors = 16;
  const size_t header_bytes =
      sizeof(BitmapFileHeader) + sizeof(BitmapInfoHeader) + kColors * sizeof(RgbQuad);
  const size_t image_bytes = static_cast<size_t>(kWidth) * kHeight / 2;
  out->assign(header_bytes + image_bytes, 0);

  auto* filehdr = reinterpret_cast<BitmapFileHeader*>(out->data());
  auto* infohdr =
      reinterpret_cast<BitmapInfoHeader*>(out->data() + sizeof(BitmapFileHeader));
  auto* pal =
      reinterpret_cast<RgbQuad*>(out->data() + sizeof(BitmapFileHeader) +
                                 sizeof(BitmapInfoHeader));

  filehdr->type = 0x4D42;  // 'BM'
  filehdr->reserved1 = 0;
  filehdr->reserved2 = 0;
  infohdr->header_size = sizeof(BitmapInfoHeader);
  infohdr->width = kWidth;
  infohdr->height = kHeight;
  infohdr->planes = 1;
  infohdr->bit_count = 4;
  infohdr->compression = 0;
  infohdr->size_image = 0;
  infohdr->x_pels_per_meter = 0;
  infohdr->y_pels_per_meter = 0;

  uint8 color_table[256] = {};
  int colors = 0;
  for (int index = 0; index < 144; ++index) {
    const Draw::Palette& src = palette[0x40 + index];
    RgbQuad rgb {};
    rgb.blue = src.blue;
    rgb.red = src.red;
    rgb.green = src.green;
    const uint32 entry = *reinterpret_cast<const uint32*>(&rgb);

    int match = 0;
    for (; match < colors; ++match) {
      const uint32 existing = *reinterpret_cast<const uint32*>(&pal[match]);
      if (((existing ^ entry) & 0xffffffU) == 0) {
        break;
      }
    }
    if (match == colors) {
      if (colors < 15) {
        pal[colors++] = rgb;
      } else {
        match = 15;
      }
    }
    color_table[64 + index] = static_cast<uint8>(match);
  }

  infohdr->clr_important = static_cast<uint32>(colors);
  colors = kColors;
  infohdr->clr_used = static_cast<uint32>(colors);

  uint8* image = out->data() + header_bytes;
  filehdr->off_bits = static_cast<uint32>(image - out->data());
  filehdr->size = static_cast<uint32>(image + image_bytes - out->data());

  uint8* dst = image;
  for (int y = 0; y < kHeight; ++y) {
    const uint8* src = framebuffer + static_cast<size_t>(bytes_per_line) * (kHeight - 1 - y);
    for (int x = 0; x < kWidth / 2; ++x, src += 2) {
      *dst++ = static_cast<uint8>(color_table[src[0]] * 16 + color_table[src[1]]);
    }
  }

  return out->size();
}

}  // namespace M88ScreenCapture
