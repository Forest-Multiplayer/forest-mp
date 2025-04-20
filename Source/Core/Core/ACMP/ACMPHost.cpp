#include "ACMPHost.h"
#include "ACMP.h"

#include "Common/Assert.h"
#include "Core/PowerPC/MMU.h"
#include "VideoCommon/OnScreenDisplay.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace ACMP
{
bool Host::init(uint16_t port)
{
  if (enet_initialize() != 0)
    return false;

  ENetAddress address;
  address.host = ENET_HOST_ANY;
  address.port = port;

  server = enet_host_create(&address, 32, 2, 0, 0);
  return server != nullptr;
}

void Host::start()
{
  if (!server || running)
    return;
  running = true;
  broadcasting = true;
  pollThread = std::thread(&Host::pollLoop, this);
  broadcastThread = std::thread(&Host::broadcastLoop, this);
}

void Host::stop()
{
  running = false;
  broadcasting = false;
  if (pollThread.joinable())
    pollThread.join();
  if (broadcastThread.joinable())
    broadcastThread.join();
}

void Host::shutdown()
{
  stop();
  if (server)
  {
    enet_host_destroy(server);
    server = nullptr;
    enet_deinitialize();
  }
}

void Host::setSelfState(const PlayerUpdatePayload& update)
{
  std::lock_guard<std::mutex> lock(stateMutex);
  players.updateFromPayload(update);
}

void Host::pollLoop()
{
  ENetEvent event;
  while (running)
  {
    while (enet_host_service(server, &event, 10) > 0)
    {
      if (event.type == ENET_EVENT_TYPE_RECEIVE)
      {
        const Message* msg = reinterpret_cast<Message*>(event.packet->data);
        handleMessage(event, msg);
        enet_packet_destroy(event.packet);
      }
      else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
      {
        std::lock_guard<std::mutex> lock(stateMutex);
        auto it = peerToId.find(event.peer);
        if (it != peerToId.end())
        {
          players.removePlayerById(it->second);
          peerToId.erase(it);
        }
      }
    }
  }
}

void Host::handleMessage(ENetEvent& event, const Message* msg)
{
  switch (static_cast<MessageType>(msg->type))
  {
  case MessageType::IDENTIFY:
    handleIdentify(event.peer, reinterpret_cast<const IdentifyPayload*>(msg->data));
    break;
  case MessageType::SPAWN_REQUEST:
    handleSpawnRequest(event.peer);
    break;
  case MessageType::PLAYER_UPDATE:
    handlePlayerUpdate(event.peer, reinterpret_cast<const PlayerUpdatePayload*>(msg->data));
    break;
  default:
    break;
  }
}

void Host::handleIdentify(ENetPeer* peer, const IdentifyPayload* payload)
{
  std::string id(payload->id);
  peerToId[peer] = id;
  players.bindPeerToId(id, peer);
  std::cout << "IDENTIFY received from " << id << "\n";
}

void Host::handleSpawnRequest(ENetPeer* peer)
{
  auto it = peerToId.find(peer);
  if (it == peerToId.end())
    return;

  SpawnData spawn {
    players.getLocalPlayerState().data.world_position.position.x,
    players.getLocalPlayerState().data.world_position.position.y,
    players.getLocalPlayerState().data.world_position.position.z,
    90.0f
  };

  sendMessage(peer, MessageType::SPAWN_ACCEPTED, &spawn, sizeof(spawn));
}

void Host::handlePlayerUpdate(ENetPeer* peer, const PlayerUpdatePayload* update)
{
  auto it = peerToId.find(peer);
  if (it == peerToId.end())
    return;

  std::lock_guard<std::mutex> lock(stateMutex);
  players.updateFromPayload(*update);
}

void Host::broadcastLoop()
{
  using namespace std::chrono;
  const auto interval = milliseconds(16);

  while (broadcasting)
  {
    auto start = steady_clock::now();

    std::lock_guard<std::mutex> lock(stateMutex);

    auto updates = players.getRemotePlayers();
    for (const auto& state : updates)
    {
      if (state.dirty)
      {
        for (auto& [peer, id] : peerToId)
        {
          sendMessage(peer, MessageType::PLAYER_UPDATE, &state.data, sizeof(state.data));
        }
      }
    }

    auto selfState = players.getLocalPlayerState();
    if (selfState.dirty)
    {
      for (auto& [peer, id] : peerToId)
      {
        sendMessage(peer, MessageType::PLAYER_UPDATE, &selfState.data, sizeof(selfState.data));
      }
    }

    players.clearDirtyFlags();

    auto elapsed = steady_clock::now() - start;
    if (elapsed < interval)
      std::this_thread::sleep_for(interval - elapsed);
  }
}

void Host::frameAdvance(const Core::CPUThreadGuard& guard) {
  u32 players_addr = s_symbolDB.GetSymbolFromName("s_acmp_players_list")->address;
  for (int i = 0; i < players.getRemotePlayers().size(); ++i) {
    auto& player = players.getRemotePlayers()[i];
    u32 player_addr = PowerPC::MMU::HostRead_U32(guard, players_addr + ((i + 1) * 0x4));

    writePositionAngle(guard, player.data.world_position, player_addr + 0x028); 
    writePositionAngle(guard, player.data.eye_position, player_addr + 0x048); 

    PowerPC::MMU::HostWrite_F32(guard, player.data.velocity[0], player_addr + 0x068);
    PowerPC::MMU::HostWrite_F32(guard, player.data.velocity[1], player_addr + 0x06C);
    PowerPC::MMU::HostWrite_F32(guard, player.data.velocity[2], player_addr + 0x070);
    PowerPC::MMU::HostWrite_F32(guard, player.data.speed, player_addr + 0x074);
    PowerPC::MMU::HostWrite_U32(guard, player.data.stateBitfield, player_addr + 0x020);

    PowerPC::MMU::HostWrite_U32(guard, player.data.requested_main_index, player_addr + 0x0D08);
    PowerPC::MMU::HostWrite_U32(guard, player.data.requested_main_index_priority, player_addr + 0x0D0C);
    PowerPC::MMU::HostWrite_U32(guard, player.data.requested_main_index_changed, player_addr + 0x0D10);
  }

  u32 local_player_addr = PowerPC::MMU::HostRead_U32(guard, players_addr);

  readPositionAngle(guard, players.getLocalPlayerState().data.world_position, local_player_addr + 0x028);
  readPositionAngle(guard, players.getLocalPlayerState().data.eye_position, local_player_addr + 0x048);

  PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x068);
  PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x06C);
  PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x070);
  PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x074);
  PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x020);

  PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D08);
  PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D0C);
  PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D10);

  setSelfState(players.getLocalPlayerState().data);
}

}  // namespace ACMP
