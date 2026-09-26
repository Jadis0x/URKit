// Places: a value reached by member and element steps.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

std::optional<PlaceContext> ResolvePlace(const URK_UnrealPlace *place, bool describe) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!place || !place->member || !engine.Available() || place->step_count > URK_UNREAL_PLACE_MAX_STEPS)
        return std::nullopt;
    PlaceContext context;
    std::uint8_t *root = nullptr;
    PropertyInfo info;
    if (place->frame) {
        if (place->object != URK_UNREAL_NULL_OBJECT)
            return std::nullopt;
        LoaderFrame *frame = FrameOf(place->frame);
        const FunctionInfo &function = frame->frame.Function();
        const FunctionParameter *parameter = function.Parameter(place->member);
        std::uint8_t *slot = parameter ? frame->frame.Slot(*parameter) : nullptr;
        if (!slot || place->member_index < 0 || place->member_index >= parameter->info.arrayDim)
            return std::nullopt;
        info = parameter->info;
        root = slot + static_cast<std::size_t>(place->member_index) * info.elementSize;
        context.frame = frame;
        context.parameter = static_cast<std::size_t>(parameter - function.parameters.data());
    } else {
        const std::optional<ResolvedMember> resolved = Resolve(engine, place->object, place->member);
        if (!resolved || place->member_index < 0 || place->member_index >= resolved->info.arrayDim)
            return std::nullopt;
        info = resolved->info;
        root = reinterpret_cast<std::uint8_t *>(static_cast<std::uintptr_t>(place->object)) + info.offset +
               static_cast<std::size_t>(place->member_index) * info.elementSize;
    }
    Places &places = Serve().places;
    std::optional<PlaceTarget> target =
        places.Walk(root, info, place->steps, place->step_count, describe, OnGameThread());
    if (!target) {
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
        return std::nullopt;
    }
    context.target = *target;
    if (!place->frame)
        context.target.owner = static_cast<Address>(place->object);
    return context;
}

int Unreal_PlaceDescribe(const URK_UnrealPlace *place, URK_UnrealPropertyInfo *info) {
    const std::optional<PlaceContext> context = ResolvePlace(place, true);
    if (!context || !info)
        return 0;
    FillInfo(info, context->target.info);
    return 1;
}

int Unreal_PlaceReadInteger(const URK_UnrealPlace *place, std::int64_t *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadInteger(context->target, output) ? 1 : 0;
}

int Unreal_PlaceWriteInteger(const URK_UnrealPlace *place, std::int64_t value) {
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.WriteInteger(target, value); });
}

int Unreal_PlaceReadFloating(const URK_UnrealPlace *place, double *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadFloating(context->target, output) ? 1 : 0;
}

int Unreal_PlaceWriteFloating(const URK_UnrealPlace *place, double value) {
    return Changed(place,
                   [&](Places &places, const PlaceTarget &target) { return places.WriteFloating(target, value); });
}

int Unreal_PlaceReadBool(const URK_UnrealPlace *place, int *output) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    bool value = false;
    if (!context || !output || !Serve().places.ReadBool(context->target, &value))
        return 0;
    *output = value ? 1 : 0;
    return 1;
}

int Unreal_PlaceWriteBool(const URK_UnrealPlace *place, int value) {
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.WriteBool(target, value != 0); });
}

URK_UnrealObject Unreal_PlaceReadObject(const URK_UnrealPlace *place) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.ReadObject(context->target, OnGameThread()) : URK_UNREAL_NULL_OBJECT;
}

int Unreal_PlaceWriteObject(const URK_UnrealPlace *place, URK_UnrealObject value) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteObject(target, value, gameThread);
    });
}

int Unreal_PlaceReadText(const URK_UnrealPlace *place, char *output, std::size_t outputSize, std::size_t *length) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context)
        return 0;
    const std::optional<std::string> text = Serve().places.ReadText(context->target, OnGameThread());
    if (!text)
        return 0;
    if (length)
        *length = text->size();
    if (!output || outputSize <= text->size())
        return 0;
    std::memcpy(output, text->data(), text->size());
    output[text->size()] = '\0';
    return 1;
}

int Unreal_PlaceWriteText(const URK_UnrealPlace *place, const char *utf8) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteText(target, utf8, gameThread);
    });
}

int Unreal_PlaceReadBytes(const URK_UnrealPlace *place, void *output, std::size_t size) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context && output && Serve().places.ReadBytes(context->target, output, size) ? 1 : 0;
}

int Unreal_PlaceWriteBytes(const URK_UnrealPlace *place, const void *value, std::size_t size) {
    if (!value)
        return 0;
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.WriteBytes(target, value, size, gameThread);
    });
}

std::int32_t Unreal_PlaceCount(const URK_UnrealPlace *place) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.Count(context->target, OnGameThread()) : -1;
}

std::int32_t Unreal_PlaceSlots(const URK_UnrealPlace *place, std::int32_t *output, std::int32_t capacity) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    return context ? Serve().places.Slots(context->target, output, capacity) : -1;
}

int Unreal_PlaceInsert(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Insert(target, index, count, gameThread);
    });
}

int Unreal_PlaceRemove(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Remove(target, index, count, gameThread);
    });
}

int Unreal_PlaceClear(const URK_UnrealPlace *place) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) { return places.Clear(target, gameThread); });
}

std::int32_t Unreal_PlaceFind(const URK_UnrealPlace *place, const URK_UnrealKey *key) {
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context || !key)
        return -1;
    Places &places = Serve().places;
    const std::int32_t slot = places.Find(context->target, *key, OnGameThread());
    if (slot < 0 && !places.Failure().empty())
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
    return slot;
}

std::int32_t Unreal_PlaceAdd(const URK_UnrealPlace *place, const URK_UnrealKey *key) {
    if (!key)
        return -1;
    const bool gameThread = OnGameThread();
    std::int32_t slot = -1;
    Changed(place, [&](Places &places, const PlaceTarget &target) {
        slot = places.Add(target, *key, gameThread);
        return slot >= 0;
    });
    return slot;
}

int Unreal_PlaceBind(const URK_UnrealPlace *place, URK_UnrealObject object, const char *function) {
    const bool gameThread = OnGameThread();
    return Changed(place, [&](Places &places, const PlaceTarget &target) {
        return places.Bind(target, object, function, gameThread);
    });
}

} // namespace URK::Unreal::SdkApi
