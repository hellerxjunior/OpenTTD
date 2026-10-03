#!/usr/bin/env bash
# Builds the OpenTTD Travel APK.
#
# Needs: the Android SDK and NDK (ANDROID_HOME, ANDROID_NDK_HOME), cmake,
# ninja, a host C++ compiler, curl, unzip and gradle on the PATH.
# Result: build-android/openttd-travel.apk in the repository root.

set -euo pipefail

ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
PROJECT="${ROOT}/os/android"
OUT="${OUT:-${ROOT}/build-android}"

NDK="${ANDROID_NDK_HOME:-${ANDROID_NDK_LATEST_HOME:-}}"
ABI="${ABI:-arm64-v8a}"
API=24
JOBS="$(nproc)"

SDL_VERSION=2.32.10
OPENGFX_VERSION=8.0
OPENSFX_VERSION=1.0.3

if [ -z "${NDK}" ] || [ ! -f "${NDK}/build/cmake/android.toolchain.cmake" ]; then
	echo "Android NDK not found; set ANDROID_NDK_HOME" >&2
	exit 1
fi

ANDROID_CMAKE=(
	-G Ninja
	-DCMAKE_TOOLCHAIN_FILE="${NDK}/build/cmake/android.toolchain.cmake"
	-DANDROID_ABI="${ABI}"
	-DANDROID_PLATFORM="android-${API}"
	-DCMAKE_BUILD_TYPE=Release
)

mkdir -p "${OUT}/downloads"

download() {
	local url="$1" file="${OUT}/downloads/$(basename "$1")"
	[ -f "${file}" ] || curl -fsSL --retry 3 "${url}" -o "${file}"
	echo "${file}"
}

echo "::group::Host tools"
cmake -S "${ROOT}" -B "${OUT}/host" -G Ninja -DOPTION_TOOLS_ONLY=ON -DCMAKE_BUILD_TYPE=Release
cmake --build "${OUT}/host" --target tools -j "${JOBS}"
echo "::endgroup::"

echo "::group::SDL2 ${SDL_VERSION}"
SDL_SRC="${OUT}/SDL2-${SDL_VERSION}"
if [ ! -d "${SDL_SRC}" ]; then
	tar -xzf "$(download "https://github.com/libsdl-org/SDL/releases/download/release-${SDL_VERSION}/SDL2-${SDL_VERSION}.tar.gz")" -C "${OUT}"
fi
cmake -S "${SDL_SRC}" -B "${OUT}/sdl-${ABI}" "${ANDROID_CMAKE[@]}" \
	-DCMAKE_INSTALL_PREFIX="${OUT}/sdl-install-${ABI}" \
	-DSDL_SHARED=ON -DSDL_STATIC=OFF -DSDL_TEST=OFF
cmake --build "${OUT}/sdl-${ABI}" -j "${JOBS}"
cmake --install "${OUT}/sdl-${ABI}"
echo "::endgroup::"

echo "::group::OpenTTD"
SDL_PREFIX="${OUT}/sdl-install-${ABI}"
cmake -S "${ROOT}" -B "${OUT}/game-${ABI}" "${ANDROID_CMAKE[@]}" \
	-DHOST_BINARY_DIR="${OUT}/host" \
	-DSDL2_DIR="${SDL_PREFIX}/lib/cmake/SDL2" \
	-DCMAKE_FIND_ROOT_PATH="${SDL_PREFIX}" \
	-DCMAKE_INSTALL_PREFIX="${OUT}/stage-${ABI}"
cmake --build "${OUT}/game-${ABI}" --target openttd -j "${JOBS}"
rm -rf "${OUT}/stage-${ABI}"
for component in Runtime language_files docs; do
	cmake --install "${OUT}/game-${ABI}" --component "${component}"
done
echo "::endgroup::"

echo "::group::APK contents"
LIBS="${PROJECT}/app/libs/${ABI}"
ASSETS="${PROJECT}/app/build/game-assets/openttd"
JAVA="${PROJECT}/app/build/sdl-java"
rm -rf "${PROJECT}/app/libs" "${ASSETS}" "${JAVA}"
mkdir -p "${LIBS}" "${ASSETS}/baseset" "${JAVA}"

cp "${SDL_PREFIX}/lib/libSDL2.so" "${LIBS}/"
cp "${OUT}/stage-${ABI}/libmain.so" "${LIBS}/"

# Game data: everything installed next to the library.
cp -r "${OUT}/stage-${ABI}/." "${ASSETS}/"
find "${ASSETS}" -name '*.so' -delete

# Free graphics and sounds.
unzip -o -q "$(download "https://cdn.openttd.org/opengfx-releases/${OPENGFX_VERSION}/opengfx-${OPENGFX_VERSION}-all.zip")" -d "${ASSETS}/baseset"
unzip -o -q "$(download "https://cdn.openttd.org/opensfx-releases/${OPENSFX_VERSION}/opensfx-${OPENSFX_VERSION}-all.zip")" -d "${ASSETS}/baseset"

# SDL's Java side has to match the native library.
cp -r "${SDL_SRC}/android-project/app/src/main/java/org" "${JAVA}/"
echo "::endgroup::"

echo "::group::Gradle"
(cd "${PROJECT}" && gradle --no-daemon assembleRelease)
cp "${PROJECT}/app/build/outputs/apk/release/app-release.apk" "${OUT}/openttd-travel.apk"
echo "::endgroup::"

echo "APK: ${OUT}/openttd-travel.apk"
