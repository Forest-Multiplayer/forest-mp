#include "ACMPClient.h"
#include "ACMP.h"

#include <Common/Assert.h>
#include <Core/Config/MainSettings.h>
#include <Core/PowerPC/MMU.h>

namespace ACMP
{
void ACMPClient::connect_to_sync_server()
{
  if ((m_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    ASSERT_MSG(CORE, 0, "Could not establish client. Is the port already in use?");
    return;
  }

  memset(&m_host_addr, 0, sizeof(m_host_addr));

  m_host_addr.sin_family = AF_INET;
  m_host_addr.sin_addr.s_addr = inet_addr(Config::Get(Config::ACMP_IP).c_str());
  m_host_addr.sin_port = htons(Config::Get(Config::ACMP_PORT));

  m_recv_thread = std::thread([=] { recv_task(); });
  m_send_thread = std::thread([=] { sender_task(); });
}

void ACMPClient::shutdown()
{
  if (m_shutdown || !m_sockfd)
  {
    return;
  }

  closesocket(m_sockfd);
  m_sockfd = 0;
  m_shutdown = true;

  m_recv_thread.join();
  m_send_thread.join();
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
      m_player_updates[i] = val;
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
    u32 player = PowerPC::MMU::HostRead_U32(guard, players_addr + (i * 0x4)); 
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
  if (m_shutdown)
  {
    return;
  }

  if (!m_sockfd)
  {
    connect_to_sync_server();
  }

  PowerPC::MMU::HostWrite_U8(guard, 1,
                             s_symbolDB.GetSymbolFromName("s_networking_started")->address);

  update_outbound_buffer(guard);
  write_inbound_updates(guard);
}

void ACMPClient::recv_task()
{
  char buffer[MOD_SYNC_BUFFER_SZ];
  int n;
  while (m_sockfd)
  {
    int addr_sz = sizeof(m_host_addr);
    n = recvfrom(m_sockfd, buffer, 1, 0, (struct sockaddr*) &m_host_addr, &addr_sz);
    if (n < 1)
    {
      if (!m_sockfd)
      {
        break;
      }

      continue;
    }

    std::lock_guard<std::mutex> lk(m_inbound_mutex);

    switch (static_cast<PacketType>(buffer[0]))
    {
    case kPlayerUpdate:
    {
      n = recvfrom(m_sockfd, buffer, sizeof(PlayerUpdate), 0, (struct sockaddr*)&m_host_addr, &addr_sz);
      PlayerUpdate* update = (PlayerUpdate*) buffer;
      u16 count = update->count;
      u8 player_id = update->player;
      if (update->count > MOD_SYNC_BUFFER_SZ)
      {
        ASSERT_MSG(CORE, 0, "Received invalid payload size from client");
        break;
      }

      n = recvfrom(m_sockfd, buffer, (count * sizeof(AddrUpdate)), 0, (struct sockaddr*)&m_host_addr,
                   &addr_sz);
      for (int i = 0; i < count; i++)
      {
        m_inbound_player_updates[player_id].push_back(
            *(AddrUpdate*)&buffer[i * sizeof(AddrUpdate)]);
      }

      break;
    }
    case kWorldUpdate:
    {
      n = recvfrom(m_sockfd, buffer, sizeof(WorldUpdate), 0, (struct sockaddr*)&m_host_addr, &addr_sz);
      WorldUpdate* update = (WorldUpdate*) buffer;
      u16 count = update->count;
      if (update->count > MOD_SYNC_BUFFER_SZ)
      {
        ASSERT_MSG(CORE, 0, "Received invalid payload size from host");
        break;
      }

      n = recvfrom(m_sockfd, buffer, (count * sizeof(AddrUpdate)), 0, (struct sockaddr*)&m_host_addr,
                   &addr_sz);
      for (int i = 0; i < count; i++)
      {
        m_inbound_world_updates.push_back(
            *(AddrUpdate*)&buffer[i * sizeof(AddrUpdate)]);
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
  int n;
  while (m_sockfd)
  {
    std::unique_lock<std::mutex> lk(m_outbound_mutex);

    PlayerUpdate player_update;
    player_update.player = 0;

    char buffer[MOD_SYNC_BUFFER_SZ];
    while (!m_player_updates.empty())
    {
      int count = 0;
      serialize_map<u16>(buffer, &count, m_player_updates);

      player_update.count = count;
      n = sendto(m_sockfd, (char*)&player_update, sizeof(player_update), 0, (sockaddr*)&m_host_addr,
             sizeof(m_host_addr));
      if (n < 1)
      {
        if (!m_sockfd)
        {
          break;
        }

        continue;
      }

      sendto(m_sockfd, buffer, count * sizeof(AddrUpdate), 0,
             (struct sockaddr*)&m_host_addr, sizeof(m_host_addr));
    }

    lk.unlock();

    m_player_updates.clear();
    using namespace std::chrono_literals;
    std::this_thread::sleep_for(33ms);
  }
}
}  // namespace ACMP
