#pragma once

#include "ACMPCommon.h"

#include "Core/Core.h"

#include <thread>
#include <mutex>
#include <shared_mutex>
#include <WinSock2.h>

namespace ACMP
{
struct RemotePlayer
{
  sockaddr_in peer_addr;
  std::vector<AddrUpdate> inbound_updates;
  std::mutex inbound_updates_mutex;
};

class ACMPHost
{
public:
  void host_sync_server();
  void shutdown();

  void update(const Core::CPUThreadGuard& guard);
  void handle_incoming_updates(const Core::CPUThreadGuard& guard);
  void update_outgoing_updates(const Core::CPUThreadGuard& guard);

  void recv_task();
  void sender_task();

private:
  int m_sockfd = 0;
  bool m_shutdown = false;

  std::vector<std::shared_ptr<RemotePlayer>> m_remote_players;
  std::shared_mutex m_players_mutex;

  std::thread m_recv_thread;
  std::mutex m_inbound_mutex;
  std::vector<AddrUpdate> m_inbound_updates;
  std::vector<AddrUpdate> m_inbound_player_updates[MAX_PLAYERS];

  std::thread m_send_thread;
  std::mutex m_outbound_mutex;
  std::vector<AddrUpdate> m_outbound_updates;

  uint32_t m_memory_snapshot[MOD_HEAP_SIZE];

  uint32_t m_host_player_snapshot[0x126c];
  std::vector<AddrUpdate> m_host_player_updates;
};
}  // namespace ACMP
