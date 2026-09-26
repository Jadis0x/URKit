// The in-process reader's range cache, against memory this test reserves, commits and frees.

#include "src/unreal/memory/unreal_process_memory.h"

#include <windows.h>

#include <cstdio>
#include <string>
#include <thread>

namespace {

using URK::Unreal::Address;
using URK::Unreal::ProcessMemory;

int g_failures = 0;

void Check(bool condition, const std::string &what) {
    std::printf("%-72s %s\n", what.c_str(), condition ? "ok" : "FAILED");
    if (!condition)
        ++g_failures;
}

Address At(void *pointer, std::size_t offset = 0) { return reinterpret_cast<Address>(pointer) + offset; }

void CommittedReads() {
    ProcessMemory memory;
    int value = 7;
    int out = 0;
    Check(memory.Read(At(&value), &out, sizeof(out)) && out == 7, "a committed int reads back");
    Check(!memory.Read(0, &out, sizeof(out)), "null is refused");
}

// A region seen unreadable, then committed: the cached "no" must not outlive the allocation.
void UnreadableHeals() {
    ProcessMemory memory;
    void *reserved = VirtualAlloc(nullptr, 1 << 20, MEM_RESERVE, PAGE_NOACCESS);
    const Address page = At(reserved, 0x1000);
    int out = 0;
    Check(!memory.Read(page, &out, sizeof(out)), "reserved memory is refused");
    VirtualAlloc(reinterpret_cast<void *>(page), 0x1000, MEM_COMMIT, PAGE_READWRITE);
    *reinterpret_cast<int *>(page) = 42;
    Sleep(2 * static_cast<DWORD>(ProcessMemory::kUnreadableMs));
    Check(memory.Read(page, &out, sizeof(out)) && out == 42, "committed later, read once the miss expires");
    VirtualFree(reserved, 0, MEM_RELEASE);
}

// A cached "yes" for memory freed since: the read fails instead of faulting.
void FreedFailsQuietly() {
    ProcessMemory memory;
    void *block = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    int out = 0;
    Check(memory.Read(At(block), &out, sizeof(out)), "fresh allocation reads");
    VirtualFree(block, 0, MEM_RELEASE);
    Check(!memory.Read(At(block), &out, sizeof(out)), "freed since the cached yes: refused, no fault");
    Check(!memory.Read(At(block), &out, sizeof(out)), "and refused again after the cache healed");
}

// More regions than the cache holds, in turn: every read still answers right. Run on its own
// thread: the cache is per thread, and a granule freed just before would still be a cached "no".
void ManyRegions() {
    ProcessMemory memory;
    constexpr int kRegions = 64;
    void *blocks[kRegions]{};
    for (int i = 0; i < kRegions; ++i) {
        blocks[i] = VirtualAlloc(nullptr, 0x1000, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
        *static_cast<int *>(blocks[i]) = i;
    }
    bool all = true;
    for (int round = 0; round < 3; ++round) {
        for (int i = 0; i < kRegions; ++i) {
            int out = -1;
            if (!memory.Read(At(blocks[i]), &out, sizeof(out)) || out != i) {
                if (all)
                    std::printf("first miss: round %d region %d at %p out=%d\n", round, i, blocks[i], out);
                all = false;
            }
        }
    }
    Check(all, "64 regions read in turn, three rounds");
    for (void *block : blocks)
        VirtualFree(block, 0, MEM_RELEASE);
}

} // namespace

int main() {
    CommittedReads();
    UnreadableHeals();
    FreedFailsQuietly();
    std::thread(ManyRegions).join();
    if (g_failures != 0)
        std::printf("%d check(s) failed\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
