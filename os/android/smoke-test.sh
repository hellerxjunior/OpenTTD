#!/usr/bin/env bash
# Installs the APK on a running emulator, starts the game twice (title
# screen, then straight into a new random game) and reports whether it
# stayed alive, with its log and a small screenshot of each run.

APK="${1:-build-android/openttd-travel.apk}"
PACKAGE=org.openttd.travel
FAILED=0

adb install -r "${APK}"

run() {
	local name="$1" args="$2"
	adb shell am force-stop "${PACKAGE}"
	adb logcat -c
	adb shell am start -n "${PACKAGE}/.OpenTTDActivity" --es args "'${args}'"
	sleep 90
	adb exec-out screencap -p > "${name}.png"

	echo "::group::logcat ${name}"
	adb logcat -d | grep -E " (OpenTTD|SDL|DEBUG|AndroidRuntime)" | grep -v "GM_TT\|orig_win.obm" | tail -150
	echo "::endgroup::"

	convert "${name}.png" -resize 640x "${name}.jpg"
	echo "SCREENSHOT_${name}_BEGIN"
	base64 -w 0 "${name}.jpg"
	echo
	echo "SCREENSHOT_${name}_END"

	if adb shell pidof "${PACKAGE}" > /dev/null; then
		echo "Game is running (${name})"
	else
		echo "Game is NOT running (${name})"
		FAILED=1
	fi
}

run title "-d misc=1"
run newgame "-g -d misc=1"

exit "${FAILED}"
