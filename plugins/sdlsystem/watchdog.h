// watchdog.h - notices the game's thread standing still, and says what every thread was doing
//
// SPDX-License-Identifier: 0BSD
#pragma once

#include <string>

namespace sdlsys {

// Starts (or changes) the watch: seconds without alive() make a report, written to the system's
// log and, where path isn't empty, to that file; 0 stops it. paused() says when standing still is
// expected (the game in the background, minimized): the watch doesn't count then.
bool watchdogStart(int seconds, const std::string &path, bool (*paused)());
void watchdogStop();
// The game's thread is running; note says where it is (shown in a report)
void watchdogAlive(const std::string &note);
// The last report, "" without one
std::string watchdogLastReport();

} // namespace sdlsys
