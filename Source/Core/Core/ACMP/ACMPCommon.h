#pragma once

#include <Common/CommonTypes.h>
#include <unordered_map>
#include <mutex>
#include <enet/enet.h>

#include "Core/PowerPC/PPCSymbolDB.h"

#define SYMBOL_DB 

#define MOD_HEAP_BASE 0x81808000
#define MOD_HEAP_SIZE 0x818FFFFF - MOD_HEAP_BASE  // ~1MB
#define MOD_SYNC_BUFFER_SZ 65000                  // 100000 / 2
#define MAX_PLAYERS 2                             // for now

namespace ACMP
{

class Playerlist;

extern std::string DebugText; 
PPCSymbolDB& symbolDb();

// Player ID size constraint
static constexpr size_t kPlayerIdSize = 16;


using f32 = float;
using s16 = int16_t;

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

#pragma pack(1)
struct PlayerUpdatePayload
{
  char id[kPlayerIdSize];
  PositionAngle world_position;
  PositionAngle eye_position;
  f32 velocity[3];
  f32 speed;
  s_xyz shape_angle;
  s8 block_x;
  s8 block_y;
  uint32_t stateBitfield;
  int32_t requested_main_index;
  int32_t requested_main_index_priority;
  int32_t requested_main_index_changed;
  int32_t animation0_idx;
  int32_t animation1_idx;
  int32_t part_table_idx;
};

struct PendingPacket {
  ENetPeer* peer;
  std::vector<uint8_t> data;
};

extern std::vector<PendingPacket> s_msg_queue;
extern std::mutex s_msg_queue_mutex;

void sendMessage(ENetPeer* peer, MessageType type, const void* data, size_t size);
void sync_game_memory(const Core::CPUThreadGuard& guard, Playerlist& players);
void readPositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr);
void writePositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr);

void readSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr);
void writeSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr);

}  // namespace ACMP
