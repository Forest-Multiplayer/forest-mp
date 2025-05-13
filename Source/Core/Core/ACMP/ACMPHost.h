#pragma once

#include "Core/Core.h"

#include <mutex>
#include <shared_mutex>
#include <thread>
#include <unordered_map>

#include <enet/enet.h>

namespace ACMP
{
class PlayerUpdatePayload;
class SpawnData;
class Message;
class IdentifyPayload;
class Playerlist;
class Host
{
public:
  bool init(std::string local_name, uint16_t port);
  void start();
  void stop();
  void shutdown();

  void frameAdvance(const Core::CPUThreadGuard& guard);

private:
  ENetHost* server = nullptr;

  std::thread pollThread;
  std::thread broadcastThread;
  std::atomic<bool> running{false};
  std::atomic<bool> broadcasting{false};

  Playerlist* players = nullptr;
  std::mutex stateMutex;

  void pollLoop();
  void broadcastLoop();

  void handleMessage(ENetEvent& event, uint8_t* msg, size_t len);
  void handleIdentify(ENetPeer* peer, const IdentifyPayload& payload);
  void handleSpawnRequest(ENetPeer* peer);
  void handlePlayerUpdate(ENetPeer* peer, const PlayerUpdatePayload& update);
};
}  // namespace ACMP
