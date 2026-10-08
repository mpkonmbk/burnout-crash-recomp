// burnoutcrash - native replacements for recompiled guest functions (default.xex)
//
// Recompiled functions are weak aliases, so a strong REX_HOOK_RAW definition here replaces them.

#include <cstdint>
#include <cstring>

#include <rex/hook.h>
#include <rex/memory/utils.h>
#include <rex/runtime.h>
#include <rex/system/kernel_state.h>
#include <rex/system/xmemory.h>
#include <rex/system/xtypes.h>

using rex::X_RESULT;  // the X_ERROR_* macros name it unqualified

namespace {

// Same host mapping as the generated REX_LOAD/REX_STORE macros.
inline uint8_t* GuestPtr(uint8_t* base, uint32_t addr) {
#if defined(_WIN32)
  return base + addr + (addr >= 0xE0000000u ? 0x1000u : 0u);
#else
  return base + addr;
#endif
}

inline uint32_t Load32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, GuestPtr(base, addr), sizeof(v));
  return __builtin_bswap32(v);
}

inline void Store32(uint8_t* base, uint32_t addr, uint32_t value) {
  value = __builtin_bswap32(value);
  std::memcpy(GuestPtr(base, addr), &value, sizeof(value));
}

// True if the 4 bytes at `addr` are mapped readable guest memory. The current thread's stack
// (KPCR stack_end_ptr..stack_base_ptr at r13+0x74/0x70) is checked first without taking heap locks.
bool GuestReadable(PPCContext& ctx, uint8_t* base, uint32_t addr) {
  if (ctx.r13.u32) {
    uint32_t stack_base = Load32(base, ctx.r13.u32 + 0x70);
    uint32_t stack_limit = Load32(base, ctx.r13.u32 + 0x74);
    if (addr >= stack_limit && addr + 4 <= stack_base) return true;
  }
  auto* heap = rex::Runtime::instance()->memory()->LookupHeap(addr);
  return heap && heap->QueryRangeAccess(addr, addr + 3) != rex::memory::PageAccess::kNoAccess;
}

}  // namespace

// EA callstack capture: fills out[] (r5, capacity r6) with {return address, frame, 1} triples by
// following PPC stack back-chains, then a zero terminator. r4 == -1 walks the calling thread;
// otherwise it starts from callstack slot r4 saved in the object at *(r3 + 4).
//
// The original stops only at a null / misaligned / non-increasing back chain. On hardware the
// outermost frame is terminated by the kernel, but under ReXGlue its back chain is the stack top
// itself, so the walk reads one word past the stack and faults. Identical output, bounded reads.
REX_HOOK_RAW(sub_82651230) {
  const uint32_t out = ctx.r5.u32;
  const uint32_t last = ctx.r6.u32 - 1;  // entries before the terminator (unsigned, as in `cmplw`)
  const uint32_t r1_in = ctx.r1.u32;
  uint32_t count = 0;
  uint32_t frame = 0;

  auto emit = [&](uint32_t lr, uint32_t sp) {
    uint32_t entry = out + count * 12;
    Store32(base, entry + 0, lr);
    Store32(base, entry + 4, sp);
    Store32(base, entry + 8, 1);
    ++count;
  };

  if (ctx.r4.s32 == -1) {
    // The original starts from its own frame (r1 - 112), whose back chain is the caller's r1 and
    // whose saved LR is our return address.
    if (count < last && r1_in != 0 && (r1_in & 0xF) == 0) {
      emit(static_cast<uint32_t>(ctx.lr), r1_in);
      frame = r1_in;
    }
  } else {
    uint32_t obj = Load32(base, ctx.r3.u32 + 4);
    uint32_t lr = 0, sp = 0, valid = 0;
    if (ctx.r4.s32 < static_cast<int32_t>(Load32(base, obj + 32))) {
      uint32_t slot = obj + 152 + ctx.r4.u32 * 12;
      lr = Load32(base, slot + 0);
      sp = Load32(base, slot + 4);
      valid = Load32(base, slot + 8);
    }
    Store32(base, out + 0, lr);
    Store32(base, out + 4, sp);
    Store32(base, out + 8, valid);
    count = 1;
    frame = sp;
  }

  while (frame != 0 && count < last) {
    if (!GuestReadable(ctx, base, frame)) break;
    uint32_t next = Load32(base, frame);
    if (next == 0 || (next & 0xF) != 0 || next < frame) break;
    if (!GuestReadable(ctx, base, next - 8)) break;
    emit(Load32(base, next - 8), next);
    frame = next;
  }

  uint32_t term = out + count * 12;
  Store32(base, term + 0, 0);
  Store32(base, term + 4, 0);
  Store32(base, term + 8, 0);

  ctx.r3.u64 = r1_in - 112 + 80;  // the original leaves r3 pointing at its scratch slot
}

// XUserFindUsers, from the game's statically linked Live client: r3 = requesting XUID, r4 = user
// count, r5 = users, r6 = results size, r7 = FIND_USERS_RESPONSE, r8 = XOVERLAPPED.
//
// The runtime has no handler for its XLIVEBASE message (0x58017), so the original bails before
// starting the request and never touches the overlapped. The caller (sub_821BA180, reached from
// PLAY) ignores the return value, later reads the still-zeroed overlapped as ERROR_SUCCESS and walks
// the uninitialized result buffer. Complete it the way a console that isn't signed in to Live does.
REX_HOOK_RAW(sub_82842F48) {
  constexpr uint32_t kNotLoggedOn = 0x80151802;  // X_ONLINE_E_LOGON_NOT_LOGGED_ON
  const uint32_t results = ctx.r7.u32;
  const uint32_t overlapped = ctx.r8.u32;

  if (results && ctx.r6.u32 >= 8) {
    Store32(base, results + 0, 0);  // dwResults
    Store32(base, results + 4, 0);  // pUsers
  }
  if (overlapped) {
    rex::system::kernel_state()->CompleteOverlappedImmediateEx(overlapped, X_ERROR_FUNCTION_FAILED,
                                                               kNotLoggedOn, 0);
    ctx.r3.u64 = X_ERROR_IO_PENDING;
  } else {
    ctx.r3.u64 = X_ERROR_FUNCTION_FAILED;
  }
}
