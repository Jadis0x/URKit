// The URK_UnrealApi table, and the loader's entry points into it.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

URK_UnrealApi BuildTable() {
    URK_UnrealApi api{};
    api.version = URK_UNREAL_API_VERSION;
    api.size = sizeof(URK_UnrealApi);

    api.is_available = &Unreal_IsAvailable;
    api.engine_version = &Unreal_EngineVersion;
    api.uses_field_properties = &Unreal_UsesFieldProperties;

    api.find_object = &Unreal_FindObject;
    api.find_object_in_outer = &Unreal_FindObjectInOuter;
    api.class_of = &Unreal_ClassOf;
    api.outer_of = &Unreal_OuterOf;
    api.name_of = &Unreal_NameOf;

    api.is_child_of = &Unreal_IsChildOf;
    api.is_a = &Unreal_IsA;
    api.default_object_of = &Unreal_DefaultObjectOf;
    api.instances_of = &Unreal_InstancesOf;

    api.describe_property = &Unreal_DescribeProperty;
    api.read_integer = &Unreal_ReadInteger;
    api.read_floating = &Unreal_ReadFloating;
    api.read_bool = &Unreal_ReadBool;
    api.read_object = &Unreal_ReadObject;
    api.read_name = &Unreal_ReadName;
    api.read_string = &Unreal_ReadString;
    api.write_integer = &Unreal_WriteInteger;
    api.write_floating = &Unreal_WriteFloating;
    api.write_bool = &Unreal_WriteBool;
    api.write_object = &Unreal_WriteObject;

    api.find_function = &Unreal_FindFunction;
    api.call_frame_create = &Unreal_CallFrameCreate;
    api.call_frame_destroy = &Unreal_CallFrameDestroy;
    api.call_frame_set = &Unreal_CallFrameSet;
    api.call_frame_get = &Unreal_CallFrameGet;
    api.call = &Unreal_Call;

    api.hook_install = &Unreal_HookInstall;
    api.hook_installed = &Unreal_HookInstalled;
    api.hook_remove = &Unreal_HookRemove;
    api.process_event_observe = &Unreal_ProcessEventObserve;
    api.game_thread_id = &Unreal_GameThreadId;
    api.post_to_game_thread = &Unreal_PostToGameThread;

    api.struct_size = &Unreal_StructSize;
    api.describe_struct_member = &Unreal_DescribeStructMember;
    api.read_struct = &Unreal_ReadStruct;
    api.write_struct = &Unreal_WriteStruct;

    api.place_describe = &Unreal_PlaceDescribe;
    api.place_read_integer = &Unreal_PlaceReadInteger;
    api.place_write_integer = &Unreal_PlaceWriteInteger;
    api.place_read_floating = &Unreal_PlaceReadFloating;
    api.place_write_floating = &Unreal_PlaceWriteFloating;
    api.place_read_bool = &Unreal_PlaceReadBool;
    api.place_write_bool = &Unreal_PlaceWriteBool;
    api.place_read_object = &Unreal_PlaceReadObject;
    api.place_write_object = &Unreal_PlaceWriteObject;
    api.place_read_text = &Unreal_PlaceReadText;
    api.place_write_text = &Unreal_PlaceWriteText;
    api.place_read_bytes = &Unreal_PlaceReadBytes;
    api.place_write_bytes = &Unreal_PlaceWriteBytes;
    api.place_count = &Unreal_PlaceCount;
    api.place_slots = &Unreal_PlaceSlots;
    api.place_insert = &Unreal_PlaceInsert;
    api.place_remove = &Unreal_PlaceRemove;
    api.place_clear = &Unreal_PlaceClear;
    api.place_find = &Unreal_PlaceFind;
    api.place_add = &Unreal_PlaceAdd;
    api.place_bind = &Unreal_PlaceBind;
    api.enum_count = &Unreal_EnumCount;
    api.enum_entry = &Unreal_EnumEntry;
    api.enum_value = &Unreal_EnumValue;
    api.enum_name = &Unreal_EnumName;
    api.script_call_observe = &Unreal_ScriptCallObserve;
    api.object_life_observe = &Unreal_ObjectLifeObserve;
    api.function_hook_add = &Unreal_FunctionHookAdd;
    api.function_hook_remove = &Unreal_FunctionHookRemove;
    api.delegate_subscribe = &Unreal_DelegateSubscribe;
    api.delegate_unsubscribe = &Unreal_DelegateUnsubscribe;

    return api;
}

} // namespace URK::Unreal::SdkApi

namespace URK::Unreal {
using namespace SdkApi;

const URK_UnrealApi *UnrealSdkApi(const HookInstaller &installer) {
    static const URK_UnrealApi table = BuildTable();
    g_installer = installer;
    return &table;
}

void UnrealSdk_SetLog(LogSink log) { g_log.store(log, std::memory_order_release); }

void UnrealSdk_ReleasePending() {
    if (!UnrealEngine::Instance().Available() || !OnGameThread())
        return;
    // Once, on the first frame, so failed measurements show at load.
    static bool measured = false;
    if (!measured) {
        measured = true;
        Services &services = Serve();
        const auto check = [](bool ok, const char *what, const std::string &why) {
            if (!ok)
                Report(kNullAddress, std::string(what) + " unavailable: " + why);
        };
        const std::optional<EngineCalls::Block> probe = services.calls.Allocate(64, 16);
        check(probe && services.calls.Free(probe->data), "engine allocation", services.calls.Failure());
        check(services.calls.TextReleaseAvailable(), "releasing text", services.calls.Failure());
        check(services.calls.MeasureWeakNow(), "weak references", services.calls.Failure());
        check(services.enums.Measure(services.calls), "enum names", services.enums.Failure());
    }
    ReleasePending();
}

const EnumNames *UnrealSdk_Enums() { return Enums(); }

bool UnrealSdk_HoldProcessEventHook() {
    std::lock_guard lock(g_hookMutex);
    if (!EnsureHookInstalled())
        return false;
    g_loaderHold = true;
    return true;
}

} // namespace URK::Unreal
