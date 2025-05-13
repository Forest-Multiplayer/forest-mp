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
#define MOD_EXT_ARENA_SIZE 150000
#define MOD_SYNC_BUFFER_SZ 65000                  // 100000 / 2
#define MAX_PLAYERS 2                             // for now
#define ACTOR_PART_NUM 8

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
  std::unordered_map<u32, SyncVal> snapshot;
};

struct PendingPacket {
  ENetPeer* peer;
  std::vector<uint8_t> data;
};

struct WorldSyncPayload {
  std::vector<AddrUpdate> updates;
};

template <class Archive>
void serialize(Archive& ar, AddrUpdate& v)
{
    ar(v.addr, v.val);
}


template <class Archive>
void serialize(Archive& ar, WorldSyncPayload& v)
{
    ar(v.updates);
}

template <class Archive>
void serialize(Archive& ar, IdentifyPayload& v)
{
    ar(v.id, v.name);
}

template <class Archive>
void serialize(Archive& ar, xyz_t& v)
{
    ar(v.x, v.y, v.z);
}

template <class Archive>
void serialize(Archive& ar, s_xyz& v)
{
    ar(v.x, v.y, v.z);
}

template <class Archive>
void serialize(Archive& ar, PositionAngle& v)
{
    ar(v.position, v.angle);
}

template <class Archive>
void serialize(Archive& ar, PlayerUpdatePayload& v)
{
    ar(v.id,
       v.world_position,
       v.eye_position,
       v.velocity,
       v.speed,
       v.shape_angle,
       v.block_x,
       v.block_y,
       v.stateBitfield,
       v.requested_main_index,
       v.requested_main_index_priority,
       v.requested_main_index_changed,
       v.animation0_idx,
       v.animation1_idx,
       v.part_table_idx);
}

extern std::vector<PendingPacket> s_msg_queue;
extern std::mutex s_msg_queue_mutex;
extern WorldSnapshot s_world_snapshot;
extern std::mutex s_world_snapshot_mutex;

extern u32 s_rel_base;
extern u32 s_acmp_players_list;
extern u32 s_malloc_entries_len;
extern u32 s_malloc_entries;
extern u32 s_primary_player;
extern u32 s_actor_info;
extern bool s_mod_ready;

void mod_post_init(const Core::CPUThreadGuard& guard);

void sendMessage(ENetPeer* peer, MessageType type, std::vector<uint8_t>& data);
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

void deserialize_player_update(const u8* buffer, size_t buffer_len, PlayerUpdatePayload& update);
void deserialize_world_update(const u8* buffer, size_t buffer_len, WorldSyncPayload& updates);
void deserialize_identify(const u8* buffer, size_t buffer_len, IdentifyPayload& id);
}  // namespace ACMP
