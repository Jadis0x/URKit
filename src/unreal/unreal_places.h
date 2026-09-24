#pragma once

// URK_UnrealPlace: a path to a value, resolved and bounds-checked on each use.

#include "mod_sdk.h"
#include "unreal_delegate_virtuals.h"
#include "unreal_enums.h"
#include "unreal_owned_values.h"
#include "unreal_sdk_api.h"

#include <cstdint>
#include <optional>
#include <string>

namespace URK::Unreal {

struct PlaceTarget {
    // Null when only the element type was asked for (an index of -1).
    std::uint8_t *value = nullptr;
    PropertyInfo info;
    // A set element or map key, or inside one: its hash places it, so it is
    // replaced by removing and adding, never changed where it is.
    bool key = false;
    // The object the root member belongs to; null for a frame parameter.
    Address owner = kNullAddress;
};

// What an object slot may hold: native code and the GC trust it blindly.
bool Assignable(const UnrealEngine &engine, const PropertyInfo &info, Address value);

// Checks an FName a struct write proposes; null refuses every changed name.
using NameCheck = bool (*)(void *context, const std::uint8_t *name, std::size_t size);

// Whether proposed may replace current: owned or unverifiable bytes must match.
bool StructChangeAllowed(const UnrealEngine &engine, Address structObject, const std::uint8_t *current,
                         const std::uint8_t *proposed, std::size_t size, int depth = 0, NameCheck names = nullptr,
                         void *namesContext = nullptr, bool strings = false);

// Copies only reflected members into merged; native bytes stay.
void MergeStructMembers(const UnrealEngine &engine, Address structObject, std::uint8_t *merged,
                        const std::uint8_t *proposed, std::size_t size, int depth = 0);

class Places {
  public:
    Places(UnrealEngine &engine, OwnedValues &owned, EnumNames &enums)
        : engine_(&engine), owned_(&owned), enums_(&enums),
          delegates_(engine.Finder(), engine.Chain(), engine.Values(), owned) {}

    const std::string &Failure() const { return failure_; }

    std::optional<PlaceTarget> Walk(std::uint8_t *root, const PropertyInfo &rootInfo, const URK_UnrealStep *steps,
                                    std::uint32_t count, bool describe, bool gameThread = false);

    // gameThread: whether the caller is on the engine's game thread. Anything
    // needing an engine call fails without it.
    bool ReadInteger(const PlaceTarget &target, std::int64_t *output);
    bool WriteInteger(const PlaceTarget &target, std::int64_t value);
    bool ReadFloating(const PlaceTarget &target, double *output);
    bool WriteFloating(const PlaceTarget &target, double value);
    bool ReadBool(const PlaceTarget &target, bool *output);
    bool WriteBool(const PlaceTarget &target, bool value);
    Address ReadObject(const PlaceTarget &target, bool gameThread);
    bool WriteObject(const PlaceTarget &target, Address value, bool gameThread);
    std::optional<std::string> ReadText(const PlaceTarget &target, bool gameThread);
    bool WriteText(const PlaceTarget &target, const char *utf8, bool gameThread);
    bool ReadBytes(const PlaceTarget &target, void *output, std::size_t size);
    bool WriteBytes(const PlaceTarget &target, const void *value, std::size_t size, bool gameThread);

    std::int32_t Count(const PlaceTarget &target, bool gameThread = false);
    std::int32_t Slots(const PlaceTarget &target, std::int32_t *output, std::int32_t capacity);
    bool Insert(const PlaceTarget &target, std::int32_t index, std::int32_t count, bool gameThread);
    bool Remove(const PlaceTarget &target, std::int32_t index, std::int32_t count, bool gameThread);
    bool Clear(const PlaceTarget &target, bool gameThread);
    std::int32_t Find(const PlaceTarget &target, const URK_UnrealKey &key, bool gameThread);
    std::int32_t Add(const PlaceTarget &target, const URK_UnrealKey &key, bool gameThread);
    bool Bind(const PlaceTarget &target, Address object, const char *function, bool gameThread);

  private:
    bool Fail(std::string why);
    bool NeedGameThread(bool gameThread);
    // Array or multicast header sanity, and the element's shape.
    std::optional<PropertyInfo> ElementOf(const PropertyInfo &container);
    bool ArrayHeader(const std::uint8_t *value, std::int32_t elementSize, std::int32_t *num);
    // A key value in a buffer: engine-made when forAdd, else a comparison stand-in.
    bool BuildKey(const PropertyInfo &key, const URK_UnrealKey &input, bool forAdd, std::vector<std::uint8_t> *bytes,
                  std::u16string *text);
    bool SignatureCompatible(Address signature, Address function);
    // An FScriptDelegate for object's function, checked against signature.
    bool MakeDelegate(Address signature, Address object, const char *function, std::uint8_t *delegate);
    // A sparse delegate's bindings list in engine storage; null when unbound.
    const std::uint8_t *SparseList(const PlaceTarget &target, bool gameThread);

    UnrealEngine *engine_;
    OwnedValues *owned_;
    EnumNames *enums_;
    DelegateVirtuals delegates_;
    // Per thread: reads run off the game thread too.
    inline static thread_local std::string failure_;
};

} // namespace URK::Unreal
