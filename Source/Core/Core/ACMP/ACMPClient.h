#pragma once

#include "ACMPCommon.h"

#include "Core/Core.h"

#include <thread>
#include <mutex>
#include <WinSock2.h>

struct AddrUpdate;

namespace ACMP
{
class ACMPClient
{
public:
  void connect_to_sync_server();
  void shutdown();

  void update_outbound_buffer(const Core::CPUThreadGuard& guard);
  void write_inbound_updates(const Core::CPUThreadGuard& guard);

  void update(const Core::CPUThreadGuard& guard);
  void recv_task();
  void sender_task();

private:
  int m_sockfd = 0;
  bool m_shutdown = false;

  sockaddr_in m_host_addr;

  std::mutex m_inbound_mutex;
  std::mutex m_outbound_mutex;

  std::thread m_recv_thread;
  std::thread m_send_thread;

  uint32_t m_player_snapshot[0x126c];
  std::vector<AddrUpdate> m_player_updates;
  std::vector<AddrUpdate> m_inbound_world_updates;
  std::vector<AddrUpdate> m_inbound_player_updates[MAX_PLAYERS];
};
}  // namespace ACMP
