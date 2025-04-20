#pragma once

#include "ACMPCommon.h"
#include "Playerlist.h"

#include "Core/Core.h"

#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

namespace ACMP
{
class Host
{
public:
  bool init(uint16_t port);
  void start();
  void stop();
  void shutdown();

  void setSelfState(const PlayerUpdatePayload& update);

  void frameAdvance(const Core::CPUThreadGuard& guard);

private:
  ENetHost* server = nullptr;
  std::unordered_map<ENetPeer*, std::string> peerToId;

  std::thread pollThread;
  std::thread broadcastThread;
  std::atomic<bool> running{false};
  std::atomic<bool> broadcasting{false};

  PlayerList players;
  std::mutex stateMutex;

  void pollLoop();
  void broadcastLoop();

  void handleMessage(ENetEvent& event, const Message* msg);
  void handleIdentify(ENetPeer* peer, const IdentifyPayload* payload);
  void handleSpawnRequest(ENetPeer* peer);
  void handlePlayerUpdate(ENetPeer* peer, const PlayerUpdatePayload* update);
};
}  // namespace ACMP
