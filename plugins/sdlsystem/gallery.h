// gallery.h - a picture into the phone's photo gallery: Android's MediaStore (gallery_android.cpp),
// iOS's Photos library (gallery_apple.mm); nowhere else
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

namespace sdlsys {

/// Whether this platform has a gallery to save to
bool galleryPossible();

/// Saves a PNG as `name` (with its extension) in the gallery, in `album` where the platform
/// keeps albums as folders (Android: Pictures/<album>). `done(ok, why)` is called once, on any
/// thread: at once on Android, after the player has answered the system's question on iOS.
/// False (with why) when it couldn't even start; done isn't called then.
bool gallerySave(const std::vector<uint8_t> &png, const std::string &name, const std::string &album,
                 std::function<void(bool, const std::string &)> done, std::string &why);

} // namespace sdlsys
