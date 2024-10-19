#include "ACMPHost.h"
#include "ACMP.h"

#include <Common/Assert.h>
#include <Core/PowerPC/MMU.h>

namespace ACMP
{
void ACMPHost::host_sync_server()
{
  struct sockaddr_in server_addr;

  if ((m_sockfd = socket(AF_INET, SOCK_DGRAM, 0)) < 0)
  {
    ASSERT_MSG(CORE, 0, "Could not establish server. Is the port already in use?");
    return;
  }

  memset(&server_addr, 0, sizeof(server_addr));

  server_addr.sin_family = AF_INET;
  server_addr.sin_addr.s_addr = htonl(INADDR_ANY);
  server_addr.sin_port = htons(4404);

  if (bind(m_sockfd, (const struct sockaddr*)&server_addr, sizeof(server_addr)) < 0)
  {
    ASSERT_MSG(CORE, 0, "Could not bind server address. Is the port already in use?");
    return;
  }

  m_recv_thread = std::thread([=] { recv_task(); });
  m_send_thread = std::thread([=] { sender_task(); });
}

void ACMPHost::shutdown()
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

void ACMPHost::update(const Core::CPUThreadGuard& guard)
{
  if (m_shutdown)
  {
    return;
  }

  if (!m_sockfd)
  {
    host_sync_server();
  }

  PowerPC::MMU::HostWrite_U8(guard, 1,
                             s_symbolDB.GetSymbolFromName("s_networking_started")->address);

  handle_incoming_updates(guard);
  update_outgoing_updates(guard);
}

void ACMPHost::handle_incoming_updates(const Core::CPUThreadGuard& guard)
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
}

void ACMPHost::update_outgoing_updates(const Core::CPUThreadGuard& guard)
{
  std::lock_guard<std::mutex> lk(m_outbound_mutex);
  std::shared_lock<std::shared_mutex> player_lk(m_players_mutex);

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
    if (m_host_player_snapshot[i] != val)
    {
      m_host_player_updates.push_back(AddrUpdate{i, val});
      m_host_player_snapshot[i] = val;
    }
  }

  for (uint32_t i = 0; i < MOD_HEAP_SIZE; i += 4)
  {
    u32 address = MOD_HEAP_BASE + i;
    u32 val = PowerPC::MMU::HostRead_U32(guard, address);
    if (m_memory_snapshot[i] != val)
    {
      m_outbound_updates.push_back(AddrUpdate{address, val});
      m_memory_snapshot[i] = val;
    }

    if (m_outbound_updates.size() > MOD_SYNC_BUFFER_SZ - 0x8)
    {
      break;
    }
  }
}

void ACMPHost::recv_task()
{
  char buffer[MOD_SYNC_BUFFER_SZ];
  int n;

  while (m_sockfd)
  {
    struct sockaddr_in client_addr;
    int addr_sz = sizeof(client_addr);
    memset(&client_addr, 0, sizeof(client_addr));

    n = recvfrom(m_sockfd, buffer, sizeof(PlayerUpdate), 0, (struct sockaddr*)&client_addr, &addr_sz);
    if (n < 1)
    {
      DWORD p = GetLastError();
      printf("%d", p);

      if (!m_sockfd)
      {
        break;
      }

      continue;
    }

    std::shared_ptr<RemotePlayer> player = nullptr;
    std::shared_lock lk(m_players_mutex);

    for (std::shared_ptr<RemotePlayer> p : m_remote_players)
    {
      if (memcmp(&client_addr, &p->peer_addr, sizeof(SOCKADDR_IN)) == 0)
      {
        player = p;
        break;
      }
    }

    if (!player)
    {
      // we must release the read lock to upgrade to a write lock, then we downgrade again
      lk.unlock();

      std::unique_lock lk2(m_players_mutex);
      std::shared_ptr<RemotePlayer> new_player = std::make_shared<RemotePlayer>();
      new_player->peer_addr = client_addr;
      player = new_player;
      m_remote_players.push_back(new_player);
      lk2.unlock();

      lk.lock();
    }

    //n = recvfrom(m_sockfd, buffer, (char*) type, 0, (struct sockaddr*) &client_addr, &addr_sz);
    // PacketType* type = (PacketType*)buffer;
    PlayerUpdate* update_msg = (PlayerUpdate*)buffer;
    u16 count = update_msg->count;

    // todo, pick slot based on manager using ip address table
    u8 slot = update_msg->player;
    if (count > MOD_SYNC_BUFFER_SZ || count == 0)
    {
      ASSERT_MSG(CORE, 0, "Received invalid payload size from client");
      lk.unlock();
      break;
    }

    n = recvfrom(m_sockfd, buffer, (count * sizeof(AddrUpdate)), 0, (struct sockaddr*)&client_addr, &addr_sz);

    std::lock_guard<std::mutex> lk2(player->inbound_updates_mutex);
    std::lock_guard<std::mutex> lk3(m_inbound_mutex);
    for (int i = 0; i < count; i++)
    {
      AddrUpdate* update = (AddrUpdate*) &buffer[i * sizeof(AddrUpdate)];
      player->inbound_updates.push_back(*update);
      m_inbound_player_updates[slot].push_back(*update);
    }

    lk.unlock();
  }
}

void ACMPHost::sender_task()
{
  int n = 0;
  while (m_sockfd)
  {
    std::unique_lock<std::mutex> lk(m_outbound_mutex);
    std::shared_lock lk2(m_players_mutex);

    PacketType world_packet = PacketType::kWorldUpdate;
    while (!m_outbound_updates.empty())
    {
      constexpr int max_updates_per_packet = MOD_SYNC_BUFFER_SZ / sizeof(AddrUpdate);

      WorldUpdate world_update;
      world_update.count = (int)m_outbound_updates.size() > max_updates_per_packet ?
                               max_updates_per_packet :
                               (int)m_outbound_updates.size();

      for (auto player : m_remote_players)
      {
        n = sendto(m_sockfd, (char*)&world_packet, sizeof(world_packet), 0,
                   (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
        if (n < 1)
        {
          if (!m_sockfd)
          {
            return;
          }

          continue;
        }

        n = sendto(m_sockfd, (char*)&world_update, sizeof(world_update), 0,
                   (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
        sendto(m_sockfd, (char*) m_outbound_updates.data(), world_update.count * sizeof(AddrUpdate), 0,
               (struct sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
      }

      m_outbound_updates.erase(m_outbound_updates.begin(),
                               m_outbound_updates.begin() + world_update.count);
    }

    PacketType player_packet = PacketType::kPlayerUpdate;
    for (auto player : m_remote_players)
    {
      int host_slot = 0;
      for (int i = 0; i < m_remote_players.size(); i++)
      {
        std::shared_ptr<RemotePlayer> other_player = m_remote_players[i];
        if (memcmp(&player->peer_addr, &other_player->peer_addr, sizeof(SOCKADDR_IN)) == 0)
        {
          host_slot = i;
          continue;
        }

        PlayerUpdate player_update;
        player_update.player = i;

        std::lock_guard<std::mutex> lk3(other_player->inbound_updates_mutex);
        player_update.count = (u16)other_player->inbound_updates.size();

        sendto(m_sockfd, (char*)&player_packet, sizeof(player_packet), 0,
               (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
        sendto(m_sockfd, (char*)&player_update, sizeof(player_update), 0,
               (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
        sendto(m_sockfd, (char*)other_player->inbound_updates.data(), player_update.count * sizeof(AddrUpdate), 0,
               (struct sockaddr*)&player->peer_addr, sizeof(player->peer_addr));

        other_player->inbound_updates.clear();
      }

      PlayerUpdate player_update;
      player_update.player = host_slot;

      player_update.count = (u16) m_host_player_updates.size();
      sendto(m_sockfd, (char*)&player_packet, sizeof(player_packet), 0,
             (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
      sendto(m_sockfd, (char*)&player_update, sizeof(player_update), 0,
               (sockaddr*)&player->peer_addr, sizeof(player->peer_addr));
      sendto(m_sockfd, (char*) m_host_player_updates.data(), player_update.count * sizeof(AddrUpdate), 0,
             (struct sockaddr*)&player->peer_addr, sizeof(player->peer_addr));

      m_host_player_updates.clear();
      m_outbound_updates.clear();
    }

    lk.unlock();
    lk2.unlock();

    using namespace std::chrono_literals;
    std::this_thread::sleep_for(33ms);
  }
}
}  // namespace ACMP
