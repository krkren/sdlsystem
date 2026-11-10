// watchdog.cpp - a thread of its own that notices the game's thread standing still (watchdog.h)
//
// A game that freezes on a player's phone and nowhere else leaves nothing to go on. When the
// game's thread hasn't said it's alive for the time given, this writes a report: how long, the
// last place the game noted, and, where /proc is there (Linux, Android), every thread of the
// process with its state, its processor time over half a second and the kernel function it waits
// in, which tells a thread busy in a loop from one blocked on a lock. It goes to the system's log
// (logcat on Android, stderr elsewhere) and to a file, once per standstill; "resumed" follows
// if the game's thread comes back. Nothing here calls into the engine: its functions aren't safe
// from another thread.
//
// SPDX-License-Identifier: 0BSD
#include "watchdog.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <mutex>
#include <thread>
#include <vector>

#if defined(__linux__) || defined(__ANDROID__)
#include <dirent.h>
#include <sys/syscall.h>
#include <unistd.h>
#define SDLSYS_PROC 1
#endif
#if defined(__ANDROID__)
#include <android/log.h>
#endif

namespace sdlsys {

namespace {

std::mutex lock;          // note, path, report, the thread's start and stop
std::string noteText, reportPath, lastReport;
std::atomic<long long> lastAlive(0);
std::atomic<int> limitMs(0);
std::atomic<bool> running(false);
std::atomic<long> gameTid(0);
bool (*isPaused)() = nullptr;
std::thread worker;

long long nowMs() {
	using namespace std::chrono;
	return duration_cast<milliseconds>(steady_clock::now().time_since_epoch()).count();
}

void logLine(const std::string &text) {
#if defined(__ANDROID__)
	__android_log_write(ANDROID_LOG_ERROR, "kagex-watchdog", text.c_str());
#else
	std::fprintf(stderr, "kagex-watchdog: %s\n", text.c_str());
	std::fflush(stderr);
#endif
}

std::string readFile(const std::string &path) {
	std::string out;
	if (FILE *f = std::fopen(path.c_str(), "rb")) {
		char buf[512];
		size_t n;
		while ((n = std::fread(buf, 1, sizeof buf, f)) > 0 && out.size() < 4096) out.append(buf, n);
		std::fclose(f);
	}
	while (!out.empty() && (out.back() == '\n' || out.back() == '\r' || out.back() == ' ')) out.pop_back();
	return out;
}

#if SDLSYS_PROC
struct ThreadInfo {
	std::string name, state, wchan;
	long long cpu = 0; // utime + stime, in clock ticks
};

// every thread of the process, by its id
std::map<long, ThreadInfo> threads() {
	std::map<long, ThreadInfo> out;
	DIR *d = opendir("/proc/self/task");
	if (!d) return out;
	while (struct dirent *e = readdir(d)) {
		if (e->d_name[0] < '0' || e->d_name[0] > '9') continue;
		long tid = std::atol(e->d_name);
		std::string base = std::string("/proc/self/task/") + e->d_name + "/";
		ThreadInfo t;
		t.name = readFile(base + "comm");
		t.wchan = readFile(base + "wchan");
		std::string stat = readFile(base + "stat");
		// after the name in parentheses: state, then fields; utime and stime are the 14th and 15th
		size_t close = stat.rfind(')');
		if (close != std::string::npos) {
			std::vector<std::string> f;
			size_t p = close + 2;
			while (p < stat.size()) {
				size_t q = stat.find(' ', p);
				if (q == std::string::npos) q = stat.size();
				f.push_back(stat.substr(p, q - p));
				p = q + 1;
			}
			if (!f.empty()) t.state = f[0];
			if (f.size() > 12) t.cpu = std::atoll(f[11].c_str()) + std::atoll(f[12].c_str());
		}
		out[tid] = t;
	}
	closedir(d);
	return out;
}
#endif

std::string makeReport(long long stalledMs) {
	char head[256];
	std::time_t t = std::time(nullptr);
	char when[64];
	std::strftime(when, sizeof when, "%Y-%m-%d %H:%M:%S", std::localtime(&t));
	std::snprintf(head, sizeof head, "The game's thread hasn't run for %.1f s (the watch waits %.1f s); %s\n",
	              stalledMs / 1000.0, limitMs / 1000.0, when);
	std::string out = head;
	{
		std::lock_guard<std::mutex> hold(lock);
		out += "Last noted: " + (noteText.empty() ? std::string("(nothing)") : noteText) + "\n";
	}
#if SDLSYS_PROC
	std::map<long, ThreadInfo> a = threads();
	std::this_thread::sleep_for(std::chrono::milliseconds(500));
	std::map<long, ThreadInfo> b = threads();
	long game = gameTid;
	out += "Threads (* the game's; CPU: processor ticks over half a second, so 0 is waiting and about "
	       "50 is a thread running flat out; state R running, S sleeping, D in the kernel):\n";
	for (const auto &kv : b) {
		const ThreadInfo &t = kv.second;
		auto before = a.find(kv.first);
		long long cpu = before != a.end() ? t.cpu - before->second.cpu : -1;
		char line[256];
		std::snprintf(line, sizeof line, "%c %ld %-16s %s cpu %lld wchan %s\n", kv.first == game ? '*' : ' ',
		              kv.first, t.name.c_str(), t.state.c_str(), cpu, t.wchan.empty() ? "-" : t.wchan.c_str());
		out += line;
	}
#else
	out += "(no thread list on this system)\n";
#endif
	return out;
}

void writeFile(const std::string &path, const std::string &text) {
	if (path.empty()) return;
	if (FILE *f = std::fopen(path.c_str(), "wb")) {
		std::fwrite(text.data(), 1, text.size(), f);
		std::fclose(f);
	}
}

void loop() {
	bool reported = false;
	long long reportedAt = 0;
	while (running) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));
		int limit = limitMs;
		if (limit <= 0) continue;
		long long now = nowMs();
		if (isPaused && isPaused()) {
			lastAlive = now; // standing still is expected: the time starts again from here
			continue;
		}
		long long stalled = now - lastAlive;
		if (!reported && stalled > limit) {
			std::string report = makeReport(stalled);
			std::string path;
			{
				std::lock_guard<std::mutex> hold(lock);
				lastReport = report;
				path = reportPath;
			}
			size_t p = 0;
			while (p < report.size()) {
				size_t q = report.find('\n', p);
				if (q == std::string::npos) q = report.size();
				logLine(report.substr(p, q - p));
				p = q + 1;
			}
			writeFile(path, report);
			reported = true;
			reportedAt = now;
		} else if (reported && stalled < limit) {
			char line[128];
			std::snprintf(line, sizeof line, "The game's thread is running again, %.1f s after the report",
			              (now - reportedAt) / 1000.0);
			logLine(line);
			reported = false;
		}
	}
}

} // namespace

bool watchdogStart(int seconds, const std::string &path, bool (*paused)()) {
	std::lock_guard<std::mutex> hold(lock);
	reportPath = path;
	isPaused = paused;
	lastAlive = nowMs();
	limitMs = seconds > 0 ? seconds * 1000 : 0;
	if (seconds > 0 && !running) {
		running = true;
		try {
			worker = std::thread(loop);
		} catch (...) {
			running = false;
			return false;
		}
	}
	return true;
}

void watchdogStop() {
	std::thread t;
	{
		std::lock_guard<std::mutex> hold(lock);
		limitMs = 0;
		if (!running) return;
		running = false;
		t = std::move(worker);
	}
	if (t.joinable()) t.join();
}

void watchdogAlive(const std::string &note) {
	lastAlive = nowMs();
#if SDLSYS_PROC
	gameTid = (long)syscall(SYS_gettid);
#endif
	if (!note.empty()) {
		std::lock_guard<std::mutex> hold(lock);
		noteText = note;
	}
}

std::string watchdogLastReport() {
	std::lock_guard<std::mutex> hold(lock);
	return lastReport;
}

} // namespace sdlsys
