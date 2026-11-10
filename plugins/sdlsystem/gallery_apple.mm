// gallery_apple.mm - iOS: a picture into the Photos library, asking to add only
// (PHAccessLevelAddOnly, iOS 14). The app's Info.plist must say why
// (NSPhotoLibraryAddUsageDescription): iOS ends an app that asks without it, so without the key
// the plugin says no instead. macOS keeps its pictures in the Pictures folder (userFolder).
//
// SPDX-License-Identifier: 0BSD
#include "gallery.h"

#include <TargetConditionals.h>

#if TARGET_OS_IOS

#import <Foundation/Foundation.h>
#import <Photos/Photos.h>

#include <memory>

namespace sdlsys {

// only with the key: without it the game offers no Photos (and saveToGallery refuses)
bool galleryPossible() { return [[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSPhotoLibraryAddUsageDescription"] != nil; }

bool gallerySave(const std::vector<uint8_t> &png, const std::string &name, const std::string &album,
                 std::function<void(bool, const std::string &)> done, std::string &why) {
	(void)album;
	if (![[NSBundle mainBundle] objectForInfoDictionaryKey:@"NSPhotoLibraryAddUsageDescription"]) {
		why = "the app's Info.plist has no NSPhotoLibraryAddUsageDescription";
		return false;
	}
	NSData *data = [NSData dataWithBytes:png.data() length:png.size()];
	NSString *file = [NSString stringWithUTF8String:name.c_str()];
	auto finish = std::make_shared<std::function<void(bool, const std::string &)>>(std::move(done));
	void (^write)(void) = ^{
		[[PHPhotoLibrary sharedPhotoLibrary]
		    performChanges:^{
			    PHAssetCreationRequest *r = [PHAssetCreationRequest creationRequestForAsset];
			    PHAssetResourceCreationOptions *o = [[PHAssetResourceCreationOptions alloc] init];
			    o.originalFilename = file;
			    [r addResourceWithType:PHAssetResourceTypePhoto data:data options:o];
		    }
		    completionHandler:^(BOOL success, NSError *error) {
			    std::string err = success ? std::string()
			                              : std::string(error ? error.localizedDescription.UTF8String : "Photos refused it");
			    (*finish)(success, err);
		    }];
	};
	if (@available(iOS 14.0, *)) {
		[PHPhotoLibrary requestAuthorizationForAccessLevel:PHAccessLevelAddOnly
		                                           handler:^(PHAuthorizationStatus st) {
			                                           if (st == PHAuthorizationStatusAuthorized || st == PHAuthorizationStatusLimited) write();
			                                           else (*finish)(false, "the player didn't allow adding to Photos");
		                                           }];
	} else {
		[PHPhotoLibrary requestAuthorization:^(PHAuthorizationStatus st) {
			if (st == PHAuthorizationStatusAuthorized) write();
			else (*finish)(false, "the player didn't allow adding to Photos");
		}];
	}
	return true;
}

} // namespace sdlsys

#endif
