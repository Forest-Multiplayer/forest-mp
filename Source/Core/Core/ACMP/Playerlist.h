// This file applies the complete implementation of the Player List system for both Host and Client
// Includes: tracking by ID, player slot array (first = local), player disconnect cleanup,
// and helper functions.

#pragma once
#include <array>
#include <string>
#include <chrono>
#include <optional>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace ACMP {

// Max number of total players (1 local + N remote)
static constexpr size_t kMaxPlayers = 32;

// Player ID size constraint
static constexpr size_t kPlayerIdSize = 64;

// Timeout duration for player inactivity
static constexpr std::chrono::seconds kPlayerTimeout = std::chrono::seconds(5);

// Provided types
using f32 = float;
using s16 = int16_t;

struct xyz_t { f32 x, y, z; };
struct s_xyz { s16 x, y, z; };

struct PositionAngle {
    xyz_t position;
    s_xyz angle;
};

struct PlayerUpdatePayload {
    char id[kPlayerIdSize];
    PositionAngle world_position;
    PositionAngle eye_position;
    f32 velocity[3];
    f32 speed;
    uint32_t stateBitfield;
    int32_t requested_main_index;
    int32_t requested_main_index_priority;
    int32_t requested_main_index_changed;
};

struct TrackedPlayerState {
    PlayerUpdatePayload data;
    std::chrono::steady_clock::time_point lastUpdated;
    bool dirty = false;
};

struct TrackedPlayer {
    std::string id; // Empty = slot free
    TrackedPlayerState state;
};

class PlayerList {
public:
    PlayerList() { players.fill({}); }

    void setLocalPlayerId(const std::string& id) {
        players[0].id = id;
    }

    // Called when we receive or produce a PlayerUpdate
    void updateFromPayload(const PlayerUpdatePayload& payload) {
        std::string id(payload.id);
        int index = findPlayerIndexById(id);
        if (index == -1) {
            index = allocatePlayerSlot(id);
            if (index == -1) return; // No space
        }

        auto& tracked = players[index].state;
        tracked.data = payload;
        tracked.lastUpdated = std::chrono::steady_clock::now();
        tracked.dirty = true;
    }

    // Mark all dirty flags false after sending
    void clearDirtyFlags() {
        for (auto& player : players) {
            player.state.dirty = false;
        }
    }

    // Call this when a known player disconnects
    void removePlayerById(const std::string& id) {
        int index = findPlayerIndexById(id);
        if (index > 0) { // never remove index 0
            players[index] = TrackedPlayer{};
            idToPeer.erase(id);
        }
    }

    // Returns index or -1
    int findPlayerIndexById(const std::string& id) const {
        for (size_t i = 0; i < players.size(); ++i) {
            if (players[i].id == id) return static_cast<int>(i);
        }
        return -1;
    }

    // Host only: link player ID to ENetPeer* for message routing
    void bindPeerToId(const std::string& id, void* peer) {
        idToPeer[id] = peer;
    }

    void* getPeerById(const std::string& id) const {
        auto it = idToPeer.find(id);
        return (it != idToPeer.end()) ? it->second : nullptr;
    }

    // Check for inactive players (host only)
    std::vector<std::string> getTimedOutPlayers() const {
        std::vector<std::string> timedOut;
        auto now = std::chrono::steady_clock::now();
        for (size_t i = 1; i < players.size(); ++i) {
            if (!players[i].id.empty() &&
                now - players[i].state.lastUpdated > kPlayerTimeout) {
                timedOut.push_back(players[i].id);
            }
        }
        return timedOut;
    }

    // Accessors
    std::optional<TrackedPlayerState> getPlayerStateById(const std::string& id) const {
        int index = findPlayerIndexById(id);
        if (index != -1) return players[index].state;
        return std::nullopt;
    }

    std::vector<TrackedPlayerState> getRemotePlayers() const {
        std::vector<TrackedPlayerState> result;
        for (size_t i = 1; i < players.size(); ++i) {
            if (!players[i].id.empty()) {
                result.push_back(players[i].state);
            }
        }
        return result;
    }

    TrackedPlayerState& getLocalPlayerState() {
        return players[0].state;
    }

private:
    std::array<TrackedPlayer, kMaxPlayers> players;
    std::unordered_map<std::string, void*> idToPeer; // optional: only meaningful on host

    int allocatePlayerSlot(const std::string& id) {
        for (size_t i = 1; i < players.size(); ++i) {
            if (players[i].id.empty()) {
                players[i].id = id;
                return static_cast<int>(i);
            }
        }
        return -1;
    }
};

} // namespace ACMP