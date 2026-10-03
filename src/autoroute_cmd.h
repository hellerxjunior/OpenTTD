/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/** @file autoroute_cmd.h Command definition for building a whole road or railway between two tiles. */

#ifndef AUTOROUTE_CMD_H
#define AUTOROUTE_CMD_H

#include "command_type.h"
#include "rail_type.h"
#include "road_type.h"
#include "transport_type.h"

CommandCost CmdBuildAutoRoute(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, TransportType transport_type, RailType railtype, RoadType roadtype);

TileIndex AutoRouteSnapEnd(TileIndex tile, TileIndex towards);

DEF_CMD_TRAIT(Commands::BuildAutoRoute, CmdBuildAutoRoute, CommandFlags({CommandFlag::Auto, CommandFlag::NoWater}), CommandType::LandscapeConstruction)

#endif /* AUTOROUTE_CMD_H */
