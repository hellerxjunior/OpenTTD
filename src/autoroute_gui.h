/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file autoroute_gui.h The "connect two places" tool of the road and rail toolbars. */

#ifndef AUTOROUTE_GUI_H
#define AUTOROUTE_GUI_H

#include "rail_type.h"
#include "road_type.h"
#include "tile_type.h"
#include "transport_type.h"
#include "window_type.h"

void PlaceProc_AutoRoute(Window *w, TileIndex tile, TransportType type, RailType railtype, RoadType roadtype);
void ResetAutoRoute();

#endif /* AUTOROUTE_GUI_H */
