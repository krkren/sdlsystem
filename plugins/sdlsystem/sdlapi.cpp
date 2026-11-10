// sdlapi.cpp - finds SDL 3's functions in the engine (sdlapi.h)
//
// SPDX-License-Identifier: 0BSD
#include "sdlapi.h"

#if defined(TVP_STATIC_PLUGIN)
// linked into the engine with SDL: the linker finds them
extern "C" {
bool SDL_OpenURL(const char *url);
bool SDL_AddEventWatch(sdlsys::EventFilter filter, void *userdata);
void SDL_RemoveEventWatch(sdlsys::EventFilter filter, void *userdata);
void *SDL_GetKeyboardFocus(void);
uint64_t SDL_GetWindowFlags(void *window);
bool SDL_SetClipboardData(sdlsys::ClipboardDataCallback callback, sdlsys::ClipboardCleanupCallback cleanup,
                          void *userdata, const char **mime_types, size_t num_mime_types);
bool SDL_HasClipboardData(const char *mime_type);
void SDL_DelayPrecise(uint64_t ns);
uint64_t SDL_GetTicksNS(void);
const char *SDL_GetError(void);
const char *SDL_GetPlatform(void);
int SDL_GetVersion(void);
const char *SDL_GetUserFolder(int folder);
bool SDL_CreateDirectory(const char *path);
bool SDL_StartTextInputWithProperties(void *window, uint32_t props);
bool SDL_StopTextInput(void *window);
bool SDL_TextInputActive(void *window);
bool SDL_SetTextInputArea(void *window, const sdlsys::Rect *rect, int cursor);
bool SDL_HasScreenKeyboardSupport(void);
bool SDL_ScreenKeyboardShown(void *window);
uint32_t SDL_CreateProperties(void);
void SDL_DestroyProperties(uint32_t props);
bool SDL_SetBooleanProperty(uint32_t props, const char *name, bool value);
bool SDL_SetNumberProperty(uint32_t props, const char *name, int64_t value);
void **SDL_GetWindows(int *count);
void SDL_free(void *mem);
}
#elif defined(_WIN32)
#include <windows.h>
#else
#include <dlfcn.h>
#endif

namespace sdlsys {

namespace {

#if !defined(TVP_STATIC_PLUGIN)
// A function by name, from the module that holds SDL
void *lookup(const char *name, const char *&where) {
#if defined(_WIN32)
	static const wchar_t *const modules[] = { L"SDL3.dll", nullptr };
	for (const wchar_t *const *m = modules; ; m++) {
		HMODULE h = *m ? GetModuleHandleW(*m) : GetModuleHandleW(nullptr); // then the executable
		if (h) {
			if (FARPROC p = GetProcAddress(h, name)) {
				where = *m ? "SDL3.dll" : "the executable";
				return reinterpret_cast<void *>(p);
			}
		}
		if (!*m) break;
	}
	return nullptr;
#else
	// every library the engine loaded at start, SDL among them, and the executable
	if (void *p = dlsym(RTLD_DEFAULT, name)) {
		where = "the engine's SDL";
		return p;
	}
	// a library loaded later with RTLD_LOCAL: ask for it by name
	static const char *const names[] = {
#if defined(__APPLE__)
		"libSDL3.0.dylib", "libSDL3.dylib", "@rpath/libSDL3.framework/libSDL3", "SDL3.framework/SDL3",
#else
		"libSDL3.so.0", "libSDL3.so",
#endif
	};
	for (const char *n : names) {
		if (void *h = dlopen(n, RTLD_NOW | RTLD_NOLOAD)) {
			if (void *p = dlsym(h, name)) {
				where = n;
				return p;
			}
		}
	}
	return nullptr;
#endif
}

template <typename F>
bool find(F &f, const char *name, const char *&where) {
	f = reinterpret_cast<F>(lookup(name, where));
	return f != nullptr;
}
#endif

Api load() {
	Api a;
#if defined(TVP_STATIC_PLUGIN)
	a.OpenURL = &SDL_OpenURL;
	a.AddEventWatch = &SDL_AddEventWatch;
	a.RemoveEventWatch = &SDL_RemoveEventWatch;
	a.GetKeyboardFocus = &SDL_GetKeyboardFocus;
	a.GetWindowFlags = &SDL_GetWindowFlags;
	a.SetClipboardData = &SDL_SetClipboardData;
	a.HasClipboardData = &SDL_HasClipboardData;
	a.DelayPrecise = &SDL_DelayPrecise;
	a.GetTicksNS = &SDL_GetTicksNS;
	a.GetError = &SDL_GetError;
	a.GetPlatform = &SDL_GetPlatform;
	a.GetVersion = &SDL_GetVersion;
	a.GetUserFolder = &SDL_GetUserFolder;
	a.MakeDirectory = &SDL_CreateDirectory;
	a.found = true;
	a.where = "the engine (linked in)";
	a.StartTextInputWithProperties = &SDL_StartTextInputWithProperties;
	a.StopTextInput = &SDL_StopTextInput;
	a.TextInputActive = &SDL_TextInputActive;
	a.SetTextInputArea = &SDL_SetTextInputArea;
	a.HasScreenKeyboardSupport = &SDL_HasScreenKeyboardSupport;
	a.ScreenKeyboardShown = &SDL_ScreenKeyboardShown;
	a.CreateProperties = &SDL_CreateProperties;
	a.DestroyProperties = &SDL_DestroyProperties;
	a.SetBooleanProperty = &SDL_SetBooleanProperty;
	a.SetNumberProperty = &SDL_SetNumberProperty;
	a.GetWindows = &SDL_GetWindows;
	a.Free = &SDL_free;
	a.keyboard = true;
#else
	const char *where = "";
	bool all = true;
	all &= find(a.OpenURL, "SDL_OpenURL", where);
	all &= find(a.AddEventWatch, "SDL_AddEventWatch", where);
	all &= find(a.RemoveEventWatch, "SDL_RemoveEventWatch", where);
	all &= find(a.GetKeyboardFocus, "SDL_GetKeyboardFocus", where);
	all &= find(a.GetWindowFlags, "SDL_GetWindowFlags", where);
	all &= find(a.SetClipboardData, "SDL_SetClipboardData", where);
	all &= find(a.HasClipboardData, "SDL_HasClipboardData", where);
	all &= find(a.DelayPrecise, "SDL_DelayPrecise", where);
	all &= find(a.GetTicksNS, "SDL_GetTicksNS", where);
	all &= find(a.GetError, "SDL_GetError", where);
	all &= find(a.GetPlatform, "SDL_GetPlatform", where);
	all &= find(a.GetVersion, "SDL_GetVersion", where);
	all &= find(a.GetUserFolder, "SDL_GetUserFolder", where);
	all &= find(a.MakeDirectory, "SDL_CreateDirectory", where);
	a.found = all;
	a.where = all ? where : "nowhere";
	const char *kwhere = "";
	bool kb = true;
	kb &= find(a.StartTextInputWithProperties, "SDL_StartTextInputWithProperties", kwhere);
	kb &= find(a.StopTextInput, "SDL_StopTextInput", kwhere);
	kb &= find(a.TextInputActive, "SDL_TextInputActive", kwhere);
	kb &= find(a.SetTextInputArea, "SDL_SetTextInputArea", kwhere);
	kb &= find(a.HasScreenKeyboardSupport, "SDL_HasScreenKeyboardSupport", kwhere);
	kb &= find(a.ScreenKeyboardShown, "SDL_ScreenKeyboardShown", kwhere);
	kb &= find(a.CreateProperties, "SDL_CreateProperties", kwhere);
	kb &= find(a.DestroyProperties, "SDL_DestroyProperties", kwhere);
	kb &= find(a.SetBooleanProperty, "SDL_SetBooleanProperty", kwhere);
	kb &= find(a.SetNumberProperty, "SDL_SetNumberProperty", kwhere);
	kb &= find(a.GetWindows, "SDL_GetWindows", kwhere);
	kb &= find(a.Free, "SDL_free", kwhere);
	a.keyboard = kb;
#endif
	return a;
}

} // namespace

const Api &api() {
	static const Api a = load();
	return a;
}

} // namespace sdlsys
