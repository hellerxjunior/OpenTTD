/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file autoroute_gui.cpp The "connect two places" tool of the road and rail toolbars. */

#include "stdafx.h"
#include "autoroute_gui.h"
#include "autoroute_cmd.h"
#include "command_func.h"
#include "company_func.h"
#include "error.h"
#include "sound_func.h"
#include "strings_func.h"
#include "textbuf_gui.h"
#include "viewport_func.h"
#include "window_func.h"

#include "table/strings.h"

#include "safeguards.h"

/** The route that is being placed. */
struct AutoRoutePlacement {
	TileIndex start = INVALID_TILE; ///< First clicked tile.
	TileIndex end = INVALID_TILE; ///< Route end, after the second click.
	TileIndex route_start = INVALID_TILE; ///< Route start, after the second click.
	TransportType type = TransportType::Rail;
	RailType railtype = INVALID_RAILTYPE;
	RoadType roadtype = INVALID_ROADTYPE;
};

static AutoRoutePlacement _autoroute;

/** Forget the clicked start. */
void ResetAutoRoute()
{
	if (_autoroute.start != INVALID_TILE) SetRedErrorSquare(INVALID_TILE);
	_autoroute.start = INVALID_TILE;
}

/** The player answered the "build this route?" question. */
static void AutoRouteConfirmed(Window *, bool confirmed)
{
	if (!confirmed) return;
	const AutoRoutePlacement &r = _autoroute;
	StringID error = (r.type == TransportType::Rail) ? STR_ERROR_CAN_T_BUILD_AUTOROUTE_RAIL : STR_ERROR_CAN_T_BUILD_AUTOROUTE_ROAD;
	if (Command<Commands::BuildAutoRoute>::Post(error, r.end, r.route_start, r.type, r.railtype, r.roadtype)) {
		if (_settings_client.sound.confirm) SndPlayTileFx(SND_1F_CONSTRUCTION_OTHER, r.end);
	}
}

/**
 * Handle a click with the "connect two places" tool.
 * The first click marks the start; the second click finds the route and
 * asks whether to build it for the shown cost.
 * @param w Toolbar window.
 * @param tile Clicked tile.
 * @param type Road or rail.
 * @param railtype Rail type to build.
 * @param roadtype Road type to build.
 */
void PlaceProc_AutoRoute(Window *w, TileIndex tile, TransportType type, RailType railtype, RoadType roadtype)
{
	if (_autoroute.start == INVALID_TILE || _autoroute.type != type) {
		_autoroute.start = tile;
		_autoroute.type = type;
		SetRedErrorSquare(tile);
		return;
	}

	TileIndex first = _autoroute.start;
	ResetAutoRoute();
	if (first == tile) return;

	_autoroute.route_start = AutoRouteSnapEnd(first, tile);
	_autoroute.end = AutoRouteSnapEnd(tile, _autoroute.route_start);
	_autoroute.railtype = railtype;
	_autoroute.roadtype = roadtype;

	StringID error = (type == TransportType::Rail) ? STR_ERROR_CAN_T_BUILD_AUTOROUTE_RAIL : STR_ERROR_CAN_T_BUILD_AUTOROUTE_ROAD;
	CommandCost cost = Command<Commands::BuildAutoRoute>::Do(DoCommandFlag::QueryCost, _autoroute.end, _autoroute.route_start, type, railtype, roadtype);
	if (cost.Failed()) {
		ShowErrorMessage(GetEncodedString(error), TileX(tile) * TILE_SIZE, TileY(tile) * TILE_SIZE, cost);
		return;
	}

	ShowQuery(
		GetEncodedString(type == TransportType::Rail ? STR_AUTOROUTE_QUERY_CAPTION_RAIL : STR_AUTOROUTE_QUERY_CAPTION_ROAD),
		GetEncodedString(STR_AUTOROUTE_QUERY_TEXT, cost.GetCost()),
		w,
		AutoRouteConfirmed
	);
}
