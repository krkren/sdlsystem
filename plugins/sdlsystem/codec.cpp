// codec.cpp - see codec.h
//
// SPDX-License-Identifier: 0BSD
#include "codec.h"

#include <cstring>

namespace sdlsys {

namespace {

void put32be(std::vector<uint8_t> &out, uint32_t v) {
	out.push_back(uint8_t(v >> 24));
	out.push_back(uint8_t(v >> 16));
	out.push_back(uint8_t(v >> 8));
	out.push_back(uint8_t(v));
}

void put16le(std::vector<uint8_t> &out, uint32_t v) {
	out.push_back(uint8_t(v));
	out.push_back(uint8_t(v >> 8));
}

void put32le(std::vector<uint8_t> &out, uint32_t v) {
	put16le(out, v & 0xffff);
	put16le(out, v >> 16);
}

void chunk(std::vector<uint8_t> &out, const char *type, const std::vector<uint8_t> &data) {
	put32be(out, uint32_t(data.size()));
	size_t start = out.size();
	out.insert(out.end(), type, type + 4);
	out.insert(out.end(), data.begin(), data.end());
	put32be(out, crc32(out.data() + start, out.size() - start));
}

} // namespace

uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc) {
	static uint32_t table[256];
	static bool made = false;
	if (!made) {
		for (uint32_t n = 0; n < 256; n++) {
			uint32_t c = n;
			for (int k = 0; k < 8; k++) c = (c & 1) ? 0xedb88320u ^ (c >> 1) : c >> 1;
			table[n] = c;
		}
		made = true;
	}
	crc = ~crc;
	for (size_t k = 0; k < size; k++) crc = table[(crc ^ data[k]) & 0xff] ^ (crc >> 8);
	return ~crc;
}

uint32_t adler32(const uint8_t *data, size_t size, uint32_t adler) {
	uint32_t a = adler & 0xffff, b = adler >> 16;
	for (size_t k = 0; k < size; k++) {
		a = (a + data[k]) % 65521;
		b = (b + a) % 65521;
	}
	return (b << 16) | a;
}

std::vector<uint8_t> encodePng(const uint8_t *bgra, int width, int height, long pitch) {
	std::vector<uint8_t> out;
	if (width <= 0 || height <= 0) return out;
	static const uint8_t sig[8] = { 0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n' };
	out.insert(out.end(), sig, sig + 8);

	std::vector<uint8_t> ihdr;
	put32be(ihdr, uint32_t(width));
	put32be(ihdr, uint32_t(height));
	ihdr.push_back(8); // bits per channel
	ihdr.push_back(2); // RGB
	ihdr.push_back(0);
	ihdr.push_back(0);
	ihdr.push_back(0);
	chunk(out, "IHDR", ihdr);

	// the rows, each after its filter byte (0: none)
	size_t rowSize = size_t(width) * 3 + 1;
	std::vector<uint8_t> raw(rowSize * size_t(height));
	for (int y = 0; y < height; y++) {
		const uint8_t *src = bgra + pitch * y;
		uint8_t *dst = raw.data() + rowSize * size_t(y);
		*dst++ = 0;
		for (int x = 0; x < width; x++, src += 4) {
			*dst++ = src[2];
			*dst++ = src[1];
			*dst++ = src[0];
		}
	}

	// zlib: stored blocks of up to 65535 bytes
	std::vector<uint8_t> z;
	z.reserve(raw.size() + raw.size() / 65535 * 5 + 16);
	z.push_back(0x78);
	z.push_back(0x01);
	size_t pos = 0;
	do {
		size_t n = raw.size() - pos;
		if (n > 65535) n = 65535;
		bool last = pos + n == raw.size();
		z.push_back(last ? 1 : 0);
		put16le(z, uint32_t(n));
		put16le(z, uint32_t(~n & 0xffff));
		z.insert(z.end(), raw.begin() + long(pos), raw.begin() + long(pos + n));
		pos += n;
	} while (pos < raw.size());
	put32be(z, adler32(raw.data(), raw.size()));
	chunk(out, "IDAT", z);
	chunk(out, "IEND", {});
	return out;
}

std::vector<uint8_t> encodeBmp(const uint8_t *bgra, int width, int height, long pitch) {
	std::vector<uint8_t> out;
	if (width <= 0 || height <= 0) return out;
	uint32_t image = uint32_t(width) * uint32_t(height) * 4;
	// BITMAPFILEHEADER: 14 bytes, packed
	out.push_back('B');
	out.push_back('M');
	put32le(out, 14 + 40 + image);
	put32le(out, 0);
	put32le(out, 14 + 40);
	// BITMAPINFOHEADER
	put32le(out, 40);
	put32le(out, uint32_t(width));
	put32le(out, uint32_t(height)); // bottom-up
	put16le(out, 1);
	put16le(out, 32);
	put32le(out, 0); // BI_RGB
	put32le(out, image);
	put32le(out, 3780); // 96 dpi
	put32le(out, 3780);
	put32le(out, 0);
	put32le(out, 0);
	out.reserve(out.size() + image);
	for (int y = height - 1; y >= 0; y--) {
		const uint8_t *src = bgra + pitch * y;
		for (int x = 0; x < width; x++, src += 4) {
			out.push_back(src[0]);
			out.push_back(src[1]);
			out.push_back(src[2]);
			out.push_back(0xff);
		}
	}
	return out;
}

std::string fileUrl(const std::string &path) {
	std::string p = path;
	for (char &c : p)
		if (c == '\\') c = '/';
	std::string out = "file://";
	if (p.size() >= 2 && p[1] == ':') out += "/"; // C:/... becomes file:///C:/...
	static const char hex[] = "0123456789ABCDEF";
	for (unsigned char c : p) {
		bool plain = (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') ||
		             c == '/' || c == '-' || c == '_' || c == '.' || c == '~' || c == ':';
		if (plain) {
			out += char(c);
		} else {
			out += '%';
			out += hex[c >> 4];
			out += hex[c & 15];
		}
	}
	return out;
}

bool hasScheme(const std::string &target) {
	// RFC 3986: ALPHA *( ALPHA / DIGIT / "+" / "-" / "." ) ":"; more than one letter, so a
	// drive letter isn't taken for one
	size_t k = 0;
	if (target.empty() || !((target[0] >= 'a' && target[0] <= 'z') || (target[0] >= 'A' && target[0] <= 'Z')))
		return false;
	for (k = 1; k < target.size(); k++) {
		char c = target[k];
		if (c == ':') return k >= 2;
		bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '+' ||
		          c == '-' || c == '.';
		if (!ok) return false;
	}
	return false;
}

std::vector<std::string> splitArgs(const std::string &line) {
	std::vector<std::string> out;
	std::string cur;
	bool have = false;
	char quote = 0;
	for (size_t k = 0; k < line.size(); k++) {
		char c = line[k];
		if (quote == '\'') {
			if (c == '\'') quote = 0;
			else cur += c;
		} else if (quote == '"') {
			if (c == '"') quote = 0;
			else if (c == '\\' && k + 1 < line.size() &&
			         (line[k + 1] == '"' || line[k + 1] == '\\' || line[k + 1] == '$' || line[k + 1] == '`'))
				cur += line[++k];
			else cur += c;
		} else if (c == '\'' || c == '"') {
			quote = c;
			have = true;
		} else if (c == '\\' && k + 1 < line.size()) {
			cur += line[++k];
			have = true;
		} else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') {
			if (have || !cur.empty()) out.push_back(cur);
			cur.clear();
			have = false;
		} else {
			cur += c;
		}
	}
	if (have || !cur.empty()) out.push_back(cur);
	return out;
}

std::wstring quoteWindowsArg(const std::wstring &arg) {
	if (!arg.empty() && arg.find_first_of(L" \t\n\v\"") == std::wstring::npos) return arg;
	std::wstring out = L"\"";
	size_t slashes = 0;
	for (wchar_t c : arg) {
		if (c == L'\\') {
			slashes++;
		} else if (c == L'"') {
			out.append(slashes * 2 + 1, L'\\');
			out += c;
			slashes = 0;
		} else {
			out.append(slashes, L'\\');
			out += c;
			slashes = 0;
		}
	}
	out.append(slashes * 2, L'\\');
	out += L'"';
	return out;
}

} // namespace sdlsys
