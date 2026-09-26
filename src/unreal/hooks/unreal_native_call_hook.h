#pragma once

// Native functions called straight from Blueprint bytecode: their Func is swapped for a stub that
// evaluates the arguments and sends the call through UObject::ProcessEvent, where hooks see it.

#include "unreal/memory/unreal_module.h"
#include "unreal/reflection/unreal_functions.h"

#include <array>
#include <atomic>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <unordered_map>

namespace URK::Unreal {

class OwnedValues;
class PropertyVirtuals;

class NativeCallHook {
  public:
    static NativeCallHook &Instance();

    // Once: GNatives and FFrame::Code from execEndFunctionParms; later calls return the first answer.
    bool Prepare(const MemoryReader &reader, std::span<const ScanRegion> data, std::span<const ScanRegion> code,
                 Address objectProcessEvent, std::int32_t funcOffset, OwnedValues &owned,
                 PropertyVirtuals &virtuals);
    bool Ready() const { return ready_; }
    const std::string &Failure() const { return failure_; }

    // Why a function is not routed (empty when it can be).
    static std::string Refusal(const FunctionInfo &info, const std::string &ownerName);
    bool Attach(std::shared_ptr<const FunctionInfo> info);
    void Detach(Address function);

  private:
    using NativeFn = void(__fastcall *)(void *context, void *stack, void *result);

    struct Entry {
        std::shared_ptr<const FunctionInfo> info;
        void *original = nullptr;
        std::uint8_t *stub = nullptr;
        std::atomic<bool> active{false};
    };

    NativeCallHook() = default;

    static void __fastcall Thunk(void *context, std::uint8_t *stack, void *result, Entry *entry);
    void Route(void *context, std::uint8_t *stack, void *result, Entry &entry);
    void Step(void *object, std::uint8_t *stack, void *result) const;
    std::uint8_t *MakeStub(Entry *entry);

    std::mutex mutex_;
    bool attempted_ = false;
    bool ready_ = false;
    std::string failure_;
    const NativeFn *natives_ = nullptr;
    std::int32_t codeOffset_ = -1;
    std::int32_t funcOffset_ = -1;
    Address processEvent_ = kNullAddress;
    OwnedValues *owned_ = nullptr;
    PropertyVirtuals *virtuals_ = nullptr;
    std::unordered_map<Address, std::unique_ptr<Entry>> entries_;
    std::uint8_t *stubPage_ = nullptr;
    std::size_t stubsUsed_ = 0;
};

} // namespace URK::Unreal
