#include "ACMPClient.h"
#include "ACMP.h"

#include <Common/Assert.h>
#include <Core/Config/MainSettings.h>
#include <Core/PowerPC/MMU.h>
#include <enet/enet.h>
#include <iostream>
#include <cstring>

namespace ACMP {
bool Client::connect(const std::string& host, uint16_t port, const std::string& id) {
    if (enet_initialize() != 0) return false;

    client = enet_host_create(nullptr, 1, 2, 0, 0);
    if (!client) return false;

    ENetAddress address;
    enet_address_set_host(&address, host.c_str());
    address.port = port;

    peer = enet_host_connect(client, &address, 2, 0);
    if (!peer) return false;

    ENetEvent event;
    if (enet_host_service(client, &event, 5000) > 0 &&
        event.type == ENET_EVENT_TYPE_CONNECT) {
        IdentifyPayload payload{};
        std::strncpy(payload.id, id.c_str(), sizeof(payload.id) - 1);
        std::strncpy(payload.name, id.c_str(), sizeof(payload.name) - 1); // use id as name for now
        sendMessage(peer, MessageType::IDENTIFY, &payload, sizeof(payload));

        players.setLocalPlayerId(id);
        return true;
    }

    return false;
}

void Client::disconnect() {
    stop();
    if (peer) enet_peer_disconnect(peer, 0);
    if (client) {
        enet_host_destroy(client);
        client = nullptr;
        enet_deinitialize();
    }
}

void Client::sendPlayerUpdate(const PlayerUpdatePayload& update) {
    if (peer) {
        sendMessage(peer, MessageType::PLAYER_UPDATE, &update, sizeof(update));
    }
}

void Client::start() {
    if (!client || running) return;
    running = true;
    pollThread = std::thread(&Client::pollLoop, this);
}

void Client::stop() {
    running = false;
    if (pollThread.joinable()) pollThread.join();
}

void Client::pollLoop() {
    ENetEvent event;
    while (running) {
        while (enet_host_service(client, &event, 10) > 0) {
            if (event.type == ENET_EVENT_TYPE_RECEIVE) {
                const Message* msg = reinterpret_cast<Message*>(event.packet->data);
                handleMessage(msg);
                enet_packet_destroy(event.packet);
            }
        }
    }
}

void Client::handleMessage(const Message* msg) {
    switch (static_cast<MessageType>(msg->type)) {
        case MessageType::SPAWN_ACCEPTED:
            handleSpawnAccepted(reinterpret_cast<const SpawnData*>(msg->data));
            break;
        case MessageType::PLAYER_UPDATE:
            handlePlayerUpdate(reinterpret_cast<const PlayerUpdatePayload*>(msg->data));
            break;
        default:
            break;
    }
}

void Client::handleSpawnAccepted(const SpawnData* data) {
//   u32 players_addr = s_symbolDB.GetSymbolFromName("s_acmp_players_list")->address;
//   u32 local_player_addr = PowerPC::MMU::HostRead_U32(guard, players_addr);
//   writePositionAngle(guard, player.data.world_position, player_addr + 0x028); 
}

void Client::handlePlayerUpdate(const PlayerUpdatePayload* update) {
    players.updateFromPayload(*update);
}

void Client::frameAdvance(const Core::CPUThreadGuard& guard) {
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

  sendPlayerUpdate(players.getLocalPlayerState().data);
}

} // namespace ACMP