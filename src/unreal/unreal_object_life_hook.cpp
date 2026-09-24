#include "unreal_object_life_hook.h"
#include "unreal_code_anchors.h"

#include <windows.h>

namespace URK::Unreal {
namespace {

// Where ObjObjects may sit past FUObjectArray's start (0x10 before UE5.8).
constexpr Address kArrayFieldsBefore = 0x40;

// An observer's own work is not reported back to it.
thread_local bool g_inObserver = false;

// Only between a patch going live and attach returning its trampoline.
template <typename Fn> Fn Original(const std::atomic<Fn> &slot) {
    Fn original = slot.load(std::memory_order_acquire);
    while (!original) {
        YieldProcessor();
        original = slot.load(std::memory_order_acquire);
    }
    return original;
}

// The one function whose code logs text, in any of the modules.
Address OnlyFunctionLogging(const MemoryReader &reader, std::span<const Address> modules, const FunctionTable &bounds,
                            std::string_view text) {
    Address found = kNullAddress;
    for (const Address module : modules) {
        for (const Address owner : FunctionsReferencingText(reader, module, bounds, text)) {
            if (found != kNullAddress && found != owner)
                return kNullAddress;
            found = owner;
        }
    }
    return found;
}

} // namespace

ObjectLifeHook &ObjectLifeHook::Instance() {
    static ObjectLifeHook hook;
    return hook;
}

bool ObjectLifeHook::Install(const MemoryReader &reader, std::span<const Address> modules, const FunctionTable &bounds,
                             const ObjectArray &objects, std::int32_t indexOffset, const HookInstaller &installer) {
    if (attempted_)
        return Installed();
    attempted_ = true;
    const auto fail = [this](std::string why) {
        failure_ = std::move(why);
        return false;
    };
    const Address objectArray = objects.BaseAddress();
    if (!installer.Valid() || bounds.Empty() || objectArray == kNullAddress || indexOffset == kOffsetNotFound)
        return fail("the hook installer or the object array is unavailable");
    const Address allocate =
        OnlyFunctionLogging(reader, modules, bounds, "Unable to add more objects to disregard for GC pool (Max: %d)");
    const Address free = OnlyFunctionLogging(
        reader, modules, bounds,
        "Removing object (0x%016llx) at index %d but the index points to a different object (0x%016llx)!");
    if (allocate == kNullAddress || free == kNullAddress || allocate == free)
        return fail("FUObjectArray's AllocateUObjectIndex and FreeUObjectIndex were not each found once");

    objectArray_ = objectArray;
    objects_.emplace(objects);
    indexOffset_ = indexOffset;
    reader_ = &reader;
    void *allocateTrampoline = nullptr;
    void *allocateHandle = installer.attach(installer.context, reinterpret_cast<void *>(allocate),
                                            reinterpret_cast<void *>(&AllocateDetour), &allocateTrampoline);
    if (!allocateHandle || !allocateTrampoline) {
        if (allocateHandle)
            installer.detach(installer.context, allocateHandle);
        return fail("AllocateUObjectIndex could not be hooked");
    }
    allocateOriginal_.store(reinterpret_cast<IndexFn>(allocateTrampoline), std::memory_order_release);
    void *freeTrampoline = nullptr;
    void *freeHandle = installer.attach(installer.context, reinterpret_cast<void *>(free),
                                        reinterpret_cast<void *>(&FreeDetour), &freeTrampoline);
    if (!freeHandle || !freeTrampoline) {
        if (freeHandle)
            installer.detach(installer.context, freeHandle);
        // Leave the first patch: a call may be inside its trampoline.
        return fail("FreeUObjectIndex could not be hooked");
    }
    freeOriginal_.store(reinterpret_cast<IndexFn>(freeTrampoline), std::memory_order_release);
    allocate_ = allocate;
    free_ = free;
    installed_.store(true, std::memory_order_release);
    return true;
}

void ObjectLifeHook::Observe(Observer observer, void *user) {
    observerUser_.store(user, std::memory_order_release);
    observer_.store(observer, std::memory_order_release);
}

// FUObjectArray: ObjObjects sits a few fields past `this`.
bool ObjectLifeHook::OnArray(void *self) {
    const auto address = reinterpret_cast<Address>(self);
    const Address known = array_.load(std::memory_order_acquire);
    if (known != kNullAddress)
        return address == known;
    if (address > objectArray_ || objectArray_ - address > kArrayFieldsBefore || address % 8 != 0)
        return false;
    Address expected = kNullAddress;
    return array_.compare_exchange_strong(expected, address, std::memory_order_acq_rel) || expected == address;
}

void *ObjectLifeHook::Subject(void *self, void *object) {
    if (OnArray(self))
        return object;
    const auto address = reinterpret_cast<Address>(self);
    const std::optional<std::int32_t> index = reader_->ReadInt32(address + indexOffset_);
    return index && *index >= 0 && objects_->ObjectAt(*index) == address ? self : nullptr;
}

void ObjectLifeHook::Report(void *object, bool created) {
    const Observer observer = observer_.load(std::memory_order_acquire);
    if (!observer || g_inObserver || !object)
        return;
    g_inObserver = true;
    observer(observerUser_.load(std::memory_order_acquire), reinterpret_cast<Address>(object), created);
    g_inObserver = false;
}

void __fastcall ObjectLifeHook::AllocateDetour(void *self, void *object, void *a, void *b, void *c, void *d) {
    ObjectLifeHook &hook = Instance();
    Original(hook.allocateOriginal_)(self, object, a, b, c, d);
    if (hook.observer_.load(std::memory_order_acquire))
        hook.Report(hook.Subject(self, object), true);
}

void __fastcall ObjectLifeHook::FreeDetour(void *self, void *object, void *a, void *b, void *c, void *d) {
    ObjectLifeHook &hook = Instance();
    if (hook.observer_.load(std::memory_order_acquire))
        hook.Report(hook.Subject(self, object), false);
    Original(hook.freeOriginal_)(self, object, a, b, c, d);
}

} // namespace URK::Unreal
