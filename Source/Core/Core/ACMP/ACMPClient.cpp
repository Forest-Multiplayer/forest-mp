#include "ACMPClient.h"
#include "ACMP.h"
#include "ACMPCommon.h"

#include <Common/Assert.h>
#include <Core/Config/MainSettings.h>
#include <Core/PowerPC/MMU.h>
#include <cstring>
#include <iostream>

namespace ACMP
{
bool Client::connect(const std::string& host, uint16_t port, const std::string& id)
{
  if (enet_initialize() != 0)
    return false;

  players = new Playerlist(id, id);

  client = enet_host_create(nullptr, 1, 2, 0, 0);
  if (!client)
    return false;

  enet_address_set_host(&address, host.c_str());
  address.port = port;

  peer = enet_host_connect(client, &address, 2, 0);
  if (!peer)
    return false;

  ENetEvent event;
  if (enet_host_service(client, &event, 5000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
  {
    IdentifyPayload payload{};
    std::strncpy(payload.id, id.c_str(), sizeof(payload.id) - 1);
    std::strncpy(payload.name, id.c_str(), sizeof(payload.name) - 1);

    sendMessage(peer, MessageType::IDENTIFY, &payload, sizeof(payload));

    return true;
  }

  return false;
}

void Client::disconnect()
{
  stop();
  if (peer)
    enet_peer_disconnect(peer, 0);
  if (client)
  {
    enet_host_destroy(client);
    client = nullptr;
    enet_deinitialize();
  }
}

void Client::sendPlayerUpdate(const PlayerUpdatePayload& update)
{
  if (peer)
  {
    sendMessage(peer, MessageType::PLAYER_UPDATE, &update, sizeof(update));
  }
}

void Client::start()
{
  if (!client || running)
    return;
  running = true;
  pollThread = std::thread(&Client::pollLoop, this);
}

void Client::stop()
{
  running = false;
  if (pollThread.joinable())
    pollThread.join();
}

void Client::pollLoop()
{
  ENetEvent event;
  bool needs_reconnect = false;
  while (running)
  {
    int r;
    while ((r = enet_host_service(client, &event, 10)) > 0)
    {
      if (event.type == ENET_EVENT_TYPE_RECEIVE)
      {
        if (!event.packet)
        {
          continue;
        }

        const Message* msg = reinterpret_cast<Message*>(event.packet->data);
        handleMessage(msg);
        enet_packet_destroy(event.packet);
      }
      else if (event.type == ENET_EVENT_TYPE_DISCONNECT)
      {
        needs_reconnect = true;
        break;
      }
      else if (event.type == ENET_EVENT_TYPE_CONNECT)
      {
        IdentifyPayload payload{};
        std::string id = std::string(players->getLocalPlayerState()->id);
        std::strncpy(payload.id, id.c_str(), sizeof(payload.id) - 1);
        std::strncpy(payload.name, id.c_str(), sizeof(payload.name) - 1);

        sendMessage(peer, MessageType::IDENTIFY, &payload, sizeof(payload));
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

    if (needs_reconnect) {
      peer = enet_host_connect(client, &address, 2, 0);
      std::cout << "Reconnecting.." << std::endl;
      needs_reconnect = false;
    }
  }
}

void Client::handleMessage(const Message* msg)
{
  switch (static_cast<MessageType>(msg->type))
  {
  case MessageType::SPAWN_ACCEPTED:
    handleSpawnAccepted(reinterpret_cast<const SpawnData*>(msg->data));
    break;
  case MessageType::PLAYER_UPDATE:
    handlePlayerUpdate(reinterpret_cast<const PlayerUpdatePayload*>(msg->data));
    break;
  case MessageType::WORLD_UPDATE:
    handleWorldUpdate(*reinterpret_cast<const std::vector<AddrUpdate>*>(msg->data));
  default:
    break;
  }
}

void Client::handleWorldUpdate(const std::vector<AddrUpdate> updates)
{
  for (const auto& update : updates)
  {
    auto& s = s_world_snapshot.snapshot[update.addr];
    s.val = update.val;
    s.dirty = true;
  }
}

void Client::handleSpawnAccepted(const SpawnData* data)
{
  //   u32 players_addr = symbolDb().GetSymbolFromName("s_acmp_players_list")->address;
  //   u32 local_player_addr = PowerPC::MMU::HostRead_U32(guard, players_addr);
  //   writePositionAngle(guard, player.data.world_position, player_addr + 0x028);
}

void Client::handlePlayerUpdate(const PlayerUpdatePayload* update)
{
  players->updatePlayer(*update);
}

void Client::frameAdvance(const Core::CPUThreadGuard& guard)
{
  std::stringstream ss;
  ss << "CLIENT\n";

  sync_game_memory(guard, *players);
  apply_world_snapshot(guard);

  for (auto& player : players->getRemotePlayers())
  {
    ss << fmt::format("Player {}:\n {}, {}, {}\n", std::string(player.state.id),
                      player.state.world_position.position.x,
                      player.state.world_position.position.y,
                      player.state.world_position.position.z);
  }

  u32 local_player_addr =
      PowerPC::MMU::HostRead_U32(guard, symbolDb().GetSymbolFromName("s_primary_player")->address);
  PlayerUpdatePayload* local_state = players->getLocalPlayerState();

  sendMessage(peer, MessageType::PLAYER_UPDATE, local_state, sizeof(PlayerUpdatePayload));

  ss << fmt::format("Local Player ({}):\n {}, {}, {}\n", local_player_addr,
                    local_state->world_position.position.x, local_state->world_position.position.y,
                    local_state->world_position.position.z);

  DebugText = ss.str();
}

}  // namespace ACMP