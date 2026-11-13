// MatrixGame - licensed under GPLv2 or any later version.
#include "CrashReport.hpp"

#include "CException.hpp"
#include "utils.hpp"

#include <cstring>
#include <exception>
#include <time.h>

namespace CrashReport {
namespace {

void write_time(FILE *out) {
    SYSTEMTIME t;
    GetLocalTime(&t);
    std::fprintf(out, "== %04u-%02u-%02u %02u:%02u:%02u pid %lu thread %lu: ", t.wYear, t.wMonth, t.wDay, t.wHour,
                 t.wMinute, t.wSecond, GetCurrentProcessId(), GetCurrentThreadId());
}

// module+offset, or the bare address outside every module.
void write_where(FILE *out, DWORD address) {
    HMODULE module = nullptr;
    char name[MAX_PATH] = "?";
    if (GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                           reinterpret_cast<LPCSTR>(static_cast<uintptr_t>(address)), &module) &&
        module) {
        GetModuleFileNameA(module, name, MAX_PATH);
        const char *base = std::strrchr(name, '\\');
        std::fprintf(out, "%08lx %s+0x%lx\n", address, base ? base + 1 : name,
                     address - static_cast<DWORD>(reinterpret_cast<uintptr_t>(module)));
    }
    else {
        std::fprintf(out, "%08lx\n", address);
    }
}

bool executable(DWORD address) {
    MEMORY_BASIC_INFORMATION m;
    return address >= 0x10000 &&
           VirtualQuery(reinterpret_cast<const void *>(static_cast<uintptr_t>(address)), &m, sizeof m) &&
           m.State == MEM_COMMIT &&
           (m.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY));
}

}  // namespace

void write_fault(FILE *out, const EXCEPTION_POINTERS &ep) {
    const EXCEPTION_RECORD &r = *ep.ExceptionRecord;
    write_time(out);
    std::fprintf(out, "fault 0x%08lx at ", r.ExceptionCode);
    write_where(out, static_cast<DWORD>(reinterpret_cast<uintptr_t>(r.ExceptionAddress)));
    if (r.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && r.NumberParameters >= 2) {
        const ULONG_PTR kind = r.ExceptionInformation[0];
        std::fprintf(out, "%s 0x%08lx\n", kind == 0 ? "reading" : kind == 1 ? "writing" : "executing",
                     static_cast<DWORD>(r.ExceptionInformation[1]));
    }
#if defined(_M_IX86) || defined(__i386__)
    const CONTEXT &c = *ep.ContextRecord;
    std::fprintf(out, "eax=%08lx ebx=%08lx ecx=%08lx edx=%08lx esi=%08lx edi=%08lx ebp=%08lx esp=%08lx\n", c.Eax,
                 c.Ebx, c.Ecx, c.Edx, c.Esi, c.Edi, c.Ebp, c.Esp);
    // Return-address candidates: stack words that point into code, from esp up to the stack's end.
    const DWORD *sp = reinterpret_cast<const DWORD *>(static_cast<uintptr_t>(c.Esp));
    MEMORY_BASIC_INFORMATION m;
    if (sp && VirtualQuery(sp, &m, sizeof m) && m.State == MEM_COMMIT) {
        const DWORD *end = reinterpret_cast<const DWORD *>(static_cast<const char *>(m.BaseAddress) + m.RegionSize);
        int written = 0;
        for (const DWORD *p = sp; p < end && written < 64; ++p) {
            if (!executable(*p))
                continue;
            std::fprintf(out, "  [esp+%x] ", static_cast<unsigned>((p - sp) * sizeof(DWORD)));
            write_where(out, *p);
            ++written;
        }
    }
#endif
    std::fflush(out);
}

void write_exception(FILE *out, const char *what) {
    write_time(out);
    std::fprintf(out, "exception: %s\n", what ? what : "?");
    std::fflush(out);
}

namespace {
// The fault's memory too (logs\crash.dmp, the last only: a few MB): crash.txt's addresses tell where, the dump what
// the objects there held. dbghelp is Windows' own, loaded only now.
bool write_dump(const EXCEPTION_POINTERS &ep) {
    using Write = BOOL(WINAPI *)(HANDLE, DWORD, HANDLE, int, void *, void *, void *);
    HMODULE dbghelp = LoadLibraryW(L"dbghelp.dll");
    const auto write =
            dbghelp ? reinterpret_cast<Write>(reinterpret_cast<void *>(GetProcAddress(dbghelp, "MiniDumpWriteDump")))
                    : nullptr;
    if (!write)
        return false;
    HANDLE file = CreateFileW(L"logs\\crash.dmp", GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE)
        return false;
    struct {
        DWORD thread;
        EXCEPTION_POINTERS *pointers;
        BOOL client;
    } info{GetCurrentThreadId(), const_cast<EXCEPTION_POINTERS *>(&ep), FALSE};
    // MiniDumpWithDataSegs | MiniDumpWithIndirectlyReferencedMemory | MiniDumpWithThreadInfo
    const BOOL done = write(GetCurrentProcess(), GetCurrentProcessId(), file, 0x1 | 0x40 | 0x1000, &info, nullptr, nullptr);
    CloseHandle(file);
    return done != FALSE;
}
}  // namespace

bool fault(const EXCEPTION_POINTERS &ep) {
    FILE *out = std::fopen(file_name, "a");
    if (!out)
        return false;
    write_fault(out, ep);
    std::fclose(out);
    const bool dumped = write_dump(ep);
    if ((out = std::fopen(file_name, "a"))) {
        std::fprintf(out, "  dump: %s\n", dumped ? "logs\\crash.dmp" : "not written");
        std::fclose(out);
    }
    return true;
}

bool exception(const char *what) {
    FILE *out = std::fopen(file_name, "a");
    if (!out)
        return false;
    write_exception(out, what);
    std::fclose(out);
    return true;
}

std::string current_exception() {
    try {
        throw;
    }
    catch (const Base::CException &e) {
        return utils::from_wstring(e.Info());
    }
    catch (const std::exception &e) {
        return e.what();
    }
    catch (...) {
        return "unknown C++ exception";
    }
}

}  // namespace CrashReport
