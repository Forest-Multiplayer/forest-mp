#pragma once

#include <Common/CommonTypes.h>
#include <enet/enet.h>

#include <unordered_map>

#define MOD_HEAP_BASE 0x81808000
#define MOD_HEAP_SIZE 0x818FFFFF - MOD_HEAP_BASE  // ~1MB
#define MOD_SYNC_BUFFER_SZ 65000                  // 100000 / 2
#define MAX_PLAYERS 2                             // for now

namespace ACMP
{

using f32 = float;
using s16 = int16_t;

static constexpr size_t kPlayerIdSize = 64;

struct xyz_t
{
  f32 x, y, z;
};

struct s_xyz
{
  s16 x, y, z;
};

struct PositionAngle
{
  xyz_t position;
  s_xyz angle;
};

void writePositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr)
{
  PowerPC::MMU::HostWrite_F32(guard, pos.position.x, base_addr + 0x000);
  PowerPC::MMU::HostWrite_F32(guard, pos.position.y, base_addr + 0x004);
  PowerPC::MMU::HostWrite_F32(guard, pos.position.z, base_addr + 0x008);
  PowerPC::MMU::HostWrite_U16(guard, pos.angle.x, base_addr + 0x00C);
  PowerPC::MMU::HostWrite_U16(guard, pos.angle.y, base_addr + 0x010);
  PowerPC::MMU::HostWrite_U16(guard, pos.angle.z, base_addr + 0x014);
}

void readPositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr)
{
  pos.position.x = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x000);
  pos.position.y = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x004);
  pos.position.z = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x008);
  pos.angle.x = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x00C);
  pos.angle.y = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x010);
  pos.angle.z = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x014);
}

enum class MessageType : uint8_t
{
  IDENTIFY = 0,
  CHAT = 1,
  SPAWN_REQUEST = 2,
  SPAWN_ACCEPTED = 3,
  PLAYER_UPDATE = 4
};

struct Message
{
  uint8_t type;
  uint8_t data[254];
};

struct IdentifyPayload
{
  char id[64];
  char name[128];
};

struct SpawnData
{
  float x;
  float y;
  float z;
  float rotation;
};

struct PlayerUpdatePayload
{
  char id[kPlayerIdSize];
  PositionAngle world_position;
  PositionAngle eye_position;
  f32 velocity[3];
  f32 speed;
  uint32_t stateBitfield;
  int32_t requested_main_index;
  int32_t requested_main_index_priority;
  int32_t requested_main_index_changed;
};

inline void sendMessage(ENetPeer* peer, MessageType type, const void* data, size_t size)
{
  Message msg;
  msg.type = static_cast<uint8_t>(type);
  std::memset(msg.data, 0, sizeof(msg.data));
  std::memcpy(msg.data, data, size);

  ENetPacket* packet = enet_packet_create(&msg, sizeof(Message), ENET_PACKET_FLAG_RELIABLE);
  enet_peer_send(peer, 0, packet);
  enet_host_flush(peer->host);
}
}  // namespace ACMP
