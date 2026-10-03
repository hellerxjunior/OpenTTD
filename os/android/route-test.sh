#!/usr/bin/env bash
# Tries the "connect two places" route finder on a generated map.
#
# Usage: route-test.sh <build dir>
# Needs a built openttd with SDL in the build dir, curl, unzip and
# ImageMagick. Prints the console output and a minimap of the result as a
# base64 JPEG between SCREENSHOT_routes_BEGIN/END.

set -euo pipefail

BUILD="$(cd "$1" && pwd)"
WORK="$(mktemp -d)"
export HOME="${WORK}" XDG_DATA_HOME="${WORK}/data" XDG_CONFIG_HOME="${WORK}/config"
DATA="${XDG_DATA_HOME}/openttd"
mkdir -p "${DATA}/baseset" "${DATA}/scripts" "${XDG_CONFIG_HOME}/openttd"

curl -fsSL --retry 3 https://cdn.openttd.org/opengfx-releases/8.0/opengfx-8.0-all.zip -o "${WORK}/opengfx.zip"
unzip -q "${WORK}/opengfx.zip" -d "${DATA}/baseset"

cat > "${XDG_CONFIG_HOME}/openttd/openttd.cfg" <<'CFG'
[game_creation]
map_x = 8
map_y = 8
[difficulty]
max_loan = 2000000
construction_cost = 0
[network]
participate_survey = no
CFG

OUT="${WORK}/routes.txt"
{
	echo "script ${OUT}"
	for pair in "road t0 t1" "road t2 t3" "road t1 t4" "rail i0 i1" "rail i2 i3" "rail i4 i5"; do
		echo "autoroute ${pair} build"
		# A second search must find the route that is already there.
		echo "autoroute ${pair}"
	done
	echo "screenshot minimap"
	echo "script"
} > "${DATA}/scripts/game_start.scr"

# The null video driver plays like a dedicated server, without a company, so
# run the real SDL driver without a screen and stop it after a while.
(cd "${BUILD}" && SDL_VIDEODRIVER=dummy timeout 90 ./openttd -v sdl -s null -m null -g -G 20261003) 2>&1 | tail -n 20 || true

echo "::group::Console output"
cat "${OUT}"
echo "::endgroup::"

shot="$(find "${DATA}" -name '*.png' | head -n 1)"
if [ -n "${shot}" ]; then
	convert "${shot}" -scale 300% -quality 80 "${WORK}/routes.jpg"
	echo "SCREENSHOT_routes_BEGIN"
	base64 -w 0 "${WORK}/routes.jpg"
	echo
	echo "SCREENSHOT_routes_END"
fi

# Every built route has to be found again as "already built".
built=$(grep -c "Route built." "${OUT}" || true)
again=$(grep -c "already built" "${OUT}" || true)
echo "Routes built: ${built}, found again: ${again}"
[ "${built}" -gt 0 ] && [ "${built}" -eq "${again}" ]
