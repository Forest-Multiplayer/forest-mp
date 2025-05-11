#include "ACMPHost.h"
#include "ACMP.h"
#include "ACMPCommon.h"
#include "Playerlist.h"

#include "Common/Assert.h"
#include "Core/PowerPC/MMU.h"
#include "VideoCommon/OnScreenDisplay.h"

#include <chrono>
#include <iostream>
#include <thread>

namespace ACMP
{
bool Host::init(std::string local_name, uint16_t port)
{
  if (enet_initialize() != 0)
    return false;

  players = new Playerlist(local_name, local_name);

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

void Host::pollLoop()
{
  ENetEvent event;
  while (running)
  {
    while (enet_host_service(server, &event, 10) > 0)
    {
      if (event.type == ENET_EVENT_TYPE_RECEIVE)
      {
        if (!event.packet) {
          continue;
        }

        handleMessage(event, event.packet->data, event.packet->dataLength);
        enet_packet_destroy(event.packet);
      } else if (event.type == ENET_EVENT_TYPE_DISCONNECT) {
        if (players) {
          players->removePlayer(event.peer);
        }

        enet_packet_destroy(event.packet);
      }
    }
    
    std::lock_guard<std::mutex> lock(s_msg_queue_mutex);
    for (auto& pending : s_msg_queue)
    {
      if (pending.peer && pending.peer->state != ENET_PEER_STATE_CONNECTED)
      {
        continue;
      }

      auto sz = sizeof(Message);
      ENetPacket* packet = enet_packet_create(pending.data.data(), sz, ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send(pending.peer, 0, packet);
      enet_host_flush(pending.peer->host);
    }

    s_msg_queue.clear();
  }
}

void Host::handleMessage(ENetEvent& event, enet_uint8* data, size_t len)
{
  switch (static_cast<MessageType>(data[0]))
  {
  case MessageType::IDENTIFY:
    handleIdentify(event.peer, reinterpret_cast<const IdentifyPayload*>(&data[1]));
    OSD::AddMessage("Peer joined: " + std::string(reinterpret_cast<const IdentifyPayload*>(&data[1])->id));
    break;
  case MessageType::SPAWN_REQUEST:
    handleSpawnRequest(event.peer);
    break;
  case MessageType::PLAYER_UPDATE:
    handlePlayerUpdate(event.peer, reinterpret_cast<const PlayerUpdatePayload*>(&data[1]));
    break;
  default:
    break;
  }
}

void Host::handleIdentify(ENetPeer* peer, const IdentifyPayload* payload)
{
  players->addPlayer(peer, payload->id, payload->name);
  std::cout << "IDENTIFY received from " << payload->id << "\n";
}

void Host::handleSpawnRequest(ENetPeer* peer)
{
  SpawnData spawn {
    players->getLocalPlayerState()->world_position.position.x,
    players->getLocalPlayerState()->world_position.position.y,
    players->getLocalPlayerState()->world_position.position.z,
    90.0f
  };

  // sendMessage(peer, MessageType::SPAWN_ACCEPTED, &spawn, sizeof(spawn));
}

void Host::handlePlayerUpdate(ENetPeer* peer, const PlayerUpdatePayload* update)
{
  std::lock_guard<std::mutex> lock(stateMutex);
  players->updatePlayer(*update);
}

void Host::broadcastLoop()
{
  using namespace std::chrono;
  const auto interval = milliseconds(16);

  while (broadcasting)
  {
    auto start = steady_clock::now();
    std::vector<AddrUpdate> world_updates;
    {
      std::lock_guard<std::mutex> lk(s_world_snapshot_mutex);
      for (auto& update : s_world_snapshot.snapshot)
      {
        if (!update.second.dirty)
          continue;

        world_updates.push_back({update.first, update.second.val});
        update.second.dirty = false;
      }
    }

    {
      std::lock_guard<std::mutex> lock(stateMutex);

      auto peers = players->getRemotePlayers();
      for (const auto& player : peers)
      {
        if (player.dirty)
        {
          for (auto& other_player : peers)
          {
            if (player.peer == other_player.peer)
              continue;

            std::vector<uint8_t> player_update_buffer;
            serialize_player_update(other_player.state, player_update_buffer);
            sendMessage(player.peer, MessageType::PLAYER_UPDATE, player_update_buffer.data(), player_update_buffer.size());
          }
        }

        auto state = players->getLocalPlayerState();

        std::vector<uint8_t> player_update_buffer;
        serialize_player_update(*state, player_update_buffer);
        sendMessage(player.peer, MessageType::PLAYER_UPDATE, player_update_buffer.data(), player_update_buffer.size());

        // // world sync
        // sendMessage(player.peer, MessageType::WORLD_UPDATE, &world_updates, sizeof(std::vector<AddrUpdate>) + (sizeof(AddrUpdate) * world_updates.size()));
      }
    }


    players->clearDirtyFlags();

    auto elapsed = steady_clock::now() - start;
    if (elapsed < interval)
      std::this_thread::sleep_for(interval - elapsed);
  }
}

void Host::frameAdvance(const Core::CPUThreadGuard& guard) {

  std::stringstream ss;
  ss << "HOST\n";

  if (players == nullptr) {
    return;
  }

  sync_game_memory(guard, *players);
  record_world_snapshot(guard);

  for (auto& player : players->getRemotePlayers()) {
    ss << fmt::format("Player {}:\n {}, {}, {}\n", std::string(player.state.id), 
                      player.state.world_position.position.x,
                      player.state.world_position.position.y,
                      player.state.world_position.position.z);
  }
    
  u32 local_player_addr =
      PowerPC::MMU::HostRead_U32(guard, symbolDb().GetSymbolFromName("s_primary_player")->address);

  PlayerUpdatePayload* local_state = players->getLocalPlayerState();
  ss << fmt::format("Local Player ({}):\n {}, {}, {}\n", local_player_addr, 
                  local_state->world_position.position.x,
                  local_state->world_position.position.y,
                  local_state->world_position.position.z);

  DebugText = ss.str();
}

}  // namespace ACMP