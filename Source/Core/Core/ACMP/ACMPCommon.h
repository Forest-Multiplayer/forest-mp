#pragma once

#include <Common/CommonTypes.h>

#include <unordered_map>
#include <mutex>
#include <vector>
#include <iostream>
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
  PLAYER_UPDATE = 4,
  WORLD_UPDATE = 5,
};

#define MSG_SZ 1 + 2
struct Message
{
  uint8_t type;
  uint16_t sz;
};

struct IdentifyPayload
{
  std::string id;
  std::string name;
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
  std::string id;
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

struct SyncVal {
  bool dirty;
  u32 val;
};

struct AddrUpdate {
  u32 addr;
  u32 val;
};

struct WorldSnapshot {
  std::map<u32, SyncVal> snapshot;
};

struct PendingPacket {
  ENetPeer* peer;
  std::vector<uint8_t> data;
};

struct WorldSyncPayload {
  uint16_t len;
  std::vector<AddrUpdate> updates;
};

extern std::vector<PendingPacket> s_msg_queue;
extern std::mutex s_msg_queue_mutex;
extern WorldSnapshot s_world_snapshot;
extern std::mutex s_world_snapshot_mutex;

void sendMessage(ENetPeer* peer, MessageType type, const void* data, size_t size);
void sync_game_memory(const Core::CPUThreadGuard& guard, Playerlist& players);
void readPositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr);
void writePositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr);

void readSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr);
void writeSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr);

void record_world_snapshot(const Core::CPUThreadGuard& guard);
void apply_world_snapshot(const Core::CPUThreadGuard& guard);

void serialize_player_update(const PlayerUpdatePayload& update, std::vector<uint8_t>& buffer);
void serialize_world_update(const WorldSyncPayload& updates, std::vector<uint8_t>& buffer);
void serialize_identify(const IdentifyPayload& id, std::vector<uint8_t>& buffer);

void deserialize_player_update(std::vector<uint8_t>& buffer, PlayerUpdatePayload& update);
void deserialize_world_update(std::vector<uint8_t>& buffer, WorldSyncPayload& updates);
void deserialize_identify(std::vector<uint8_t>& buffer, IdentifyPayload& id);
}  // namespace ACMP
