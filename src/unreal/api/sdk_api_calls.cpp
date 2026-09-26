// Call frames and calls through ProcessEvent.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

LoaderFrame *FrameOf(URK_UnrealCallFrame *frame) { return reinterpret_cast<LoaderFrame *>(frame); }

const LoaderFrame *FrameOf(const URK_UnrealCallFrame *frame) { return reinterpret_cast<const LoaderFrame *>(frame); }

// The return value and non-const out parameters: what the engine assigns into.
bool Written(const FunctionParameter &parameter) {
    const std::uint64_t flags = parameter.info.propertyFlags;
    return parameter.returned || (flags & kPropertyFlagReturnParm) != 0 ||
           ((flags & kPropertyFlagOutParm) != 0 && (flags & kPropertyFlagConstParm) == 0);
}

// Game thread. Releasing calls into the engine, which can come back here.
static thread_local bool t_releasing = false;

static std::mutex g_pendingMutex;

static std::vector<std::unique_ptr<LoaderFrame>> g_pending;

void ReleaseFrame(LoaderFrame &loaderFrame) {
    OwnedValues &owned = Serve().owned;
    const FunctionInfo &function = loaderFrame.frame.Function();
    auto *data = static_cast<std::uint8_t *>(loaderFrame.frame.Data());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (!loaderFrame.engineOwned[index])
            continue;
        const PropertyInfo &info = function.parameters[index].info;
        for (std::int32_t i = 0; i < info.arrayDim; ++i) {
            if (!owned.Destroy(info, data + info.offset + static_cast<std::size_t>(i) * info.elementSize)) {
                Report(function.function, "a parameter's value could not be released (" + owned.Failure() + ")");
                return;
            }
        }
        loaderFrame.engineOwned[index] = 0;
    }
}

void ReleasePending() {
    if (t_releasing)
        return;
    std::vector<std::unique_ptr<LoaderFrame>> batch;
    {
        std::lock_guard lock(g_pendingMutex);
        batch.swap(g_pending);
    }
    if (batch.empty())
        return;
    t_releasing = true;
    for (const std::unique_ptr<LoaderFrame> &loaderFrame : batch)
        ReleaseFrame(*loaderFrame);
    t_releasing = false;
}

// A UFunction's outer is its class; climb for inherited ones.
URK_UnrealObject Unreal_FindFunction(URK_UnrealObject ownerClass, const char *name) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!name || !Live(engine, ownerClass))
        return URK_UNREAL_NULL_OBJECT;

    Address owner = ownerClass;
    if (!ObjectIs(engine.Finder(), engine.Structs(), owner, kCastFlagClass))
        owner = engine.Finder().ClassOf(owner);
    constexpr int kMaxDepth = 64;
    for (int depth = 0; owner != kNullAddress && depth < kMaxDepth; ++depth) {
        const Address function = engine.Finder().FindInOuter(name, owner);
        if (function != kNullAddress)
            return function;
        owner = engine.Types().SuperOf(owner);
    }
    return URK_UNREAL_NULL_OBJECT;
}

URK_UnrealCallFrame *Unreal_CallFrameCreate(URK_UnrealObject function) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!Live(engine, function) || !ObjectIs(engine.Finder(), engine.Structs(), function, kCastFlagFunction))
        return nullptr;

    const std::optional<FunctionInfo> info =
        DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), function);
    if (!info)
        return nullptr;

    auto loaderFrame = std::make_unique<LoaderFrame>(*info);
    for (const FunctionParameter &parameter : info->parameters) {
        if (Written(parameter) && Serve().owned.Classify(parameter.info) == Ownership::Unreleasable &&
            loaderFrame->unreleasable.empty())
            loaderFrame->unreleasable = parameter.name + " (" + PropertyKindName(parameter.info.kind) + ")";
    }
    return reinterpret_cast<URK_UnrealCallFrame *>(loaderFrame.release());
}

void Unreal_CallFrameDestroy(URK_UnrealCallFrame *frame) {
    // A hooked call's frame lives on the loader's stack.
    if (frame && FrameOf(frame)->frame.View()) {
        Report(FrameOf(frame)->frame.Function().function, "a hooked call's frame is not destroyed by the mod");
        return;
    }
    std::unique_ptr<LoaderFrame> loaderFrame(FrameOf(frame));
    if (!loaderFrame ||
        std::find(loaderFrame->engineOwned.begin(), loaderFrame->engineOwned.end(), 1) == loaderFrame->engineOwned.end())
        return;
    if (OnGameThread() && !t_releasing) {
        t_releasing = true;
        ReleaseFrame(*loaderFrame);
        t_releasing = false;
        return;
    }
    // Released by the game thread's next call or frame.
    std::lock_guard lock(g_pendingMutex);
    g_pending.push_back(std::move(loaderFrame));
}

int Unreal_CallFrameSet(URK_UnrealCallFrame *frame, const char *parameterName, const void *value, std::size_t size) {
    if (!frame || !parameterName || !value)
        return 0;
    LoaderFrame *loaderFrame = FrameOf(frame);
    CallFrame *callFrame = &loaderFrame->frame;
    const FunctionParameter *parameter = callFrame->Function().Parameter(parameterName);
    if (!parameter)
        return 0;
    const std::size_t index = static_cast<std::size_t>(parameter - callFrame->Function().parameters.data());
    const PropertyKind kind = parameter->info.kind;
    const Ownership ownership = Serve().owned.Classify(parameter->info);
    // place_clear releases engine memory before raw bytes overwrite it.
    if (ownership != Ownership::None && loaderFrame->engineOwned[index])
        return 0;
    // A hooked call's strings and containers are the engine's; places replace them.
    if (ownership != Ownership::None && callFrame->View())
        return 0;
    // The engine frees the old value on assignment, so it must be engine-owned or empty.
    if (Written(*parameter) && kind != PropertyKind::Struct && ownership != Ownership::None) {
        const std::size_t at = static_cast<std::size_t>(parameter->info.offset);
        if (parameter->info.offset < 0 || size != static_cast<std::size_t>(parameter->info.elementSize) ||
            at + size > callFrame->Size() ||
            std::memcmp(static_cast<const std::uint8_t *>(callFrame->Data()) + at, value, size) != 0)
            return 0;
    }
    if (kind == PropertyKind::Object || kind == PropertyKind::Class) {
        Address object = kNullAddress;
        if (size != sizeof(object))
            return 0;
        std::memcpy(&object, value, sizeof(object));
        if (!Assignable(UnrealEngine::Instance(), parameter->info, object))
            return 0;
    }
    if (kind == PropertyKind::Struct) {
        const std::uint8_t *current = callFrame->Slot(*parameter);
        if (!current || size != static_cast<std::size_t>(parameter->info.elementSize))
            return 0;
        if (!StructChangeAllowed(UnrealEngine::Instance(), parameter->info.inner, current,
                                 static_cast<const std::uint8_t *>(value), size))
            return 0;
    }
    return callFrame->Set(parameterName, value, size) ? 1 : 0;
}

int Unreal_CallFrameGet(const URK_UnrealCallFrame *frame, const char *parameterName, void *output,
                        std::size_t size) {
    if (!frame || !parameterName)
        return 0;
    return FrameOf(frame)->frame.Get(parameterName, output, size) ? 1 : 0;
}

int Unreal_Call(URK_UnrealObject object, URK_UnrealCallFrame *frame) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!frame || !Live(engine, object) || !engine.ProcessEventResolved())
        return 0;

    LoaderFrame *loaderFrame = FrameOf(frame);
    const FunctionInfo &function = loaderFrame->frame.Function();
    ProcessEventHook &hook = ProcessEventHook::Instance();
    if (!hook.Installed()) {
        Report(function.function, "call refused: the ProcessEvent hook is not installed");
        return 0;
    }
    if (hook.GameThreadId() == 0 || hook.GameThreadId() != GetCurrentThreadId()) {
        // The usual cause: a menu button, which runs on the render thread.
        Report(function.function, "call refused: not on the game thread (from a menu, use on_game_thread)");
        return 0;
    }
    if (loaderFrame->frame.View()) {
        Report(function.function, "call refused: a hooked call's frame belongs to that call");
        return 0;
    }
    if (!loaderFrame->unreleasable.empty()) {
        Report(function.function, "call refused: it returns or writes " + loaderFrame->unreleasable +
                                      ", memory of a kind the loader cannot release");
        return 0;
    }
    OwnedValues &owned = Serve().owned;
    auto *data = static_cast<std::uint8_t *>(loaderFrame->frame.Data());
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const FunctionParameter &parameter = function.parameters[index];
        // Texts the call leaves here are dropped by their own Release later.
        if (Written(parameter) && owned.HoldsText(parameter.info) && !owned.Engine().TextReleaseAvailable()) {
            Report(function.function, "call refused: it writes text, and releasing text is unavailable (" +
                                          owned.Engine().Failure() + ")");
            return 0;
        }
    }
    ReleasePending();
    // A text parameter must hold a text: the engine dereferences a zeroed one.
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        const PropertyInfo &info = function.parameters[index].info;
        if (!owned.NeedsInitialize(info))
            continue;
        for (std::int32_t i = 0; i < info.arrayDim; ++i) {
            bool made = false;
            if (!owned.FillNullTexts(info, data + info.offset + static_cast<std::size_t>(i) * info.elementSize,
                                     &made)) {
                Report(function.function, "call refused: a text parameter could not be made (" + owned.Failure() + ")");
                return 0;
            }
            if (made)
                loaderFrame->engineOwned[index] = 1;
        }
    }
    if (!InvokeProcessEvent(engine.ProcessEvent(), object, function.function, data))
        return 0;
    loaderFrame->called = true;
    for (std::size_t index = 0; index < function.parameters.size(); ++index) {
        if (Written(function.parameters[index]) && owned.Classify(function.parameters[index].info) == Ownership::Owned)
            loaderFrame->engineOwned[index] = 1;
    }
    return 1;
}

} // namespace URK::Unreal::SdkApi
