#pragma once

#include "ACMPCommon.h"
#include "Playerlist.h"

#include "Core/Core.h"

#include <mutex>
#include <thread>
#include <unordered_map>

struct AddrUpdate;

namespace ACMP
{
class Client
{
public:
  bool connect(const std::string& host, uint16_t port, const std::string& id);
  void disconnect();
  void sendPlayerUpdate(const PlayerUpdatePayload& update);
  void start();
  void stop();

  void frameAdvance(const Core::CPUThreadGuard& guard);

  PlayerList& getPlayers() { return players; }

private:
  ENetHost* client = nullptr;
  ENetPeer* peer = nullptr;

  std::thread pollThread;
  std::atomic<bool> running{false};

  PlayerList players;

  void pollLoop();
  void handleMessage(const Message* msg);
  void handleSpawnAccepted(const SpawnData* data);
  void handlePlayerUpdate(const PlayerUpdatePayload* update);
};
}  // namespace ACMP
