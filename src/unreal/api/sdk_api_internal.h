#pragma once

// Internal to the SDK API files: what one part of the table shares with another.

#include "unreal/unreal_sdk_api.h"
#include "unreal/hooks/unreal_object_life_hook.h"
#include "unreal/hooks/unreal_script_hook.h"
#include "unreal/reflection/unreal_enums.h"
#include "unreal/values/unreal_owned_values.h"
#include "unreal/values/unreal_places.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace URK::Unreal::SdkApi {

// Resolves a member for every value entry, so failures are uniform.
struct ResolvedMember {
    PropertyInfo info;
};

// Engine memory services, built after calibration. Changes on the game thread only.
struct Services {
    explicit Services(UnrealEngine &engine)
        : calls(engine.Finder(), engine.Chain(), engine.Values(), engine.Functions(), engine.Types(), engine.Structs(),
                engine.ProcessEvent(), engine.Bounds()),
          owned(engine.Finder(), engine.Chain(), engine.Values(), engine.Types(), calls),
          enums(engine.Finder(), engine.Structs()), places(engine, owned, enums) {}
    EngineCalls calls;
    OwnedValues owned;
    EnumNames enums;
    Places places;
};

// Mod-held frame: parameter block plus which parameters own engine memory.
struct LoaderFrame {
    explicit LoaderFrame(FunctionInfo info)
        : frame(std::move(info)), engineOwned(frame.Function().parameters.size(), 0) {}
    // A hooked call's own block: the engine made it and gives it back.
    LoaderFrame(std::shared_ptr<const FunctionInfo> info, void *data, void *returned)
        : frame(std::move(info), data, returned), engineOwned(frame.Function().parameters.size(), 0) {}
    CallFrame frame;
    // Made through a place, or written by a call.
    std::vector<char> engineOwned;
    // A written parameter whose memory cannot be released; calls are refused.
    std::string unreleasable;
    bool called = false;
};

// Engine flag value (EPropertyFlags).
inline constexpr std::uint64_t kPropertyFlagConstParm = 0x2;

struct PlaceContext {
    PlaceTarget target;
    LoaderFrame *frame = nullptr;
    std::size_t parameter = 0;
};

struct ModFunctionHook {
    URK_UnrealFunctionHookFn before = nullptr;
    URK_UnrealFunctionHookFn after = nullptr;
    void *user = nullptr;
    Address function = kNullAddress;
    std::shared_ptr<const FunctionInfo> info;
    // Blueprint overrides, described on their first call.
    struct Override {
        std::uint64_t stamp = 0;
        std::shared_ptr<const FunctionInfo> info;
    };
    std::mutex overridesMutex;
    std::unordered_map<Address, Override> overrides;
};

inline constexpr const char *kListenerFunction = "ExecuteUbergraph";

extern HookInstaller g_installer;
extern std::atomic<LogSink> g_log;
extern std::mutex g_hookMutex;
extern bool g_loaderHold;

bool CopyOut(const std::string &text, char *output, std::size_t outputSize);
bool Live(const UnrealEngine &engine, Address object);
bool AssignableMember(const UnrealEngine &engine, const PropertyInfo &info, Address value);
void FillInfo(URK_UnrealPropertyInfo *info, const PropertyInfo &from);
bool IsStruct(const UnrealEngine &engine, Address object);
std::optional<ResolvedMember> Resolve(UnrealEngine &engine, Address object, const char *memberName);
int Unreal_IsAvailable();
void Unreal_EngineVersion(std::int32_t *major, std::int32_t *minor, std::int32_t *patch);
int Unreal_UsesFieldProperties();
URK_UnrealObject Unreal_FindObject(const char *name);
URK_UnrealObject Unreal_FindObjectInOuter(const char *name, const char *outerName);
URK_UnrealObject Unreal_ClassOf(URK_UnrealObject object);
URK_UnrealObject Unreal_OuterOf(URK_UnrealObject object);
int Unreal_NameOf(URK_UnrealObject object, char *output, std::size_t outputSize);
int Unreal_IsChildOf(URK_UnrealObject structObject, URK_UnrealObject base);
int Unreal_IsA(URK_UnrealObject object, URK_UnrealObject classObject);
URK_UnrealObject Unreal_DefaultObjectOf(URK_UnrealObject classObject);
std::size_t Unreal_InstancesOf(URK_UnrealObject classObject, URK_UnrealObject *output, std::size_t outputCapacity,
                               int exact);
int Unreal_DescribeProperty(URK_UnrealObject object, const char *memberName, URK_UnrealPropertyInfo *info);
int Unreal_ReadInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t *output);
int Unreal_ReadFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double *output);
int Unreal_ReadBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int *output);
URK_UnrealObject Unreal_ReadObject(URK_UnrealObject object, const char *memberName, std::int32_t index);
int Unreal_ReadName(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                    std::size_t outputSize);
int Unreal_ReadString(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                      std::size_t outputSize);
int Unreal_WriteInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t value);
int Unreal_WriteFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double value);
int Unreal_WriteBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int value);
int Unreal_WriteObject(URK_UnrealObject object, const char *memberName, std::int32_t index, URK_UnrealObject value);
std::int32_t Unreal_StructSize(URK_UnrealObject structObject);
int Unreal_DescribeStructMember(URK_UnrealObject structObject, const char *memberName, URK_UnrealPropertyInfo *info,
                                std::int32_t *offset);
int Unreal_ReadStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, void *output,
                      std::size_t size);
int Unreal_WriteStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, const void *value,
                       std::size_t size);
Services &Serve();
void Report(Address subject, const std::string &message);
bool OnGameThread();
LoaderFrame *FrameOf(URK_UnrealCallFrame *frame);
const LoaderFrame *FrameOf(const URK_UnrealCallFrame *frame);
bool Written(const FunctionParameter &parameter);
void ReleaseFrame(LoaderFrame &loaderFrame);
void ReleasePending();
URK_UnrealObject Unreal_FindFunction(URK_UnrealObject ownerClass, const char *name);
URK_UnrealCallFrame *Unreal_CallFrameCreate(URK_UnrealObject function);
void Unreal_CallFrameDestroy(URK_UnrealCallFrame *frame);
int Unreal_CallFrameSet(URK_UnrealCallFrame *frame, const char *parameterName, const void *value, std::size_t size);
int Unreal_CallFrameGet(const URK_UnrealCallFrame *frame, const char *parameterName, void *output,
                        std::size_t size);
int Unreal_Call(URK_UnrealObject object, URK_UnrealCallFrame *frame);
std::optional<PlaceContext> ResolvePlace(const URK_UnrealPlace *place, bool describe);
int Unreal_PlaceDescribe(const URK_UnrealPlace *place, URK_UnrealPropertyInfo *info);
int Unreal_PlaceReadInteger(const URK_UnrealPlace *place, std::int64_t *output);
int Unreal_PlaceWriteInteger(const URK_UnrealPlace *place, std::int64_t value);
int Unreal_PlaceReadFloating(const URK_UnrealPlace *place, double *output);
int Unreal_PlaceWriteFloating(const URK_UnrealPlace *place, double value);
int Unreal_PlaceReadBool(const URK_UnrealPlace *place, int *output);
int Unreal_PlaceWriteBool(const URK_UnrealPlace *place, int value);
URK_UnrealObject Unreal_PlaceReadObject(const URK_UnrealPlace *place);
int Unreal_PlaceWriteObject(const URK_UnrealPlace *place, URK_UnrealObject value);
int Unreal_PlaceReadText(const URK_UnrealPlace *place, char *output, std::size_t outputSize, std::size_t *length);
int Unreal_PlaceWriteText(const URK_UnrealPlace *place, const char *utf8);
int Unreal_PlaceReadBytes(const URK_UnrealPlace *place, void *output, std::size_t size);
int Unreal_PlaceWriteBytes(const URK_UnrealPlace *place, const void *value, std::size_t size);
std::int32_t Unreal_PlaceCount(const URK_UnrealPlace *place);
std::int32_t Unreal_PlaceSlots(const URK_UnrealPlace *place, std::int32_t *output, std::int32_t capacity);
int Unreal_PlaceInsert(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count);
int Unreal_PlaceRemove(const URK_UnrealPlace *place, std::int32_t index, std::int32_t count);
int Unreal_PlaceClear(const URK_UnrealPlace *place);
std::int32_t Unreal_PlaceFind(const URK_UnrealPlace *place, const URK_UnrealKey *key);
std::int32_t Unreal_PlaceAdd(const URK_UnrealPlace *place, const URK_UnrealKey *key);
int Unreal_PlaceBind(const URK_UnrealPlace *place, URK_UnrealObject object, const char *function);
const EnumNames *Enums();
std::int32_t Unreal_EnumCount(URK_UnrealObject enumObject);
int Unreal_EnumEntry(URK_UnrealObject enumObject, std::int32_t index, char *name, std::size_t nameSize,
                     std::int64_t *value);
int Unreal_EnumValue(URK_UnrealObject enumObject, const char *name, std::int64_t *value);
int Unreal_EnumName(URK_UnrealObject enumObject, std::int64_t value, char *output, std::size_t outputSize);
bool EnsureHookInstalled();
int Unreal_HookInstall();
int Unreal_HookInstalled();
int Unreal_HookRemove();
bool ObserverTrampoline(void *, Address object, Address function, void *parms);
void Unreal_ProcessEventObserve(URK_UnrealProcessEventObserverFn observer, void *userData);
void ScriptObserverTrampoline(void *, Address object, Address function, void *locals, void *result, bool after);
bool EnsureScriptHook();
int Unreal_ScriptCallObserve(URK_UnrealScriptCallObserverFn observer, void *userData);
void LifeObserverTrampoline(void *, Address object, bool created);
int Unreal_ObjectLifeObserve(URK_UnrealObjectLifeObserverFn observer, void *userData);
std::uint64_t FunctionStamp(const UnrealEngine &engine, Address function);
std::shared_ptr<const FunctionInfo> InfoFor(ModFunctionHook &hook, Address called);
bool ModHookCallback(void *user, const HookedCall &call);
std::uint64_t Unreal_FunctionHookAdd(URK_UnrealObject function, URK_UnrealFunctionHookFn before,
                                     URK_UnrealFunctionHookFn after, void *userData);
int Unreal_FunctionHookRemove(std::uint64_t id);
std::uint32_t Unreal_GameThreadId();
int Unreal_PostToGameThread(URK_UnrealPostedWorkFn work, void *userData);
bool ListenerCallback(void *user, const HookedCall &call);
URK_UnrealPlace Element(const URK_UnrealPlace &place, std::int32_t index);
std::int32_t IndexOf(const URK_UnrealPlace &place, Address object);
Address TheGameInstance();
Address MakeListener(std::string *why);
void ReleaseListener(void *user);
std::uint64_t Unreal_DelegateSubscribe(const URK_UnrealPlace *place, URK_UnrealFunctionHookFn callback,
                                       void *userData);
int Unreal_DelegateUnsubscribe(std::uint64_t id);

// On success a frame parameter may now own engine memory.
template <typename Change> int Changed(const URK_UnrealPlace *place, Change change) {
    std::optional<PlaceContext> context = ResolvePlace(place, false);
    if (!context)
        return 0;
    if (context->target.key) {
        Report(kNullAddress, std::string("place ") + place->member +
                                 ": a set element or map key is replaced by removing it and adding the new one");
        return 0;
    }
    Places &places = Serve().places;
    if (!change(places, context->target)) {
        Report(kNullAddress, std::string("place ") + place->member + ": " + places.Failure());
        return 0;
    }
    if (context->frame)
        context->frame->engineOwned[context->parameter] = 1;
    return 1;
}

} // namespace URK::Unreal::SdkApi
