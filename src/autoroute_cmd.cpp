/*
 * This file is part of OpenTTD.
 * OpenTTD is free software; you can redistribute it and/or modify it under the terms of the GNU General Public License as published by the Free Software Foundation, version 2.
 * OpenTTD is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details. You should have received a copy of the GNU General Public License along with OpenTTD. If not, see <https://www.gnu.org/licenses/old-licenses/gpl-2.0>.
 */

/**
 * @file autoroute_cmd.cpp Building a whole road or railway between two tiles.
 *
 * The route is found with an A* search over (tile, direction) states. Every
 * piece the search wants to use is checked with the normal build command in
 * test mode, so the route follows exactly the same rules as building by hand:
 * houses and industries are never demolished, existing roads and rails are
 * reused for free and obstacles in a straight line are crossed with a bridge.
 * The search is deterministic, so the test and execute runs of the command
 * find the same route.
 */

#include "stdafx.h"
#include "autoroute_cmd.h"
#include "command_func.h"
#include "debug.h"
#include "strings_func.h"
#include "map_func.h"
#include "bridge.h"
#include "industry.h"
#include "rail.h"
#include "rail_cmd.h"
#include "road.h"
#include "road_cmd.h"
#include "road_func.h"
#include "road_map.h"
#include "station_map.h"
#include "tilearea_type.h"
#include "tunnelbridge_cmd.h"
#include "tunnelbridge_map.h"
#include "settings_type.h"

#include <queue>

#include "table/strings.h"

#include "safeguards.h"

namespace {

constexpr int STEP_COST = 100; ///< Cost of moving one tile over existing infrastructure.
constexpr int HEURISTIC_COST = 150; ///< Estimated cost per tile to the goal; above STEP_COST to keep the search small.
constexpr int TURN_COST = 60; ///< Penalty for each 45 degree turn of a railway, or each corner of a road.
constexpr int SLOPE_COST = 40; ///< Penalty for a railway going up or down a level.
constexpr size_t MAX_NODES = 300000; ///< Give up after visiting this many states.
constexpr uint MAX_AUTO_BRIDGE_LENGTH = 16; ///< Longest bridge the search tries (tiles between the heads).

constexpr uint8_t NO_DIR = 0x1F; ///< Direction of a road start state.
constexpr uint64_t NO_PARENT = UINT64_MAX;

/** One build step of the route. */
struct Piece {
	enum class Kind : uint8_t { None, Rail, Road, Bridge };

	Kind kind = Kind::None;
	uint8_t arg = 0; ///< Track for rails, road bits for roads, bridge type for bridges.
	TileIndex tile = INVALID_TILE;
	TileIndex end = INVALID_TILE; ///< Other head of a bridge.
	Money cost = 0; ///< Build cost found in test mode.
};

/** A state of the search. */
struct Node {
	int g = 0; ///< Cost from the start.
	bool closed = false;
	uint64_t parent = NO_PARENT;
	std::array<Piece, 2> pieces{}; ///< What to build when moving from the parent to this state.
};

/** Result of trying to build one piece in test mode. */
struct TestResult {
	bool ok;
	Money cost;
};

/** Travel direction of a railway piece, used to count turns. */
Direction TrackdirDirection(Trackdir td)
{
	static const Direction dirs[] = {
		Direction::NE, Direction::SE, Direction::E, Direction::E, Direction::S, Direction::S, Direction::Invalid, Direction::Invalid,
		Direction::SW, Direction::NW, Direction::W, Direction::W, Direction::N, Direction::N, Direction::Invalid, Direction::Invalid,
	};
	return dirs[to_underlying(td)];
}

/** Number of 45 degree steps between two directions. */
int TurnSteps(Direction a, Direction b)
{
	int diff = (to_underlying(a) - to_underlying(b) + 8) % 8;
	return std::min(diff, 8 - diff);
}

/** The route finder for one command run. */
class AutoRouteFinder {
public:
	AutoRouteFinder(TileIndex start, TileIndex end, TransportType type, RailType railtype, RoadType roadtype) :
			start(start), end(end), type(type), railtype(railtype), roadtype(roadtype)
	{
		Money unit = (type == TransportType::Rail) ? RailBuildCost(railtype) : RoadBuildCost(roadtype) * 2;
		this->unit_cost = std::max<Money>(unit, 1);
	}

	/**
	 * Search the route.
	 * @return The pieces to build in order, or std::nullopt when there is no route.
	 */
	std::optional<std::vector<Piece>> Find()
	{
		if (this->type == TransportType::Rail) {
			for (Trackdir td : ALL_TRACKDIRS) {
				Piece p = this->RailPiece(this->start, TrackdirToTrack(td));
				if (p.kind == Piece::Kind::None) continue;
				this->Push(Key(this->start, to_underlying(td), false), NO_PARENT, this->PieceCost(p), {p, Piece{}});
			}
		} else {
			this->Push(Key(this->start, NO_DIR, false), NO_PARENT, 0, {});
		}

		while (!this->open.empty()) {
			auto [f, key] = this->open.top();
			this->open.pop();
			Node &node = this->nodes[key];
			if (node.closed) continue;
			node.closed = true;

			if (KeyTile(key) == this->end && (this->type == TransportType::Rail || KeyDir(key) == GOAL_DIR)) return this->Path(key);
			if (this->nodes.size() > MAX_NODES) break;

			if (this->type == TransportType::Rail) {
				this->ExpandRail(key);
			} else {
				this->ExpandRoad(key);
			}
		}
		return std::nullopt;
	}

private:
	static constexpr uint8_t GOAL_DIR = 0x1E; ///< Direction of the final road state.
	static constexpr std::array<Trackdir, 12> ALL_TRACKDIRS = {
		Trackdir::X_NE, Trackdir::Y_SE, Trackdir::Upper_E, Trackdir::Lower_E, Trackdir::Left_S, Trackdir::Right_S,
		Trackdir::X_SW, Trackdir::Y_NW, Trackdir::Upper_W, Trackdir::Lower_W, Trackdir::Left_N, Trackdir::Right_N,
	};

	TileIndex start;
	TileIndex end;
	TransportType type;
	RailType railtype;
	RoadType roadtype;
	Money unit_cost;

	std::unordered_map<uint64_t, Node> nodes;
	std::unordered_map<uint64_t, TestResult> tested;
	std::priority_queue<std::pair<int, uint64_t>, std::vector<std::pair<int, uint64_t>>, std::greater<>> open;

	static uint64_t Key(TileIndex tile, uint8_t dir, bool bridge) { return (static_cast<uint64_t>(tile.base()) << 6) | (dir << 1) | (bridge ? 1 : 0); }
	static TileIndex KeyTile(uint64_t key) { return TileIndex{static_cast<uint32_t>(key >> 6)}; }
	static uint8_t KeyDir(uint64_t key) { return (key >> 1) & 0x1F; }
	static bool KeyBridge(uint64_t key) { return (key & 1) != 0; }

	/** Can the route use this tile at all? */
	static bool IsUsableTile(TileIndex tile)
	{
		return tile != INVALID_TILE && IsValidTile(tile) && DistanceFromEdge(tile) > 0;
	}

	/** Search cost of building a piece. */
	int PieceCost(const Piece &p) const
	{
		if (p.kind == Piece::Kind::None) return 0;
		Money extra = p.cost * STEP_COST / this->unit_cost;
		return static_cast<int>(std::min<Money>(extra, 1000000));
	}

	void Push(uint64_t key, uint64_t parent, int g, std::array<Piece, 2> pieces)
	{
		auto [it, inserted] = this->nodes.try_emplace(key);
		Node &node = it->second;
		if (!inserted && (node.closed || node.g <= g)) return;
		node.g = g;
		node.parent = parent;
		node.pieces = pieces;
		int h = static_cast<int>(DistanceManhattan(KeyTile(key), this->end)) * HEURISTIC_COST;
		this->open.emplace(g + h, key);
	}

	/** Test one cached build; key_extra separates the different pieces on the same tile. */
	template <typename F>
	TestResult Test(TileIndex tile, uint32_t key_extra, F &&build)
	{
		uint64_t key = (static_cast<uint64_t>(tile.base()) << 32) | key_extra;
		auto it = this->tested.find(key);
		if (it != this->tested.end()) return it->second;
		TestResult r = build();
		this->tested.emplace(key, r);
		return r;
	}

	/** A railway piece on a tile, or an empty piece when it cannot be built. */
	Piece RailPiece(TileIndex tile, Track track)
	{
		TestResult r = this->Test(tile, 0x100 | to_underlying(track), [&]() -> TestResult {
			CommandCost ret = Command<Commands::BuildRail>::Do({DoCommandFlag::Auto, DoCommandFlag::NoWater}, tile, this->railtype, track, false);
			if (ret.Succeeded()) return {true, ret.GetCost()};
			if (ret.GetErrorMessage() == STR_ERROR_ALREADY_BUILT) return {true, 0};
			/* Trains may run straight through existing stations. */
			if (HasStationTileRail(tile) && GetRailStationTrack(tile) == track && GetRailType(tile) == this->railtype) return {true, 0};
			return {false, 0};
		});
		if (!r.ok) return {};
		return {Piece::Kind::Rail, static_cast<uint8_t>(to_underlying(track)), tile, INVALID_TILE, r.cost};
	}

	/** A road piece on a tile, or an empty piece when it cannot be built. */
	Piece RoadPiece(TileIndex tile, RoadBits bits)
	{
		TestResult r = this->Test(tile, 0x200 | bits.base(), [&]() -> TestResult {
			CommandCost ret = Command<Commands::BuildRoad>::Do({DoCommandFlag::Auto, DoCommandFlag::NoWater}, tile, bits, this->roadtype, {}, TownID::Invalid());
			if (ret.Succeeded()) return {true, ret.GetCost()};
			if (ret.GetErrorMessage() == STR_ERROR_ALREADY_BUILT) return {true, 0};
			/* Road vehicles may drive through existing drive through stops and the like. */
			if (!IsTileType(tile, TileType::TunnelBridge) && GetAnyRoadBits(tile, GetRoadTramType(this->roadtype)).All(bits)) return {true, 0};
			return {false, 0};
		});
		if (!r.ok) return {};
		return {Piece::Kind::Road, bits.base(), tile, INVALID_TILE, r.cost};
	}

	/**
	 * A bridge over an obstacle, starting at \a head and going in direction \a dir.
	 * @return The bridge, or an empty piece when there is none.
	 */
	Piece BridgePiece(TileIndex head, DiagDirection dir)
	{
		if (!IsUsableTile(head)) return {};
		TileIndexDiffC step = TileIndexDiffCByDiagDir(dir);
		uint max_length = std::min<uint>(_settings_game.construction.max_bridge_length, MAX_AUTO_BRIDGE_LENGTH);

		TileIndex other = head;
		for (uint length = 0; length <= max_length; length++) {
			other = AddTileIndexDiffCWrap(other, step);
			if (!IsUsableTile(other)) break;
			if (length == 0) continue; // Bridges cross at least one tile.

			std::optional<BridgeType> bridge = this->ChooseBridge(length);
			if (!bridge.has_value()) break;

			TestResult r = this->Test(other, 0x400 | (to_underlying(dir) << 6) | length, [&]() -> TestResult {
				CommandCost ret = Command<Commands::BuildBridge>::Do({DoCommandFlag::Auto, DoCommandFlag::NoWater}, other, head, this->type, *bridge,
						this->type == TransportType::Rail ? this->railtype : INVALID_RAILTYPE,
						this->type == TransportType::Road ? this->roadtype : INVALID_ROADTYPE);
				return {ret.Succeeded(), ret.Succeeded() ? ret.GetCost() : Money(0)};
			});
			if (r.ok) return {Piece::Kind::Bridge, static_cast<uint8_t>(*bridge), head, other, r.cost};
		}
		return {};
	}

	/**
	 * An existing bridge or tunnel of our kind starting at \a head in direction \a dir.
	 * @return The crossing with no cost, or an empty piece when there is none.
	 */
	Piece ExistingCrossing(TileIndex head, DiagDirection dir) const
	{
		if (!IsUsableTile(head) || !IsTileType(head, TileType::TunnelBridge)) return {};
		if (GetTunnelBridgeDirection(head) != dir || GetTunnelBridgeTransportType(head) != this->type) return {};
		if (this->type == TransportType::Rail) {
			if (GetRailType(head) != this->railtype || !IsTileOwner(head, _current_company)) return {};
		} else if (!HasTileRoadType(head, GetRoadTramType(this->roadtype))) {
			return {};
		}
		return {Piece::Kind::Bridge, 0, head, GetOtherTunnelBridgeEnd(head), 0};
	}

	/** Pick a bridge type for a length: the fastest for railways, the cheapest for roads. */
	std::optional<BridgeType> ChooseBridge(uint length) const
	{
		std::optional<BridgeType> best;
		for (BridgeType i = 0; i < MAX_BRIDGES; i++) {
			if (CheckBridgeAvailability(i, length).Failed()) continue;
			if (!best.has_value()) {
				best = i;
				continue;
			}
			const BridgeSpec *a = GetBridgeSpec(i);
			const BridgeSpec *b = GetBridgeSpec(*best);
			bool better = (this->type == TransportType::Rail) ?
					(a->speed > b->speed || (a->speed == b->speed && a->price < b->price)) :
					(a->price < b->price);
			if (better) best = i;
		}
		return best;
	}

	/** Is moving in a straight line from \a tile blocked by something a bridge could cross? */
	bool IsBlockedAhead(TileIndex tile, DiagDirection dir)
	{
		TileIndex next = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(dir));
		if (!IsUsableTile(next)) return false;
		if (this->type == TransportType::Rail) {
			return this->RailPiece(next, DiagDirToDiagTrack(dir)).kind == Piece::Kind::None;
		}
		return this->RoadPiece(next, AxisToRoadBits(DiagDirToAxis(dir))).kind == Piece::Kind::None;
	}

	void ExpandRail(uint64_t key)
	{
		const Node &node = this->nodes[key];
		const int g = node.g;
		TileIndex tile = KeyTile(key);
		Trackdir td = static_cast<Trackdir>(KeyDir(key));
		DiagDirection exit = TrackdirToExitdir(td);
		TileIndex next = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(exit));
		if (!IsUsableTile(next)) return;

		TrackdirBits reachable = DiagdirReachesTrackdirs(exit);
		if (_settings_game.pf.forbid_90_deg) reachable.Reset(TrackdirCrossesTrackdirs(td));

		int slope = std::abs(GetTileZ(next) - GetTileZ(tile)) * SLOPE_COST;
		for (Trackdir next_td : ALL_TRACKDIRS) {
			if (!reachable.Test(next_td)) continue;
			Piece p = this->RailPiece(next, TrackdirToTrack(next_td));
			if (p.kind == Piece::Kind::None) continue;
			int turn = TurnSteps(TrackdirDirection(td), TrackdirDirection(next_td)) * TURN_COST;
			this->Push(Key(next, to_underlying(next_td), false), key, g + STEP_COST + this->PieceCost(p) + turn + slope, {p, Piece{}});
		}

		/* Bridges start at the next tile, in line with a straight track. */
		if (!IsDiagonalTrackdir(td)) return;
		Piece bridge = this->ExistingCrossing(next, exit);
		if (bridge.kind == Piece::Kind::None && this->IsBlockedAhead(next, exit)) bridge = this->BridgePiece(next, exit);
		if (bridge.kind != Piece::Kind::None) {
			int length = DistanceManhattan(tile, bridge.end);
			this->Push(Key(bridge.end, to_underlying(td), true), key, g + STEP_COST * length + this->PieceCost(bridge), {bridge, Piece{}});
		}
	}

	void ExpandRoad(uint64_t key)
	{
		const Node &node = this->nodes[key];
		const int g = node.g;
		TileIndex tile = KeyTile(key);
		uint8_t dir = KeyDir(key);
		bool on_bridge_head = KeyBridge(key);

		for (DiagDirection exit = DiagDirection::Begin; exit < DiagDirection::End; exit++) {
			RoadBits bits = DiagDirToRoadBits(exit);
			int turn = 0;
			if (dir != NO_DIR) {
				DiagDirection travel = static_cast<DiagDirection>(dir);
				if (exit == ReverseDiagDir(travel)) continue;
				if (on_bridge_head && exit != travel) continue;
				if (exit != travel) turn = TURN_COST;
				bits.Set(DiagDirToRoadBits(ReverseDiagDir(travel)));
			}

			TileIndex next = AddTileIndexDiffCWrap(tile, TileIndexDiffCByDiagDir(exit));
			if (!IsUsableTile(next)) continue;

			/* The road piece on this tile; a bridge head already has its road. */
			Piece here{};
			if (!on_bridge_head) {
				here = this->RoadPiece(tile, bits);
				if (here.kind == Piece::Kind::None) continue;
			}
			int base = g + STEP_COST + this->PieceCost(here) + turn;

			if (next == this->end) {
				Piece last = this->RoadPiece(next, DiagDirToRoadBits(ReverseDiagDir(exit)));
				if (last.kind != Piece::Kind::None) this->Push(Key(next, GOAL_DIR, false), key, base + this->PieceCost(last), {here, last});
			} else {
				this->Push(Key(next, to_underlying(exit), false), key, base, {here, Piece{}});
			}

			Piece bridge = this->ExistingCrossing(next, exit);
			if (bridge.kind == Piece::Kind::None && this->IsBlockedAhead(next, exit)) bridge = this->BridgePiece(next, exit);
			if (bridge.kind != Piece::Kind::None) {
				int length = DistanceManhattan(tile, bridge.end);
				uint8_t bridge_dir = (bridge.end == this->end) ? GOAL_DIR : to_underlying(exit);
				this->Push(Key(bridge.end, bridge_dir, bridge_dir != GOAL_DIR), key,
						g + STEP_COST * length + this->PieceCost(here) + this->PieceCost(bridge) + turn, {here, bridge});
			}
		}
	}

	/** Collect the pieces from the start to the state \a key. */
	std::vector<Piece> Path(uint64_t key)
	{
		std::vector<Piece> path;
		for (uint64_t k = key; k != NO_PARENT; k = this->nodes[k].parent) {
			const Node &node = this->nodes[k];
			for (auto it = node.pieces.rbegin(); it != node.pieces.rend(); ++it) {
				if (it->kind != Piece::Kind::None) path.push_back(*it);
			}
		}
		std::reverse(path.begin(), path.end());
		return path;
	}
};

/** Build one piece of the route. */
CommandCost BuildPiece(DoCommandFlags flags, const Piece &p, TransportType type, RailType railtype, RoadType roadtype)
{
	switch (p.kind) {
		case Piece::Kind::Rail:
			return Command<Commands::BuildRail>::Do(flags, p.tile, railtype, static_cast<Track>(p.arg), false);

		case Piece::Kind::Road:
			return Command<Commands::BuildRoad>::Do(flags, p.tile, RoadBits(p.arg), roadtype, {}, TownID::Invalid());

		case Piece::Kind::Bridge:
			return Command<Commands::BuildBridge>::Do(flags, p.end, p.tile, type, p.arg,
					type == TransportType::Rail ? railtype : INVALID_RAILTYPE,
					type == TransportType::Road ? roadtype : INVALID_ROADTYPE);

		default:
			NOT_REACHED();
	}
}

} // namespace

/** Can a route start or end on this tile itself? */
static bool IsOpenTile(TileIndex tile)
{
	switch (GetTileType(tile)) {
		case TileType::Clear:
		case TileType::Trees:
		case TileType::Road:
		case TileType::Railway:
			return true;
		default:
			return false;
	}
}

/**
 * Find the tile a route should use for a clicked tile. A click on an
 * industry means a tile next to it; a click on a house means the nearest road.
 * @param tile Clicked tile.
 * @param towards The other end of the route, to choose the closest side.
 * @return The tile to use.
 */
TileIndex AutoRouteSnapEnd(TileIndex tile, TileIndex towards)
{
	if (IsTileType(tile, TileType::Industry)) {
		TileArea around = Industry::GetByTile(tile)->location;
		around.Expand(1);
		TileIndex best = tile;
		uint best_distance = UINT_MAX;
		for (TileIndex t : around) {
			if (!IsOpenTile(t) || DistanceFromEdge(t) == 0) continue;
			uint distance = DistanceManhattan(t, towards);
			if (distance < best_distance) {
				best = t;
				best_distance = distance;
			}
		}
		return best;
	}

	if (IsTileType(tile, TileType::House)) {
		TileArea around(tile, 1, 1);
		around.Expand(4);
		TileIndex best = tile;
		uint best_distance = UINT_MAX;
		for (TileIndex t : around) {
			if (!IsNormalRoadTile(t)) continue;
			uint distance = DistanceManhattan(t, tile) * 1000 + DistanceManhattan(t, towards);
			if (distance < best_distance) {
				best = t;
				best_distance = distance;
			}
		}
		return best;
	}

	return tile;
}

/**
 * Build a whole road or railway between two tiles, finding the route automatically.
 * @param flags Type of operation.
 * @param end_tile Tile where the route ends.
 * @param start_tile Tile where the route starts.
 * @param transport_type Build a road or a railway.
 * @param railtype Rail type of a railway.
 * @param roadtype Road type of a road.
 * @return The cost of this operation or an error.
 */
CommandCost CmdBuildAutoRoute(DoCommandFlags flags, TileIndex end_tile, TileIndex start_tile, TransportType transport_type, RailType railtype, RoadType roadtype)
{
	if (start_tile >= Map::Size() || end_tile >= Map::Size() || start_tile == end_tile) return CMD_ERROR;
	switch (transport_type) {
		case TransportType::Rail:
			if (!ValParamRailType(railtype)) return CMD_ERROR;
			break;
		case TransportType::Road:
			if (!ValParamRoadType(roadtype)) return CMD_ERROR;
			break;
		default:
			return CMD_ERROR;
	}

	AutoRouteFinder finder(start_tile, end_tile, transport_type, railtype, roadtype);
	std::optional<std::vector<Piece>> path = finder.Find();
	if (!path.has_value()) return CommandCost(STR_ERROR_AUTOROUTE_NOT_FOUND);

	CommandCost total(ExpensesType::Construction);
	for (const Piece &p : *path) {
		total.AddCost(p.cost);
		if (p.cost != 0) Debug(Facility::Misc, Severity::Info, "[autoroute] piece {} at {},{} arg {} cost {}", to_underlying(p.kind), TileX(p.tile), TileY(p.tile), p.arg, p.cost);
	}
	Debug(Facility::Misc, Severity::Info, "[autoroute] {} pieces, {} {}", path->size(), flags.Test(DoCommandFlag::Execute) ? "building" : "testing", total.GetCost());
	if (total.GetCost() == 0) return CommandCost(STR_ERROR_ALREADY_BUILT);
	if (!flags.Test(DoCommandFlag::Execute)) return total;

	total = CommandCost(ExpensesType::Construction);

	CommandCost last_error = CMD_ERROR;
	bool had_success = false;
	for (const Piece &p : *path) {
		if (p.cost == 0) {
			/* Already there; building would only fail with "already built". */
			continue;
		}
		CommandCost ret = BuildPiece(flags, p, transport_type, railtype, roadtype);
		if (ret.Succeeded()) {
			had_success = true;
			total.AddCost(ret.GetCost());
		} else if (ret.GetErrorMessage() != STR_ERROR_ALREADY_BUILT) {
			Debug(Facility::Misc, Severity::Info, "[autoroute] failed piece {} at {},{} arg {}: {}", to_underlying(p.kind), TileX(p.tile), TileY(p.tile), p.arg, GetString(ret.GetErrorMessage()));
			last_error = std::move(ret);
		}
	}

	if (!had_success) return last_error;
	return total;
}
