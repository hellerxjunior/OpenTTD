/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file unix_main.cpp Main entry for Unix. */

#include "../../stdafx.h"
#include "../../openttd.h"
#include "../../crashlog.h"
#include "../../core/random_func.hpp"
#include "../../string_func.h"

#include <time.h>
#include <signal.h>

#ifdef __ANDROID__
/* SDL renames main() to SDL_main(), which its Java activity calls. */
#	include <SDL.h>
#	include <SDL_main.h>
#	include <android/log.h>
#	include <unistd.h>
#	include <thread>
#endif

#include "../../safeguards.h"

#ifdef __ANDROID__
/** Android drops stdout and stderr; send them to the system log instead. */
static void RedirectOutputToLog()
{
	int fds[2];
	if (pipe(fds) != 0) return;
	dup2(fds[1], STDOUT_FILENO);
	dup2(fds[1], STDERR_FILENO);
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IONBF, 0);

	std::thread([fd = fds[0]]() {
		std::string line;
		char buf[512];
		ssize_t n;
		while ((n = read(fd, buf, sizeof(buf))) > 0) {
			for (ssize_t i = 0; i < n; i++) {
				if (buf[i] == '\n') {
					__android_log_write(ANDROID_LOG_INFO, "OpenTTD", line.c_str());
					line.clear();
				} else {
					line += buf[i];
				}
			}
		}
	}).detach();
}

/**
 * Point OpenTTD at the app's private storage, where the Java activity has
 * unpacked the game data, and pick the language of the phone.
 */
static void SetupAndroidEnvironment()
{
	const char *files = SDL_AndroidGetInternalStoragePath();
	if (files != nullptr) {
		std::string base = files;
		setenv("HOME", base.c_str(), 1);
		setenv("XDG_DATA_HOME", (base + "/data").c_str(), 1);
		setenv("XDG_CONFIG_HOME", (base + "/config").c_str(), 1);
	}

	SDL_Locale *locales = SDL_GetPreferredLocales();
	if (locales != nullptr && locales[0].language != nullptr) {
		std::string lang = locales[0].language;
		if (locales[0].country != nullptr) lang = lang + "_" + locales[0].country;
		setenv("LANG", (lang + ".UTF-8").c_str(), 1);
	}
	SDL_free(locales);
}
#endif

int CDECL main(int argc, char *argv[])
{
#ifdef __ANDROID__
	RedirectOutputToLog();
	SetupAndroidEnvironment();
#endif

	/* Make sure our arguments contain only valid UTF-8 characters. */
	std::vector<std::string_view> params;
	for (int i = 0; i < argc; ++i) {
		StrMakeValidInPlace(argv[i]);
		params.emplace_back(argv[i]);
	}

	CrashLog::InitialiseCrashLog();

	SetRandomSeed(time(nullptr));

	signal(SIGPIPE, SIG_IGN);

	return openttd_main(params);
}
