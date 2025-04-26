#pragma once

#include "ACMPCommon.h"

#include <array>
#include <string>
#include <chrono>
#include <optional>
#include <cstring>
#include <vector>
#include <unordered_map>

namespace ACMP {

struct Player {
  ENetPeer* peer; // host only
  bool dirty;
  
  PlayerUpdatePayload state;
};

// Max number of total players (1 local + N remote)
static constexpr size_t kMaxPlayers = 32;

// Timeout duration for player inactivity
static constexpr std::chrono::seconds kPlayerTimeout = std::chrono::seconds(5);

class Playerlist {
public:
  Playerlist(const std::string local_id, const std::string local_name);

  void addPlayer(ENetPeer* peer, const std::string& id, const std::string& name);
  void removePlayer(const std::string& id);
  void removePlayer(ENetPeer* peer);
  void updatePlayer(const PlayerUpdatePayload& update);
  void clearDirtyFlags();

  PlayerUpdatePayload* getLocalPlayerState();
  std::vector<Player> getRemotePlayers() const {
    return m_players;
  }
private:
    std::vector<Player> m_players;
    std::string local_id;
    std::string local_name;
    PlayerUpdatePayload local_state {};
};
} // namespace ACMP