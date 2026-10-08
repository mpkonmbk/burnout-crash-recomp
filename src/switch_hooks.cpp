// burnoutcrash - mid-asm hooks for CRASH.DLL.XEX
//
// EASharp's AOT compiler emits jump-table dispatch as
//     rlwinm rI, rI, 2, 0, 29 ; lwzx rI, rI, rBase ; mtctr rI ; bctr
// which leaves no register holding the case index at the bctr. config/crash_dll.toml
// declares those tables as switches on rBase and attaches this hook to the bctr.

#include <cstdint>

#include <rex/logging.h>
#include <rex/ppc.h>
#include <rex/runtime.h>

// `target` holds the code address just loaded from the jump table and `table` the table's guest
// address (dead after the lwzx). Rewrites `table` with the case index whose entry equals `target`.
// Several cases may share a target; any of them reaches the same label.
void SwitchIndexFromTarget(PPCRegister& target, PPCRegister& table) {
  static uint8_t* const membase = rex::Runtime::instance()->virtual_membase();
  constexpr uint32_t kMaxEntries = 4096;

  const uint32_t table_addr = table.u32;
  for (uint32_t i = 0; i < kMaxEntries; ++i) {
    uint32_t entry =
        __builtin_bswap32(*reinterpret_cast<const uint32_t*>(membase + table_addr + i * 4));
    if (entry == target.u32) {
      table.u64 = i;
      return;
    }
  }

  REXLOG_ERROR("SwitchIndexFromTarget: target {:08X} not found in jump table {:08X}", target.u32,
               table_addr);
  table.u64 = UINT32_MAX;  // out of range: the generated switch traps
}
