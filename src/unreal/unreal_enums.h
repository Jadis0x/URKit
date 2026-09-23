#pragma once

// A UEnum's names and values as the running game holds them. Where UEnum keeps
// them moved between versions (a TArray of name/value pairs, then two parallel
// arrays in UE5.8), so the storage is found by what it holds and accepted only
// when the engine's own KismetNodeHelperLibrary::GetEnumeratorName agrees with
// every name it can be asked about. Measuring runs on the game thread; reading
// afterwards works from any thread.

#include "unreal_engine_calls.h"

#include <atomic>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace URK::Unreal {

class EnumNames {
  public:
    EnumNames(const ObjectFinder &finder, const StructOffsets &structs) : finder_(&finder), structs_(structs) {}

    // Game thread, once. False (with Failure) when no storage agreed.
    bool Measure(EngineCalls &engine);
    bool Measured() const { return state_.load(std::memory_order_acquire) == 1; }
    const std::string &Failure() const { return failure_; }

    struct Entry {
        std::string name; // as stored, "Enum::Value" for scoped enums
        std::int64_t value = 0;
    };
    std::optional<std::vector<Entry>> Entries(Address enumObject) const;

    // The value name without its "Enum::" scope.
    static std::string_view ShortName(std::string_view name);
    std::optional<std::int64_t> ValueOf(Address enumObject, std::string_view name) const;
    std::optional<std::string> NameOf(Address enumObject, std::int64_t value) const;

  private:
    enum class Form { Pairs, Parallel };
    std::optional<std::vector<Entry>> Read(Address enumObject, std::int32_t offset, Form form) const;

    const ObjectFinder *finder_;
    StructOffsets structs_;
    std::atomic<int> state_{0}; // 0 unmeasured, 1 ready, 2 failed
    std::string failure_;
    std::int32_t offset_ = kOffsetNotFound;
    Form form_ = Form::Pairs;
};

} // namespace URK::Unreal
