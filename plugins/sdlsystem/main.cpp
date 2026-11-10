// main.cpp - the sdlsystem plugin: what SDL knows that the engine doesn't hand to scripts
//
//   SDLSystem.available          whether SDL's functions were found in the engine
//   SDLSystem.platform           SDL's name for the platform ("Windows", "Linux", "Android"...)
//   SDLSystem.open(target)       opens a page or anything with a URL scheme ("https:", "mailto:"),
//                                or a local file or folder (a native path, as from
//                                Storages.getLocalName) with what the system opens it with
//   SDLSystem.run(program, args) starts a program (args: an Array of strings, or more parameters)
//                                without waiting for it; desktops only
//   SDLSystem.canRun             whether this platform can start programs
//   System.shellExecute(target, params = "")
//                                Kirikiri's: on Windows ShellExecute itself; elsewhere a URL or
//                                a file or folder opens as with open(), a program starts with
//                                params split into its arguments
//   SDLSystem.focused            whether the game's window has the keyboard focus
//   SDLSystem.minimized          whether the window is minimized (or hidden)
//   SDLSystem.background         whether the system has put the game in the background (phones,
//                                and the browser's hidden tab)
//   SDLSystem.lowMemory          how many times the system warned that memory is low
//   SDLSystem.takeEvents()       the events since the last call, oldest first, as names: "focus",
//                                "blur", "minimize", "restore", "show", "hide", "background",
//                                "foreground", "lowmemory", "terminating"
//   SDLSystem.onEvent            a function (or void), called at once with the name for
//                                "background", "lowmemory" and "terminating": the system may
//                                stop the game as soon as it has been told, so a save has to be
//                                written there and then. Such an event isn't also in
//                                takeEvents(). The function must catch its own exceptions
//   SDLSystem.copyImage(layer)   puts the layer's picture on the clipboard (PNG and BMP), opaque
//   SDLSystem.clipboardHas(type) whether the clipboard holds data of that MIME type
//   SDLSystem.frameLimit         frames a second at most (0: no limit); not on the web, where
//                                the browser paces the frames
//   SDLSystem.userFolder(name)   one of the player's folders, a native path ending in a separator
//                                ("" where the system has none): home, desktop, documents,
//                                downloads, music, pictures, publicshare, savedgames,
//                                screenshots, templates, videos
//   SDLSystem.makeFolder(path)   makes a folder (a native path) and any missing above it
//   SDLSystem.keyboard           whether SDL's keyboard functions were found (the four below)
//   SDLSystem.screenKeyboard     whether the system shows a keyboard on the screen (phones,
//                                tablets, Steam Deck): text input there opens it
//   SDLSystem.startTextInput(multiline = false, x, y, w, h)
//                                starts SDL's text input on the game's window, so the system's
//                                keyboard comes up where it has one: multiline asks for one
//                                whose Return makes a new line (Android's TYPE_TEXT_FLAG_MULTI_LINE)
//                                and sentence capitals; x, y, w, h (window pixels) say where the
//                                field is, so the system keeps it in sight. The engine stops it
//                                again when the focus moves to a layer with another imeMode
//   SDLSystem.stopTextInput()    stops it (and the keyboard goes)
//   SDLSystem.textInputActive    whether it's on
//   SDLSystem.galleryPossible    whether there's a photo gallery to save to (Android, iOS)
//   SDLSystem.saveToGallery(layer, name, album = "")
//                                adds the layer's picture to the gallery as a PNG named name
//                                (Android: in Pictures/<album>); the result comes later, as the
//                                event "gallerysaved" or "galleryfailed" (takeEvents), with
//                                galleryError saying why; false when it couldn't start
//   SDLSystem.galleryError       why the last gallery save failed
//   SDLSystem.watchdog(seconds, path = "")
//                                watches the game's thread from a thread of its own: when
//                                alive() hasn't come for that long (and the game isn't in the
//                                background or minimized), a report goes to the system's log
//                                (logcat, stderr) and to the file path (a native path): the last
//                                note, and every thread's state and processor time (Linux,
//                                Android); 0 stops it
//   SDLSystem.alive(note = "")   the game's thread is running; note says where (in a report)
//   SDLSystem.watchdogReport     the last report ("" without one)
//   SDLSystem.prefFolder(org, app)
//                                the folder SDL keeps an app's own files in (SDL_GetPrefPath:
//                                where the engine's user:// is), a native path
//   SDLSystem.lastError          why the last call that returned false failed
//
// SPDX-License-Identifier: 0BSD

#include "ncbind.hpp"

#include "codec.h"
#include "gallery.h"
#include "sdlapi.h"
#include "spawn.h"
#include "watchdog.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace {

using namespace sdlsys;

// ------------------------------------------------------------------------
// Strings: TJS's are UTF-16, SDL's and the system's UTF-8
// ------------------------------------------------------------------------

std::string toUtf8(const ttstr &s) {
	const tjs_char *p = s.c_str();
	size_t len = size_t(s.length());
	std::string out;
	out.reserve(len);
	for (size_t k = 0; k < len; k++) {
		uint32_t c = uint16_t(p[k]);
		if (c >= 0xD800 && c < 0xDC00 && k + 1 < len && uint16_t(p[k + 1]) >= 0xDC00 && uint16_t(p[k + 1]) < 0xE000) {
			c = 0x10000 + ((c - 0xD800) << 10) + (uint16_t(p[k + 1]) - 0xDC00);
			k++;
		} else if (c >= 0xD800 && c < 0xE000) {
			c = 0xFFFD;
		}
		if (c < 0x80) {
			out.push_back(char(c));
		} else if (c < 0x800) {
			out.push_back(char(0xC0 | (c >> 6)));
			out.push_back(char(0x80 | (c & 0x3F)));
		} else if (c < 0x10000) {
			out.push_back(char(0xE0 | (c >> 12)));
			out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(char(0x80 | (c & 0x3F)));
		} else {
			out.push_back(char(0xF0 | (c >> 18)));
			out.push_back(char(0x80 | ((c >> 12) & 0x3F)));
			out.push_back(char(0x80 | ((c >> 6) & 0x3F)));
			out.push_back(char(0x80 | (c & 0x3F)));
		}
	}
	return out;
}

ttstr fromUtf8(const char *s) {
	std::u16string out;
	const unsigned char *p = reinterpret_cast<const unsigned char *>(s ? s : "");
	while (*p) {
		uint32_t c = *p++;
		int more = 0;
		if (c >= 0xF0) c &= 0x07, more = 3;
		else if (c >= 0xE0) c &= 0x0F, more = 2;
		else if (c >= 0xC0) c &= 0x1F, more = 1;
		for (; more > 0 && (*p & 0xC0) == 0x80; more--) c = (c << 6) | (*p++ & 0x3F);
		if (c >= 0x10000) {
			c -= 0x10000;
			out.push_back(char16_t(0xD800 + (c >> 10)));
			out.push_back(char16_t(0xDC00 + (c & 0x3FF)));
		} else {
			out.push_back(char16_t(c));
		}
	}
	std::vector<tjs_char> buf(out.begin(), out.end());
	buf.push_back(0);
	return ttstr(buf.data());
}

// ------------------------------------------------------------------------
// State
// ------------------------------------------------------------------------

std::string lastErrorText;

void fail(const std::string &why) {
	lastErrorText = why;
	TVPAddLog(ttstr(TJS_W("sdlsystem: ")) + fromUtf8(why.c_str()));
}

std::mutex eventLock;
std::vector<std::string> pendingEvents;
std::atomic<bool> focused(true), minimized(false), inBackground(false);
std::atomic<int> lowMemoryCount(0);
std::thread::id mainThread;
bool watching = false;
bool inCallback = false;
// made at link time: a tTJSVariant can't be built before the engine has given the plugin its
// functions (a global one crashed the engine as the library loaded)
tTJSVariant *onEventFunc = nullptr;

// Calls onEvent with the name, on the main thread only; false when it couldn't
bool deliverNow(const char *name) {
	if (std::this_thread::get_id() != mainThread || inCallback) return false;
	if (!onEventFunc || onEventFunc->Type() != tvtObject) return false;
	tTJSVariantClosure clo = onEventFunc->AsObjectClosureNoAddRef();
	if (!clo.Object) return false;
	inCallback = true;
	tTJSVariant arg(fromUtf8(name)), result;
	tTJSVariant *args[] = { &arg };
	// the function catches its own exceptions (documented): one that came through here
	// would unwind through this plugin's frames, which some engines can't do
	clo.FuncCall(0, nullptr, nullptr, &result, 1, args, nullptr);
	inCallback = false;
	return true;
}

void note(const char *name, bool urgent) {
	if (urgent && deliverNow(name)) return;
	std::lock_guard<std::mutex> hold(eventLock);
	if (pendingEvents.size() < 256) pendingEvents.push_back(name);
}

bool SDLSYS_CALL watch(void *, Event *e) {
	switch (e->type) {
	case EVENT_WINDOW_FOCUS_GAINED:
		focused = true;
		note("focus", false);
		break;
	case EVENT_WINDOW_FOCUS_LOST:
		focused = false;
		note("blur", false);
		break;
	case EVENT_WINDOW_MINIMIZED:
		minimized = true;
		note("minimize", false);
		break;
	case EVENT_WINDOW_RESTORED:
	case EVENT_WINDOW_MAXIMIZED:
		if (minimized) {
			minimized = false;
			note("restore", false);
		}
		break;
	case EVENT_WINDOW_HIDDEN:
		note("hide", false);
		break;
	case EVENT_WINDOW_SHOWN:
		note("show", false);
		break;
	case EVENT_WILL_ENTER_BACKGROUND:
		if (!inBackground) {
			inBackground = true;
			note("background", true);
		}
		break;
	case EVENT_DID_ENTER_FOREGROUND:
		if (inBackground) {
			inBackground = false;
			note("foreground", false);
		}
		break;
	case EVENT_LOW_MEMORY:
		lowMemoryCount++;
		note("lowmemory", true);
		break;
	case EVENT_TERMINATING:
		note("terminating", true);
		break;
	default:
		break;
	}
	return true;
}

// ------------------------------------------------------------------------
// The frame limit: a continuous handler that waits out the rest of each frame
// ------------------------------------------------------------------------

class FrameLimiter : public tTVPContinuousEventCallbackIntf {
public:
	int fps = 0;
	uint64_t next = 0;
	bool hooked = false;

	void TJS_INTF_METHOD OnContinuousCallback(tjs_uint64) override {
		const Api &a = api();
		if (fps <= 0 || !a.found) return;
		uint64_t period = 1000000000ull / uint64_t(fps);
		uint64_t now = a.GetTicksNS();
		if (next == 0 || now > next + period) next = now; // a long frame: start counting again
		if (now < next) {
			a.DelayPrecise(next - now);
		}
		next += period;
	}

	void set(int v) {
		fps = v < 0 ? 0 : v;
		next = 0;
		if (fps > 0 && !hooked) {
			TVPAddContinuousEventHook(this);
			hooked = true;
		} else if (fps == 0 && hooked) {
			TVPRemoveContinuousEventHook(this);
			hooked = false;
		}
	}
};

FrameLimiter limiter;

bool frameLimitPossible() {
#if defined(__EMSCRIPTEN__)
	return false;
#else
	return true;
#endif
}

// ------------------------------------------------------------------------
// The clipboard
// ------------------------------------------------------------------------

struct ClipImage {
	std::vector<uint8_t> png, bmp;
};

const void *SDLSYS_CALL clipData(void *userdata, const char *mime, size_t *size) {
	ClipImage *c = static_cast<ClipImage *>(userdata);
	const std::vector<uint8_t> *d = nullptr;
	if (mime && std::string(mime) == "image/png") d = &c->png;
	else if (mime && std::string(mime) == "image/bmp") d = &c->bmp;
	if (!d || d->empty()) {
		*size = 0;
		return nullptr;
	}
	*size = d->size();
	return d->data();
}

void SDLSYS_CALL clipCleanup(void *userdata) { delete static_cast<ClipImage *>(userdata); }

bool getInt(iTJSDispatch2 *obj, const tjs_char *name, tjs_int64 &out) {
	tTJSVariant v;
	if (TJS_FAILED(obj->PropGet(0, name, nullptr, &v, obj))) return false;
	if (v.Type() != tvtInteger && v.Type() != tvtReal) return false;
	out = v.AsInteger();
	return true;
}

// The game's window: the one with the keyboard focus, else the first
void *gameWindow() {
	const Api &a = api();
	void *w = a.GetKeyboardFocus ? a.GetKeyboardFocus() : nullptr;
	if (w || !a.GetWindows) return w;
	int n = 0;
	void **list = a.GetWindows(&n);
	if (list) {
		if (n > 0) w = list[0];
		a.Free(list);
	}
	return w;
}

std::mutex galleryLock;
std::string galleryErrorText;

// the watchdog doesn't count while the game is put away (its loop stops then)
bool watchPaused() { return inBackground || minimized; }

} // namespace

// ------------------------------------------------------------------------
// The class
// ------------------------------------------------------------------------

class SDLSystem {
public:
	static bool getAvailable() { return api().found; }
	static ttstr getWhere() { return fromUtf8(api().where); }
	static ttstr getPlatform() { return api().found ? fromUtf8(api().GetPlatform()) : ttstr(); }
	static tjs_int getSdlVersion() { return api().found ? api().GetVersion() : 0; }
	static bool getCanRun() { return canRun(); }
	static bool getFocused() { return focused; }
	static bool getMinimized() { return minimized; }
	static bool getBackground() { return inBackground; }
	static tjs_int getLowMemory() { return lowMemoryCount; }
	static ttstr getLastError() { return fromUtf8(lastErrorText.c_str()); }
	static tjs_int getFrameLimit() { return limiter.fps; }
	static void setFrameLimit(tjs_int v) {
		if (!frameLimitPossible()) return;
		limiter.set(int(v));
	}
	static bool getFrameLimitPossible() { return frameLimitPossible() && api().found; }

	static bool open(ttstr target) {
		lastErrorText.clear();
		std::string t = toUtf8(target);
		if (t.empty()) {
			fail("open: nothing to open");
			return false;
		}
#if defined(_WIN32)
		// a local path: ShellExecute knows it better than a file: URL
		if (!hasScheme(t)) {
			std::string err;
			if (shellOpenWindows(t, std::string(), err)) return true;
			fail("open " + t + ": " + err);
			return false;
		}
#endif
		const Api &a = api();
		if (!a.found) {
			fail("open: SDL's functions weren't found in the engine");
			return false;
		}
		std::string url = hasScheme(t) ? t : fileUrl(t);
		if (!a.OpenURL(url.c_str())) {
			fail("open " + url + ": " + std::string(a.GetError()));
			return false;
		}
		return true;
	}

	static tjs_error TJS_INTF_METHOD run(tTJSVariant *result, tjs_int numparams, tTJSVariant **param, iTJSDispatch2 *) {
		lastErrorText.clear();
		if (numparams < 1 || param[0]->Type() == tvtVoid) {
			fail("run: no program");
			if (result) *result = false;
			return TJS_S_OK;
		}
		std::string program = toUtf8(param[0]->AsStringNoAddRef());
		std::vector<std::string> args;
		if (numparams >= 2 && param[1]->Type() == tvtObject && numparams == 2) {
			// an Array
			iTJSDispatch2 *arr = param[1]->AsObjectNoAddRef();
			tTJSVariant count;
			if (arr && TJS_SUCCEEDED(arr->PropGet(0, TJS_W("count"), nullptr, &count, arr))) {
				tjs_int n = count.AsInteger();
				for (tjs_int k = 0; k < n; k++) {
					tTJSVariant v;
					arr->PropGetByNum(0, k, &v, arr);
					args.push_back(v.Type() == tvtVoid ? std::string() : toUtf8(ttstr(v)));
				}
			}
		} else {
			for (tjs_int k = 1; k < numparams; k++)
				args.push_back(param[k]->Type() == tvtVoid ? std::string() : toUtf8(ttstr(*param[k])));
		}
		std::string err;
		bool ok = runProgram(program, args, err);
		if (!ok) fail("run: " + err);
		if (result) *result = ok;
		return TJS_S_OK;
	}

	static tjs_error TJS_INTF_METHOD takeEvents(tTJSVariant *result, tjs_int, tTJSVariant **, iTJSDispatch2 *) {
		std::vector<std::string> list;
		{
			std::lock_guard<std::mutex> hold(eventLock);
			list.swap(pendingEvents);
		}
		iTJSDispatch2 *arr = TJSCreateArrayObject();
		for (size_t k = 0; k < list.size(); k++) {
			tTJSVariant v(fromUtf8(list[k].c_str()));
			arr->PropSetByNum(TJS_MEMBERENSURE, tjs_int(k), &v, arr);
		}
		if (result) *result = tTJSVariant(arr, arr);
		arr->Release();
		return TJS_S_OK;
	}

	static tTJSVariant getOnEvent() { return onEventFunc ? *onEventFunc : tTJSVariant(); }
	static void setOnEvent(tTJSVariant v) {
		if (onEventFunc) *onEventFunc = v;
	}

	static bool copyImage(tTJSVariant layer) {
		lastErrorText.clear();
		const Api &a = api();
		if (!a.found) {
			fail("copyImage: SDL's functions weren't found in the engine");
			return false;
		}
		if (layer.Type() != tvtObject || !layer.AsObjectNoAddRef()) {
			fail("copyImage: not a layer");
			return false;
		}
		iTJSDispatch2 *obj = layer.AsObjectNoAddRef();
		tjs_int64 buffer = 0, pitch = 0, width = 0, height = 0;
		if (!getInt(obj, TJS_W("imageWidth"), width) || !getInt(obj, TJS_W("imageHeight"), height) ||
		    !getInt(obj, TJS_W("mainImageBufferPitch"), pitch) || !getInt(obj, TJS_W("mainImageBuffer"), buffer) ||
		    buffer == 0 || width <= 0 || height <= 0) {
			fail("copyImage: the layer has no picture");
			return false;
		}
		const uint8_t *px = reinterpret_cast<const uint8_t *>(static_cast<intptr_t>(buffer));
		ClipImage *c = new ClipImage;
		c->png = encodePng(px, int(width), int(height), long(pitch));
		c->bmp = encodeBmp(px, int(width), int(height), long(pitch));
		const char *types[] = { "image/png", "image/bmp" };
		if (!a.SetClipboardData(clipData, clipCleanup, c, types, 2)) {
			fail(std::string("copyImage: ") + a.GetError());
			delete c;
			return false;
		}
		return true;
	}

	static ttstr userFolder(ttstr name) {
		static const char *const names[] = { "home", "desktop", "documents", "downloads", "music", "pictures",
		                                     "publicshare", "savedgames", "screenshots", "templates", "videos" };
		const Api &a = api();
		if (!a.found) return ttstr();
		std::string n = toUtf8(name);
		for (int k = 0; k < int(sizeof names / sizeof names[0]); k++)
			if (n == names[k]) {
				const char *p = a.GetUserFolder(k);
				return p ? fromUtf8(p) : ttstr();
			}
		return ttstr();
	}

	static bool makeFolder(ttstr path) {
		lastErrorText.clear();
		const Api &a = api();
		if (!a.found) {
			fail("makeFolder: SDL's functions weren't found in the engine");
			return false;
		}
		std::string p = toUtf8(path);
		if (!a.MakeDirectory(p.c_str())) {
			fail("makeFolder " + p + ": " + std::string(a.GetError()));
			return false;
		}
		return true;
	}

	// the watchdog (watchdog.cpp): seconds without alive() make a report
	static tjs_error TJS_INTF_METHOD watchdog(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
	                                          iTJSDispatch2 *) {
		int seconds = numparams >= 1 && param[0]->Type() != tvtVoid ? (int)param[0]->AsInteger() : 0;
		std::string path = numparams >= 2 && param[1]->Type() != tvtVoid ? toUtf8(ttstr(*param[1])) : std::string();
		bool ok = seconds > 0 ? sdlsys::watchdogStart(seconds, path, &watchPaused) : (sdlsys::watchdogStop(), true);
		if (result) *result = ok;
		return TJS_S_OK;
	}
	static tjs_error TJS_INTF_METHOD alive(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
	                                       iTJSDispatch2 *) {
		sdlsys::watchdogAlive(numparams >= 1 && param[0]->Type() != tvtVoid ? toUtf8(ttstr(*param[0])) : std::string());
		if (result) result->Clear();
		return TJS_S_OK;
	}
	static ttstr getWatchdogReport() { return fromUtf8(sdlsys::watchdogLastReport().c_str()); }
	static ttstr prefFolder(ttstr org, ttstr app) {
		const Api &a = api();
		if (!a.GetPrefPath || !a.Free) return ttstr();
		char *p = a.GetPrefPath(toUtf8(org).c_str(), toUtf8(app).c_str());
		if (!p) return ttstr();
		ttstr r = fromUtf8(p);
		a.Free(p);
		return r;
	}

	static bool getKeyboard() { return api().keyboard; }
	static bool getScreenKeyboard() { return api().keyboard && api().HasScreenKeyboardSupport(); }
	static bool getTextInputActive() {
		const Api &a = api();
		void *w = a.keyboard ? gameWindow() : nullptr;
		return w && a.TextInputActive(w);
	}

	static tjs_error TJS_INTF_METHOD startTextInput(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
	                                                iTJSDispatch2 *) {
		lastErrorText.clear();
		const Api &a = api();
		void *w = a.keyboard ? gameWindow() : nullptr;
		if (!w) {
			fail(a.keyboard ? "startTextInput: no window" : "startTextInput: SDL's keyboard functions weren't found");
			if (result) *result = false;
			return TJS_S_OK;
		}
		bool multiline = numparams >= 1 && param[0]->Type() != tvtVoid && param[0]->AsInteger() != 0;
		uint32_t props = a.CreateProperties();
		a.SetNumberProperty(props, SDLSYS_PROP_TEXTINPUT_TYPE, TEXTINPUT_TYPE_TEXT);
		a.SetNumberProperty(props, SDLSYS_PROP_TEXTINPUT_CAPITALIZATION, multiline ? CAPITALIZE_SENTENCES : CAPITALIZE_NONE);
		a.SetBooleanProperty(props, SDLSYS_PROP_TEXTINPUT_AUTOCORRECT, multiline);
		a.SetBooleanProperty(props, SDLSYS_PROP_TEXTINPUT_MULTILINE, multiline);
		int64_t android = ANDROID_TYPE_CLASS_TEXT;
		if (multiline) android |= ANDROID_TYPE_TEXT_FLAG_MULTI_LINE | ANDROID_TYPE_TEXT_FLAG_CAP_SENTENCES | ANDROID_TYPE_TEXT_FLAG_AUTO_CORRECT;
		a.SetNumberProperty(props, SDLSYS_PROP_TEXTINPUT_ANDROID_INPUTTYPE, android);
		if (numparams >= 5) {
			Rect r = { int(param[1]->AsInteger()), int(param[2]->AsInteger()), int(param[3]->AsInteger()),
			           int(param[4]->AsInteger()) };
			if (r.w > 0 && r.h > 0) a.SetTextInputArea(w, &r, 0);
		}
		bool ok = a.StartTextInputWithProperties(w, props);
		a.DestroyProperties(props);
		if (!ok) fail(std::string("startTextInput: ") + a.GetError());
		if (result) *result = ok;
		return TJS_S_OK;
	}

	static bool stopTextInput() {
		lastErrorText.clear();
		const Api &a = api();
		void *w = a.keyboard ? gameWindow() : nullptr;
		if (!w) return false;
		return a.StopTextInput(w);
	}

	static bool getGalleryPossible() { return galleryPossible(); }
	static ttstr getGalleryError() {
		std::lock_guard<std::mutex> hold(galleryLock);
		return fromUtf8(galleryErrorText.c_str());
	}

	static tjs_error TJS_INTF_METHOD saveToGallery(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
	                                               iTJSDispatch2 *) {
		lastErrorText.clear();
		if (result) *result = false;
		if (numparams < 2 || param[0]->Type() != tvtObject || !param[0]->AsObjectNoAddRef()) {
			fail("saveToGallery: a layer and a name");
			return TJS_S_OK;
		}
		iTJSDispatch2 *obj = param[0]->AsObjectNoAddRef();
		tjs_int64 buffer = 0, pitch = 0, width = 0, height = 0;
		if (!getInt(obj, TJS_W("imageWidth"), width) || !getInt(obj, TJS_W("imageHeight"), height) ||
		    !getInt(obj, TJS_W("mainImageBufferPitch"), pitch) || !getInt(obj, TJS_W("mainImageBuffer"), buffer) ||
		    buffer == 0 || width <= 0 || height <= 0) {
			fail("saveToGallery: the layer has no picture");
			return TJS_S_OK;
		}
		std::string name = toUtf8(ttstr(*param[1]));
		if (name.empty()) name = "screenshot.png";
		std::string album = numparams >= 3 && param[2]->Type() != tvtVoid ? toUtf8(ttstr(*param[2])) : std::string();
		const uint8_t *px = reinterpret_cast<const uint8_t *>(static_cast<intptr_t>(buffer));
		std::vector<uint8_t> png = encodePng(px, int(width), int(height), long(pitch));
		std::string why;
		bool started = gallerySave(png, name, album,
		                           [](bool ok, const std::string &err) {
			                           {
				                           std::lock_guard<std::mutex> hold(galleryLock);
				                           galleryErrorText = ok ? std::string() : err;
			                           }
			                           // (no TVPAddLog: on iOS this runs on another thread; the
			                           // script reads galleryError as the event comes)
			                           note(ok ? "gallerysaved" : "galleryfailed", false);
		                           },
		                           why);
		if (!started) {
			fail("saveToGallery: " + why);
			std::lock_guard<std::mutex> hold(galleryLock);
			galleryErrorText = why;
		}
		if (result) *result = started;
		return TJS_S_OK;
	}

	static bool clipboardHas(ttstr mime) {
		const Api &a = api();
		if (!a.found) return false;
		std::string m = toUtf8(mime);
		return a.HasClipboardData(m.c_str());
	}
};

// System.shellExecute, as Kirikiri's shellExecute plugin gives it on Windows
static tjs_error TJS_INTF_METHOD shellExecute(tTJSVariant *result, tjs_int numparams, tTJSVariant **param,
                                              iTJSDispatch2 *) {
	lastErrorText.clear();
	if (numparams < 1 || param[0]->Type() == tvtVoid) {
		fail("shellExecute: nothing to open");
		if (result) *result = false;
		return TJS_S_OK;
	}
	std::string target = toUtf8(param[0]->AsStringNoAddRef());
	std::string params = numparams >= 2 && param[1]->Type() != tvtVoid ? toUtf8(ttstr(*param[1])) : std::string();
	bool ok = false;
	std::string err;
#if defined(_WIN32)
	ok = shellOpenWindows(target, params, err);
	if (!ok) fail("shellExecute " + target + ": " + err);
#else
	if (!hasScheme(target) && isProgram(target)) {
		ok = runProgram(target, splitArgs(params), err);
		if (!ok) fail("shellExecute " + target + ": " + err);
	} else {
		ok = SDLSystem::open(ttstr(param[0]->AsStringNoAddRef()));
	}
#endif
	if (result) *result = ok;
	return TJS_S_OK;
}

NCB_REGISTER_CLASS(SDLSystem) {
	NCB_PROPERTY_RO(available, getAvailable);
	NCB_PROPERTY_RO(where, getWhere);
	NCB_PROPERTY_RO(platform, getPlatform);
	NCB_PROPERTY_RO(sdlVersion, getSdlVersion);
	NCB_PROPERTY_RO(canRun, getCanRun);
	NCB_PROPERTY_RO(focused, getFocused);
	NCB_PROPERTY_RO(minimized, getMinimized);
	NCB_PROPERTY_RO(background, getBackground);
	NCB_PROPERTY_RO(lowMemory, getLowMemory);
	NCB_PROPERTY_RO(lastError, getLastError);
	NCB_PROPERTY(frameLimit, getFrameLimit, setFrameLimit);
	NCB_PROPERTY_RO(frameLimitPossible, getFrameLimitPossible);
	NCB_PROPERTY(onEvent, getOnEvent, setOnEvent);
	NCB_METHOD(open);
	NCB_METHOD_RAW_CALLBACK(run, &SDLSystem::run, TJS_STATICMEMBER);
	NCB_METHOD_RAW_CALLBACK(takeEvents, &SDLSystem::takeEvents, TJS_STATICMEMBER);
	NCB_METHOD(copyImage);
	NCB_METHOD(clipboardHas);
	NCB_METHOD(userFolder);
	NCB_METHOD(makeFolder);
	NCB_METHOD_RAW_CALLBACK(watchdog, &SDLSystem::watchdog, TJS_STATICMEMBER);
	NCB_METHOD_RAW_CALLBACK(alive, &SDLSystem::alive, TJS_STATICMEMBER);
	NCB_PROPERTY_RO(watchdogReport, getWatchdogReport);
	NCB_METHOD(prefFolder);
	NCB_PROPERTY_RO(keyboard, getKeyboard);
	NCB_PROPERTY_RO(screenKeyboard, getScreenKeyboard);
	NCB_PROPERTY_RO(textInputActive, getTextInputActive);
	NCB_METHOD_RAW_CALLBACK(startTextInput, &SDLSystem::startTextInput, TJS_STATICMEMBER);
	NCB_METHOD(stopTextInput);
	NCB_PROPERTY_RO(galleryPossible, getGalleryPossible);
	NCB_PROPERTY_RO(galleryError, getGalleryError);
	NCB_METHOD_RAW_CALLBACK(saveToGallery, &SDLSystem::saveToGallery, TJS_STATICMEMBER);
}

NCB_ATTACH_FUNCTION(shellExecute, System, shellExecute);

static void sdlsystemRegist() {
	mainThread = std::this_thread::get_id();
	if (!onEventFunc) onEventFunc = new tTJSVariant();
	const Api &a = api();
	if (!a.found) {
		TVPAddLog(TJS_W("sdlsystem: SDL's functions weren't found in the engine; opening pages, the window's "
		                "events and the clipboard do nothing"));
		return;
	}
	void *win = a.GetKeyboardFocus();
	focused = win != nullptr;
	if (win) minimized = (a.GetWindowFlags(win) & (WINDOW_MINIMIZED | WINDOW_HIDDEN)) != 0;
	watching = a.AddEventWatch(watch, nullptr);
}

static void sdlsystemUnregist() {
	sdlsys::watchdogStop(); // its thread goes before the library does
	const Api &a = api();
	if (watching && a.found) a.RemoveEventWatch(watch, nullptr);
	watching = false;
	limiter.set(0);
	delete onEventFunc;
	onEventFunc = nullptr;
}

NCB_POST_REGIST_CALLBACK(sdlsystemRegist);
NCB_PRE_UNREGIST_CALLBACK(sdlsystemUnregist);
