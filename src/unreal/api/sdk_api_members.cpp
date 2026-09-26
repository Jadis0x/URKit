// Members by name, struct copies and enum names.

#include "unreal/api/sdk_api_internal.h"

namespace URK::Unreal::SdkApi {

int Unreal_DescribeProperty(URK_UnrealObject object, const char *memberName, URK_UnrealPropertyInfo *info) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!info)
        return 0;
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;

    FillInfo(info, resolved->info);
    return 1;
}

int Unreal_ReadInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<std::int64_t> value = engine.Values().ReadInteger(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<double> value = engine.Values().ReadFloating(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value;
    return 1;
}

int Unreal_ReadBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int *output) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output)
        return 0;
    const std::optional<bool> value = engine.Values().ReadBool(object, resolved->info, index);
    if (!value)
        return 0;
    *output = *value ? 1 : 0;
    return 1;
}

URK_UnrealObject Unreal_ReadObject(URK_UnrealObject object, const char *memberName, std::int32_t index) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return URK_UNREAL_NULL_OBJECT;
    return engine.Values().ReadObject(object, resolved->info, index);
}

int Unreal_ReadName(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                    std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadName(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_ReadString(URK_UnrealObject object, const char *memberName, std::int32_t index, char *output,
                      std::size_t outputSize) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    const std::optional<std::string> value = engine.Values().ReadString(object, resolved->info, index);
    return value ? (CopyOut(*value, output, outputSize) ? 1 : 0) : 0;
}

int Unreal_WriteInteger(URK_UnrealObject object, const char *memberName, std::int32_t index, std::int64_t value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteInteger(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteFloating(URK_UnrealObject object, const char *memberName, std::int32_t index, double value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteFloating(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

int Unreal_WriteBool(URK_UnrealObject object, const char *memberName, std::int32_t index, int value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved)
        return 0;
    return engine.Values().WriteBool(engine.Writer(), object, resolved->info, value != 0, index) ? 1 : 0;
}

int Unreal_WriteObject(URK_UnrealObject object, const char *memberName, std::int32_t index, URK_UnrealObject value) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !AssignableMember(engine, resolved->info, value))
        return 0;
    return engine.Values().WriteObject(engine.Writer(), object, resolved->info, value, index) ? 1 : 0;
}

std::int32_t Unreal_StructSize(URK_UnrealObject structObject) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const StructOffsets &structs = engine.Structs();
    if (!IsStruct(engine, structObject) || structs.propertiesSize == kOffsetNotFound)
        return 0;
    // GetStructureSize(): what a member of this type occupies.
    const std::int32_t size = engine.Reader().ReadInt32(structObject + structs.propertiesSize).value_or(0);
    const std::int32_t alignment = structs.minAlignment == kOffsetNotFound
                                       ? 1
                                       : engine.Reader().ReadAs<std::int16_t>(structObject + structs.minAlignment)
                                             .value_or(1);
    if (size <= 0 || alignment <= 0)
        return size > 0 ? size : 0;
    return (size + alignment - 1) / alignment * alignment;
}

int Unreal_DescribeStructMember(URK_UnrealObject structObject, const char *memberName, URK_UnrealPropertyInfo *info,
                                std::int32_t *offset) {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!memberName || !info || !offset || !IsStruct(engine, structObject))
        return 0;
    const Address field = engine.Chain().FindMemberDeep(structObject, memberName);
    const std::optional<PropertyInfo> described =
        field != kNullAddress ? engine.Values().Describe(field) : std::nullopt;
    if (!described || !described->Resolved())
        return 0;
    FillInfo(info, *described);
    *offset = described->offset;
    return 1;
}

int Unreal_ReadStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, void *output,
                      std::size_t size) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !output || resolved->info.kind != PropertyKind::Struct ||
        size != static_cast<std::size_t>(resolved->info.elementSize))
        return 0;
    const Address at = engine.Values().ValueAddress(object, resolved->info, index);
    return at != kNullAddress && engine.Reader().Read(at, output, size) ? 1 : 0;
}

int Unreal_WriteStruct(URK_UnrealObject object, const char *memberName, std::int32_t index, const void *value,
                       std::size_t size) {
    UnrealEngine &engine = UnrealEngine::Instance();
    const std::optional<ResolvedMember> resolved = Resolve(engine, object, memberName);
    if (!resolved || !value || resolved->info.kind != PropertyKind::Struct ||
        size != static_cast<std::size_t>(resolved->info.elementSize))
        return 0;
    const Address at = engine.Values().ValueAddress(object, resolved->info, index);
    std::vector<std::uint8_t> current(size);
    if (at == kNullAddress || !engine.Reader().Read(at, current.data(), size))
        return 0;
    if (!StructChangeAllowed(engine, resolved->info.inner, current.data(), static_cast<const std::uint8_t *>(value),
                             size))
        return 0;
    std::vector<std::uint8_t> merged = current;
    MergeStructMembers(engine, resolved->info.inner, merged.data(), static_cast<const std::uint8_t *>(value), size);
    return engine.Writer().Write(at, merged.data(), size) ? 1 : 0;
}

// Names are measured on the game thread; after that any thread reads them.
const EnumNames *Enums() {
    UnrealEngine &engine = UnrealEngine::Instance();
    if (!engine.Available())
        return nullptr;
    Services &services = Serve();
    if (!services.enums.Measured() && !(OnGameThread() && services.enums.Measure(services.calls)))
        return nullptr;
    return &services.enums;
}

std::int32_t Unreal_EnumCount(URK_UnrealObject enumObject) {
    const EnumNames *enums = Enums();
    const auto entries = enums ? enums->Entries(enumObject) : std::nullopt;
    return entries ? static_cast<std::int32_t>(entries->size()) : 0;
}

int Unreal_EnumEntry(URK_UnrealObject enumObject, std::int32_t index, char *name, std::size_t nameSize,
                     std::int64_t *value) {
    const EnumNames *enums = Enums();
    const auto entries = enums ? enums->Entries(enumObject) : std::nullopt;
    if (!entries || index < 0 || static_cast<std::size_t>(index) >= entries->size())
        return 0;
    const EnumNames::Entry &entry = (*entries)[static_cast<std::size_t>(index)];
    if (value)
        *value = entry.value;
    return !name || CopyOut(std::string(EnumNames::ShortName(entry.name)), name, nameSize) ? 1 : 0;
}

int Unreal_EnumValue(URK_UnrealObject enumObject, const char *name, std::int64_t *value) {
    const EnumNames *enums = Enums();
    const std::optional<std::int64_t> found = enums && name ? enums->ValueOf(enumObject, name) : std::nullopt;
    if (!found || !value)
        return 0;
    *value = *found;
    return 1;
}

int Unreal_EnumName(URK_UnrealObject enumObject, std::int64_t value, char *output, std::size_t outputSize) {
    const EnumNames *enums = Enums();
    const std::optional<std::string> name = enums ? enums->NameOf(enumObject, value) : std::nullopt;
    return name && CopyOut(*name, output, outputSize) ? 1 : 0;
}

} // namespace URK::Unreal::SdkApi
