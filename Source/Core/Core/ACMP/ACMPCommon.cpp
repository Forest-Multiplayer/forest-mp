#include "ACMPCommon.h"

#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

#include "Playerlist.h"

#include <cereal/archives/binary.hpp>
#include <cereal/types/map.hpp>
#include <cereal/types/vector.hpp>
#include <cereal/types/string.hpp>
#include <cereal/types/complex.hpp>

namespace ACMP
{
std::vector<PendingPacket> s_msg_queue;
std::mutex s_msg_queue_mutex;

WorldSnapshot s_dirty_snapshot;
std::mutex s_dirty_snapshot_mutex;

std::vector<u32> s_snapshot_addresses(0x3000000);
std::mutex s_world_snapshot_mutex;

u32 s_rel_base = 0;
u32 s_acmp_players_list = 0;
u32 s_malloc_entries_len = 0;
u32 s_malloc_entries = 0;
u32 s_primary_player = 0;
u32 s_actor_info = 0;
bool s_mod_ready = false;

std::string DebugText;

void mod_post_init(const Core::CPUThreadGuard& guard) {
  s_rel_base = symbolDb().GetSymbolFromName("s_rel_base")->address;
  s_acmp_players_list = symbolDb().GetSymbolFromName("s_acmp_players_list")->address;
  s_malloc_entries_len = symbolDb().GetSymbolFromName("s_malloc_entries_len")->address;
  s_malloc_entries = symbolDb().GetSymbolFromName("s_malloc_entries")->address;
  s_primary_player = symbolDb().GetSymbolFromName("s_primary_player")->address; 
  s_actor_info = symbolDb().GetSymbolFromName("s_actor_info")->address;
  s_mod_ready = true;
}

PPCSymbolDB& symbolDb()
{
  return Core::System::GetInstance().GetPowerPC().GetSymbolDB();
}

void writePositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr)
{
  PowerPC::MMU::HostWrite_F32(guard, pos.position.x, base_addr + 0x000);
  PowerPC::MMU::HostWrite_F32(guard, pos.position.y, base_addr + 0x004);
  PowerPC::MMU::HostWrite_F32(guard, pos.position.z, base_addr + 0x008);
  writeSXyz(guard, pos.angle, base_addr + 0x00C);
}

void readPositionAngle(const Core::CPUThreadGuard& guard, PositionAngle& pos, u32 base_addr)
{
  pos.position.x = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x000);
  pos.position.y = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x004);
  pos.position.z = PowerPC::MMU::HostRead_F32(guard, base_addr + 0x008);
  readSXyz(guard, pos.angle, base_addr + 0x00C);
}

void writeSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr)
{
  PowerPC::MMU::HostWrite_U16(guard, xyz.x, base_addr + 0x000);
  PowerPC::MMU::HostWrite_U16(guard, xyz.y, base_addr + 0x002);
  PowerPC::MMU::HostWrite_U16(guard, xyz.z, base_addr + 0x004);
}

void readSXyz(const Core::CPUThreadGuard& guard, s_xyz& xyz, u32 base_addr)
{
  xyz.x = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x000);
  xyz.y = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x002);
  xyz.z = PowerPC::MMU::HostRead_U16(guard, base_addr + 0x004);
}

void sendMessage(ENetPeer* peer, MessageType type, std::vector<uint8_t>& data)
{
  if (!peer || peer->state != ENET_PEER_STATE_CONNECTED)
  {
    return;
  }

  data.emplace(data.begin(), static_cast<uint8_t>(type));

  PendingPacket pending;
  pending.peer = peer;
  pending.data = data;

  std::lock_guard<std::mutex> lock(s_msg_queue_mutex);
  s_msg_queue.push_back(pending);
}

void sync_game_memory(const Core::CPUThreadGuard& guard, Playerlist& player_list)
{
  u32 idx = 0;

  auto& players = player_list.getRemotePlayers();
  for (auto& player : player_list.getRemotePlayers())
  {
    u32 player_addr = PowerPC::MMU::HostRead_U32(guard, s_acmp_players_list + (idx * 0x4));

    writePositionAngle(guard, player->state.world_position, player_addr + 0x028);
    writePositionAngle(guard, player->state.eye_position, player_addr + 0x048);

    writeSXyz(guard, player->state.shape_angle, player_addr + 0x0DC);

    PowerPC::MMU::HostWrite_F32(guard, player->state.velocity[0], player_addr + 0x068);
    PowerPC::MMU::HostWrite_F32(guard, player->state.velocity[1], player_addr + 0x06C);
    PowerPC::MMU::HostWrite_F32(guard, player->state.velocity[2], player_addr + 0x070);
    PowerPC::MMU::HostWrite_F32(guard, player->state.speed, player_addr + 0x074);
    PowerPC::MMU::HostWrite_U32(guard, player->state.stateBitfield, player_addr + 0x020);

    PowerPC::MMU::HostWrite_U8(guard, player->state.block_x, player_addr + 0x008);
    PowerPC::MMU::HostWrite_U8(guard, player->state.block_y, player_addr + 0x009);

    PowerPC::MMU::HostWrite_U32(guard, player->state.requested_main_index, player_addr + 0x0D08);
    PowerPC::MMU::HostWrite_U32(guard, player->state.requested_main_index_priority,
                                player_addr + 0x0D0C);
    PowerPC::MMU::HostWrite_U32(guard, player->state.requested_main_index_changed,
                                player_addr + 0x0D10);

    PowerPC::MMU::HostWrite_U32(guard, player->state.animation0_idx, player_addr + 0x0DB4);
    PowerPC::MMU::HostWrite_U32(guard, player->state.animation1_idx, player_addr + 0x0DB8);
    PowerPC::MMU::HostWrite_U32(guard, player->state.part_table_idx, player_addr + 0x0DBC);

    u32 move_func = symbolDb().GetSymbolFromName("acmp_primary_move_hook")->address;
    if (move_func)
    {
      PowerPC::MMU::HostWrite_U32(guard, move_func, player_addr + 0x164);
    }

    idx++;
  }

  u32 local_player_addr = PowerPC::MMU::HostRead_U32(guard, s_primary_player);
  PlayerUpdatePayload* local_state = player_list.getLocalPlayerState();

  readPositionAngle(guard, local_state->world_position, local_player_addr + 0x028);
  readPositionAngle(guard, local_state->eye_position, local_player_addr + 0x048);

  readSXyz(guard, local_state->shape_angle, local_player_addr + 0x0DC);

  local_state->velocity[0] = PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x068);
  local_state->velocity[1] = PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x06C);
  local_state->velocity[2] = PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x070);
  local_state->speed = PowerPC::MMU::HostRead_F32(guard, local_player_addr + 0x074);
  local_state->stateBitfield = PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x020);
  local_state->block_x = PowerPC::MMU::HostRead_U8(guard, local_player_addr + 0x008);
  local_state->block_y = PowerPC::MMU::HostRead_U8(guard, local_player_addr + 0x009);

  local_state->requested_main_index = PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D08);
  local_state->requested_main_index_priority =
      PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D0C);
  local_state->requested_main_index_changed =
      PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0D10);

  local_state->animation0_idx = PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0DB4);
  local_state->animation1_idx = PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0DB8);
  local_state->part_table_idx = PowerPC::MMU::HostRead_U32(guard, local_player_addr + 0x0DBC);
}

void record_world_snapshot(const Core::CPUThreadGuard& guard) {
  std::unordered_map<u32, u32> allocations_table;
  u32 entries_base = s_malloc_entries;
  u32 len = PowerPC::MMU::HostRead_U32(guard, s_malloc_entries_len);
  for (u32 i = 0; i < len; i++) {
    u32 base =
        PowerPC::MMU::HostRead_U32(guard, entries_base + (i * 0x8));
    if (base == 0x0) {
      continue;
    }

    u32 size = PowerPC::MMU::HostRead_U32(guard, entries_base + (i * 0x8) + 0x4);
    allocations_table.emplace(base, size);
  }

  std::lock_guard<std::mutex> lk(s_world_snapshot_mutex);
  u32 actor = 0x0;

  u32 actor_list = PowerPC::MMU::HostRead_U32(guard, s_actor_info);
  if (actor_list == 0x0) {
    return;
  }

  u32 addresses_scanned = 0;
  u32 entity_count = 0;
  for (int i = 0; i < ACTOR_PART_NUM; i++) {
    if (i == 3 /* Player */) {
      continue;
    }

    actor = PowerPC::MMU::HostRead_U32(guard, actor_list + 0x8 + (i * 0x8));
    addresses_scanned++;
    
    while (actor != 0) {
      auto entry = allocations_table.find(actor);

      if (entry == allocations_table.end()) {
        // make sure clients know to delete it
        s_snapshot_addresses[(actor - 0x80000000) + 0x15c] = 0x0;
      } else {
        for (u32 offset = 0x0; offset < entry->second; offset += 0x4) {
          u32 val = PowerPC::MMU::HostRead_U32(guard, actor + offset);
          u32& last_val = s_snapshot_addresses[actor + offset - 0x80000000];
          if (last_val != val) {
            std::lock_guard<std::mutex> lk(s_dirty_snapshot_mutex);
            s_dirty_snapshot.dirty_addresses.push_back({actor + offset, val});
          }

          s_snapshot_addresses[actor + offset - 0x80000000] = val;
          addresses_scanned++;
        }
      }

      actor = PowerPC::MMU::HostRead_U32(guard, actor + 0x158);
      addresses_scanned++;
      entity_count++;
    }
  }

  std::cout << addresses_scanned << " addresses scanned over " << entity_count << " entitites\n";
}

void apply_world_snapshot(const Core::CPUThreadGuard& guard) {
  std::lock_guard<std::mutex> lk(s_world_snapshot_mutex);
  for (auto& update : s_dirty_snapshot.dirty_addresses) {
    PowerPC::MMU::HostWrite_U32(guard, update.val, update.addr);
  }

  s_dirty_snapshot.dirty_addresses.clear();
}

void serialize_player_update(const PlayerUpdatePayload& update, std::vector<uint8_t>& buffer) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive oarchive(ss);
    oarchive(update);
  }

  std::string str_buffer = ss.str();
  buffer.resize(str_buffer.size());
  memcpy(buffer.data(), str_buffer.data(), str_buffer.size());
}

void serialize_world_update(const WorldSyncPayload& updates, std::vector<uint8_t>& buffer) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive oarchive(ss);
    oarchive(updates);
  }

  std::string str_buffer = ss.str();
  buffer.resize(str_buffer.size());
  memcpy(buffer.data(), str_buffer.data(), str_buffer.size());
}

void serialize_identify(const IdentifyPayload& id, std::vector<uint8_t>& buffer) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive oarchive(ss);
    oarchive(id);
  }

  std::string str_buffer = ss.str();
  buffer.resize(str_buffer.size());
  memcpy(buffer.data(), str_buffer.data(), str_buffer.size());
}

void deserialize_player_update(const u8* buffer, size_t buffer_len, PlayerUpdatePayload& update) {
  std::stringstream ss;
  ss.write(reinterpret_cast<const char*>(buffer), buffer_len);
  ss.flush();

  cereal::BinaryInputArchive iarchive(ss);
  iarchive(update);
}

void deserialize_world_update(const u8* buffer, size_t buffer_len, WorldSyncPayload& updates) {
  std::stringstream ss;
  ss.write(reinterpret_cast<const char*>(buffer), buffer_len);
  ss.flush();

  cereal::BinaryInputArchive iarchive(ss);
  iarchive(updates);
}

void deserialize_identify(const u8* buffer, size_t buffer_len, IdentifyPayload& id) {
  std::stringstream ss;
  ss.write(reinterpret_cast<const char*>(buffer), buffer_len);
  ss.flush();

  cereal::BinaryInputArchive oarchive(ss);
  oarchive(id);
}
}  // namespace ACMP