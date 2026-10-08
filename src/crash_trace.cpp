// burnoutcrash - access-violation tracer (Windows)
//
// Logs the first few access violations with a symbolized host stack. Recompiled functions are
// named sub_XXXXXXXX after their guest address, so the trace points straight at the guest code.
// Written to crash_trace.txt next to the exe (flushed per line, survives a hard exit).

#if defined(_WIN32)

#include <windows.h>
#include <dbghelp.h>

#include <atomic>
#include <cstdio>
#include <mutex>

namespace {

constexpr int kMaxReports = 8;
constexpr int kMaxFrames = 24;

std::atomic<int> g_reports{0};
std::mutex g_mutex;
FILE* g_out = nullptr;
bool g_sym_ready = false;

void OpenOutput() {
  if (g_out) return;
  wchar_t path[MAX_PATH];
  DWORD n = GetModuleFileNameW(nullptr, path, MAX_PATH);
  while (n > 0 && path[n - 1] != L'\\') --n;
  wcscpy_s(path + n, MAX_PATH - n, L"crash_trace.txt");
  _wfopen_s(&g_out, path, L"w");
}

void Describe(HANDLE process, DWORD64 addr, char* buf, size_t len) {
  alignas(SYMBOL_INFO) char storage[sizeof(SYMBOL_INFO) + 256];
  auto* sym = reinterpret_cast<SYMBOL_INFO*>(storage);
  sym->SizeOfStruct = sizeof(SYMBOL_INFO);
  sym->MaxNameLen = 255;
  DWORD64 disp = 0;
  IMAGEHLP_LINE64 line{};
  line.SizeOfStruct = sizeof(line);
  DWORD line_disp = 0;
  char module[MAX_PATH] = "?";
  HMODULE mod = nullptr;
  if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                             GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                         reinterpret_cast<LPCSTR>(addr), &mod)) {
    char full[MAX_PATH];
    if (GetModuleFileNameA(mod, full, MAX_PATH)) {
      const char* slash = strrchr(full, '\\');
      strcpy_s(module, slash ? slash + 1 : full);
    }
  }
  if (g_sym_ready && SymFromAddr(process, addr, &disp, sym)) {
    if (SymGetLineFromAddr64(process, addr, &line_disp, &line)) {
      const char* file = strrchr(line.FileName, '\\');
      snprintf(buf, len, "%s!%s+0x%llx (%s:%lu)", module, sym->Name,
               static_cast<unsigned long long>(disp), file ? file + 1 : line.FileName,
               line.LineNumber);
    } else {
      snprintf(buf, len, "%s!%s+0x%llx", module, sym->Name, static_cast<unsigned long long>(disp));
    }
  } else {
    snprintf(buf, len, "%s+0x%llx", module,
             static_cast<unsigned long long>(addr - reinterpret_cast<DWORD64>(mod)));
  }
}

LONG CALLBACK OnException(EXCEPTION_POINTERS* info) {
  if (info->ExceptionRecord->ExceptionCode != EXCEPTION_ACCESS_VIOLATION) {
    return EXCEPTION_CONTINUE_SEARCH;
  }
  if (g_reports.fetch_add(1) >= kMaxReports) return EXCEPTION_CONTINUE_SEARCH;

  std::lock_guard<std::mutex> lock(g_mutex);
  OpenOutput();
  if (!g_out) return EXCEPTION_CONTINUE_SEARCH;

  HANDLE process = GetCurrentProcess();
  if (!g_sym_ready) {
    SymSetOptions(SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS | SYMOPT_LOAD_LINES);
    g_sym_ready = SymInitialize(process, nullptr, TRUE) != FALSE;
  }

  const auto& rec = *info->ExceptionRecord;
  const char* kind = rec.ExceptionInformation[0] == 1 ? "write" : rec.ExceptionInformation[0] == 8 ? "execute" : "read";
  fprintf(g_out, "=== access violation #%d: %s of 0x%016llx, thread %lu ===\n", g_reports.load(), kind,
          static_cast<unsigned long long>(rec.ExceptionInformation[1]), GetCurrentThreadId());

  CONTEXT ctx = *info->ContextRecord;
  STACKFRAME64 frame{};
  frame.AddrPC.Offset = ctx.Rip;
  frame.AddrPC.Mode = AddrModeFlat;
  frame.AddrFrame.Offset = ctx.Rbp;
  frame.AddrFrame.Mode = AddrModeFlat;
  frame.AddrStack.Offset = ctx.Rsp;
  frame.AddrStack.Mode = AddrModeFlat;
  char desc[512];
  for (int i = 0; i < kMaxFrames; ++i) {
    Describe(process, frame.AddrPC.Offset, desc, sizeof(desc));
    fprintf(g_out, "  #%02d %016llx %s\n", i, static_cast<unsigned long long>(frame.AddrPC.Offset), desc);
    if (!StackWalk64(IMAGE_FILE_MACHINE_AMD64, process, GetCurrentThread(), &frame, &ctx, nullptr,
                     SymFunctionTableAccess64, SymGetModuleBase64, nullptr) ||
        frame.AddrPC.Offset == 0) {
      break;
    }
  }
  fflush(g_out);
  return EXCEPTION_CONTINUE_SEARCH;  // diagnostics only: let the runtime's handlers decide
}

// Installed at static-init time so it also covers early startup.
const PVOID g_handler = AddVectoredExceptionHandler(1, OnException);

}  // namespace

#endif  // _WIN32
