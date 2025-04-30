#pragma once

#include "Playerlist.h"

#include "Core/Core.h"

#include <mutex>
#include <thread>
#include <unordered_map>

#include <enet/enet.h>

class PlayerList;
class PlayerUpdatePayload;
class SpawnData;
class Message;

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

private:
  ENetHost* client = nullptr;
  ENetPeer* peer = nullptr;
  ENetAddress address;

  std::thread pollThread;
  std::atomic<bool> running{false};

  Playerlist* players = nullptr;

  void pollLoop();
  void handleMessage(const Message* msg);
  void handleSpawnAccepted(const SpawnData* data);
  void handlePlayerUpdate(const PlayerUpdatePayload* update);
  void handleWorldUpdate(const std::vector<AddrUpdate> updates);
};
}  // namespace ACMP
