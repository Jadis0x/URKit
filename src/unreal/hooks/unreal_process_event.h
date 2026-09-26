#pragma once

// ProcessEvent's vtable slot, found by the fields it reads. Address is per object (overrides).

#include "unreal/memory/unreal_module.h"
#include "unreal/reflection/unreal_functions.h"
#include "unreal/reflection/unreal_type_queries.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

namespace URK::Unreal {

struct ProcessEventLocation {
    std::int32_t vtableIndex = kOffsetNotFound;
    Address baseImplementation = kNullAddress;

    // Why this slot won, so a caller can judge the margin.
    std::int32_t parmsSizeReferences = 0;
    std::int32_t flagsReferences = 0;
    std::int32_t returnOffsetReferences = 0;
    // Any rival means the fields stopped naming one function; answer untrusted.
    std::vector<std::int32_t> rivalSlots;

    bool Unique() const { return rivalSlots.empty(); }
    bool Resolved() const {
        return vtableIndex != kOffsetNotFound && baseImplementation != kNullAddress && Unique();
    }
};

// Unresolved with rivals listed when more than one slot qualifies.
std::optional<ProcessEventLocation> FindProcessEvent(const ObjectFinder &finder, const TypeQueries &types,
                                                     const StructOffsets &structs, const FunctionOffsets &functions,
                                                     std::span<const ScanRegion> codeRegions,
                                                     const FunctionTable &bounds = {}, std::string *why = nullptr);

// The implementation this object dispatches to, read from its own vtable.
Address ProcessEventFor(const MemoryReader &reader, const ProcessEventLocation &location, Address object);

// Every distinct implementation, base first; hooking only the base misses actors.
std::vector<Address> ProcessEventImplementations(const ObjectFinder &finder, const TypeQueries &types,
                                                 const StructOffsets &structs,
                                                 const ProcessEventLocation &location);

// Call parameter block, zeroed including padding.
class CallFrame {
  public:
    // Owns its FunctionInfo; frames outlive the caller's copy across the ABI.
    explicit CallFrame(FunctionInfo info)
        : info_(std::make_shared<const FunctionInfo>(std::move(info))),
          bytes_(static_cast<std::size_t>(info_->parmsSize), 0) {}

    // View over an in-flight call; returned points at an external return value.
    CallFrame(std::shared_ptr<const FunctionInfo> info, void *data, void *returned)
        : info_(std::move(info)), view_(static_cast<std::uint8_t *>(data)),
          returned_(static_cast<std::uint8_t *>(returned)) {}

    const FunctionInfo &Function() const { return *info_; }
    bool View() const { return view_ != nullptr; }
    void *Data() { return view_ ? view_ : bytes_.empty() ? nullptr : bytes_.data(); }
    const void *Data() const { return view_ ? view_ : bytes_.empty() ? nullptr : bytes_.data(); }
    std::size_t Size() const { return view_ ? static_cast<std::size_t>(info_->parmsSize) : bytes_.size(); }

    // Where a parameter's value lives; null when it does not fit the block.
    std::uint8_t *Slot(const FunctionParameter &parameter);
    const std::uint8_t *Slot(const FunctionParameter &parameter) const;

    void Clear() { std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0}); }

    // Size must match the property's; a wrong-shaped frame corrupts later calls.
    bool Set(std::string_view name, const void *value, std::size_t size);
    template <typename T> bool Set(std::string_view name, const T &value) {
        static_assert(std::is_trivially_copyable_v<T>, "the frame holds raw bytes");
        return Set(name, &value, sizeof(T));
    }

    bool Get(std::string_view name, void *out, std::size_t size) const;
    template <typename T> std::optional<T> Get(std::string_view name) const {
        static_assert(std::is_trivially_copyable_v<T>, "the frame holds raw bytes");
        T value{};
        if (!Get(name, &value, sizeof(T)))
            return std::nullopt;
        return value;
    }

    template <typename T> std::optional<T> Returned() const {
        const FunctionParameter *parameter = info_->Returned();
        return parameter ? Get<T>(parameter->name) : std::nullopt;
    }

  private:
    const FunctionParameter *Find(std::string_view name) const;

    // Declared before bytes_, which is sized from it.
    std::shared_ptr<const FunctionInfo> info_;
    std::vector<std::uint8_t> bytes_;
    std::uint8_t *view_ = nullptr;
    std::uint8_t *returned_ = nullptr;
};

// In-process only, on an engine thread.
bool InvokeProcessEvent(const ProcessEventLocation &location, Address object, Address function, void *frame);

inline bool InvokeProcessEvent(const ProcessEventLocation &location, Address object, CallFrame &frame) {
    return InvokeProcessEvent(location, object, frame.Function().function, frame.Data());
}

} // namespace URK::Unreal
