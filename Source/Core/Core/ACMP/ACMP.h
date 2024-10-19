#pragma once

#include "Core/Core.h"
#include "Core/PowerPC/PPCSymbolDB.h"

#include "ACMPClient.h"
#include "ACMPHost.h"

namespace ACMP {
extern PPCSymbolDB s_symbolDB;

void run_mod(const Core::CPUThreadGuard& guard);
void shutdown();

bool start_host();
bool start_client();

void init_mod(const Core::CPUThreadGuard& guard);
void setup_bat();
void write_elf(const Core::CPUThreadGuard& guard);

void bl_to_symbol(const Core::CPUThreadGuard& guard, u32 addr, std::string_view symbol);
void b_to_symbol(const Core::CPUThreadGuard& guard, u32 addr, std::string_view symbol);

ACMPHost* host();
ACMPClient* client();
}
