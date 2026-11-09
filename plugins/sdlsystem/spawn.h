// spawn.h - starting programs, and opening files and pages through the system
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <string>
#include <vector>

namespace sdlsys {

/// Whether this platform can start programs (Windows, macOS, Linux; not the web or phones)
bool canRun();

/// Starts a program with its arguments (UTF-8), not waiting for it and without a shell. A
/// program without a folder is looked for on PATH. false, with `error` set, when it can't start
bool runProgram(const std::string &program, const std::vector<std::string> &args, std::string &error);

/// Windows' ShellExecute: opens a file, folder or page, or starts a program with a command line
/// (`params`). Elsewhere false: the plugin goes through SDL_OpenURL or runProgram instead
bool shellOpenWindows(const std::string &target, const std::string &params, std::string &error);

/// Whether a local path is a program this process could start (POSIX: an executable regular
/// file; Windows: .exe, .com, .bat or .cmd)
bool isProgram(const std::string &path);

} // namespace sdlsys
