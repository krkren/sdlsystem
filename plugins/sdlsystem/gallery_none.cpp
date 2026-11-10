// gallery_none.cpp - every platform without a photo gallery the game can add to (gallery.h)
//
// SPDX-License-Identifier: 0BSD
#include "gallery.h"

#if defined(__APPLE__)
#include <TargetConditionals.h>
#endif

#if !defined(__ANDROID__) && !(defined(__APPLE__) && TARGET_OS_IOS)

namespace sdlsys {

bool galleryPossible() { return false; }

bool gallerySave(const std::vector<uint8_t> &, const std::string &, const std::string &,
                 std::function<void(bool, const std::string &)>, std::string &why) {
	why = "this platform has no photo gallery to save to";
	return false;
}

} // namespace sdlsys

#endif
