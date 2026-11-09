// sdlapi.h - the few SDL 3 functions the plugin calls, found in the engine at run time
//
// The engine is built on SDL 3 and the plugin calls SDL itself, so it needs no change to the
// engine. Where every plugin is linked into the engine (the web, Android and iOS: the engine
// defines TVP_STATIC_PLUGIN for them) the functions are linked as usual; where the plugin is a
// library of its own (Windows, Linux, macOS) they're looked up in the SDL the engine loaded
// (SDL3.dll, libSDL3.so.0, or the executable itself when SDL is linked into it).
//
// The declarations are SDL 3's own (SDL_events.h, SDL_video.h, SDL_clipboard.h, SDL_misc.h,
// SDL_timer.h), written out here so the plugin builds without SDL's headers. SDL 3 keeps its
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

	bool found = false;      ///< every function above was found
	const char *where = "";  ///< where they were found, for the log
};

/// The functions, looked up once
const Api &api();

} // namespace sdlsys
