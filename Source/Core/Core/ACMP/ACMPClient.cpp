#include "ACMPClient.h"
#include "ACMP.h"

#include <Common/Assert.h>
#include <Core/Config/MainSettings.h>
#include <Core/PowerPC/MMU.h>

namespace ACMP
{
void ACMPClient::connect_to_sync_server()
{
  struct sockaddr_in server_addr;

  if ((m_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    ASSERT_MSG(CORE, 0, "Could not establish client. Is the port already in use?");
    return;
  }

  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = inet_addr(Config::Get(Config::ACMP_IP).c_str());
  server_addr.sin_port = htons(Config::Get(Config::ACMP_PORT));

  m_recv_thread = std::thread([=] { recv_task(); });
  m_send_thread = std::thread([=] { sender_task(); });
}

void ACMPClient::update_outbound_buffer(const Core::CPUThreadGuard& guard)
{
  std::unique_lock<std::mutex> lk(m_outbound_mutex);
  u32 host_player_addr =
      PowerPC::MMU::HostRead_U32(guard, s_symbolDB.GetSymbolFromName("s_primary_player")->address);

  for (uint32_t i = 0; i < 0x126c; i += 4)
  {
    if ((i > 0x394 && i < 0xa18) || (i > 0xcf4 && i < 0xda08))
    {
      continue;
    }

    u32 address = host_player_addr + i;
    u32 val = PowerPC::MMU::HostRead_U32(guard, address);
    if (m_player_snapshot[i] != val)
    {
      m_player_updates.push_back(AddrUpdate{address, val});
      m_player_snapshot[i] = val;
    }
  }
}

void ACMPClient::write_inbound_updates(const Core::CPUThreadGuard& guard)
{
  u32 players_addr = s_symbolDB.GetSymbolFromName("s_acmp_players_list")->address;
  std::lock_guard<std::mutex> lk(m_inbound_mutex);
  for (int i = 0; i < MAX_PLAYERS; i++)
  {
    u32 player = PowerPC::MMU::HostRead_U32(guard, players_addr + i); 
    if (player)
    {
      for (auto entry : m_inbound_player_updates[i])
      {
        PowerPC::MMU::HostWrite_U32(guard, entry.value, player + entry.addr);
      }

      m_inbound_player_updates[i].clear();
    }
  }

  for (auto entry : m_inbound_world_updates)
  {
    PowerPC::MMU::HostWrite_U32(guard, entry.value, entry.addr);
  }

  m_inbound_world_updates.clear();
}

void ACMPClient::update(const Core::CPUThreadGuard& guard)
{
  update_outbound_buffer(guard);
  write_inbound_updates(guard);
}

void ACMPClient::recv_task()
{
  char buffer[MOD_SYNC_BUFFER_SZ];
  while (m_sockfd)
  {
    recvfrom(m_sockfd, buffer, MOD_SYNC_BUFFER_SZ, 0, (struct sockaddr*) &m_host_addr, nullptr);
    std::lock_guard<std::mutex> lk(m_inbound_mutex);

    switch (static_cast<PacketType>(buffer[0]))
    {
    case kPlayerUpdate:
    {
      PlayerUpdate* update = (PlayerUpdate*) buffer;
      if (update->count > MOD_SYNC_BUFFER_SZ)
      {
        ASSERT_MSG(CORE, 0, "Received invalid payload size from client");
        break;
      }

      for (int i = 0; i < update->count; i++)
      {
        m_inbound_player_updates[update->player].push_back(
            *(AddrUpdate*)&buffer[sizeof(PlayerUpdate) + (i * sizeof(AddrUpdate))]);
      }

      break;
    }
    case kWorldUpdate:
    {
      WorldUpdate* update = (WorldUpdate*) buffer;
      if (update->count > MOD_SYNC_BUFFER_SZ)
      {
        ASSERT_MSG(CORE, 0, "Received invalid payload size from client");
        break;
      }

      for (int i = 0; i < update->count; i++)
      {
        m_inbound_world_updates.push_back(
            *(AddrUpdate*)&buffer[sizeof(PlayerUpdate) + (i * sizeof(AddrUpdate))]);
      }

      break;
    }
    default:
      break;
    }
  }
}

void ACMPClient::sender_task()
{
  std::unique_lock<std::mutex> lk(m_outbound_mutex);
  while (m_sockfd)
  {
    PlayerUpdate player_update;
    player_update.type = kPlayerUpdate;
    player_update.player = 1;
    player_update.count = (u16) m_player_updates.size();

    for (auto entry : m_player_updates)
    {
      sendto(m_sockfd, (char*)&player_update, sizeof(player_update), 0, (sockaddr*)&m_host_addr,
             sizeof(m_host_addr));
      sendto(m_sockfd, (char*)m_player_updates.data(), player_update.count * sizeof(AddrUpdate), 0,
             (struct sockaddr*)&m_host_addr, sizeof(m_host_addr));
    }

    m_player_updates.clear();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(33ms);
  }
}
}
