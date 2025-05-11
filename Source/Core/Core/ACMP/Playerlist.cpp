#include "Playerlist.h"

#include "ACMPCommon.h"

#include <algorithm>
#include <iostream>

namespace ACMP
{

Playerlist::Playerlist(const std::string local_id, const std::string local_name)
    : local_id(local_id), local_name(local_name)
{
  local_state.id = local_id;
}

void Playerlist::addPlayer(ENetPeer* peer, const std::string& id, const std::string& name)
{
  PlayerUpdatePayload payload{.id = id,
                              .world_position = {},
                              .eye_position = {},
                              .velocity = {0.0f, 0.0f, 0.0f},
                              .speed = 0.0f,
                              .stateBitfield = 0,
                              .requested_main_index = 0,
                              .requested_main_index_priority = 0,
                              .requested_main_index_changed = 0};

  m_players.push_back(Player{peer, false, payload});
}

void Playerlist::removePlayer(const std::string& id)
{
  for (auto it = m_players.begin(); it != m_players.end();)
  {
    if (it->state.id.compare(id) == 0)
    {
      it = m_players.erase(it);  // erase returns the next iterator
      return;
    }
    else
    {
      ++it;
    }
  }
}

void Playerlist::removePlayer(ENetPeer* peer)
{
  for (auto it = m_players.begin(); it != m_players.end();)
  {
    if (it->peer == peer)
    {
      std::cout << "Peer disconnected: " << std::string(it->state.id) << std::endl;
      it = m_players.erase(it);  // erase returns the next iterator
      return;
    }
    else
    {
      ++it;
    }
  }
}

void Playerlist::updatePlayer(const PlayerUpdatePayload& update)
{
  auto it = std::find_if(m_players.begin(), m_players.end(), [&update](const auto& p) {
    return std::strcmp(p.state.id, update.id) == 0;
  });

  if (it != m_players.end())
  {
    it->state = update;
  }
  else
  {
    m_players.push_back(Player{nullptr, false, update});
  }
}

void Playerlist::clearDirtyFlags()
{
  for (auto& player : m_players)
  {
    player.dirty = false;
  }
}

PlayerUpdatePayload* Playerlist::getLocalPlayerState()
{
  return &local_state;
}

}  // namespace ACMP