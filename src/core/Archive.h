/*
 * PowerPlay — reading what skins are made of: zip archives (.wsz) and
 * Windows bitmaps.
 *
 * Portable: no Haiku headers, so the host tests can use it.
 *
 * Copyright 2026 the PowerPlay authors. MIT licence.
 */
#ifndef POWERPLAY_ARCHIVE_H
#define POWERPLAY_ARCHIVE_H

#include <cstddef>
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace skin {

// The files of a zip archive, stored or deflated (no zip64, no
// encryption), by their path in the archive.
class ZipArchive {
public:
	// False if data isn't a zip archive this can read.
	bool Open(std::vector<uint8_t> data);

	std::vector<std::string> Names() const;
	// The file's contents, or false.
	bool Extract(const std::string& name, std::vector<uint8_t>& out) const;

private:
	struct Entry {
		uint16_t method;
		uint32_t compressedSize;
		uint32_t size;
		uint32_t offset;	// of the local header
	};

	std::vector<uint8_t> fData;
	std::map<std::string, Entry> fEntries;
};


// An image as 0xAARRGGBB pixels, rows top to bottom.
struct Image {
	int width = 0;
	int height = 0;
	std::vector<uint32_t> pixels;

	bool IsValid() const { return width > 0 && height > 0; }
	uint32_t At(int x, int y) const { return pixels[y * width + x]; }
};

// Windows BMP: 1, 4, 8, 16, 24 and 32 bits, uncompressed, RLE4, RLE8 or
// bitfields, bottom-up or top-down. False if it can't.
bool DecodeBMP(const std::vector<uint8_t>& data, Image& image);

}	// namespace skin

#endif	// POWERPLAY_ARCHIVE_H
