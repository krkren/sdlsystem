// The clipboard's pictures and the text helpers, with no engine
//
// SPDX-License-Identifier: 0BSD
#include "codec.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace sdlsys;

static int failures = 0;

#define CHECK(cond)                                                                                                    \
	do {                                                                                                               \
		if (!(cond)) {                                                                                                 \
			std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                                                \
			failures++;                                                                                                \
		}                                                                                                              \
	} while (0)

static uint32_t be32(const uint8_t *p) { return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | p[3]; }
static uint32_t le32(const uint8_t *p) { return uint32_t(p[0]) | (uint32_t(p[1]) << 8) | (uint32_t(p[2]) << 16) | (uint32_t(p[3]) << 24); }

// Reads the PNG back: its chunks' CRCs, the header, and the rows out of the stored blocks
static bool readPng(const std::vector<uint8_t> &png, int &w, int &h, std::vector<uint8_t> &raw) {
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	if (png.size() < 8 || std::memcmp(png.data(), sig, 8) != 0) return false;
	size_t pos = 8;
	std::vector<uint8_t> z;
	bool end = false;
	while (pos + 12 <= png.size()) {
		uint32_t len = be32(&png[pos]);
		const uint8_t *type = &png[pos + 4];
		if (pos + 12 + len > png.size()) return false;
		if (crc32(type, len + 4) != be32(&png[pos + 8 + len])) return false;
		if (!std::memcmp(type, "IHDR", 4)) {
			w = int(be32(type + 4));
			h = int(be32(type + 8));
			if (type[12] != 8 || type[13] != 2) return false;
		} else if (!std::memcmp(type, "IDAT", 4)) {
			z.insert(z.end(), type + 4, type + 4 + len);
		} else if (!std::memcmp(type, "IEND", 4)) {
			end = true;
		}
		pos += 12 + len;
	}
	if (!end || z.size() < 6 || z[0] != 0x78) return false;
	size_t p = 2;
	for (;;) {
		uint8_t head = z[p];
		uint32_t n = uint32_t(z[p + 1]) | (uint32_t(z[p + 2]) << 8);
		uint32_t nn = uint32_t(z[p + 3]) | (uint32_t(z[p + 4]) << 8);
		if ((n ^ 0xffff) != nn) return false;
		raw.insert(raw.end(), z.begin() + long(p + 5), z.begin() + long(p + 5 + n));
		p += 5 + n;
		if (head & 1) break;
	}
	return be32(&z[p]) == adler32(raw.data(), raw.size());
}

int main() {
	// the checksums' standard check values
	CHECK(crc32(reinterpret_cast<const uint8_t *>("123456789"), 9) == 0xCBF43926u);
	CHECK(adler32(reinterpret_cast<const uint8_t *>("Wikipedia"), 9) == 0x11E60398u);

	// a picture with padding at the end of each row, over 65535 bytes (several stored blocks)
	const int W = 150, H = 160;
	const long pitch = W * 4 + 12;
	std::vector<uint8_t> px(size_t(pitch) * H, 0xee);
	for (int y = 0; y < H; y++)
		for (int x = 0; x < W; x++) {
			uint8_t *p = &px[size_t(pitch) * y + size_t(x) * 4];
			p[0] = uint8_t(x);       // B
			p[1] = uint8_t(y);       // G
			p[2] = uint8_t(x ^ y);   // R
			p[3] = uint8_t(x & 1 ? 0 : 128); // alpha that means nothing
		}
	std::vector<uint8_t> png = encodePng(px.data(), W, H, pitch);
	int w = 0, h = 0;
	std::vector<uint8_t> raw;
	CHECK(readPng(png, w, h, raw));
	CHECK(w == W && h == H);
	CHECK(raw.size() == size_t(H) * (W * 3 + 1));
	bool rowsOk = raw.size() == size_t(H) * (W * 3 + 1);
	for (int y = 0; rowsOk && y < H; y++) {
		const uint8_t *r = &raw[size_t(y) * (W * 3 + 1)];
		if (r[0] != 0) rowsOk = false;
		for (int x = 0; rowsOk && x < W; x++)
			if (r[1 + x * 3] != uint8_t(x ^ y) || r[2 + x * 3] != uint8_t(y) || r[3 + x * 3] != uint8_t(x)) rowsOk = false;
	}
	CHECK(rowsOk);
	CHECK(encodePng(px.data(), 0, 10, pitch).empty());

	// the BMP: header, bottom-up rows, opaque
	std::vector<uint8_t> bmp = encodeBmp(px.data(), W, H, pitch);
	CHECK(bmp.size() == 54 + size_t(W) * H * 4);
	CHECK(bmp[0] == 'B' && bmp[1] == 'M');
	CHECK(le32(&bmp[2]) == bmp.size());
	CHECK(le32(&bmp[10]) == 54);
	CHECK(le32(&bmp[14]) == 40);
	CHECK(int(le32(&bmp[18])) == W && int(le32(&bmp[22])) == H);
	const uint8_t *first = &bmp[54]; // the bottom row
	CHECK(first[0] == 0 && first[1] == uint8_t(H - 1) && first[2] == uint8_t(0 ^ (H - 1)) && first[3] == 0xff);

	// file URLs
	CHECK(fileUrl("/home/me/My Game/screenshots") == "file:///home/me/My%20Game/screenshots");
	CHECK(fileUrl("C:\\Games\\KAGEX\\") == "file:///C:/Games/KAGEX/");
	CHECK(fileUrl("/tmp/\xe3\x81\x82") == "file:///tmp/%E3%81%82");
	CHECK(fileUrl("/a#b?c%d") == "file:///a%23b%3Fc%25d");

	// schemes
	CHECK(hasScheme("https://example.com"));
	CHECK(hasScheme("mailto:me@example.com"));
	CHECK(hasScheme("file:///tmp"));
	CHECK(!hasScheme("C:\\Games"));
	CHECK(!hasScheme("c:/games"));
	CHECK(!hasScheme("/usr/bin/kate"));
	CHECK(!hasScheme("readme.txt"));
	CHECK(!hasScheme(""));

	// command lines
	std::vector<std::string> a = splitArgs("  --line 12 \"my file.ks\" 'it''s' a\\ b \"q\\\"x\" \"\" ");
	CHECK(a.size() == 7);
	if (a.size() == 7) {
		CHECK(a[0] == "--line");
		CHECK(a[1] == "12");
		CHECK(a[2] == "my file.ks");
		CHECK(a[3] == "its");
		CHECK(a[4] == "a b");
		CHECK(a[5] == "q\"x");
		CHECK(a[6] == "");
	}
	CHECK(splitArgs("").empty());

	CHECK(quoteWindowsArg(L"plain") == L"plain");
	CHECK(quoteWindowsArg(L"") == L"\"\"");
	CHECK(quoteWindowsArg(L"two words") == L"\"two words\"");
	CHECK(quoteWindowsArg(L"say \"hi\"") == L"\"say \\\"hi\\\"\"");
	CHECK(quoteWindowsArg(L"C:\\a b\\") == L"\"C:\\a b\\\\\"");

	if (failures) {
		std::printf("%d failed\n", failures);
		return 1;
	}
	std::printf("codec_test: all passed\n");
	return 0;
}
