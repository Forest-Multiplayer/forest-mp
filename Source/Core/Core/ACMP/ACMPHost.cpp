#include "ACMPHost.h"
#include "ACMP.h"
#include "ACMPCommon.h"
#include "Playerlist.h"

#include "Common/Assert.h"
#include "Core/PowerPC/MMU.h"
#include "VideoCommon/OnScreenDisplay.h"

#include <cereal/archives/binary.hpp>
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
  if (!server || running.load())
    return;
  running.store(true);
  broadcasting = true;
  pollThread = std::thread(&Host::pollLoop, this);
  broadcastThread = std::thread(&Host::broadcastLoop, this);
}

void Host::stop()
{
  running.store(false);
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
  while (running.load())
  {
    if (!players) {
      return;
    }

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

      ENetPacket* packet = enet_packet_create(pending.data.data(), pending.data.size(), ENET_PACKET_FLAG_RELIABLE);
      enet_peer_send(pending.peer, 0, packet);
      enet_host_flush(pending.peer->host);
    }

    s_msg_queue.clear();
  }
}

void Host::handleMessage(ENetEvent& event, enet_uint8* data, size_t len)
{
  try {
    switch (static_cast<MessageType>(data[0]))
    {
    case MessageType::IDENTIFY: {
      IdentifyPayload payload;
      deserialize_identify(&data[1], len - 1, payload);
      handleIdentify(event.peer, payload);

      OSD::AddMessage("Peer joined: " + payload.name);
      break;
    }
    case MessageType::SPAWN_REQUEST:
      handleSpawnRequest(event.peer);
      break;
    case MessageType::PLAYER_UPDATE: {
      PlayerUpdatePayload payload;
      deserialize_player_update(&data[1], len - 1, payload);
      handlePlayerUpdate(event.peer, payload);
      break;
    }
    default:
      break;
    }
  } catch (const cereal::Exception& e) {
    std::cerr << "Cereal exception: " << e.what() << "\n";
  }
}

void Host::handleIdentify(ENetPeer* peer, const IdentifyPayload& payload)
{
  players->addPlayer(peer, payload.id, payload.name);
  std::cout << "IDENTIFY received from " << payload.id << "\n";
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

void Host::handlePlayerUpdate(ENetPeer* peer, const PlayerUpdatePayload& update)
{
  std::lock_guard<std::mutex> lock(stateMutex);
  players->updatePlayer(update);
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
      std::lock_guard<std::mutex> lk(s_dirty_snapshot_mutex);
      world_updates.insert(world_updates.end(),
                           s_dirty_snapshot.dirty_addresses.begin(),
                           s_dirty_snapshot.dirty_addresses.end());
      s_dirty_snapshot.dirty_addresses.clear();
    }

    {
      std::lock_guard<std::mutex> lock(stateMutex);

      auto& peers = players->getRemotePlayers();
      for (auto& player : peers)
      {
        if (player->dirty)
        {
          for (auto& other_player : peers)
          {
            if (player->peer == other_player->peer)
              continue;

            std::vector<uint8_t> player_update_buffer;
            serialize_player_update(other_player->state, player_update_buffer);
            sendMessage(player->peer, MessageType::PLAYER_UPDATE, player_update_buffer);
          }
        }

        auto state = players->getLocalPlayerState();

        std::vector<uint8_t> player_update_buffer;
        serialize_player_update(*state, player_update_buffer);
        sendMessage(player->peer, MessageType::PLAYER_UPDATE, player_update_buffer);

        if (world_updates.empty())
          continue;

        std::vector<uint8_t> world_update_buffer;
        serialize_world_update(WorldSyncPayload {
            world_updates
        }, world_update_buffer);
        sendMessage(player->peer, MessageType::WORLD_UPDATE, world_update_buffer);
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
    ss << fmt::format("Player {}:\n {}, {}, {}\n", std::string(player->state.id), 
                      player->state.world_position.position.x,
                      player->state.world_position.position.y,
                      player->state.world_position.position.z);
  }
    
  PlayerUpdatePayload* local_state = players->getLocalPlayerState();
  ss << fmt::format("Local Player ({}):\n {}, {}, {}\n", s_primary_player, 
                  local_state->world_position.position.x,
                  local_state->world_position.position.y,
                  local_state->world_position.position.z);

  DebugText = ss.str();
}

}  // namespace ACMP