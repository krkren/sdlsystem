// spawn.cpp - see spawn.h
//
// SPDX-License-Identifier: 0BSD
#include "spawn.h"
#include "codec.h"

#if defined(_WIN32)
#include <windows.h>
#include <shellapi.h>
#elif defined(__EMSCRIPTEN__) || defined(__ANDROID__)
#define SDLSYS_NO_PROGRAMS 1
#elif defined(__APPLE__)
#include <TargetConditionals.h>
#if TARGET_OS_IPHONE
#define SDLSYS_NO_PROGRAMS 1
#endif
#endif

#if !defined(_WIN32) && !defined(SDLSYS_NO_PROGRAMS)
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace sdlsys {

#if defined(_WIN32)

namespace {

std::wstring wide(const std::string &s) {
	if (s.empty()) return std::wstring();
	int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
	std::wstring w(size_t(n), L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), &w[0], n);
	return w;
}

std::string lastError(const char *what) {
	DWORD e = GetLastError();
	return std::string(what) + " failed (Windows error " + std::to_string(unsigned(e)) + ")";
}

} // namespace

bool canRun() { return true; }

bool runProgram(const std::string &program, const std::vector<std::string> &args, std::string &error) {
	std::wstring line = quoteWindowsArg(wide(program));
	for (const std::string &a : args) line += L" " + quoteWindowsArg(wide(a));
	STARTUPINFOW si;
	ZeroMemory(&si, sizeof si);
	si.cb = sizeof si;
	PROCESS_INFORMATION pi;
	ZeroMemory(&pi, sizeof pi);
	std::vector<wchar_t> buf(line.begin(), line.end());
	buf.push_back(0);
	// a program without a folder is looked for as Windows looks (the executable's folder,
	// System32, then PATH): CreateProcess takes the name from the command line for that
	if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE, CREATE_NEW_PROCESS_GROUP, nullptr, nullptr, &si,
	                    &pi)) {
		error = lastError("CreateProcess");
		return false;
	}
	CloseHandle(pi.hThread);
	CloseHandle(pi.hProcess);
	return true;
}

bool shellOpenWindows(const std::string &target, const std::string &params, std::string &error) {
	std::wstring t = wide(target), p = wide(params);
	SHELLEXECUTEINFOW sei;
	ZeroMemory(&sei, sizeof sei);
	sei.cbSize = sizeof sei;
	sei.fMask = SEE_MASK_NOASYNC | SEE_MASK_FLAG_NO_UI;
	sei.lpVerb = L"open";
	sei.lpFile = t.c_str();
	sei.lpParameters = p.empty() ? nullptr : p.c_str();
	sei.nShow = SW_SHOWNORMAL;
	if (!ShellExecuteExW(&sei)) {
		error = lastError("ShellExecuteEx");
		return false;
	}
	return true;
}

bool isProgram(const std::string &path) {
	std::wstring w = wide(path);
	DWORD a = GetFileAttributesW(w.c_str());
	if (a == INVALID_FILE_ATTRIBUTES || (a & FILE_ATTRIBUTE_DIRECTORY)) return false;
	size_t dot = path.find_last_of('.');
	if (dot == std::string::npos) return false;
	std::string ext = path.substr(dot);
	for (char &c : ext) c = char(tolower((unsigned char)c));
	return ext == ".exe" || ext == ".com" || ext == ".bat" || ext == ".cmd";
}

#elif defined(SDLSYS_NO_PROGRAMS)

bool canRun() { return false; }

bool runProgram(const std::string &, const std::vector<std::string> &, std::string &error) {
	error = "programs can't be started on this platform";
	return false;
}

bool shellOpenWindows(const std::string &, const std::string &, std::string &error) {
	error = "not Windows";
	return false;
}

bool isProgram(const std::string &) { return false; }

#else // Linux, macOS

namespace {

bool executableFile(const std::string &path) {
	struct stat st;
	return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode) && access(path.c_str(), X_OK) == 0;
}

// The program's path: as given when it has a folder, else the first match on PATH
std::string resolve(const std::string &program) {
	if (program.find('/') != std::string::npos) return program;
	const char *path = getenv("PATH");
	std::string all = path ? path : "/usr/local/bin:/usr/bin:/bin";
	size_t start = 0;
	while (start <= all.size()) {
		size_t end = all.find(':', start);
		if (end == std::string::npos) end = all.size();
		std::string dir = all.substr(start, end - start);
		if (dir.empty()) dir = ".";
		std::string candidate = dir + "/" + program;
		if (executableFile(candidate)) return candidate;
		start = end + 1;
	}
	return std::string();
}

} // namespace

bool canRun() { return true; }

bool runProgram(const std::string &program, const std::vector<std::string> &args, std::string &error) {
	std::string path = resolve(program);
	if (path.empty() || !executableFile(path)) {
		error = "no program " + program;
		return false;
	}
	// everything the child needs is made before fork: between fork and exec only
	// async-signal-safe calls may run in a process with threads
	std::vector<std::string> owned;
	owned.push_back(path);
	owned.insert(owned.end(), args.begin(), args.end());
	std::vector<char *> argv;
	for (std::string &s : owned) argv.push_back(&s[0]);
	argv.push_back(nullptr);

	// exec's failure comes back through a pipe closed on exec
	int fds[2];
	if (pipe(fds) != 0) {
		error = std::string("pipe: ") + strerror(errno);
		return false;
	}
	fcntl(fds[1], F_SETFD, FD_CLOEXEC);

	// a child that starts the program as a grandchild and leaves at once, so the program
	// is nobody's to wait for (no zombie) and isn't in the game's session
	pid_t child = fork();
	if (child < 0) {
		close(fds[0]);
		close(fds[1]);
		error = std::string("fork: ") + strerror(errno);
		return false;
	}
	if (child == 0) {
		close(fds[0]);
		setsid();
		pid_t grandchild = fork();
		if (grandchild == 0) {
			int devnull = open("/dev/null", O_RDWR);
			if (devnull >= 0) {
				dup2(devnull, 0);
				if (devnull > 2) close(devnull);
			}
			execv(argv[0], argv.data());
			int e = errno;
			ssize_t w = write(fds[1], &e, sizeof e);
			(void)w;
			_exit(127);
		}
		_exit(grandchild < 0 ? 1 : 0);
	}
	close(fds[1]);
	int status = 0;
	while (waitpid(child, &status, 0) < 0 && errno == EINTR) {
	}
	int e = 0;
	ssize_t r;
	do {
		r = read(fds[0], &e, sizeof e);
	} while (r < 0 && errno == EINTR);
	close(fds[0]);
	if (!WIFEXITED(status) || WEXITSTATUS(status) != 0) {
		error = "fork failed";
		return false;
	}
	if (r == ssize_t(sizeof e)) {
		error = std::string("exec ") + path + ": " + strerror(e);
		return false;
	}
	return true;
}

bool shellOpenWindows(const std::string &, const std::string &, std::string &error) {
	error = "not Windows";
	return false;
}

bool isProgram(const std::string &path) {
	if (!executableFile(path)) return false;
#if defined(__APPLE__)
	return true;
#else
	// a file marked executable that isn't a program (a picture copied from a FAT disk) is
	// opened rather than run: an ELF file, or a script with #!
	int fd = open(path.c_str(), O_RDONLY);
	if (fd < 0) return false;
	char head[4] = { 0, 0, 0, 0 };
	ssize_t n = read(fd, head, 4);
	close(fd);
	if (n >= 4 && head[0] == 0x7f && head[1] == 'E' && head[2] == 'L' && head[3] == 'F') return true;
	return n >= 2 && head[0] == '#' && head[1] == '!';
#endif
}

#endif

} // namespace sdlsys
