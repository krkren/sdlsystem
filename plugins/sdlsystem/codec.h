// codec.h - pictures for the clipboard, and the small text helpers the plugin needs; no engine
// in here, so the tests build it on its own
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sdlsys {

/// A PNG of a 32-bit BGRA picture (rows top first, `pitch` bytes apart), opaque: the engine's
/// layers keep an alpha channel that means nothing on screen. Stored without compression:
/// it's for the clipboard, where the size doesn't matter and speed does.
std::vector<uint8_t> encodePng(const uint8_t *bgra, int width, int height, long pitch);

/// A BMP file (32 bits, bottom-up, opaque) of the same picture: Windows' clipboard takes it as
/// CF_DIB through SDL's "image/bmp"
std::vector<uint8_t> encodeBmp(const uint8_t *bgra, int width, int height, long pitch);

/// CRC-32 (PNG's) and Adler-32 (zlib's), for the tests
uint32_t crc32(const uint8_t *data, size_t size, uint32_t crc = 0);
uint32_t adler32(const uint8_t *data, size_t size, uint32_t adler = 1);

/// A local path as a file:// URL (UTF-8, percent-encoded; Windows' C:\ becomes file:///C:/)
std::string fileUrl(const std::string &path);

/// Whether a target has a URL scheme ("https:", "mailto:", "file:"...); a Windows drive letter
/// ("C:\...") isn't one
bool hasScheme(const std::string &target);

/// Splits a command line into its arguments as a POSIX shell would for plain words, quotes
/// ('...' and "...") and backslashes, without expanding anything
std::vector<std::string> splitArgs(const std::string &line);

/// One argument quoted for Windows' command line (CommandLineToArgvW's rules)
std::wstring quoteWindowsArg(const std::wstring &arg);

} // namespace sdlsys
