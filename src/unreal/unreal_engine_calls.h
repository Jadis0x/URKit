#pragma once

// Engine memory handled by native Kismet calls, each measured before use.
// Game thread only.

#include "unreal_functions.h"
#include "unreal_module.h"
#include "unreal_process_event.h"
#include "unreal_type_queries.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace URK::Unreal {

// FString, TArray, FUtf8String: data pointer, Num, Max.
inline constexpr std::int32_t kArrayHeaderSize = 16;
// Far above any real container; a larger count means the header is not one.
inline constexpr std::int32_t kMaxContainerElements = 1 << 26;

// Where facts worth one log line go: a hash rule proven, a buffer leaked.
using MemoryNote = void (*)(const std::string &message);
void SetMemoryNote(MemoryNote note);
void NoteMemory(const std::string &message);

// Executable code of a loaded image (in process).
bool ImageCode(const void *address);

struct ParameterSpec {
    const char *name;
    PropertyKind kind;
    std::int32_t size; // 0: any
};

// One native function and its parameter block, bound once.
class NativeCall {
  public:
    bool Bind(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
              const FunctionOffsets &functions, const TypeQueries &types, const char *library, const char *function,
              std::initializer_list<ParameterSpec> parameters, std::string *failure);
    bool Bound() const { return function_ != kNullAddress; }

    std::uint8_t *At(const char *parameter);
    std::int32_t SizeOf(const char *parameter) const;
    bool Invoke(const ProcessEventLocation &processEvent);
    void Clear();

  private:
    Address library_ = kNullAddress;
    Address function_ = kNullAddress;
    FunctionInfo info_;
    std::vector<std::uint8_t> parms_;
};

class EngineCalls {
  public:
    EngineCalls(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
                const FunctionOffsets &functions, const TypeQueries &types, const StructOffsets &structs,
                const ProcessEventLocation &processEvent, const FunctionTable &bounds)
        : finder_(&finder), chain_(&chain), values_(&values), functions_(&functions), types_(&types),
          structs_(structs), processEvent_(processEvent), bounds_(&bounds) {}

    // Why the last operation failed.
    const std::string &Failure() const { return failure_; }

    struct Block {
        std::uint8_t *data = nullptr;
        std::size_t bytes = 0;
    };
    // At least bytes of FMemory aligned to alignment, allocated by the engine as
    // a string buffer; the capacity it reports is the usable size.
    std::optional<Block> Allocate(std::size_t bytes, std::size_t alignment);
    // Frees an FMemory block through the engine's move assignment. Null is fine.
    bool Free(void *data);
    // Whether Free can run: the native call bound and measured, nothing freed.
    bool FreeReady();
    // Free for a buffer already detached from engine memory: a failure leaks
    // it and is noted, never leaves it reachable.
    void ReleaseOrLeak(void *data, const char *what);
    // Frees an array's buffer (never its elements) and zeroes the header. If
    // the engine failed after taking the buffer, the header is zeroed anyway.
    bool EmptyArray(std::uint8_t *header);
    // New engine-owned buffer; the old one is freed by the engine.
    bool AssignChars(std::uint8_t *header, const void *chars, std::size_t count, std::size_t charSize);

    // FText. Reads go through the engine's own conversion; assignment puts the
    // old value where a native call's move assignment releases it.
    std::optional<std::string> TextToString(const std::uint8_t *text);
    bool AssignText(std::uint8_t *text, std::u16string_view value);
    // Makes a zeroed (or valid) slot hold the engine's empty text.
    bool MakeEmptyText(std::uint8_t *text);
    // Drops the slot's reference through the text data's own Release (measured)
    // and zeroes it. Whether this build allows it is known after the first try.
    bool ReleaseText(std::uint8_t *text);
    bool TextReleaseAvailable();
    // Whether ReleaseText would release this text, without releasing it.
    bool CanReleaseText(const std::uint8_t *text);

    // FName from text, through the engine's name table.
    bool MakeName(std::u16string_view text, std::uint8_t *name, std::size_t size);
    // Whether name holds an FName the table has: its entry is read, then named
    // again by the engine, and only the engine's own answer is accepted.
    bool ValidName(const std::uint8_t *name, std::size_t size);

    // Soft references: TSoftObjectPtr (or TSoftClassPtr when isClass).
    bool AssignSoftObject(std::uint8_t *soft, std::size_t size, Address object, bool isClass);
    bool AssignSoftPath(std::uint8_t *soft, std::size_t size, std::u16string_view path, bool isClass);
    std::optional<std::string> SoftPath(const std::uint8_t *soft, std::size_t size, bool isClass);
    Address SoftTarget(const std::uint8_t *soft, std::size_t size, bool isClass);
    // Where FSoftObjectPath sits in a soft reference and its struct; for release.
    std::int32_t SoftPathOffset();
    Address SoftPathStruct();

    // Weak references: {ObjectIndex, SerialNumber}, the serial assigned by the engine.
    static constexpr std::size_t kWeakSize = 8;
    bool MakeWeak(Address object, std::uint8_t *weak);
    Address WeakTarget(const std::uint8_t *weak);
    // Reading a weak pointer needs the serial number's place, measured once on
    // the game thread; after that it is a plain read.
    bool WeakReady() const { return weakState_ == State::Ready; }
    bool MeasureWeakNow() { return MeasureWeak(); }

    // FName to text through the engine, for checking name reads.
    std::optional<std::string> EnumeratorName(Address enumObject, std::uint8_t value);

  private:
    enum class State { Unbound, Ready, Failed };
    bool Ensure(State &state, NativeCall &call, const char *library, const char *function,
                std::initializer_list<ParameterSpec> parameters);
    bool Fail(State &state, std::string why);
    // *handedOver: the engine got the buffer, so on failure it may be gone.
    bool FreeThroughLeft(std::uint8_t *header, bool *handedOver);
    bool MeasureTextRelease();
    bool MeasureSharedTextRelease(std::uint8_t *probe);
    bool MeasureWeak();
    bool TakeString(NativeCall &call, const char *parameter, std::optional<std::string> *out);

    const ObjectFinder *finder_;
    const PropertyChain *chain_;
    const PropertyValues *values_;
    const FunctionOffsets *functions_;
    const TypeQueries *types_;
    StructOffsets structs_;
    ProcessEventLocation processEvent_;
    const FunctionTable *bounds_;
    std::string failure_;

    State leftState_ = State::Unbound;
    NativeCall left_;
    State padState_ = State::Unbound;
    NativeCall pad_;
    State textToStringState_ = State::Unbound;
    NativeCall textToString_;
    State stringToTextState_ = State::Unbound;
    NativeCall stringToText_;
    State emptyTextState_ = State::Unbound;
    NativeCall emptyText_;
    State textReleaseState_ = State::Unbound;
    // 16: TRefCountPtr<ITextData>. 24: TSharedRef<ITextData, ThreadSafe> (UE4).
    std::int32_t textSize_ = 0;
    State nameState_ = State::Unbound;
    NativeCall name_;
    State softFromObjectState_[2] = {State::Unbound, State::Unbound};
    NativeCall softFromObject_[2];
    State softPathMakeState_[2] = {State::Unbound, State::Unbound};
    NativeCall softPathMake_[2];
    State softFromPathState_[2] = {State::Unbound, State::Unbound};
    NativeCall softFromPath_[2];
    State softToStringState_[2] = {State::Unbound, State::Unbound};
    NativeCall softToString_[2];
    State softToObjectState_[2] = {State::Unbound, State::Unbound};
    NativeCall softToObject_[2];
    State softLayoutState_ = State::Unbound;
    std::int32_t softPathOffset_ = kOffsetNotFound;
    std::int32_t softSubPathOffset_ = kOffsetNotFound;
    PropertyKind softSubPathKind_ = PropertyKind::Unknown;
    Address softPathStruct_ = kNullAddress;
    State weakState_ = State::Unbound;
    std::int32_t serialOffset_ = kOffsetNotFound;
    State enumeratorState_ = State::Unbound;
    NativeCall enumerator_;
    std::vector<Address> releaseFunctions_;
};

} // namespace URK::Unreal
