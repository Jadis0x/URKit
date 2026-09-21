#pragma once

// Calling a reflected function.
//
// Everything below this file reads the game; this is where it can be asked to
// do something. A reflected call goes through UObject::ProcessEvent, which a
// shipped title neither exports nor names, so it has to be recognised.
//
// It is recognised by the one thing it cannot avoid doing. To call a function
// ProcessEvent must build that function's parameter frame, and the sizes it
// needs are the fields the ladder has already measured - NumParms, ParmsSize,
// ReturnValueOffset. A UObject virtual that reads ParmsSize is doing the only
// job that field exists for, and no other slot in the vtable touches it. So the
// search is for a slot whose code refers to offsets this build was measured to
// use, not for bytes some compiler happened to emit.
//
// What is calibrated is the slot *index*, never the address behind it. Classes
// override ProcessEvent - AActor does - and an override is the implementation a
// call on one of its instances has to reach, so the address is read from the
// object's own vtable at call time.

#include "unreal_functions.h"
#include "unreal_module.h"
#include "unreal_type_queries.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <type_traits>
#include <vector>

namespace URK::Unreal {

struct ProcessEventLocation {
    // Slot in the UObject vtable. What is stable across classes.
    std::int32_t vtableIndex = kOffsetNotFound;
    // UObject's own implementation, which classes that do not override it use.
    Address baseImplementation = kNullAddress;

    // What the slot was chosen by, kept so a caller can see the margin.
    std::int32_t parmsSizeReferences = 0;
    std::int32_t flagsReferences = 0;
    std::int32_t returnOffsetReferences = 0;
    // Other slots that also referred to ParmsSize. Any at all means the field
    // stopped naming one function, so the answer is not trusted.
    std::vector<std::int32_t> rivalSlots;

    bool Unique() const { return rivalSlots.empty(); }
    bool Resolved() const {
        return vtableIndex != kOffsetNotFound && baseImplementation != kNullAddress && Unique();
    }
};

// Walks the UObject vtable looking for the slot that reads ParmsSize. Returns
// nothing when no slot does; returns an unresolved location, rivals listed,
// when more than one does, so a caller can see why rather than only that.
std::optional<ProcessEventLocation> FindProcessEvent(const ObjectFinder &finder, const TypeQueries &types,
                                                     const StructOffsets &structs, const FunctionOffsets &functions,
                                                     std::span<const ScanRegion> codeRegions);

// The implementation this object dispatches to, read from its own vtable.
Address ProcessEventFor(const MemoryReader &reader, const ProcessEventLocation &location, Address object);

// The parameter block a call is made with.
//
// The engine reads every byte of it, including the padding no parameter covers,
// so it is zeroed whole rather than only where parameters sit. Out parameters
// are read back from it after the call, which is also where a return value is.
class CallFrame {
  public:
    explicit CallFrame(const FunctionInfo &info) : info_(&info), bytes_(static_cast<std::size_t>(info.parmsSize), 0) {}

    const FunctionInfo &Function() const { return *info_; }
    void *Data() { return bytes_.empty() ? nullptr : bytes_.data(); }
    const void *Data() const { return bytes_.empty() ? nullptr : bytes_.data(); }
    std::size_t Size() const { return bytes_.size(); }

    void Clear() { std::fill(bytes_.begin(), bytes_.end(), std::uint8_t{0}); }

    // Writes a value into the slot the named parameter occupies. The size has
    // to match what the property says it is: a call whose frame is the wrong
    // shape corrupts the ones after it.
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

    // Whatever the function answers with, when it answers with something.
    template <typename T> std::optional<T> Returned() const {
        const FunctionParameter *parameter = info_->Returned();
        return parameter ? Get<T>(parameter->name) : std::nullopt;
    }

  private:
    const FunctionParameter *Find(std::string_view name) const;

    const FunctionInfo *info_;
    std::vector<std::uint8_t> bytes_;
};

// Calls a function on an object.
//
// This one is in-process only, and deliberately so: the frame has to live at an
// address the game can read, and the call has to happen on a thread the engine
// owns. Reading a game from outside asks nothing of it, and calling into one
// cannot be made to ask nothing, so the two are not offered through the same
// door.
bool InvokeProcessEvent(const ProcessEventLocation &location, Address object, Address function, void *frame);

// The same, with the frame the caller filled.
inline bool InvokeProcessEvent(const ProcessEventLocation &location, Address object, CallFrame &frame) {
    return InvokeProcessEvent(location, object, frame.Function().function, frame.Data());
}

} // namespace URK::Unreal
