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
  bool s_rel_initialized = false;
  Host* s_server = nullptr;
  Client* s_client = nullptr;

  void run_mod(const Core::CPUThreadGuard& guard)
  {
    // u32 val = PowerPC::MMU::HostRead_U16(guard, CURR_FRAME_ADDR);
    // if (val < 1)
    // {
    //   // wait for a full boot, the game initializes twice.
    //   return;
    // }

    setup_bat();

    if (!s_initialized || PowerPC::MMU::HostRead_U32(guard, 0x80005a68) == 0x819b0034)
    {
      init_mod(guard);
      s_initialized = true;
      s_rel_initialized = false;
      return;
    }

    if (!s_rel_initialized) {
      u32 rel_base = PowerPC::MMU::HostRead_U32(guard, symbolDb().GetSymbolFromName("s_rel_base")->address);
      if (rel_base) {
        // from prolog to base of the file
        rel_base -= 0xe8;
  
        // std::cout << "rel_base: " << std::hex << rel_base << std::endl;
        bl_to_symbol(guard, rel_base + 0x5530, "acmp_malloc");
        bl_to_symbol(guard, rel_base + 0x5b90, "acmp_free");
        bl_to_symbol(guard, rel_base + 0x4c00, "acmp_spawn_player_actors");

        mod_post_init(guard);
        s_rel_initialized = true;
      }
    }

    if (!s_mod_ready) {
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
    write_elf(guard);

    // provide actor_profile as an argument to select_malloc
    // PowerPC::MMU::HostWrite_U32(guard, 0x7C832378, 0x803756ec);  // mr r3, r4
    
    // bl_to_symbol(guard, 0x803756f0, "acmp_select_malloc"); // redirect non player npc's to the extended arena
    // bl_to_symbol(guard, 0x803756a4, "acmp_malloc");
    // b_to_symbol(guard, 0x803756a4, "acmp_malloc");
    // bl_to_symbol(guard, 0x80375d50, "acmp_free");



    bl_to_symbol(guard, 0x80005a68, "load_link_hook");

    // make sure that no matter where an entity is cleared, the right arena is chosen
    
    // bl_to_symbol(guard, 0x804105d4, "acmp_select_free");
  // bl_to_symbol(guard, 0x80005324, "acmp_select_free");
  // bl_to_symbol(guard, 0x80005b90, "acmp_select_free");
  // bl_to_symbol(guard, 0x8002a748, "acmp_select_free");
  // bl_to_symbol(guard, 0x80032e3c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80032e44, "acmp_select_free");
  // bl_to_symbol(guard, 0x80032e4c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033f50, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033f70, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033f88, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033f90, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033fa4, "acmp_select_free");
  // bl_to_symbol(guard, 0x80033fbc, "acmp_select_free");
  // bl_to_symbol(guard, 0x80034394, "acmp_select_free");
  // bl_to_symbol(guard, 0x8003439c, "acmp_select_free");
  // bl_to_symbol(guard, 0x800349dc, "acmp_select_free");
  // bl_to_symbol(guard, 0x80034b2c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80034b50, "acmp_select_free");
  // bl_to_symbol(guard, 0x80034b68, "acmp_select_free");
  // bl_to_symbol(guard, 0x80034b8c, "acmp_select_free");
  // bl_to_symbol(guard, 0x8003c2ec, "acmp_select_free");
  // bl_to_symbol(guard, 0x80045674, "acmp_select_free");
  // bl_to_symbol(guard, 0x800887f8, "acmp_select_free");
  // bl_to_symbol(guard, 0x80088810, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008882c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80088844, "acmp_select_free");
  // bl_to_symbol(guard, 0x80088b74, "acmp_select_free");
  // bl_to_symbol(guard, 0x80088f58, "acmp_select_free");
  // bl_to_symbol(guard, 0x80088f80, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089200, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089228, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089420, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089448, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089714, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008973c, "acmp_select_free");
  // bl_to_symbol(guard, 0x800899d4, "acmp_select_free");
  // bl_to_symbol(guard, 0x800899fc, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089bec, "acmp_select_free");
  // bl_to_symbol(guard, 0x80089c14, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008a1bc, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008a414, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008a424, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008a434, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008e754, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008e900, "acmp_select_free");
  // bl_to_symbol(guard, 0x8008e910, "acmp_select_free");
  // bl_to_symbol(guard, 0x80093650, "acmp_select_free");
  // bl_to_symbol(guard, 0x800a0414, "acmp_select_free");
  // bl_to_symbol(guard, 0x800a1a60, "acmp_select_free");
  // bl_to_symbol(guard, 0x800a1a70, "acmp_select_free");
  // bl_to_symbol(guard, 0x800b4c6c, "acmp_select_free");
  // bl_to_symbol(guard, 0x800ba2c8, "acmp_select_free");
  // bl_to_symbol(guard, 0x800be0c4, "acmp_select_free");
  // bl_to_symbol(guard, 0x800be0d4, "acmp_select_free");
  // bl_to_symbol(guard, 0x800ff3bc, "acmp_select_free");
  // bl_to_symbol(guard, 0x800ff3cc, "acmp_select_free");
  // bl_to_symbol(guard, 0x80103084, "acmp_select_free");
  // bl_to_symbol(guard, 0x801030d8, "acmp_select_free");
  // bl_to_symbol(guard, 0x801030f0, "acmp_select_free");
  // bl_to_symbol(guard, 0x80103108, "acmp_select_free");
  // bl_to_symbol(guard, 0x80103118, "acmp_select_free");
  // bl_to_symbol(guard, 0x801036e8, "acmp_select_free");
  // bl_to_symbol(guard, 0x80112f70, "acmp_select_free");
  // bl_to_symbol(guard, 0x80112f78, "acmp_select_free");
  // bl_to_symbol(guard, 0x8011707c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80117094, "acmp_select_free");
  // bl_to_symbol(guard, 0x8012e09c, "acmp_select_free");
  // bl_to_symbol(guard, 0x8012e8e4, "acmp_select_free");
  // bl_to_symbol(guard, 0x8012e8f4, "acmp_select_free");
  // bl_to_symbol(guard, 0x80130460, "acmp_select_free");
  // bl_to_symbol(guard, 0x80130da0, "acmp_select_free");
  // bl_to_symbol(guard, 0x80133740, "acmp_select_free");
  // bl_to_symbol(guard, 0x80140008, "acmp_select_free");
  // bl_to_symbol(guard, 0x8014738c, "acmp_select_free");
  // bl_to_symbol(guard, 0x8014ee84, "acmp_select_free");
  // bl_to_symbol(guard, 0x8015629c, "acmp_select_free");
  // bl_to_symbol(guard, 0x801a5f34, "acmp_select_free");
  // bl_to_symbol(guard, 0x801d4e14, "acmp_select_free");
  // bl_to_symbol(guard, 0x8023234c, "acmp_select_free");
  // bl_to_symbol(guard, 0x802412a0, "acmp_select_free");
  // bl_to_symbol(guard, 0x8024ce8c, "acmp_select_free");
  // bl_to_symbol(guard, 0x80258158, "acmp_select_free");
  // bl_to_symbol(guard, 0x8025c010, "acmp_select_free");
  // bl_to_symbol(guard, 0x8025d0e4, "acmp_select_free");
  // bl_to_symbol(guard, 0x8025ea78, "acmp_select_free");
  // bl_to_symbol(guard, 0x8026112c, "acmp_select_free");
  // bl_to_symbol(guard, 0x8026cf20, "acmp_select_free");
  // bl_to_symbol(guard, 0x80277870, "acmp_select_free");
  // bl_to_symbol(guard, 0x8027a8dc, "acmp_select_free");
  // bl_to_symbol(guard, 0x8029248c, "acmp_select_free");
  // bl_to_symbol(guard, 0x802bd318, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c30c8, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c3298, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c341c, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c369c, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c4790, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c4ea8, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c502c, "acmp_select_free");
  // bl_to_symbol(guard, 0x802c51b0, "acmp_select_free");
  // bl_to_symbol(guard, 0x802cd568, "acmp_select_free");
  // bl_to_symbol(guard, 0x802cfac8, "acmp_select_free");

    PowerPC::MMU::HostWrite_U32(guard, 0x38600001, 0x80374a98);  // li r3, 1
    PowerPC::MMU::HostWrite_U32(guard, 0x4e800020, 0x80374a9c);  // blr

    // bl_to_symbol(guard, 0x8062aaa8, "acmp_Game_play_Reset_destiny_hook");

    // bl_to_symbol(guard, 0x80374dc0, "acmp_spawn_player_actors");
    //4c00

    // bl_to_symbol(guard, 0x804e6bdc, "acmp_main_walk_stand_controller_hook");
    // bl_to_symbol(guard, 0x804e6038, "acmp_main_walk_stand_controller_hook");

    // b_to_symbol(guard, 0x803d9728, "acmp_get_player_without_check");

    // bl_to_symbol(guard, 0x8037e0bc, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x8038022c, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x80380e40, "acmp_get_primary_player");
    // bl_to_symbol(guard, 0x803827d8, "acmp_get_primary_player");

    // if you disable the panic code, then your game cant break anymore
    PowerPC::MMU::HostWrite_U32(guard, 0x8005a8a0, 0x60000000);

    s_initialized = true;
  }

  void setup_bat()
  {
    Core::System& system = Core::System::GetInstance();
    auto& ppc_state = system.GetPPCState();
    auto& mmu = system.GetMMU();
    bool should_update =
        !(ppc_state.spr[SPR_DBAT2U] & 0x00000100) || !(ppc_state.spr[SPR_IBAT2U] & 0x00000100);
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
      elf_file.LoadIntoMemory(Core::System::GetInstance(), false);
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