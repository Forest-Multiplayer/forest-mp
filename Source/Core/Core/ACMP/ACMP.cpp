#pragma GCC push_options
#pragma GCC optimize ("O0")

#include "ACMP.h"

#include "ACMPClient.h"
#include "ACMPHost.h"

#include "Common/SymbolDB.h"
#include "Common/FileUtil.h"

#include "Core/Boot/ElfReader.h"
#include "Core/PowerPC/PPCSymbolDB.h"
#include "Core/PowerPC/Gekko.h"
#include "Core/PowerPC/MMU.h"
#include "Core/PowerPC/PowerPC.h"
#include "Core/System.h"

#include <iostream>

#define ID_ADDR 0x80003100
#define ID_VAL 0x7c0802a6
#define CURR_FRAME_ADDR 0x812f31d4

namespace ACMP
{
  bool s_initialized = false;
  Host* s_server = nullptr;
  Client* s_client = nullptr;

  void run_mod(const Core::CPUThreadGuard& guard)
  {
    if (PowerPC::MMU::HostRead_U32(guard, CURR_FRAME_ADDR) == 0)
    {
      // wait for a full boot, the game initializes twice.
      return;
    }

    if (!s_initialized)
    {
      init_mod(guard);
      s_initialized = true;
      return;
    }

    if (s_server)
    {
      s_server->frameAdvance(guard);
    }

    if (s_client)
    {
      s_client->frameAdvance(guard);
    }
  }

  void shutdown()
  {
    if (s_server)
    {
      s_server->shutdown();
    }

    if (s_client)
    {
      s_client->stop();
      s_client->disconnect();
    }

    s_server = nullptr;
    s_client = nullptr;
    s_initialized = false;
  }


  bool start_host()
  {
    if (!s_server)
    {
      s_server = new Host();
      s_server->init("host", 4404);
      s_server->start();

      return true;
    }

    return false;
  }

  bool start_client()
  {
    if (!s_client) {
      s_client = new Client();
      
      s_client->connect("localhost", 4404, fmt::format("player-{}", random()));
      s_client->start();

      return true;
    }

    return false;
  }

  void init_mod(const Core::CPUThreadGuard& guard)
  {
    setup_bat();
    write_elf(guard);

    // PowerPC::MMU::HostWrite_U32(guard, 0x7C832378, 0x803756ec);  // mr r3, r4
    // bl_to_symbol(guard, 0x803756f0, "acmp_select_malloc");

    // bl_to_symbol(guard, 0x803756a4, "acmp_malloc");

    // PowerPC::MMU::HostWrite_U32(guard, 0x38600001, 0x80374a98);  // li r3, 1
    // PowerPC::MMU::HostWrite_U32(guard, 0x4e800020, 0x80374a9c);  // blr

    // bl_to_symbol(guard, 0x80375d50, "acmp_select_free");
    // // bl_to_symbol(guard, 0x8062aaa8, "acmp_Game_play_Reset_destiny_hook");

    // bl_to_symbol(guard, 0x80374dc0, "acmp_spawn_player_actors");

    // bl_to_symbol(guard, 0x804e6bdc, "acmp_main_walk_stand_controller_hook");
    // bl_to_symbol(guard, 0x804e6038, "acmp_main_walk_stand_controller_hook");

    // b_to_symbol(guard, 0x803d9728, "acmp_get_player_without_check");

    // bl_to_symbol(guard, 0x8037e0bc, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x8038022c, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x80380e40, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x803827d8, "acmp_get_primary_player");

    // // if you disable the panic code, then your game cant break anymore
    // PowerPC::MMU::HostWrite_U32(guard, 0x8005a8a0, 0x60000000);

    s_initialized = true;
  }

  void setup_bat()
  {
    Core::System& system = Core::System::GetInstance();
    auto& ppc_state = system.GetPPCState();
    auto& mmu = system.GetMMU();
    bool should_update =
        !(ppc_state.spr[SPR_DBAT2U] & 0x00000100 || ppc_state.spr[SPR_IBAT2U] & 0x00000100);
    if (should_update)
    {
      ppc_state.spr[SPR_DBAT2U] |= 0x00000100;
      ppc_state.spr[SPR_IBAT2U] |= 0x00000100;

      mmu.DBATUpdated();
      mmu.IBATUpdated();
    }
  }

  void write_elf(const Core::CPUThreadGuard& guard)
  {
    ElfReader elf_file(File::GetSysDirectory() + "/ACMP/acmp.elf");

    if (elf_file.IsValid())
    {
      // elf_file.LoadIntoMemory(Core::System::GetInstance(), false);
      elf_file.LoadSymbols(guard, symbolDb(), "acmp-symbols");
      
    }
  }

  void bl_to_symbol(const Core::CPUThreadGuard& guard, u32 addr, std::string_view symbol)
  {
    u32 bl = 0x48000001 | (symbolDb().GetSymbolFromName(symbol)->address - addr);

    PowerPC::MMU::HostWrite_U32(guard, bl, addr);
    Core::System::GetInstance().GetPowerPC().ScheduleInvalidateCacheThreadSafe(addr);
  }

  void b_to_symbol(const Core::CPUThreadGuard& guard, u32 addr, std::string_view symbol)
  {
    u32 b = 0x48000000 | (symbolDb().GetSymbolFromName(symbol)->address - addr);

    PowerPC::MMU::HostWrite_U32(guard, b, addr);
    Core::System::GetInstance().GetPowerPC().ScheduleInvalidateCacheThreadSafe(addr);
  }
}

