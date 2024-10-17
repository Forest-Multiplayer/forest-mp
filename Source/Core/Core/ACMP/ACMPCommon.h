#pragma once

#include <Common/CommonTypes.h>

#define MOD_HEAP_BASE 0x81808000
#define MOD_HEAP_SIZE 0x818FFFFF - MOD_HEAP_BASE // ~1MB
#define MOD_SYNC_BUFFER_SZ 65000 // 100000 / 2
#define MAX_PLAYERS 2 // for now

namespace ACMP {
enum PacketType : u8
{
  kJoinReq,
  kJoinAcc,
  kPlayerUpdate,
  kWorldUpdate,
};

struct JoinRequest
{
  PacketType type;
  char username[16];
  char uuid[16];
};

struct JoinAccept
{
  PacketType type;
  u8 player_count;
  // followed by the list of PlayerInfo
};

struct PlayerInfo
{
  char username[16];
  bool is_host;
  s8 block_x;
  s8 block_y;
  float pos_x, pos_y, pos_z;
  s16 rot_x, rot_y, rot_z;
  // todo, costume info, item being held, etc
};

struct AddrUpdate
{
  u32 addr;
  u32 value;
};

struct PlayerUpdate
{
  PacketType type;
  u8 player;
  u16 count;
  // followed by the list of AddrUpdates
};

struct WorldUpdate
{
  PacketType type;
  // u8 area; 
  u16 count;
  // followed by the list of AddrUpdates
};
}
