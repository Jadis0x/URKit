// Multicast delegate subscriptions through listener objects.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

struct Subscription {
    URK_UnrealFunctionHookFn callback = nullptr;
    void *user = nullptr;
    Address listener = kNullAddress;
    Address signature = kNullAddress;
    std::shared_ptr<const FunctionInfo> parameters;
    // The delegate's place, its strings owned, to unbind later.
    URK_UnrealPlace place{};
    std::string member;
    std::array<std::string, URK_UNREAL_PLACE_MAX_STEPS> steps;
};

std::mutex g_subscriptionsMutex;

std::unordered_map<std::uint64_t, std::unique_ptr<Subscription>> g_subscriptions;

bool ListenerCallback(void *user, const HookedCall &call) {
    const auto *subscription = static_cast<const Subscription *>(user);
    if (call.object != subscription->listener)
        return true;
    LoaderFrame view(subscription->parameters, call.parms, nullptr);
    const URK_UnrealHookedCall raw{subscription->place.object, subscription->signature,
                                   reinterpret_cast<URK_UnrealCallFrame *>(&view), 0, 0};
    subscription->callback(subscription->user, &raw);
    return false;
}

URK_UnrealPlace Element(const URK_UnrealPlace &place, std::int32_t index) {
    URK_UnrealPlace element = place;
    element.steps[element.step_count++] = URK_UnrealStep{URK_UNREAL_STEP_ELEMENT, index, nullptr};
    return element;
}

// Index of object among a delegate's or array's elements, or -1.
std::int32_t IndexOf(const URK_UnrealPlace &place, Address object) {
    const std::int32_t count = Unreal_PlaceCount(&place);
    for (std::int32_t i = 0; i < count; ++i) {
        const URK_UnrealPlace element = Element(place, i);
        if (Unreal_PlaceReadObject(&element) == object)
            return i;
    }
    return -1;
}

Address TheGameInstance() {
    static Address cached = kNullAddress;
    UnrealEngine &engine = UnrealEngine::Instance();
    if (cached != kNullAddress && Live(engine, cached))
        return cached;
    const Address klass = Unreal_FindObjectInOuter("GameInstance", "/Script/Engine");
    Address found = kNullAddress;
    cached = klass && Unreal_InstancesOf(klass, &found, 1, 0) > 0 ? found : kNullAddress;
    return cached;
}

// A plain object nothing else calls, kept alive by the game instance as RegisterReferencedObject does.
Address MakeListener(std::string *why) {
    const Address statics = Unreal_FindObjectInOuter("GameplayStatics", "/Script/Engine");
    const Address spawn = statics ? Unreal_FindFunction(statics, "SpawnObject") : kNullAddress;
    const Address klass = Unreal_FindObjectInOuter("DamageType", "/Script/Engine");
    const Address owner = TheGameInstance();
    URK_UnrealCallFrame *frame = spawn && klass && owner ? Unreal_CallFrameCreate(spawn) : nullptr;
    if (!frame) {
        *why = "no GameplayStatics::SpawnObject, DamageType or game instance";
        return kNullAddress;
    }
    Address listener = kNullAddress;
    if (!Unreal_CallFrameSet(frame, "ObjectClass", &klass, sizeof(klass)) ||
        !Unreal_CallFrameSet(frame, "Outer", &owner, sizeof(owner)) ||
        !Unreal_Call(Unreal_DefaultObjectOf(statics), frame) ||
        !Unreal_CallFrameGet(frame, "ReturnValue", &listener, sizeof(listener)))
        listener = kNullAddress;
    Unreal_CallFrameDestroy(frame);
    URK_UnrealPlace kept{};
    kept.object = owner;
    kept.member = "ReferencedObjects";
    const std::int32_t count = Unreal_PlaceCount(&kept);
    const URK_UnrealPlace slot = Element(kept, count);
    if (listener == kNullAddress || count < 0 || !Unreal_PlaceInsert(&kept, count, 1) ||
        !Unreal_PlaceWriteObject(&slot, listener)) {
        if (count >= 0 && Unreal_PlaceCount(&kept) > count)
            Unreal_PlaceRemove(&kept, count, 1);
        *why = "the listener object could not be made or kept";
        return kNullAddress;
    }
    return listener;
}

// Game thread: unbinds the listener where its owner still lives and lets the listener go.
void ReleaseListener(void *user) {
    std::unique_ptr<Subscription> subscription(static_cast<Subscription *>(user));
    UnrealEngine &engine = UnrealEngine::Instance();
    if (Live(engine, subscription->place.object)) {
        const std::int32_t bound = IndexOf(subscription->place, subscription->listener);
        if (bound >= 0)
            Unreal_PlaceRemove(&subscription->place, bound, 1);
    }
    URK_UnrealPlace kept{};
    kept.object = TheGameInstance();
    kept.member = "ReferencedObjects";
    const std::int32_t held = kept.object ? IndexOf(kept, subscription->listener) : -1;
    if (held >= 0)
        Unreal_PlaceRemove(&kept, held, 1);
}

std::uint64_t Unreal_DelegateSubscribe(const URK_UnrealPlace *place, URK_UnrealFunctionHookFn callback,
                                       void *userData) {
    if (!callback || !place || !place->member || place->object == URK_UNREAL_NULL_OBJECT ||
        place->step_count >= URK_UNREAL_PLACE_MAX_STEPS)
        return 0;
    const auto refuse = [&](const std::string &why) {
        Report(kNullAddress, std::string("subscribe to ") + place->member + " refused: " + why);
        return std::uint64_t{0};
    };
    if (!ProcessEventHook::Instance().Installed())
        return refuse("ProcessEvent is not hooked");
    if (!OnGameThread())
        return refuse("not on the game thread");
    const std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context || !context->target.value)
        return refuse("the delegate is not readable");
    const PropertyInfo &info = context->target.info;
    if (info.kind != PropertyKind::MulticastDelegate && info.kind != PropertyKind::SparseDelegate)
        return refuse("not a multicast delegate");
    UnrealEngine &engine = UnrealEngine::Instance();
    std::optional<FunctionInfo> parameters =
        DescribeFunction(engine.Chain(), engine.Values(), engine.Functions(), info.typeObject);
    const Address object = Unreal_FindObjectInOuter("Object", "/Script/CoreUObject");
    const Address function = object ? Unreal_FindFunction(object, kListenerFunction) : kNullAddress;
    if (!parameters || function == kNullAddress)
        return refuse("its signature or UObject::ExecuteUbergraph is not readable");

    auto subscription = std::make_unique<Subscription>();
    subscription->callback = callback;
    subscription->user = userData;
    subscription->signature = info.typeObject;
    subscription->parameters = std::make_shared<const FunctionInfo>(std::move(*parameters));
    subscription->place = *place;
    subscription->member = place->member;
    subscription->place.member = subscription->member.c_str();
    for (std::uint32_t i = 0; i < place->step_count; ++i) {
        if (place->steps[i].name) {
            subscription->steps[i] = place->steps[i].name;
            subscription->place.steps[i].name = subscription->steps[i].c_str();
        }
    }
    std::string why;
    subscription->listener = MakeListener(&why);
    if (subscription->listener == kNullAddress)
        return refuse(why);

    // A sparse delegate is bound whole; an inline one gets a new element.
    const Address listener = subscription->listener;
    const auto bind = [listener](Places &places, const PlaceTarget &target) {
        return places.Bind(target, listener, kListenerFunction, true, false);
    };
    const URK_UnrealPlace &at = subscription->place;
    bool bound = false;
    if (info.kind == PropertyKind::SparseDelegate) {
        bound = Changed(&at, bind) != 0;
    } else if (const std::int32_t count = Unreal_PlaceCount(&at); count >= 0 && Unreal_PlaceInsert(&at, count, 1)) {
        const URK_UnrealPlace element = Element(at, count);
        bound = Changed(&element, bind) != 0;
        if (!bound)
            Unreal_PlaceRemove(&at, count, 1);
    }
    if (!bound) {
        ReleaseListener(subscription.release());
        return refuse("the listener did not bind");
    }
    FunctionHooks::Instance().SetSuperOffset(engine.Structs().superStruct);
    const std::lock_guard lock(g_subscriptionsMutex);
    const std::uint64_t id = FunctionHooks::Instance().Add(function, &ListenerCallback, nullptr, subscription.get());
    if (id == 0) {
        ReleaseListener(subscription.release());
        return 0;
    }
    g_subscriptions.emplace(id, std::move(subscription));
    return id;
}

int Unreal_DelegateUnsubscribe(std::uint64_t id) {
    std::unique_ptr<Subscription> subscription;
    {
        const std::lock_guard lock(g_subscriptionsMutex);
        const auto found = g_subscriptions.find(id);
        if (found == g_subscriptions.end())
            return 0;
        subscription = std::move(found->second);
        g_subscriptions.erase(found);
    }
    if (!FunctionHooks::Instance().Remove(id)) {
        Report(kNullAddress, "a delegate callback did not finish in time; the mod must stay loaded");
        subscription.release();
        return 0;
    }
    // The binding stays harmless (the engine runs nothing for it) until the game thread drops it.
    Subscription *released = subscription.release();
    if (OnGameThread())
        ReleaseListener(released);
    else if (!ProcessEventHook::Instance().Post(&ReleaseListener, released))
        delete released;
    return 1;
}

} // namespace URK::Unreal::SdkApi
