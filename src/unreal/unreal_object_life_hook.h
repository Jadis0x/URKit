#pragma once

// Object create/delete via FUObjectArray::AllocateUObjectIndex/FreeUObjectIndex,
// found by their fatal log strings. May be inlined into ~UObjectBase (5.4).

#include "unreal_module.h"
#include "unreal_object_array.h"
#include "unreal_process_event_hook.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <span>
#include <string>

namespace URK::Unreal {

class ObjectLifeHook {
  public:
    using Observer = void (*)(void *user, Address object, bool created);

    static ObjectLifeHook &Instance();

    // Once; later calls return the first answer.
    bool Install(const MemoryReader &reader, std::span<const Address> modules, const FunctionTable &bounds,
                 const ObjectArray &objects, std::int32_t indexOffset, const HookInstaller &installer);
    bool Installed() const { return installed_.load(std::memory_order_acquire); }
    const std::string &Failure() const { return failure_; }
    Address Allocate() const { return allocate_; }
    Address Free() const { return free_; }

    void Observe(Observer observer, void *user);

  private:
    // Covers every version's signature.
    using IndexFn = void(__fastcall *)(void *self, void *object, void *a, void *b, void *c, void *d);

    ObjectLifeHook() = default;

    static void __fastcall AllocateDetour(void *self, void *object, void *a, void *b, void *c, void *d);
    static void __fastcall FreeDetour(void *self, void *object, void *a, void *b, void *c, void *d);
    bool OnArray(void *self);
    // The array's second argument, or `this` when inlined into the object.
    void *Subject(void *self, void *object);
    void Report(void *object, bool created);

    std::atomic<bool> installed_{false};
    bool attempted_ = false;
    std::string failure_;
    Address allocate_ = kNullAddress;
    Address free_ = kNullAddress;
    Address objectArray_ = kNullAddress;
    std::optional<ObjectArray> objects_;
    std::int32_t indexOffset_ = kOffsetNotFound;
    const MemoryReader *reader_ = nullptr;
    // Learned from the first matching call.
    std::atomic<Address> array_{kNullAddress};
    std::atomic<IndexFn> allocateOriginal_{nullptr};
    std::atomic<IndexFn> freeOriginal_{nullptr};
    std::atomic<Observer> observer_{nullptr};
    std::atomic<void *> observerUser_{nullptr};
};

} // namespace URK::Unreal
