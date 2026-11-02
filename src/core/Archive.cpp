/*
 * PowerPlay — reading what skins are made of: zip archives (.wsz) and
 * Windows bitmaps.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#include "Archive.h"

#include <cstring>

#include <zlib.h>

namespace skin {

static uint16_t
Read16(const uint8_t* p)
{
	return uint16_t(p[0] | (p[1] << 8));
}


static uint32_t
Read32(const uint8_t* p)
{
	return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16)
		| (uint32_t(p[3]) << 24);
}


//	#pragma mark - ZipArchive


bool
ZipArchive::Open(std::vector<uint8_t> data)
{
	fData.swap(data);
	fEntries.clear();
	const size_t size = fData.size();
	if (size < 22)
		return false;

	// The end of central directory record, within the last 64 kB
	size_t end = size - 22;
	size_t stop = size > 65557 ? size - 65557 : 0;
	bool found = false;
	for (size_t i = end + 1; i-- > stop;) {
		if (Read32(&fData[i]) == 0x06054b50) {
			end = i;
			found = true;
			break;
		}
	}
	if (!found)
		return false;

	uint16_t count = Read16(&fData[end + 10]);
	uint32_t directorySize = Read32(&fData[end + 12]);
	uint32_t offset = Read32(&fData[end + 16]);
	if (uint64_t(offset) + directorySize > size)
		return false;

	size_t p = offset;
	for (uint16_t i = 0; i < count; i++) {
		if (p + 46 > size || Read32(&fData[p]) != 0x02014b50)
			return false;
		Entry entry;
		entry.method = Read16(&fData[p + 10]);
		entry.compressedSize = Read32(&fData[p + 20]);
		entry.size = Read32(&fData[p + 24]);
		uint16_t nameLength = Read16(&fData[p + 28]);
		uint16_t extraLength = Read16(&fData[p + 30]);
		uint16_t commentLength = Read16(&fData[p + 32]);
		entry.offset = Read32(&fData[p + 42]);
		if (p + 46 + nameLength > size)
			return false;
		std::string name((const char*)&fData[p + 46], nameLength);
		// Folders end in '/'
		if (!name.empty() && name.back() != '/')
			fEntries[name] = entry;
		p += 46 + nameLength + extraLength + commentLength;
	}
	return true;
}


std::vector<std::string>
ZipArchive::Names() const
{
	std::vector<std::string> names;
	for (const auto& entry : fEntries)
		names.push_back(entry.first);
	return names;
}


bool
ZipArchive::Extract(const std::string& name, std::vector<uint8_t>& out) const
{
	auto found = fEntries.find(name);
	if (found == fEntries.end())
		return false;
	const Entry& entry = found->second;
	size_t p = entry.offset;
	if (p + 30 > fData.size() || Read32(&fData[p]) != 0x04034b50)
		return false;
	size_t data = p + 30 + Read16(&fData[p + 26]) + Read16(&fData[p + 28]);
	if (data + entry.compressedSize > fData.size())
		return false;
	// Skins are small; anything huge is not a skin.
	if (entry.size > 64 * 1024 * 1024)
		return false;

	out.assign(entry.size, 0);
	if (entry.method == 0) {
		if (entry.compressedSize != entry.size)
			return false;
		std::memcpy(out.data(), &fData[data], entry.size);
		return true;
	}
	if (entry.method != 8)
		return false;

	z_stream stream;
	std::memset(&stream, 0, sizeof(stream));
	if (inflateInit2(&stream, -MAX_WBITS) != Z_OK)
		return false;
	stream.next_in = const_cast<Bytef*>(&fData[data]);
	stream.avail_in = entry.compressedSize;
	stream.next_out = out.data();
	stream.avail_out = entry.size;
	int result = inflate(&stream, Z_FINISH);
	inflateEnd(&stream);
	return result == Z_STREAM_END && stream.total_out == entry.size;
}


//	#pragma mark - BMP


static int
ShiftOf(uint32_t mask)
{
	int shift = 0;
	if (mask == 0)
		return 0;
	while ((mask & 1) == 0) {
		mask >>= 1;
		shift++;
	}
	return shift;
}


static uint8_t
Channel(uint32_t value, uint32_t mask)
{
	if (mask == 0)
		return 0;
	uint32_t bits = (value & mask) >> ShiftOf(mask);
	uint32_t max = mask >> ShiftOf(mask);
	return uint8_t(bits * 255 / max);
}


bool
DecodeBMP(const std::vector<uint8_t>& data, Image& image)
{
	const size_t size = data.size();
	if (size < 26 || data[0] != 'B' || data[1] != 'M')
		return false;
	const uint8_t* d = data.data();
	uint32_t pixelOffset = Read32(d + 10);
	uint32_t headerSize = Read32(d + 14);
	if (14 + uint64_t(headerSize) > size)
		return false;

	int32_t width, height;
	uint16_t bpp;
	uint32_t compression = 0, colors = 0;
	int paletteEntrySize = 4;
	uint32_t masks[4] = {0, 0, 0, 0};
	if (headerSize == 12) {
		// OS/2 core header
		width = Read16(d + 18);
		height = int16_t(Read16(d + 20));
		bpp = Read16(d + 24);
		paletteEntrySize = 3;
	} else if (headerSize >= 40) {
		width = int32_t(Read32(d + 18));
		height = int32_t(Read32(d + 22));
		bpp = Read16(d + 28);
		compression = Read32(d + 30);
		colors = Read32(d + 46);
		if (compression == 3 && headerSize >= 52) {
			masks[0] = Read32(d + 54);
			masks[1] = Read32(d + 58);
			masks[2] = Read32(d + 62);
		} else if (compression == 3 && 14 + 40 + 12 <= size) {
			// Masks right after a 40-byte header
			masks[0] = Read32(d + 54);
			masks[1] = Read32(d + 58);
			masks[2] = Read32(d + 62);
		}
		if (headerSize >= 56)
			masks[3] = Read32(d + 66);
	} else
		return false;

	bool topDown = height < 0;
	if (topDown)
		height = -height;
	if (width <= 0 || height <= 0 || width > 16384 || height > 16384)
		return false;

	// The palette
	std::vector<uint32_t> palette;
	if (bpp <= 8) {
		uint32_t count = colors != 0 ? colors : (1u << bpp);
		size_t p = 14 + headerSize;
		if (compression == 3)
			p += 12;
		for (uint32_t i = 0; i < count && p + paletteEntrySize <= size; i++) {
			palette.push_back(0xff000000u | (uint32_t(d[p + 2]) << 16)
				| (uint32_t(d[p + 1]) << 8) | d[p]);
			p += paletteEntrySize;
		}
	}
	auto color = [&](uint32_t index) {
		return index < palette.size() ? palette[index] : 0xff000000u;
	};

	image.width = width;
	image.height = height;
	image.pixels.assign(size_t(width) * height, 0xff000000u);
	auto put = [&](int x, int y, uint32_t pixel) {
		if (x < 0 || x >= width || y < 0 || y >= height)
			return;
		int row = topDown ? y : height - 1 - y;
		image.pixels[size_t(row) * width + x] = pixel;
	};

	if (pixelOffset >= size)
		return false;

	if (compression == 1 || compression == 2) {
		// RLE8 / RLE4: rows from the bottom, x and y as the codes say
		bool rle4 = compression == 2;
		size_t p = pixelOffset;
		int x = 0, y = 0;
		while (p + 1 < size && y < height) {
			uint8_t count = d[p++];
			uint8_t value = d[p++];
			if (count > 0) {
				for (int i = 0; i < count; i++) {
					uint32_t index = rle4
						? (i % 2 == 0 ? value >> 4 : value & 0x0f) : value;
					put(x++, y, color(index));
				}
			} else if (value == 0) {
				x = 0;
				y++;
			} else if (value == 1) {
				break;
			} else if (value == 2) {
				if (p + 1 >= size)
					break;
				x += d[p++];
				y += d[p++];
			} else {
				// value pixels as they are, padded to 16 bits
				int bytes = rle4 ? (value + 1) / 2 : value;
				for (int i = 0; i < value && p + (rle4 ? i / 2 : i) < size;
						i++) {
					uint8_t byte = d[p + (rle4 ? i / 2 : i)];
					uint32_t index = rle4
						? (i % 2 == 0 ? byte >> 4 : byte & 0x0f) : byte;
					put(x++, y, color(index));
				}
				p += bytes + (bytes & 1);
			}
		}
		return true;
	}

	if (compression != 0 && compression != 3)
		return false;
	if (bpp == 16 && compression == 0) {
		masks[0] = 0x7c00;
		masks[1] = 0x03e0;
		masks[2] = 0x001f;
	}
	if (bpp == 32 && compression == 0) {
		masks[0] = 0x00ff0000;
		masks[1] = 0x0000ff00;
		masks[2] = 0x000000ff;
	}

	size_t rowBytes = ((size_t(width) * bpp + 31) / 32) * 4;
	for (int y = 0; y < height; y++) {
		size_t row = pixelOffset + size_t(y) * rowBytes;
		if (row + rowBytes > size)
			return y > 0;	// a truncated file: keep what's there
		const uint8_t* r = d + row;
		for (int x = 0; x < width; x++) {
			uint32_t pixel;
			switch (bpp) {
				case 1:
					pixel = color((r[x / 8] >> (7 - x % 8)) & 1);
					break;
				case 4:
					pixel = color(x % 2 == 0 ? r[x / 2] >> 4 : r[x / 2] & 0x0f);
					break;
				case 8:
					pixel = color(r[x]);
					break;
				case 16:
				{
					uint32_t v = Read16(r + x * 2);
					pixel = 0xff000000u | (uint32_t(Channel(v, masks[0])) << 16)
						| (uint32_t(Channel(v, masks[1])) << 8)
						| Channel(v, masks[2]);
					break;
				}
				case 24:
					pixel = 0xff000000u | (uint32_t(r[x * 3 + 2]) << 16)
						| (uint32_t(r[x * 3 + 1]) << 8) | r[x * 3];
					break;
				case 32:
				{
					uint32_t v = Read32(r + x * 4);
					// Skins are opaque; an alpha channel is ignored.
					pixel = 0xff000000u | (uint32_t(Channel(v, masks[0])) << 16)
						| (uint32_t(Channel(v, masks[1])) << 8)
						| Channel(v, masks[2]);
					break;
				}
				default:
					return false;
			}
			put(x, y, pixel);
		}
	}
	return true;
}

}	// namespace skin
