// gallery_android.cpp - Android: a picture into MediaStore's images (Pictures/<album>), through
// JNI on the engine's thread (SDL's, which SDL has attached to the VM)
//
// Android 10 (API 29) and later let an app add its own pictures to the shared collection with
// no permission (RELATIVE_PATH, IS_PENDING); earlier versions want WRITE_EXTERNAL_STORAGE,
// which the game doesn't ask for, so those say no.
//
// SPDX-License-Identifier: 0BSD
#include "gallery.h"

#if defined(__ANDROID__)

#include <jni.h>

extern "C" {
void *SDL_GetAndroidJNIEnv(void);
void *SDL_GetAndroidActivity(void);
}

namespace sdlsys {

namespace {

bool failed(JNIEnv *env) {
	if (env->ExceptionCheck()) {
		env->ExceptionClear();
		return true;
	}
	return false;
}

int sdkInt(JNIEnv *e) {
	jclass ver = e->FindClass("android/os/Build$VERSION");
	if (failed(e) || !ver) return 0;
	jfieldID f = e->GetStaticFieldID(ver, "SDK_INT", "I");
	if (failed(e) || !f) return 0;
	return e->GetStaticIntField(ver, f);
}

// ContentValues.put(key, String) or put(key, Integer)
bool putString(JNIEnv *e, jobject cv, const char *key, const std::string &value) {
	jclass cls = e->GetObjectClass(cv);
	jmethodID m = e->GetMethodID(cls, "put", "(Ljava/lang/String;Ljava/lang/String;)V");
	if (failed(e) || !m) return false;
	e->CallVoidMethod(cv, m, e->NewStringUTF(key), e->NewStringUTF(value.c_str()));
	return !failed(e);
}

bool putInt(JNIEnv *e, jobject cv, const char *key, int value) {
	jclass integer = e->FindClass("java/lang/Integer");
	if (failed(e) || !integer) return false;
	jmethodID valueOf = e->GetStaticMethodID(integer, "valueOf", "(I)Ljava/lang/Integer;");
	if (failed(e) || !valueOf) return false;
	jobject boxed = e->CallStaticObjectMethod(integer, valueOf, value);
	if (failed(e) || !boxed) return false;
	jclass cls = e->GetObjectClass(cv);
	jmethodID m = e->GetMethodID(cls, "put", "(Ljava/lang/String;Ljava/lang/Integer;)V");
	if (failed(e) || !m) return false;
	e->CallVoidMethod(cv, m, e->NewStringUTF(key), boxed);
	return !failed(e);
}

jobject newValues(JNIEnv *e) {
	jclass cls = e->FindClass("android/content/ContentValues");
	if (failed(e) || !cls) return nullptr;
	jmethodID ctor = e->GetMethodID(cls, "<init>", "()V");
	if (failed(e) || !ctor) return nullptr;
	jobject cv = e->NewObject(cls, ctor);
	return failed(e) ? nullptr : cv;
}

bool save(JNIEnv *e, const std::vector<uint8_t> &png, const std::string &name, const std::string &album,
          std::string &why) {
	if (sdkInt(e) < 29) {
		why = "saving to the gallery needs Android 10 or later";
		return false;
	}
	jobject activity = static_cast<jobject>(SDL_GetAndroidActivity());
	if (!activity) {
		why = "no activity";
		return false;
	}
	jclass actCls = e->GetObjectClass(activity);
	jmethodID getResolver = e->GetMethodID(actCls, "getContentResolver", "()Landroid/content/ContentResolver;");
	if (failed(e) || !getResolver) {
		why = "no getContentResolver";
		return false;
	}
	jobject resolver = e->CallObjectMethod(activity, getResolver);
	if (failed(e) || !resolver) {
		why = "no content resolver";
		return false;
	}
	jclass media = e->FindClass("android/provider/MediaStore$Images$Media");
	if (failed(e) || !media) {
		why = "no MediaStore";
		return false;
	}
	jfieldID uriField = e->GetStaticFieldID(media, "EXTERNAL_CONTENT_URI", "Landroid/net/Uri;");
	if (failed(e) || !uriField) {
		why = "no EXTERNAL_CONTENT_URI";
		return false;
	}
	jobject collection = e->GetStaticObjectField(media, uriField);
	jobject cv = newValues(e);
	if (!collection || !cv) {
		why = "MediaStore's collection";
		return false;
	}
	std::string dir = "Pictures";
	if (!album.empty()) dir += "/" + album;
	if (!putString(e, cv, "_display_name", name) || !putString(e, cv, "mime_type", "image/png") ||
	    !putString(e, cv, "relative_path", dir) || !putInt(e, cv, "is_pending", 1)) {
		why = "ContentValues";
		return false;
	}
	jclass resCls = e->GetObjectClass(resolver);
	jmethodID insert = e->GetMethodID(resCls, "insert", "(Landroid/net/Uri;Landroid/content/ContentValues;)Landroid/net/Uri;");
	jmethodID openOut = e->GetMethodID(resCls, "openOutputStream", "(Landroid/net/Uri;)Ljava/io/OutputStream;");
	jmethodID update = e->GetMethodID(resCls, "update",
	                                  "(Landroid/net/Uri;Landroid/content/ContentValues;Ljava/lang/String;[Ljava/lang/String;)I");
	jmethodID remove = e->GetMethodID(resCls, "delete", "(Landroid/net/Uri;Ljava/lang/String;[Ljava/lang/String;)I");
	if (failed(e) || !insert || !openOut || !update || !remove) {
		why = "ContentResolver's methods";
		return false;
	}
	jobject uri = e->CallObjectMethod(resolver, insert, collection, cv);
	if (failed(e) || !uri) {
		why = "MediaStore refused the picture";
		return false;
	}
	bool ok = false;
	jobject out = e->CallObjectMethod(resolver, openOut, uri);
	if (!failed(e) && out) {
		jbyteArray bytes = e->NewByteArray(jsize(png.size()));
		if (!failed(e) && bytes) {
			e->SetByteArrayRegion(bytes, 0, jsize(png.size()), reinterpret_cast<const jbyte *>(png.data()));
			jclass osCls = e->GetObjectClass(out);
			jmethodID write = e->GetMethodID(osCls, "write", "([B)V");
			jmethodID close = e->GetMethodID(osCls, "close", "()V");
			if (!failed(e) && write && close) {
				e->CallVoidMethod(out, write, bytes);
				bool wrote = !failed(e);
				e->CallVoidMethod(out, close);
				ok = wrote && !failed(e);
			}
		}
	}
	if (!ok) {
		why = "writing the picture failed";
		e->CallIntMethod(resolver, remove, uri, nullptr, nullptr);
		failed(e);
		return false;
	}
	jobject done = newValues(e);
	if (!done || !putInt(e, done, "is_pending", 0)) {
		why = "ContentValues";
		return false;
	}
	e->CallIntMethod(resolver, update, uri, done, nullptr, nullptr);
	if (failed(e)) {
		why = "MediaStore didn't take the finished picture";
		return false;
	}
	return true;
}

} // namespace

bool galleryPossible() { return true; }

bool gallerySave(const std::vector<uint8_t> &png, const std::string &name, const std::string &album,
                 std::function<void(bool, const std::string &)> done, std::string &why) {
	JNIEnv *e = static_cast<JNIEnv *>(SDL_GetAndroidJNIEnv());
	if (!e) {
		why = "no JNI environment";
		return false;
	}
	e->PushLocalFrame(64);
	std::string err;
	bool ok = save(e, png, name, album, err);
	e->PopLocalFrame(nullptr);
	done(ok, err);
	return true;
}

} // namespace sdlsys

#endif
