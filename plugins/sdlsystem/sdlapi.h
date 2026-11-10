// sdlapi.h - the few SDL 3 functions the plugin calls, found in the engine at run time
//
// The engine is built on SDL 3 and the plugin calls SDL itself, so it needs no change to the
// engine. Where every plugin is linked into the engine (the web, Android and iOS: the engine
// defines TVP_STATIC_PLUGIN for them) the functions are linked as usual; where the plugin is a
// library of its own (Windows, Linux, macOS) they're looked up in the SDL the engine loaded
// (SDL3.dll, libSDL3.so.0, or the executable itself when SDL is linked into it).
//
// The declarations are SDL 3's own (SDL_events.h, SDL_video.h, SDL_clipboard.h, SDL_misc.h,
// SDL_timer.h, SDL_keyboard.h, SDL_properties.h), written out here so the plugin builds without SDL's headers. SDL 3 keeps its
// ABI within the major version.
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <cstddef>
#include <cstdint>

namespace sdlsys {

// SDL_Event: a union of 128 bytes whose first member is the event type
union Event {
	uint32_t type;
	uint8_t padding[128];
};

enum : uint32_t {
	EVENT_TERMINATING = 0x101,
	EVENT_LOW_MEMORY = 0x102,
	EVENT_WILL_ENTER_BACKGROUND = 0x103,
	EVENT_DID_ENTER_BACKGROUND = 0x104,
	EVENT_WILL_ENTER_FOREGROUND = 0x105,
	EVENT_DID_ENTER_FOREGROUND = 0x106,
	EVENT_WINDOW_SHOWN = 0x202,
	EVENT_WINDOW_HIDDEN = 0x203,
	EVENT_WINDOW_MINIMIZED = 0x209,
	EVENT_WINDOW_MAXIMIZED = 0x20a,
	EVENT_WINDOW_RESTORED = 0x20b,
	EVENT_WINDOW_FOCUS_GAINED = 0x20e,
	EVENT_WINDOW_FOCUS_LOST = 0x20f,
	EVENT_WINDOW_OCCLUDED = 0x216,
};

// SDL_Folder, for SDL_GetUserFolder
enum Folder {
	FOLDER_HOME, FOLDER_DESKTOP, FOLDER_DOCUMENTS, FOLDER_DOWNLOADS, FOLDER_MUSIC, FOLDER_PICTURES,
	FOLDER_PUBLICSHARE, FOLDER_SAVEDGAMES, FOLDER_SCREENSHOTS, FOLDER_TEMPLATES, FOLDER_VIDEOS
};

enum : uint64_t {
	WINDOW_OCCLUDED = 0x4,
	WINDOW_HIDDEN = 0x8,
	WINDOW_MINIMIZED = 0x40,
	WINDOW_INPUT_FOCUS = 0x200,
};

#if defined(_WIN32)
#define SDLSYS_CALL __cdecl
#else
#define SDLSYS_CALL
#endif

// SDL_Rect
struct Rect {
	int x, y, w, h;
};

// SDL_keyboard.h's text input properties, and Android's InputType flags for the last one
#define SDLSYS_PROP_TEXTINPUT_TYPE "SDL.textinput.type"
#define SDLSYS_PROP_TEXTINPUT_CAPITALIZATION "SDL.textinput.capitalization"
#define SDLSYS_PROP_TEXTINPUT_AUTOCORRECT "SDL.textinput.autocorrect"
#define SDLSYS_PROP_TEXTINPUT_MULTILINE "SDL.textinput.multiline"
#define SDLSYS_PROP_TEXTINPUT_ANDROID_INPUTTYPE "SDL.textinput.android.inputtype"
enum : int64_t {
	TEXTINPUT_TYPE_TEXT = 0,
	CAPITALIZE_NONE = 0,
	CAPITALIZE_SENTENCES = 1,
	ANDROID_TYPE_CLASS_TEXT = 0x1,
	ANDROID_TYPE_TEXT_FLAG_CAP_SENTENCES = 0x4000,
	ANDROID_TYPE_TEXT_FLAG_AUTO_CORRECT = 0x8000,
	ANDROID_TYPE_TEXT_FLAG_MULTI_LINE = 0x20000,
};

typedef bool(SDLSYS_CALL *EventFilter)(void *userdata, Event *event);
typedef const void *(SDLSYS_CALL *ClipboardDataCallback)(void *userdata, const char *mime_type, size_t *size);
typedef void(SDLSYS_CALL *ClipboardCleanupCallback)(void *userdata);

struct Api {
	bool (SDLSYS_CALL *OpenURL)(const char *url) = nullptr;
	bool (SDLSYS_CALL *AddEventWatch)(EventFilter filter, void *userdata) = nullptr;
	void (SDLSYS_CALL *RemoveEventWatch)(EventFilter filter, void *userdata) = nullptr;
	void *(SDLSYS_CALL *GetKeyboardFocus)(void) = nullptr;
	uint64_t (SDLSYS_CALL *GetWindowFlags)(void *window) = nullptr;
	bool (SDLSYS_CALL *SetClipboardData)(ClipboardDataCallback callback, ClipboardCleanupCallback cleanup,
	                                     void *userdata, const char **mime_types, size_t num_mime_types) = nullptr;
	bool (SDLSYS_CALL *HasClipboardData)(const char *mime_type) = nullptr;
	void (SDLSYS_CALL *DelayPrecise)(uint64_t ns) = nullptr;
	uint64_t (SDLSYS_CALL *GetTicksNS)(void) = nullptr;
	const char *(SDLSYS_CALL *GetError)(void) = nullptr;
	const char *(SDLSYS_CALL *GetPlatform)(void) = nullptr;
	int (SDLSYS_CALL *GetVersion)(void) = nullptr;
	const char *(SDLSYS_CALL *GetUserFolder)(int folder) = nullptr;
	// not "CreateDirectory": a macro of Windows' headers
	bool (SDLSYS_CALL *MakeDirectory)(const char *path) = nullptr;

	bool found = false;      ///< every function above was found
	const char *where = "";  ///< where they were found, for the log

	// The keyboard (SDL_keyboard.h, SDL_properties.h): looked up apart, so an SDL without them
	// leaves the rest working
	bool (SDLSYS_CALL *StartTextInputWithProperties)(void *window, uint32_t props) = nullptr;
	bool (SDLSYS_CALL *StopTextInput)(void *window) = nullptr;
	bool (SDLSYS_CALL *TextInputActive)(void *window) = nullptr;
	bool (SDLSYS_CALL *SetTextInputArea)(void *window, const Rect *rect, int cursor) = nullptr;
	bool (SDLSYS_CALL *HasScreenKeyboardSupport)(void) = nullptr;
	bool (SDLSYS_CALL *ScreenKeyboardShown)(void *window) = nullptr;
	uint32_t (SDLSYS_CALL *CreateProperties)(void) = nullptr;
	void (SDLSYS_CALL *DestroyProperties)(uint32_t props) = nullptr;
	bool (SDLSYS_CALL *SetBooleanProperty)(uint32_t props, const char *name, bool value) = nullptr;
	bool (SDLSYS_CALL *SetNumberProperty)(uint32_t props, const char *name, int64_t value) = nullptr;
	void **(SDLSYS_CALL *GetWindows)(int *count) = nullptr;
	void (SDLSYS_CALL *Free)(void *mem) = nullptr;
	bool keyboard = false;   ///< every keyboard function was found
	// SDL_GetPrefPath (the folder the engine's user:// storage is in), freed with Free; optional
	char *(SDLSYS_CALL *GetPrefPath)(const char *org, const char *app) = nullptr;
};

/// The functions, looked up once
const Api &api();

} // namespace sdlsys
