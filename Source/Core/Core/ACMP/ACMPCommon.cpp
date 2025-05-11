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

WorldSnapshot s_world_snapshot;
std::mutex s_world_snapshot_mutex;

std::string DebugText;

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

void sendMessage(ENetPeer* peer, MessageType type, std::vector<uint8_t> data, size_t size)
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

void sync_game_memory(const Core::CPUThreadGuard& guard, Playerlist& players)
{
  u32 players_addr = symbolDb().GetSymbolFromName("s_acmp_players_list")->address;
  u32 idx = 0;
  for (auto& player : players.getRemotePlayers())
  {
    u32 player_addr = PowerPC::MMU::HostRead_U32(guard, players_addr + (idx * 0x4));

    writePositionAngle(guard, player.state.world_position, player_addr + 0x028);
    writePositionAngle(guard, player.state.eye_position, player_addr + 0x048);

    writeSXyz(guard, player.state.shape_angle, player_addr + 0x0DC);

    PowerPC::MMU::HostWrite_F32(guard, player.state.velocity[0], player_addr + 0x068);
    PowerPC::MMU::HostWrite_F32(guard, player.state.velocity[1], player_addr + 0x06C);
    PowerPC::MMU::HostWrite_F32(guard, player.state.velocity[2], player_addr + 0x070);
    PowerPC::MMU::HostWrite_F32(guard, player.state.speed, player_addr + 0x074);
    PowerPC::MMU::HostWrite_U32(guard, player.state.stateBitfield, player_addr + 0x020);

    PowerPC::MMU::HostWrite_U8(guard, player.state.block_x, player_addr + 0x008);
    PowerPC::MMU::HostWrite_U8(guard, player.state.block_y, player_addr + 0x009);

    PowerPC::MMU::HostWrite_U32(guard, player.state.requested_main_index, player_addr + 0x0D08);
    PowerPC::MMU::HostWrite_U32(guard, player.state.requested_main_index_priority,
                                player_addr + 0x0D0C);
    PowerPC::MMU::HostWrite_U32(guard, player.state.requested_main_index_changed,
                                player_addr + 0x0D10);

    PowerPC::MMU::HostWrite_U32(guard, player.state.animation0_idx, player_addr + 0x0DB4);
    PowerPC::MMU::HostWrite_U32(guard, player.state.animation1_idx, player_addr + 0x0DB8);
    PowerPC::MMU::HostWrite_U32(guard, player.state.part_table_idx, player_addr + 0x0DBC);

    u32 move_func = symbolDb().GetSymbolFromName("acmp_primary_move_hook")->address;
    if (move_func)
    {
      PowerPC::MMU::HostWrite_U32(guard, move_func, player_addr + 0x164);
    }

    idx++;
  }

  u32 local_player_addr =
      PowerPC::MMU::HostRead_U32(guard, symbolDb().GetSymbolFromName("s_primary_player")->address);
  PlayerUpdatePayload* local_state = players.getLocalPlayerState();

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
  std::lock_guard<std::mutex> lk(s_world_snapshot_mutex);
  for (u32 addr = MOD_HEAP_BASE; addr < MOD_HEAP_BASE + MOD_HEAP_SIZE; addr += 0x4) {
    u32 val = PowerPC::MMU::HostRead_U32(guard, addr);
    SyncVal& s = s_world_snapshot.snapshot[addr];
    if (s.val != val) {
      s.val = val;
      s.dirty = true;
    }
  }
}

void apply_world_snapshot(const Core::CPUThreadGuard& guard) {
  std::lock_guard<std::mutex> lk(s_world_snapshot_mutex);
  for (auto& update : s_world_snapshot.snapshot) {
    if (!update.second.dirty)
      continue;

    PowerPC::MMU::HostWrite_U32(guard, update.second.val, update.first);
    update.second.dirty = false;
  }
}

void serialize_player_update(const PlayerUpdatePayload& update, std::vector<uint8_t>& buffer) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive oarchive(ss);
    oarchive(update.id,
             update.animation0_idx,
             update.animation1_idx,
             update.part_table_idx,
             update.requested_main_index,
             update.requested_main_index_priority,
             update.requested_main_index_changed,
             update.stateBitfield,
             update.block_x,
             update.block_y,
             update.velocity,
             update.speed,
             update.world_position,
             update.eye_position,
             update.shape_angle);

  }

  std::string str_buffer = ss.str();
  buffer.resize(str_buffer.size());
  memcpy(buffer.data(), str_buffer.data(), str_buffer.size());
}

void serialize_world_update(const WorldSyncPayload& updates, std::vector<uint8_t>& buffer) {

}

void serialize_identify(const IdentifyPayload& id, std::vector<uint8_t>& buffer) {
  std::stringstream ss;
  {
    cereal::BinaryOutputArchive oarchive(ss);
    oarchive(id.id, id.name);
  }

  std::string str_buffer = ss.str();
  buffer.resize(str_buffer.size());
  memcpy(buffer.data(), str_buffer.data(), str_buffer.size());
}

void deserialize_player_update(std::vector<uint8_t>& buffer, PlayerUpdatePayload& update) {
  std::stringstream ss;
  ss.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  ss.flush();

  cereal::BinaryInputArchive iarchive(ss);
  iarchive(update.id,
            update.animation0_idx,
            update.animation1_idx,
            update.part_table_idx,
            update.requested_main_index,
            update.requested_main_index_priority,
            update.requested_main_index_changed,
            update.stateBitfield,
            update.block_x,
            update.block_y,
            update.velocity,
            update.speed,
            update.world_position,
            update.eye_position,
            update.shape_angle);
}

void deserialize_world_update(std::vector<uint8_t>& buffer, WorldSyncPayload& updates) {

}

void deserialize_identify(std::vector<uint8_t>& buffer, IdentifyPayload& id) {
  std::stringstream ss;
  ss.write(reinterpret_cast<const char*>(buffer.data()), buffer.size());
  ss.flush();

  cereal::BinaryInputArchive oarchive(ss);
  oarchive(id.id, id.name);
}
}  // namespace ACMP