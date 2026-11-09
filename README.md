# sdlsystem

A plugin for the SDL 3 builds of the Kirikiri Z engine (krkrz_dev) that gives scripts what SDL
knows and the engine doesn't hand them: opening web pages, files and folders, starting programs,
the window's focus, the game going into the background, the system's low-memory warning,
pictures on the clipboard and a frame-rate limit. It needs no change to the engine: it calls SDL
itself, the SDL the engine is built on.

Written for [KAGEX](https://github.com/krkren/kagex), whose engines have it built in, but it
works with any game on those engines.

## For scripts

```
Plugins.link("sdlsystem.dll");

SDLSystem.open("https://example.com");             // the browser
SDLSystem.open(Storages.getLocalName(folder));      // a folder, in the file manager
SDLSystem.run("code", ["--goto", "first.ks:12"]);  // a program, not waited for
System.shellExecute("readme.txt");                  // Kirikiri's, on every platform
```

| Member | |
|---|---|
| `available` | whether SDL's functions were found in the engine; without them, everything below that needs SDL does nothing |
| `platform` | SDL's name for the platform: `Windows`, `macOS`, `Linux`, `Android`, `iOS`, `Emscripten` |
| `open(target)` | opens a page or anything with a URL scheme (`https:`, `mailto:`), or a local file or folder (a native path) with what the system opens it with; true when the system took it |
| `run(program, args)` | starts a program with its arguments (an Array, or more parameters), without a shell and without waiting; a program without a folder is looked for on `PATH`. Windows, macOS and Linux |
| `canRun` | whether this platform can start programs |
| `System.shellExecute(target, params = "")` | Kirikiri's: Windows' `ShellExecute` there; elsewhere a program starts with `params` split as a shell would split them, and anything else opens as with `open()` |
| `focused` | whether the game's window has the keyboard focus |
| `minimized` | whether the window is minimized |
| `background` | whether the system has put the game in the background (phones, and a browser's hidden tab) |
| `lowMemory` | how many times the system has warned that memory is low (Android, iOS) |
| `takeEvents()` | the events since the last call, oldest first: `focus`, `blur`, `minimize`, `restore`, `show`, `hide`, `background`, `foreground`, `lowmemory`, `terminating` |
| `onEvent` | a function called at once, on the engine's thread, with `background`, `lowmemory` or `terminating`: the system may stop the game as soon as it has told it, so a save has to be written there and then. An event given to it isn't in `takeEvents()` too. **It must catch its own exceptions**: one that came back through the plugin would unwind through its frames, which some engines' builds can't do |
| `copyImage(layer)` | puts the layer's picture on the clipboard, opaque, as PNG and BMP (Windows reads the BMP); true when SDL took it |
| `clipboardHas(mimeType)` | whether the clipboard holds data of that type |
| `frameLimit` | frames a second at most, 0 for no limit: a continuous handler waits out the rest of each frame. Not on the web, where the browser paces the frames (`frameLimitPossible` says so) |
| `lastError` | why the last call that returned false failed (also in the log) |

## Building

As part of the engine: add this repository's `plugins` folder to krkrz_dev's
`TVP_PLUGIN_FOLDERS` (after krkrz_dev's own `src/plugins`, whose tp_stub and ncbind it uses) and
`sdlsystem` to `TVP_PLUGINS`, or to a platform kit's `pluginFolders` and `plugins`. Where every
plugin is linked into the engine (the web, Android, iOS: `TVP_STATIC_PLUGIN`), SDL's functions are
linked as usual; where the plugin is a library of its own, it finds them in the SDL the engine
loaded (`SDL3.dll`, `libSDL3.so.0`, or the executable when SDL is linked into it).

On its own, with its tests:

```
cmake -B build -DKRKRZ_DEV_DIR=/path/to/krkrz_dev && cmake --build build && ctest --test-dir build
```

The tests (`tests/codec_test.cpp`) cover what needs no engine: the PNG and BMP the clipboard
gets, file URLs, URL schemes and the splitting and quoting of command lines.

## Notes

- SDL's declarations are written out in `sdlapi.h`, so the plugin builds without SDL's headers;
  SDL 3 keeps its ABI within the major version.
- A program is started as a grandchild that the plugin's child leaves at once, so nothing has to
  wait for it and it isn't in the game's session; whether it started is told back through a pipe
  closed on exec.
- The plugin never lets an exception out: a failure returns false and sets `lastError`.

## Licence

0BSD (`LICENSE`).
