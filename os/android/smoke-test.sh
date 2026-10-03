#!/usr/bin/env bash
# Installs the APK on a running emulator, starts the game and reports
# whether it is still alive, with its log and a small screenshot.

APK="${1:-build-android/openttd-travel.apk}"
PACKAGE=org.openttd.travel

adb install -r "${APK}"
adb logcat -c
adb shell am start -n "${PACKAGE}/.OpenTTDActivity"

for wait in 45 45 30; do
	sleep "${wait}"
	adb exec-out screencap -p > screen.png
done

echo "::group::logcat"
adb logcat -d | grep -iE "openttd|SDL|libc|DEBUG|AndroidRuntime|FATAL" | tail -200
echo "::endgroup::"

convert screen.png -resize 640x screen.jpg
echo "SCREENSHOT_BASE64_BEGIN"
base64 -w 0 screen.jpg
echo
echo "SCREENSHOT_BASE64_END"

if adb shell pidof "${PACKAGE}" > /dev/null; then
	echo "Game is running"
else
	echo "Game is NOT running"
	exit 1
fi
