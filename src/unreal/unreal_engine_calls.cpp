#include "unreal_engine_calls.h"
#include "unreal_process_event_hook.h"
#include "unreal_text.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cstring>

namespace URK::Unreal {

// Leaf functions have no .pdata entry; probes prove the rest.
bool ImageCode(const void *address) {
    MEMORY_BASIC_INFORMATION info{};
    if (!address || VirtualQuery(address, &info, sizeof(info)) != sizeof(info))
        return false;
    constexpr DWORD kExecutable = PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY;
    return info.State == MEM_COMMIT && info.Type == MEM_IMAGE && (info.Protect & kExecutable) != 0;
}

namespace {

constexpr const char *kLibraryPackage = "/Script/Engine";

bool AllZero(const std::uint8_t *bytes, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) {
        if (bytes[i])
            return false;
    }
    return true;
}

template <typename T> T Load(const std::uint8_t *at) {
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

template <typename T> void Store(std::uint8_t *at, T value) { std::memcpy(at, &value, sizeof(T)); }

// Mod-side FString passed as const input; the thunk copies it, we keep the buffer.
struct InputString {
    explicit InputString(std::u16string_view text) : units(text) { units.push_back(u'\0'); }
    void WriteHeader(std::uint8_t *at) const {
        if (units.size() == 1) {
            std::memset(at, 0, kArrayHeaderSize);
            return;
        }
        Store<const void *>(at, units.data());
        Store<std::int32_t>(at + 8, static_cast<std::int32_t>(units.size()));
        Store<std::int32_t>(at + 12, static_cast<std::int32_t>(units.size()));
    }
    std::u16string units;
};

// Hidden result pointer (FReturnedRefCountValue); older builds ignore it.
using CountFn = void *(__fastcall *)(const void *self, std::uint32_t *result);
using AddRefFn = void(__fastcall *)(const void *self);


std::uint32_t CallCount(void *function, const void *self) {
    alignas(8) std::uint32_t buffer[2] = {0xFFFFFFFFu, 0};
    void *returned = reinterpret_cast<CountFn>(function)(self, buffer);
    if (returned == static_cast<void *>(buffer))
        return buffer[0];
    return static_cast<std::uint32_t>(reinterpret_cast<std::uintptr_t>(returned));
}

} // namespace

// --- NativeCall -------------------------------------------------------------

bool NativeCall::Bind(const ObjectFinder &finder, const PropertyChain &chain, const PropertyValues &values,
                      const FunctionOffsets &functions, const TypeQueries &types, const char *library,
                      const char *function, std::initializer_list<ParameterSpec> parameters, std::string *failure) {
    const std::string name = std::string(library) + "::" + function;
    const Address klass = finder.FindInOuter(library, kLibraryPackage);
    const Address found = klass != kNullAddress ? finder.FindInOuter(function, klass) : kNullAddress;
    const Address cdo = klass != kNullAddress ? types.DefaultObjectOf(klass) : kNullAddress;
    const std::optional<FunctionInfo> info =
        found != kNullAddress ? DescribeFunction(chain, values, functions, found) : std::nullopt;
    if (!info || cdo == kNullAddress) {
        *failure = name + " was not found";
        return false;
    }
    if (info->parameters.size() != parameters.size()) {
        *failure = name + " has an unexpected signature";
        return false;
    }
    for (const ParameterSpec &spec : parameters) {
        const FunctionParameter *parameter = info->Parameter(spec.name);
        if (!parameter || parameter->info.kind != spec.kind || (spec.size && parameter->info.elementSize != spec.size) ||
            parameter->info.offset < 0 || parameter->info.elementSize <= 0 ||
            parameter->info.offset + parameter->info.elementSize > info->parmsSize) {
            *failure = name + " has an unexpected parameter " + spec.name;
            return false;
        }
    }
    library_ = cdo;
    function_ = found;
    info_ = *info;
    parms_.assign(static_cast<std::size_t>(info->parmsSize), 0);
    return true;
}

std::uint8_t *NativeCall::At(const char *parameter) {
    const FunctionParameter *found = info_.Parameter(parameter);
    return found ? parms_.data() + found->info.offset : nullptr;
}

std::int32_t NativeCall::SizeOf(const char *parameter) const {
    const FunctionParameter *found = info_.Parameter(parameter);
    return found ? found->info.elementSize : 0;
}

bool NativeCall::Invoke(const ProcessEventLocation &processEvent) {
    const ProcessEventHook::PassThrough quiet;
    return InvokeProcessEvent(processEvent, library_, function_, parms_.data());
}

void NativeCall::Clear() { std::fill(parms_.begin(), parms_.end(), std::uint8_t{0}); }

// --- EngineCalls ------------------------------------------------------------

bool EngineCalls::Fail(State &state, std::string why) {
    state = State::Failed;
    failure_ = std::move(why);
    return false;
}

bool EngineCalls::Ensure(State &state, NativeCall &call, const char *library, const char *function,
                         std::initializer_list<ParameterSpec> parameters) {
    if (state == State::Ready)
        return true;
    if (state == State::Failed) {
        failure_ = std::string(library) + "::" + function + " is unusable in this build";
        return false;
    }
    std::string why;
    if (!processEvent_.Resolved())
        return Fail(state, "ProcessEvent is not resolved");
    if (!call.Bind(*finder_, *chain_, *values_, *functions_, *types_, library, function, parameters, &why))
        return Fail(state, why);
    state = State::Ready;
    return true;
}

// Left("", 0) returns an empty FString; its move assignment frees the slot.
bool EngineCalls::FreeReady() {
    const bool fresh = leftState_ == State::Unbound;
    if (!Ensure(leftState_, left_, "KismetStringLibrary", "Left",
                {{"SourceString", PropertyKind::String, kArrayHeaderSize},
                 {"Count", PropertyKind::Int32, 4},
                 {"ReturnValue", PropertyKind::String, kArrayHeaderSize}}))
        return false;
    if (fresh) {
        // Measured, not assumed: an empty source must come back as an empty slot.
        left_.Clear();
        if (!left_.Invoke(processEvent_) || !AllZero(left_.At("ReturnValue"), kArrayHeaderSize))
            return Fail(leftState_, "KismetStringLibrary::Left(\"\", 0) did not return an unallocated FString");
    }
    return true;
}

bool EngineCalls::FreeThroughLeft(std::uint8_t *header, bool *handedOver) {
    *handedOver = false;
    if (!FreeReady())
        return false;
    std::uint8_t *returned = left_.At("ReturnValue");
    left_.Clear();
    std::memcpy(returned, header, kArrayHeaderSize);
    *handedOver = true;
    if (!left_.Invoke(processEvent_) || !AllZero(returned, kArrayHeaderSize)) {
        // The buffer may now be gone or not: stop rather than free twice.
        left_.Clear();
        return Fail(leftState_, "freeing through KismetStringLibrary::Left left memory behind");
    }
    return true;
}

bool EngineCalls::Free(void *data) {
    if (!data)
        return true;
    std::uint8_t header[kArrayHeaderSize]{};
    Store<void *>(header, data);
    Store<std::int32_t>(header + 12, 1);
    bool handedOver = false;
    return FreeThroughLeft(header, &handedOver);
}

bool EngineCalls::EmptyArray(std::uint8_t *header) {
    if (AllZero(header, kArrayHeaderSize))
        return true;
    bool handedOver = false;
    if (Load<void *>(header) && !FreeThroughLeft(header, &handedOver)) {
        // The engine may have freed it; forget the pointer (leak at worst).
        if (handedOver)
            std::memset(header, 0, kArrayHeaderSize);
        return false;
    }
    std::memset(header, 0, kArrayHeaderSize);
    return true;
}

// LeftPad(FString, ChCount) allocates ChCount spaces; Max is the capacity.
std::optional<EngineCalls::Block> EngineCalls::Allocate(std::size_t bytes, std::size_t alignment) {
    if (!Ensure(padState_, pad_, "KismetStringLibrary", "LeftPad",
                {{"SourceString", PropertyKind::String, kArrayHeaderSize},
                 {"ChCount", PropertyKind::Int32, 4},
                 {"ReturnValue", PropertyKind::String, kArrayHeaderSize}}))
        return std::nullopt;
    const std::size_t chars = std::max<std::size_t>(1, (bytes + 1) / 2);
    if (chars >= static_cast<std::size_t>(kMaxContainerElements)) {
        failure_ = "an allocation of " + std::to_string(bytes) + " bytes is beyond any real container";
        return std::nullopt;
    }
    pad_.Clear();
    Store<std::int32_t>(pad_.At("ChCount"), static_cast<std::int32_t>(chars));
    if (!pad_.Invoke(processEvent_)) {
        pad_.Clear();
        failure_ = "KismetStringLibrary::LeftPad could not be called";
        return std::nullopt;
    }
    std::uint8_t header[kArrayHeaderSize];
    std::memcpy(header, pad_.At("ReturnValue"), kArrayHeaderSize);
    pad_.Clear();
    auto *data = Load<std::uint8_t *>(header);
    const std::int32_t num = Load<std::int32_t>(header + 8);
    const std::int32_t max = Load<std::int32_t>(header + 12);
    if (!data || num != static_cast<std::int32_t>(chars + 1) || max < num) {
        // Whatever came back is the engine's; hand it back if it is a buffer.
        if (data)
            Free(data);
        Fail(padState_, "KismetStringLibrary::LeftPad did not return the string it was asked for");
        return std::nullopt;
    }
    if (alignment > 1 && reinterpret_cast<std::uintptr_t>(data) % alignment != 0) {
        Free(data);
        failure_ = "the engine's allocation is not " + std::to_string(alignment) + "-byte aligned";
        return std::nullopt;
    }
    const std::size_t capacity = static_cast<std::size_t>(max) * 2;
    std::memset(data, 0, capacity);
    return Block{data, capacity};
}

bool EngineCalls::AssignChars(std::uint8_t *header, const void *chars, std::size_t count, std::size_t charSize) {
    if (count == 0)
        return EmptyArray(header);
    const std::optional<Block> block = Allocate((count + 1) * charSize, charSize);
    if (!block)
        return false;
    std::memcpy(block->data, chars, count * charSize);
    // Assign first, free after: a failed free only leaks.
    std::uint8_t old[kArrayHeaderSize];
    std::memcpy(old, header, kArrayHeaderSize);
    Store<void *>(header, block->data);
    Store<std::int32_t>(header + 8, static_cast<std::int32_t>(count + 1));
    Store<std::int32_t>(header + 12, static_cast<std::int32_t>(block->bytes / charSize));
    ReleaseOrLeak(Load<void *>(old), "a replaced string's buffer");
    return true;
}

void EngineCalls::ReleaseOrLeak(void *data, const char *what) {
    if (data && !Free(data))
        NoteMemory(std::string(what) + " was leaked: " + failure_);
}

namespace {
std::atomic<MemoryNote> memoryNote{nullptr};
} // namespace

void SetMemoryNote(MemoryNote note) { memoryNote.store(note, std::memory_order_release); }

void NoteMemory(const std::string &message) {
    if (const MemoryNote note = memoryNote.load(std::memory_order_acquire))
        note(message);
}

// Reads a string a call returned into its frame, then frees it.
bool EngineCalls::TakeString(NativeCall &call, const char *parameter, std::optional<std::string> *out) {
    std::uint8_t *header = call.At(parameter);
    *out = values_->ReadStringAt(reinterpret_cast<Address>(header), PropertyKind::String);
    std::uint8_t copy[kArrayHeaderSize];
    std::memcpy(copy, header, kArrayHeaderSize);
    std::memset(header, 0, kArrayHeaderSize);
    return EmptyArray(copy);
}

// --- FText ---------------------------------------------------------------------

std::optional<std::string> EngineCalls::TextToString(const std::uint8_t *text) {
    if (!Ensure(textToStringState_, textToString_, "KismetTextLibrary", "Conv_TextToString",
                {{"InText", PropertyKind::Text, 0}, {"ReturnValue", PropertyKind::String, kArrayHeaderSize}}))
        return std::nullopt;
    // A zeroed slot is no text at all; the engine would dereference it.
    if (!Load<void *>(text))
        return std::string();
    textToString_.Clear();
    // Bitwise alias as const input; the thunk adds and drops its own ref.
    std::memcpy(textToString_.At("InText"), text, static_cast<std::size_t>(textToString_.SizeOf("InText")));
    if (!textToString_.Invoke(processEvent_)) {
        textToString_.Clear();
        return std::nullopt;
    }
    std::optional<std::string> result;
    const bool freed = TakeString(textToString_, "ReturnValue", &result);
    textToString_.Clear();
    return freed ? result : std::nullopt;
}

bool EngineCalls::AssignText(std::uint8_t *text, std::u16string_view value) {
    if (!Ensure(stringToTextState_, stringToText_, "KismetTextLibrary", "Conv_StringToText",
                {{"InString", PropertyKind::String, kArrayHeaderSize}, {"ReturnValue", PropertyKind::Text, 0}}))
        return false;
    const std::size_t size = static_cast<std::size_t>(stringToText_.SizeOf("ReturnValue"));
    const InputString input(value);
    stringToText_.Clear();
    input.WriteHeader(stringToText_.At("InString"));
    // The old text goes where the thunk's move assignment releases it.
    std::memcpy(stringToText_.At("ReturnValue"), text, size);
    const bool called = stringToText_.Invoke(processEvent_);
    if (called)
        std::memcpy(text, stringToText_.At("ReturnValue"), size);
    stringToText_.Clear();
    return called && Load<void *>(text) != nullptr;
}

bool EngineCalls::MakeEmptyText(std::uint8_t *text) {
    if (!Ensure(emptyTextState_, emptyText_, "KismetTextLibrary", "GetEmptyText",
                {{"ReturnValue", PropertyKind::Text, 0}}))
        return false;
    const std::size_t size = static_cast<std::size_t>(emptyText_.SizeOf("ReturnValue"));
    emptyText_.Clear();
    std::memcpy(emptyText_.At("ReturnValue"), text, size);
    const bool called = emptyText_.Invoke(processEvent_);
    if (called)
        std::memcpy(text, emptyText_.At("ReturnValue"), size);
    emptyText_.Clear();
    return called && Load<void *>(text) != nullptr;
}

// ITextData's Release slot, proven by GetRefCount reading 1, 2, 1.
namespace {

// UE4 TSharedRef controller: vtable (DestroyObject, deleting dtor), shared, weak.
constexpr std::size_t kSharedCount = 8;
constexpr std::size_t kWeakCount = 12;
using DestroyObjectFn = void(__fastcall *)(void *controller);
using DeletingDestructorFn = void *(__fastcall *)(void *controller, unsigned flags);

std::int32_t Count32(const std::uint8_t *at) {
    std::int32_t value = 0;
    std::memcpy(&value, at, sizeof(value));
    return value;
}

// SharedPointerInternals::ReleaseSharedReference, ThreadSafe mode.
void ReleaseShared(std::uint8_t *controller) {
    auto **vtable = *reinterpret_cast<void ***>(controller);
    if (_InterlockedDecrement(reinterpret_cast<volatile long *>(controller + kSharedCount)) != 0)
        return;
    reinterpret_cast<DestroyObjectFn>(vtable[0])(controller);
    if (_InterlockedDecrement(reinterpret_cast<volatile long *>(controller + kWeakCount)) == 0)
        reinterpret_cast<DeletingDestructorFn>(vtable[1])(controller, 1);
}

} // namespace

// Fresh controller, counts 1/1: MakeShared stores data inline at +16, `new` stores a pointer.
bool EngineCalls::MeasureSharedTextRelease(std::uint8_t *probe) {
    auto *data = Load<std::uint8_t *>(probe);
    auto *controller = Load<std::uint8_t *>(probe + sizeof(Address));
    if (!data || !controller || (data != controller + 16 && Load<std::uint8_t *>(controller + 16) != data))
        return Fail(textReleaseState_, "a fresh text's reference controller does not hold its data");
    auto **vtable = *reinterpret_cast<void ***>(controller);
    if (!ImageCode(vtable[0]) || !ImageCode(vtable[1]))
        return Fail(textReleaseState_, "the text's reference controller has no engine vtable");
    if (Count32(controller + kSharedCount) != 1 || Count32(controller + kWeakCount) != 1)
        return Fail(textReleaseState_, "a fresh text's reference counts are not 1 and 1");
    // A second reference, as a TSharedRef copy takes it: releasing it keeps the text.
    _InterlockedIncrement(reinterpret_cast<volatile long *>(controller + kSharedCount));
    ReleaseShared(controller);
    if (Count32(controller + kSharedCount) != 1)
        return Fail(textReleaseState_, "releasing a second reference did not leave one");
    ReleaseShared(controller);
    std::memset(probe, 0, 24);
    releaseFunctions_.push_back(reinterpret_cast<Address>(vtable[1]));
    textSize_ = 24;
    textReleaseState_ = State::Ready;
    return true;
}

bool EngineCalls::MeasureTextRelease() {
    if (textReleaseState_ != State::Unbound)
        return textReleaseState_ == State::Ready;
    std::uint8_t probe[32]{};
    if (!AssignText(probe, u"URKit text release probe"))
        return Fail(textReleaseState_, "a probe text could not be made (" + failure_ + ")");
    const std::int32_t size = stringToText_.SizeOf("ReturnValue");
    if (size == 24)
        return MeasureSharedTextRelease(probe);
    if (size != 16)
        return Fail(textReleaseState_, "FText is " + std::to_string(size) + " bytes here, a layout not measured");
    auto *data = Load<void *>(probe);
    auto **vtable = *reinterpret_cast<void ***>(data);
    for (int slot = 1; slot <= 3; ++slot) {
        if (!ImageCode(vtable[slot]))
            return Fail(textReleaseState_, "the text's vtable slot " + std::to_string(slot) +
                                               " is not code of a loaded image");
    }
    const std::uint32_t initial = CallCount(vtable[3], data);
    if (initial != 1)
        return Fail(textReleaseState_, "a fresh text reported " + std::to_string(initial) + " references, not 1");
    reinterpret_cast<AddRefFn>(vtable[1])(data);
    const std::uint32_t added = CallCount(vtable[3], data);
    if (added != 2)
        return Fail(textReleaseState_, "AddRef left " + std::to_string(added) + " references, not 2");
    // Release's return value is unreliable (5.4 returns 0); the count isn't.
    CallCount(vtable[2], data);
    const std::uint32_t after = CallCount(vtable[3], data);
    if (after != 1)
        return Fail(textReleaseState_, "Release left " + std::to_string(after) + " references, not 1");
    // The last reference goes the same way; the text is gone after it.
    CallCount(vtable[2], data);
    std::memset(probe, 0, sizeof(probe));
    releaseFunctions_.push_back(reinterpret_cast<Address>(vtable[2]));
    textSize_ = 16;
    textReleaseState_ = State::Ready;
    return true;
}

bool EngineCalls::TextReleaseAvailable() { return MeasureTextRelease(); }

bool EngineCalls::CanReleaseText(const std::uint8_t *text) {
    void *data = Load<void *>(text);
    if (!data)
        return true;
    if (!MeasureTextRelease())
        return false;
    // UE4: the controller's deleting destructor is the per-class entry.
    void *owner = textSize_ == 24 ? Load<void *>(text + sizeof(Address)) : data;
    if (!owner) {
        failure_ = "a text has data but no reference controller";
        return false;
    }
    auto **vtable = *reinterpret_cast<void ***>(owner);
    const std::size_t slot = textSize_ == 24 ? 1 : 2;
    const Address release = reinterpret_cast<Address>(vtable[slot]);
    // All histories share FTextHistory's Release; other classes are still engine code at that slot.
    if (std::find(releaseFunctions_.begin(), releaseFunctions_.end(), release) == releaseFunctions_.end()) {
        if (!ImageCode(vtable[slot]) || (textSize_ == 24 && !ImageCode(vtable[0]))) {
            failure_ = "a text's Release slot is not code of a loaded image";
            return false;
        }
        releaseFunctions_.push_back(release);
    }
    return true;
}

bool EngineCalls::ReleaseText(std::uint8_t *text) {
    void *data = Load<void *>(text);
    if (!data)
        return true;
    if (!CanReleaseText(text))
        return false;
    if (textSize_ == 24) {
        ReleaseShared(Load<std::uint8_t *>(text + sizeof(Address)));
        std::memset(text, 0, 2 * sizeof(Address));
        return true;
    }
    auto **vtable = *reinterpret_cast<void ***>(data);
    CallCount(vtable[2], data);
    std::memset(text, 0, 8);
    return true;
}

// --- FName ---------------------------------------------------------------------

bool EngineCalls::MakeName(std::u16string_view text, std::uint8_t *name, std::size_t size) {
    if (!Ensure(nameState_, name_, "KismetStringLibrary", "Conv_StringToName",
                {{"InString", PropertyKind::String, kArrayHeaderSize}, {"ReturnValue", PropertyKind::Name, 0}}))
        return false;
    if (static_cast<std::size_t>(name_.SizeOf("ReturnValue")) != size) {
        failure_ = "FName sizes disagree";
        return false;
    }
    const InputString input(text);
    name_.Clear();
    input.WriteHeader(name_.At("InString"));
    const bool called = name_.Invoke(processEvent_);
    if (called)
        std::memcpy(name, name_.At("ReturnValue"), size);
    name_.Clear();
    return called;
}

bool EngineCalls::ValidName(const std::uint8_t *name, std::size_t size) {
    const std::optional<std::string> text = finder_->Names().ReadFName(reinterpret_cast<Address>(name));
    if (!text || text->empty() || size > 16)
        return false;
    std::uint8_t made[16]{};
    if (!MakeName(Utf8ToUtf16(*text), made, size))
        return false;
    return std::memcmp(made, name, size) == 0;
}

// --- soft references -------------------------------------------------------------

// TPersistentObjectPtr: weak pointer, then the reflected FSoftObjectPath.
std::int32_t EngineCalls::SoftPathOffset() {
    if (softLayoutState_ == State::Ready)
        return softPathOffset_;
    if (softLayoutState_ == State::Failed)
        return kOffsetNotFound;
    softPathStruct_ = finder_->FindInOuter("SoftObjectPath", "/Script/CoreUObject");
    const Address field =
        softPathStruct_ != kNullAddress ? chain_->FindMemberDeep(softPathStruct_, "SubPathString") : kNullAddress;
    const std::optional<PropertyInfo> subPath = field != kNullAddress ? values_->Describe(field) : std::nullopt;
    if (!subPath || !subPath->Resolved() || subPath->elementSize != kArrayHeaderSize ||
        (subPath->kind != PropertyKind::String && subPath->kind != PropertyKind::Utf8String &&
         subPath->kind != PropertyKind::AnsiString)) {
        Fail(softLayoutState_, "FSoftObjectPath::SubPathString is not a reflected string");
        return kOffsetNotFound;
    }
    // The path ends the soft pointer; UE4 has an int32 tag between it and the weak part.
    const Address library = finder_->FindInOuter("KismetSystemLibrary", kLibraryPackage);
    const Address convert =
        library != kNullAddress ? finder_->FindInOuter("Conv_ObjectToSoftObjectReference", library) : kNullAddress;
    const Address returned = convert != kNullAddress ? chain_->FindMemberDeep(convert, "ReturnValue") : kNullAddress;
    const std::optional<PropertyInfo> soft = returned != kNullAddress ? values_->Describe(returned) : std::nullopt;
    const std::int32_t pathSize =
        finder_->Reader().ReadInt32(softPathStruct_ + structs_.propertiesSize).value_or(kOffsetNotFound);
    const std::int32_t pathAt = soft && pathSize > 0 ? soft->elementSize - pathSize : kOffsetNotFound;
    if (pathAt != static_cast<std::int32_t>(kWeakSize) && pathAt != static_cast<std::int32_t>(kWeakSize) + 8) {
        Fail(softLayoutState_, "the soft pointer's path offset is not measurable (" + std::to_string(pathAt) + ")");
        return kOffsetNotFound;
    }
    softSubPathOffset_ = subPath->offset;
    softSubPathKind_ = subPath->kind;
    softPathOffset_ = pathAt;
    softLayoutState_ = State::Ready;
    return softPathOffset_;
}

Address EngineCalls::SoftPathStruct() { return SoftPathOffset() == kOffsetNotFound ? kNullAddress : softPathStruct_; }

bool EngineCalls::AssignSoftObject(std::uint8_t *soft, std::size_t size, Address object, bool isClass) {
    NativeCall &call = softFromObject_[isClass ? 1 : 0];
    const char *argument = isClass ? "Class" : "Object";
    if (!Ensure(softFromObjectState_[isClass ? 1 : 0], call, "KismetSystemLibrary",
                isClass ? "Conv_ClassToSoftClassReference" : "Conv_ObjectToSoftObjectReference",
                {{argument, isClass ? PropertyKind::Class : PropertyKind::Object, 8},
                 {"ReturnValue", PropertyKind::SoftObject, 0}}))
        return false;
    if (static_cast<std::size_t>(call.SizeOf("ReturnValue")) != size) {
        failure_ = "soft reference sizes disagree";
        return false;
    }
    call.Clear();
    Store<Address>(call.At(argument), object);
    // The old value goes where the thunk's move assignment frees what it owns.
    std::memcpy(call.At("ReturnValue"), soft, size);
    const bool called = call.Invoke(processEvent_);
    if (called)
        std::memcpy(soft, call.At("ReturnValue"), size);
    call.Clear();
    return called;
}

bool EngineCalls::AssignSoftPath(std::uint8_t *soft, std::size_t size, std::u16string_view path, bool isClass) {
    const int which = isClass ? 1 : 0;
    NativeCall &make = softPathMake_[which];
    NativeCall &convert = softFromPath_[which];
    const char *pathArgument = isClass ? "SoftClassPath" : "SoftObjectPath";
    if (SoftPathOffset() == kOffsetNotFound ||
        !Ensure(softPathMakeState_[which], make, "KismetSystemLibrary",
                isClass ? "MakeSoftClassPath" : "MakeSoftObjectPath",
                {{"PathString", PropertyKind::String, kArrayHeaderSize}, {"ReturnValue", PropertyKind::Struct, 0}}) ||
        !Ensure(softFromPathState_[which], convert, "KismetSystemLibrary",
                isClass ? "Conv_SoftClassPathToSoftClassRef" : "Conv_SoftObjPathToSoftObjRef",
                {{pathArgument, PropertyKind::Struct, 0}, {"ReturnValue", PropertyKind::SoftObject, 0}}))
        return false;
    const std::int32_t pathSize = make.SizeOf("ReturnValue");
    if (static_cast<std::size_t>(convert.SizeOf("ReturnValue")) != size || convert.SizeOf(pathArgument) != pathSize ||
        softSubPathOffset_ + kArrayHeaderSize > pathSize) {
        failure_ = "soft path sizes disagree";
        return false;
    }
    const InputString input(path);
    make.Clear();
    input.WriteHeader(make.At("PathString"));
    if (!make.Invoke(processEvent_)) {
        make.Clear();
        return false;
    }
    convert.Clear();
    std::memcpy(convert.At(pathArgument), make.At("ReturnValue"), static_cast<std::size_t>(pathSize));
    std::memcpy(convert.At("ReturnValue"), soft, size);
    const bool called = convert.Invoke(processEvent_);
    if (called)
        std::memcpy(soft, convert.At("ReturnValue"), size);
    convert.Clear();
    // The intermediate path is ours: its string goes back to the engine.
    const bool freed = EmptyArray(make.At("ReturnValue") + softSubPathOffset_);
    make.Clear();
    return called && freed;
}

std::optional<std::string> EngineCalls::SoftPath(const std::uint8_t *soft, std::size_t size, bool isClass) {
    const int which = isClass ? 1 : 0;
    NativeCall &call = softToString_[which];
    const char *argument = isClass ? "SoftClassReference" : "SoftObjectReference";
    if (!Ensure(softToStringState_[which], call, "KismetSystemLibrary",
                isClass ? "Conv_SoftClassReferenceToString" : "Conv_SoftObjectReferenceToString",
                {{argument, PropertyKind::SoftObject, 0}, {"ReturnValue", PropertyKind::String, kArrayHeaderSize}}))
        return std::nullopt;
    if (static_cast<std::size_t>(call.SizeOf(argument)) != size)
        return std::nullopt;
    call.Clear();
    std::memcpy(call.At(argument), soft, size);
    if (!call.Invoke(processEvent_)) {
        call.Clear();
        return std::nullopt;
    }
    std::optional<std::string> result;
    const bool freed = TakeString(call, "ReturnValue", &result);
    call.Clear();
    return freed ? result : std::nullopt;
}

Address EngineCalls::SoftTarget(const std::uint8_t *soft, std::size_t size, bool isClass) {
    const int which = isClass ? 1 : 0;
    NativeCall &call = softToObject_[which];
    const char *argument = isClass ? "SoftClass" : "SoftObject";
    if (!Ensure(softToObjectState_[which], call, "KismetSystemLibrary",
                isClass ? "Conv_SoftClassReferenceToClass" : "Conv_SoftObjectReferenceToObject",
                {{argument, PropertyKind::SoftObject, 0},
                 {"ReturnValue", isClass ? PropertyKind::Class : PropertyKind::Object, 8}}))
        return kNullAddress;
    if (static_cast<std::size_t>(call.SizeOf(argument)) != size)
        return kNullAddress;
    call.Clear();
    std::memcpy(call.At(argument), soft, size);
    const Address target = call.Invoke(processEvent_) ? Load<Address>(call.At("ReturnValue")) : kNullAddress;
    call.Clear();
    return target;
}

// --- weak references ---------------------------------------------------------------

// Weak pointer creation assigns the serial; a soft ref from the object carries one.
bool EngineCalls::MakeWeak(Address object, std::uint8_t *weak) {
    NativeCall &call = softFromObject_[0];
    if (object == kNullAddress) {
        std::memset(weak, 0, kWeakSize);
        return true;
    }
    if (SoftPathOffset() == kOffsetNotFound ||
        !Ensure(softFromObjectState_[0], call, "KismetSystemLibrary", "Conv_ObjectToSoftObjectReference",
                {{"Object", PropertyKind::Object, 8}, {"ReturnValue", PropertyKind::SoftObject, 0}}))
        return false;
    const std::int32_t size = call.SizeOf("ReturnValue");
    if (softPathOffset_ + softSubPathOffset_ + kArrayHeaderSize > size) {
        failure_ = "soft reference sizes disagree";
        return false;
    }
    call.Clear();
    Store<Address>(call.At("Object"), object);
    if (!call.Invoke(processEvent_)) {
        call.Clear();
        return false;
    }
    std::uint8_t *returned = call.At("ReturnValue");
    std::memcpy(weak, returned, kWeakSize);
    const bool freed = EmptyArray(returned + softPathOffset_ + softSubPathOffset_);
    call.Clear();
    // The pointer's index must be the object's own.
    const ObjectOffsets &offsets = finder_->Offsets();
    const std::optional<std::int32_t> index = finder_->Reader().ReadInt32(object + offsets.index);
    if (!freed || !index || Load<std::int32_t>(weak) != *index || Load<std::int32_t>(weak + 4) == 0) {
        failure_ = "a weak pointer made by the engine does not name its object";
        return false;
    }
    return true;
}

// FUObjectItem serial offset: the one matching fresh serials on several objects.
bool EngineCalls::MeasureWeak() {
    if (weakState_ != State::Unbound)
        return weakState_ == State::Ready;
    const char *libraries[] = {"KismetStringLibrary", "KismetSystemLibrary", "KismetTextLibrary",
                               "KismetMathLibrary"};
    const ObjectArray &objects = finder_->Objects();
    const ObjectItemLayout &item = objects.Layout().item;
    std::vector<std::int32_t> candidates;
    for (std::int32_t offset = 0; offset + 4 <= item.stride; offset += 4) {
        if (offset + 4 > item.pointerOffset && offset < item.pointerOffset + 8)
            continue;
        candidates.push_back(offset);
    }
    int samples = 0;
    std::vector<std::int32_t> serials;
    for (const char *library : libraries) {
        const Address klass = finder_->FindInOuter(library, kLibraryPackage);
        const Address object = klass != kNullAddress ? types_->DefaultObjectOf(klass) : kNullAddress;
        std::uint8_t weak[kWeakSize];
        if (object == kNullAddress || !MakeWeak(object, weak))
            continue;
        const std::int32_t index = Load<std::int32_t>(weak);
        const std::int32_t serial = Load<std::int32_t>(weak + 4);
        const Address at = objects.ItemAt(index);
        if (at == kNullAddress || std::find(serials.begin(), serials.end(), serial) != serials.end())
            continue;
        serials.push_back(serial);
        std::erase_if(candidates, [&](std::int32_t offset) {
            return finder_->Reader().ReadInt32(at + offset).value_or(0) != serial;
        });
        ++samples;
    }
    if (samples < 3 || candidates.size() != 1)
        return Fail(weakState_, "the object item's serial number could not be located (" + std::to_string(samples) +
                                    " samples, " + std::to_string(candidates.size()) + " candidates)");
    serialOffset_ = candidates.front();
    weakState_ = State::Ready;
    return true;
}

Address EngineCalls::WeakTarget(const std::uint8_t *weak) {
    const std::int32_t index = Load<std::int32_t>(weak);
    const std::int32_t serial = Load<std::int32_t>(weak + 4);
    if (index < 0 || serial == 0 || !MeasureWeak())
        return kNullAddress;
    const ObjectArray &objects = finder_->Objects();
    const Address at = objects.ItemAt(index);
    if (at == kNullAddress || finder_->Reader().ReadInt32(at + serialOffset_).value_or(0) != serial)
        return kNullAddress;
    const Address object = objects.ObjectAt(index);
    return IsLiveObject(*finder_, object) ? object : kNullAddress;
}

// --- enum names, as the engine reports them ----------------------------------------

std::optional<std::string> EngineCalls::EnumeratorName(Address enumObject, std::uint8_t value) {
    if (!Ensure(enumeratorState_, enumerator_, "KismetNodeHelperLibrary", "GetEnumeratorName",
                {{"Enum", PropertyKind::Object, 8},
                 {"EnumeratorValue", PropertyKind::Byte, 1},
                 {"ReturnValue", PropertyKind::Name, 0}}))
        return std::nullopt;
    enumerator_.Clear();
    Store<Address>(enumerator_.At("Enum"), enumObject);
    Store<std::uint8_t>(enumerator_.At("EnumeratorValue"), value);
    std::optional<std::string> name;
    if (enumerator_.Invoke(processEvent_))
        name = finder_->Names().ReadFName(reinterpret_cast<Address>(enumerator_.At("ReturnValue")));
    enumerator_.Clear();
    return name;
}

} // namespace URK::Unreal
