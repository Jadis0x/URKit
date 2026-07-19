#pragma once
#include "unity_shortcuts.h"

namespace URK::Unity {
// URK_UNITY_INSPECT_BEGIN
namespace Inspect {
inline constexpr std::uint32_t kStaticMemberFlag = 0x0010u;
struct TypeInfo {
    const void* handle = nullptr;
    std::string namespc;
    std::string name;
    std::string full_name;
    std::uint32_t flags = 0;
    bool is_value_type = false;
    bool is_enum = false;
};
struct FieldInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* type = nullptr;
    std::string type_name;
    std::uint32_t flags = 0;
    bool is_static = false;
    bool is_value_type = false;
    bool is_enum = false;
};
struct MethodParamInfo {
    const void* type = nullptr;
    std::string name;
    std::string type_name;
};
struct MethodInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* return_type_handle = nullptr;
    std::string return_type;
    std::vector<MethodParamInfo> parameters;
    std::uint32_t flags = 0;
    std::uint32_t iflags = 0;
    bool is_static = false;
};
struct PropertyInfo {
    const void* handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void* type = nullptr;
    std::string type_name;
    const void* get_method = nullptr;
    const void* set_method = nullptr;
    std::uint32_t flags = 0;
    bool can_read = false;
    bool can_write = false;
    bool is_value_type = false;
    bool is_enum = false;
};
enum class ValueKind {
    Unavailable,
    Null,
    Boolean,
    SignedInteger,
    UnsignedInteger,
    FloatingPoint,
    String,
    ObjectReference,
    ArrayReference,
    Enum,
    ValueType,
};
struct ValueInfo {
    ValueKind kind = ValueKind::Unavailable;
    std::string type_name;
    std::string display;
    void* object = nullptr;
    bool bool_value = false;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    double floating_value = 0.0;
    std::size_t array_length = 0;
    bool readable = false;
};
struct ObjectRefInfo {
    void* handle = nullptr;
    TypeInfo type;
    std::string display;
    bool is_null = true;
    bool expandable = false;
};
struct ObjectHandle {
    std::uint32_t handle = 0;
    bool weak = false;
    bool pinned = false;
};
inline void emit(DiagnosticSink sink, const char* text) { if (sink) sink(text); }
inline std::string type_name(const void* type) { char out[256]{}; return URK::il2cpp::type_get_name(static_cast<const URK::il2cpp::Type*>(type), out, sizeof(out)) ? std::string(out) : std::string{}; }
inline TypeInfo DescribeClass(const void* klass) {
    TypeInfo out{};
    out.handle = klass;
    if (!klass) return out;
    const auto* k = static_cast<const URK::il2cpp::Class*>(klass);
    const char* ns = URK::il2cpp::class_get_namespace(k);
    const char* name = URK::il2cpp::class_get_name(k);
    out.namespc = ns ? ns : "";
    out.name = name ? name : "";
    out.full_name = out.namespc.empty() ? out.name : out.namespc + "." + out.name;
    out.flags = URK::il2cpp::class_get_flags(k);
    out.is_value_type = URK::il2cpp::class_is_valuetype(k);
    out.is_enum = URK::il2cpp::class_is_enum(k);
    return out;
}
inline TypeInfo DescribeType(const void* type) {
    return DescribeClass(URK::il2cpp::type_get_class_or_element_class(static_cast<const URK::il2cpp::Type*>(type)));
}
inline TypeInfo TypeOf(Object object) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::TypeOf failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return DescribeClass(klass);
}
inline ObjectRefInfo DescribeObject(Object object) {
    detail::clear_error();
    ObjectRefInfo out{};
    out.handle = object.handle();
    out.is_null = out.handle == nullptr;
    if (out.is_null) {
        out.display = "null";
        return out;
    }
    out.type = TypeOf(object);
    if (!out.type.handle) {
        out.display = "<unavailable object>";
        return out;
    }
    out.display = out.type.full_name.empty() ? "<object>" : out.type.full_name;
    out.expandable = true;
    return out;
}
inline ObjectRefInfo ExpandValue(const ValueInfo& value) {
    if (value.kind == ValueKind::Null) {
        ObjectRefInfo out{};
        out.display = "null";
        return out;
    }
    if ((value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference) || !value.object) {
        detail::set_error("Unity Inspect::ExpandValue failed: value is not an object reference");
        return {};
    }
    return DescribeObject(Object{value.object});
}
inline ObjectHandle PinObject(Object object, bool pinned = false) {
    detail::clear_error();
    ObjectHandle out{};
    out.pinned = pinned;
    if (!object.handle()) { detail::set_error("Unity Inspect::PinObject failed: object is null"); return out; }
    out.handle = detail::Backend::gchandle_new(object.handle(), pinned ? 1 : 0);
    if (!out.handle) { detail::set_error("Unity Inspect::PinObject failed: backend gchandle_new API is unavailable or failed"); detail::append_backend_error(); }
    return out;
}
inline ObjectHandle PinValue(const ValueInfo& value, bool pinned = false) {
    if ((value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference && value.kind != ValueKind::String) || !value.object) {
        detail::set_error("Unity Inspect::PinValue failed: value is not a managed object reference");
        return {};
    }
    return PinObject(Object{value.object}, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    detail::clear_error();
    ObjectHandle out{};
    out.weak = true;
    if (!object.handle()) { detail::set_error("Unity Inspect::WeakObject failed: object is null"); return out; }
    out.handle = detail::Backend::gchandle_new_weakref(object.handle(), trackResurrection ? 1 : 0);
    if (!out.handle) { detail::set_error("Unity Inspect::WeakObject failed: backend gchandle_new_weakref API is unavailable or failed"); detail::append_backend_error(); }
    return out;
}
inline Object ResolveObjectHandle(const ObjectHandle& handle) {
    detail::clear_error();
    if (!handle.handle) { detail::set_error("Unity Inspect::ResolveObjectHandle failed: handle is empty"); return {}; }
    void* object = detail::Backend::gchandle_get_target(handle.handle);
    if (!object) { detail::set_error("Unity Inspect::ResolveObjectHandle failed: backend gchandle_get_target API is unavailable, failed, or target was collected"); detail::append_backend_error(); return {}; }
    return Object{object};
}
inline void FreeObjectHandle(ObjectHandle& handle) {
    detail::clear_error();
    if (!handle.handle) return;
    detail::Backend::gchandle_free(handle.handle);
    handle.handle = 0;
    handle.weak = false;
    handle.pinned = false;
}
inline std::vector<FieldInfo> fields_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<FieldInfo> out;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void* it = nullptr;
        while (const auto* field = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class*>(current), &it)) {
            FieldInfo info{};
            info.handle = field;
            info.declaring_type = declaring;
            const char* name = URK::il2cpp::field_get_name(field);
            info.name = name ? name : "";
            const void* fieldType = URK::il2cpp::field_get_type(field);
            const TypeInfo fieldTypeInfo = DescribeType(fieldType);
            info.type = fieldType;
            info.type_name = type_name(fieldType);
            info.flags = URK::il2cpp::field_get_flags(field);
            info.is_static = (info.flags & kStaticMemberFlag) != 0;
            info.is_value_type = fieldTypeInfo.is_value_type;
            info.is_enum = fieldTypeInfo.is_enum;
            out.push_back(info);
        }
    }
    return out;
}
inline std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Fields failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return fields_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Fields failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return fields_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Fields(Object{object.handle}, includeInherited) : std::vector<FieldInfo>{};
}
inline MethodInfo method_info(const URK::il2cpp::Method* method, TypeInfo declaring) {
    MethodInfo info{};
    info.handle = method;
    info.declaring_type = std::move(declaring);
    const char* name = URK::il2cpp::method_get_name(method);
    info.name = name ? name : "";
    info.flags = URK::il2cpp::method_get_flags(method, &info.iflags);
    info.is_static = (info.flags & kStaticMemberFlag) != 0;
    info.return_type_handle = URK::il2cpp::method_get_return_type(method);
    info.return_type = type_name(info.return_type_handle);
    const std::uint32_t count = URK::il2cpp::method_get_param_count(method);
    info.parameters.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const void* paramType = URK::il2cpp::method_get_param(method, i);
        const char* paramName = URK::il2cpp::method_get_param_name(method, i);
        info.parameters.push_back(MethodParamInfo{paramType, paramName ? paramName : "", type_name(paramType)});
    }
    return info;
}
inline std::vector<MethodInfo> methods_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<MethodInfo> out;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void* it = nullptr;
        while (const auto* method = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class*>(current), &it))
            out.push_back(method_info(method, declaring));
    }
    return out;
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Methods failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return methods_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Methods failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return methods_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Methods(Object{object.handle}, includeInherited) : std::vector<MethodInfo>{};
}
inline PropertyInfo property_info(const URK::il2cpp::Property* property, TypeInfo declaring) {
    PropertyInfo info{};
    info.handle = property;
    info.declaring_type = std::move(declaring);
    const char* name = URK::il2cpp::property_get_name(property);
    info.name = name ? name : "";
    info.flags = URK::il2cpp::property_get_flags(property);
    info.get_method = URK::il2cpp::property_get_get_method(property);
    info.set_method = URK::il2cpp::property_get_set_method(property);
    info.can_read = info.get_method != nullptr;
    info.can_write = info.set_method != nullptr;
    const void* propertyType = info.get_method ? URK::il2cpp::method_get_return_type(static_cast<const URK::il2cpp::Method*>(info.get_method))
                                               : (info.set_method ? URK::il2cpp::method_get_param(static_cast<const URK::il2cpp::Method*>(info.set_method), 0) : nullptr);
    const TypeInfo propertyTypeInfo = DescribeType(propertyType);
    info.type = propertyType;
    info.type_name = type_name(propertyType);
    info.is_value_type = propertyTypeInfo.is_value_type;
    info.is_enum = propertyTypeInfo.is_enum;
    return info;
}
inline std::vector<PropertyInfo> properties_from_class(const URK::il2cpp::Class* klass, bool includeInherited) {
    std::vector<PropertyInfo> out;
    for (const void* current = klass; current; current = includeInherited ? URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class*>(current)) : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void* it = nullptr;
        while (const auto* property = URK::il2cpp::class_get_properties(static_cast<const URK::il2cpp::Class*>(current), &it))
            out.push_back(property_info(property, declaring));
    }
    return out;
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = type.resolve_class();
    if (!klass) { detail::set_error("Unity Inspect::Properties failed: type lookup failure"); detail::append_backend_error(); return {}; }
    return properties_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void* klass = detail::Backend::object_get_class(object.handle());
    if (!klass) { detail::set_error("Unity Inspect::Properties failed: object_get_class failed"); detail::append_backend_error(); return {}; }
    return properties_from_class(static_cast<const URK::il2cpp::Class*>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo& object, bool includeInherited = true) {
    return object.handle ? Properties(Object{object.handle}, includeInherited) : std::vector<PropertyInfo>{};
}
inline ValueInfo unavailable_value(std::string typeName, std::string message) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.display = std::move(message);
    out.kind = ValueKind::Unavailable;
    out.readable = false;
    return out;
}
inline ValueInfo value_type_placeholder(std::string typeName) {
    ValueInfo out{};
    out.kind = ValueKind::ValueType;
    out.type_name = std::move(typeName);
    out.display = "<value type>";
    out.readable = false;
    return out;
}
inline ValueInfo enum_placeholder(std::string typeName) {
    ValueInfo out{};
    out.kind = ValueKind::Enum;
    out.type_name = std::move(typeName);
    out.display = "<enum>";
    out.readable = false;
    return out;
}
inline std::string enum_underlying_type_name(const void* type) {
    const void* klass = type ? URK::il2cpp::type_get_class_or_element_class(static_cast<const URK::il2cpp::Type*>(type)) : nullptr;
    if (!klass) return {};
    const void* underlying = URK::il2cpp::class_enum_basetype(static_cast<const URK::il2cpp::Class*>(klass));
    if (underlying) return type_name(underlying);
    void* it = nullptr;
    while (const auto* field = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class*>(klass), &it)) {
        const char* name = URK::il2cpp::field_get_name(field);
        if (name && std::string_view{name} == "value__") return type_name(URK::il2cpp::field_get_type(field));
    }
    return {};
}
inline ValueInfo scalar_from_pointer(std::string typeName, void* data);
inline ValueInfo enum_from_pointer(std::string typeName, std::string underlyingTypeName, void* data) {
    ValueInfo out = scalar_from_pointer(underlyingTypeName, data);
    if (!out.readable) return unavailable_value(std::move(typeName), std::string("enum underlying type is unsupported: ") + underlyingTypeName);
    out.kind = ValueKind::Enum;
    out.type_name = std::move(typeName);
    return out;
}
inline bool type_name_looks_array(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.array") return true;
    if (normalized.size() >= 2 && normalized.compare(normalized.size() - 2, 2, "[]") == 0) return true;
    const std::size_t open = normalized.rfind('[');
    return open != std::string::npos && !normalized.empty() && normalized.back() == ']' && open + 1 < normalized.size() && normalized[open + 1] == ',';
}
inline ValueInfo array_reference_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    out.kind = ValueKind::ArrayReference;
    if (detail::Backend::has_array_length()) {
        out.array_length = detail::Backend::array_length(object);
        out.display = "array[" + std::to_string(out.array_length) + "]";
    } else {
        out.display = "<array>";
    }
    return out;
}
inline ValueInfo object_reference_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    if (type_name_looks_array(out.type_name)) return array_reference_value(out.type_name, object);
    out.kind = ValueKind::ObjectReference;
    out.display = detail::class_display_name(detail::Backend::object_get_class(object));
    if (out.display.empty()) out.display = "<object>";
    return out;
}
inline ValueInfo string_value(std::string typeName, void* object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    out.kind = ValueKind::String;
    out.display = detail::managed_string_to_utf8(object);
    return out;
}
inline ValueInfo scalar_from_pointer(std::string typeName, void* data) {
    if (!data) return unavailable_value(std::move(typeName), "value data is null");
    const std::string normalized = detail::normalized_type_name(typeName);
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.readable = true;
    if (normalized == "system.boolean") {
        out.kind = ValueKind::Boolean;
        out.bool_value = *static_cast<bool*>(data);
        out.display = out.bool_value ? "true" : "false";
        return out;
    }
    if (normalized == "system.int16") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int16_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.int32") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int32_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.int64") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int64_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.sbyte") { out.kind = ValueKind::SignedInteger; out.signed_value = *static_cast<std::int8_t*>(data); out.display = std::to_string(out.signed_value); return out; }
    if (normalized == "system.uint16" || normalized == "system.char") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint16_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.uint32") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint32_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.uint64") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint64_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.byte") { out.kind = ValueKind::UnsignedInteger; out.unsigned_value = *static_cast<std::uint8_t*>(data); out.display = std::to_string(out.unsigned_value); return out; }
    if (normalized == "system.single") { out.kind = ValueKind::FloatingPoint; out.floating_value = *static_cast<float*>(data); char text[64]{}; std::snprintf(text, sizeof(text), "%.6g", out.floating_value); out.display = text; return out; }
    if (normalized == "system.double") { out.kind = ValueKind::FloatingPoint; out.floating_value = *static_cast<double*>(data); char text[64]{}; std::snprintf(text, sizeof(text), "%.12g", out.floating_value); out.display = text; return out; }
    return value_type_placeholder(out.type_name);
}
inline bool read_field_raw(Object object, const FieldInfo& field, void* output) {
    if (!field.handle) { detail::set_error("Unity Inspect::ReadField failed: field handle is null"); return false; }
    if (field.is_static) {
        if (!field.declaring_type.handle) { detail::set_error("Unity Inspect::ReadField failed: static field declaring type is unavailable"); return false; }
        if (!detail::Backend::field_static_get_value(field.declaring_type.handle, field.handle, output)) { detail::set_error(std::string("Unity Inspect::ReadField failed: static field read failed: ") + field.name); detail::append_backend_error(); return false; }
        return true;
    }
    if (!object.handle()) { detail::set_error("Unity Inspect::ReadField failed: target object is null"); return false; }
    if (!detail::Backend::field_get_value(object.handle(), field.handle, output)) { detail::set_error(std::string("Unity Inspect::ReadField failed: field read failed: ") + field.name); detail::append_backend_error(); return false; }
    return true;
}
struct WriteStorage {
    bool b{};
    std::int8_t i8{};
    std::int16_t i16{};
    std::int32_t i32{};
    std::int64_t i64{};
    std::uint8_t u8{};
    std::uint16_t u16{};
    std::uint32_t u32{};
    std::uint64_t u64{};
    float f32{};
    double f64{};
    void* reference{};
};
inline const char* value_kind_name(ValueKind kind) {
    switch (kind) {
    case ValueKind::Unavailable: return "Unavailable";
    case ValueKind::Null: return "Null";
    case ValueKind::Boolean: return "Boolean";
    case ValueKind::SignedInteger: return "SignedInteger";
    case ValueKind::UnsignedInteger: return "UnsignedInteger";
    case ValueKind::FloatingPoint: return "FloatingPoint";
    case ValueKind::String: return "String";
    case ValueKind::ObjectReference: return "ObjectReference";
    case ValueKind::ArrayReference: return "ArrayReference";
    case ValueKind::Enum: return "Enum";
    case ValueKind::ValueType: return "ValueType";
    }
    return "<unknown>";
}
template<class T> inline bool assign_integral_value(const ValueInfo& value, T& output, std::string_view targetType, std::string_view context) {
    if (value.kind == ValueKind::UnsignedInteger || (value.kind == ValueKind::Enum && std::is_unsigned_v<T>)) {
        const std::uint64_t unsignedValue = value.unsigned_value;
        if constexpr (std::is_signed_v<T>) {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(unsignedValue);
        return true;
    }
    if (value.kind == ValueKind::SignedInteger || value.kind == ValueKind::Enum) {
        const std::int64_t signedValue = value.signed_value;
        if constexpr (std::is_unsigned_v<T>) {
            if (signedValue < 0 || static_cast<std::uint64_t>(signedValue) > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (signedValue < static_cast<std::int64_t>(std::numeric_limits<T>::min()) || signedValue > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(signedValue);
        return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected integer value for " + std::string(targetType) + ", got " + value_kind_name(value.kind));
    return false;
}
inline bool scalar_write_pointer(std::string_view typeName, const ValueInfo& value, WriteStorage& storage, void*& pointer, std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") {
        if (value.kind != ValueKind::Boolean) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected Boolean for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.b = value.bool_value; pointer = &storage.b; return true;
    }
    if (normalized == "system.sbyte") { if (!assign_integral_value(value, storage.i8, typeName, context)) return false; pointer = &storage.i8; return true; }
    if (normalized == "system.int16") { if (!assign_integral_value(value, storage.i16, typeName, context)) return false; pointer = &storage.i16; return true; }
    if (normalized == "system.int32") { if (!assign_integral_value(value, storage.i32, typeName, context)) return false; pointer = &storage.i32; return true; }
    if (normalized == "system.int64") { if (!assign_integral_value(value, storage.i64, typeName, context)) return false; pointer = &storage.i64; return true; }
    if (normalized == "system.byte") { if (!assign_integral_value(value, storage.u8, typeName, context)) return false; pointer = &storage.u8; return true; }
    if (normalized == "system.uint16" || normalized == "system.char") { if (!assign_integral_value(value, storage.u16, typeName, context)) return false; pointer = &storage.u16; return true; }
    if (normalized == "system.uint32") { if (!assign_integral_value(value, storage.u32, typeName, context)) return false; pointer = &storage.u32; return true; }
    if (normalized == "system.uint64") { if (!assign_integral_value(value, storage.u64, typeName, context)) return false; pointer = &storage.u64; return true; }
    if (normalized == "system.single") {
        if (value.kind != ValueKind::FloatingPoint) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected FloatingPoint for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        if (value.floating_value > static_cast<double>(std::numeric_limits<float>::max()) || value.floating_value < -static_cast<double>(std::numeric_limits<float>::max())) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: floating point value is out of range for " + std::string(typeName)); return false; }
        storage.f32 = static_cast<float>(value.floating_value); pointer = &storage.f32; return true;
    }
    if (normalized == "system.double") {
        if (value.kind != ValueKind::FloatingPoint) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected FloatingPoint for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.f64 = value.floating_value; pointer = &storage.f64; return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: unsupported value type write: " + std::string(typeName));
    return false;
}
inline bool reference_write_value(std::string_view typeName, const ValueInfo& value, WriteStorage& storage, std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (value.kind == ValueKind::Null) { storage.reference = nullptr; return true; }
    if (normalized == "system.string") {
        if (value.kind != ValueKind::String) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected String or Null for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
        storage.reference = value.object ? value.object : detail::Backend::new_string(value.display);
        if (!storage.reference) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: managed string allocation failed"); detail::append_backend_error(); return false; }
        return true;
    }
    if (value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected ObjectReference, ArrayReference, or Null for " + std::string(typeName) + ", got " + value_kind_name(value.kind)); return false; }
    if (!value.object) { detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: reference value object is null but kind is " + value_kind_name(value.kind)); return false; }
    storage.reference = value.object;
    return true;
}
inline std::string array_element_type_name(std::string_view arrayTypeName) {
    if (arrayTypeName.size() >= 2 && arrayTypeName.substr(arrayTypeName.size() - 2) == "[]")
        return std::string(arrayTypeName.substr(0, arrayTypeName.size() - 2));
    return {};
}
inline int scalar_element_size(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean" || normalized == "system.byte" || normalized == "system.sbyte") return 1;
    if (normalized == "system.int16" || normalized == "system.uint16" || normalized == "system.char") return 2;
    if (normalized == "system.int32" || normalized == "system.uint32" || normalized == "system.single") return 4;
    if (normalized == "system.int64" || normalized == "system.uint64" || normalized == "system.double") return 8;
    return 0;
}
inline bool validate_array_element_access(const ValueInfo& array, std::size_t index, std::size_t& length) {
    if (array.kind != ValueKind::ArrayReference || !array.object) { detail::set_error("Unity Inspect array element access failed: value is not an array reference"); return false; }
    if (!detail::Backend::has_array_length()) { detail::set_error("Unity Inspect array element access failed: backend array_length API is unavailable"); detail::append_backend_error(); return false; }
    length = detail::Backend::array_length(array.object);
    if (index >= length) { detail::set_error("Unity Inspect array element access failed: index out of range"); return false; }
    return true;
}
inline ValueInfo ReadArrayElement(const ValueInfo& array, std::size_t index) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length)) return unavailable_value(array.type_name, detail::fallback_error() ? detail::fallback_error() : "array element access failed");
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty()) return unavailable_value(array.type_name, std::string("array element type unsupported or not single-dimensional: ") + array.type_name);
    const int elementSize = scalar_element_size(elementType);
    if (elementSize > 0) {
        void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
        if (!slot) { detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: array_addr_with_size failed for ") + elementType); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array element address failed"); }
        return scalar_from_pointer(elementType, slot);
    }
    if (!detail::Backend::has_array_ref_at()) { detail::set_error("Unity Inspect::ReadArrayElement failed: backend array_ref_at API is unavailable"); detail::append_backend_error(); return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error() : "array reference element access failed"); }
    void* ref = detail::Backend::array_ref_at(array.object, index);
    return detail::normalized_type_name(elementType) == "system.string" ? string_value(elementType, ref) : object_reference_value(elementType, ref);
}
inline bool SetArrayElement(const ValueInfo& array, std::size_t index, const ValueInfo& value) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length)) return false;
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty()) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: array element type unsupported or not single-dimensional: ") + array.type_name); return false; }
    WriteStorage storage{};
    const int elementSize = scalar_element_size(elementType);
    if (elementSize <= 0) {
        if (!detail::Backend::has_array_set_ref()) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: backend array_set_ref API is unavailable for ") + array.type_name); detail::append_backend_error(); return false; }
        if (!reference_write_value(elementType, value, storage, "SetArrayElement")) return false;
        if (!detail::Backend::array_set_ref(array.object, index, storage.reference)) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: reference array write failed for ") + array.type_name); detail::append_backend_error(); return false; }
        return true;
    }
    void* source = nullptr;
    if (!scalar_write_pointer(elementType, value, storage, source, "SetArrayElement")) return false;
    void* slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
    if (!slot) { detail::set_error(std::string("Unity Inspect::SetArrayElement failed: array_addr_with_size failed for ") + elementType); detail::append_backend_error(); return false; }
    std::memcpy(slot, source, static_cast<std::size_t>(elementSize));
    return true;
}
inline bool method_argument_pointer(const MethodParamInfo& parameter, const ValueInfo& value, WriteStorage& storage, void*& pointer) {
    const TypeInfo type = DescribeType(parameter.type);
    if (!type.handle && parameter.type_name.empty()) { detail::set_error("Unity Inspect::InvokeMethod failed: parameter type metadata is unavailable"); return false; }
    const std::string normalized = detail::normalized_type_name(parameter.type_name);
    if (type.is_enum) {
        const std::string underlying = enum_underlying_type_name(parameter.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: enum parameter underlying type unavailable: ") + parameter.name); return false; }
        return scalar_write_pointer(underlying, value, storage, pointer, "InvokeMethod");
    }
    if (normalized == "system.string" || !type.is_value_type)
        return reference_write_value(parameter.type_name, value, storage, "InvokeMethod") ? (pointer = storage.reference, true) : false;
    return scalar_write_pointer(parameter.type_name, value, storage, pointer, "InvokeMethod");
}
inline ValueInfo void_value() {
    ValueInfo out{};
    out.kind = ValueKind::Null;
    out.type_name = "System.Void";
    out.display = "void";
    out.readable = true;
    return out;
}
inline ValueInfo invoke_result_value(std::string typeName, const void* type, void* result, std::string_view methodName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.void" || typeName.empty()) return void_value();
    if (normalized == "system.string") return string_value(std::move(typeName), result);
    const TypeInfo resultType = DescribeType(type);
    if (!resultType.is_value_type) return object_reference_value(std::move(typeName), result);
    if (!result) return unavailable_value(std::move(typeName), std::string("Unity Inspect::InvokeMethod failed: value-type result is null: ") + std::string(methodName));
    void* raw = detail::Backend::object_unbox(result);
    if (!raw) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: object_unbox failed for result: ") + std::string(methodName)); detail::append_backend_error(); return unavailable_value(std::move(typeName), detail::fallback_error() ? detail::fallback_error() : "method result unbox failed"); }
    if (resultType.is_enum) {
        const std::string underlying = enum_underlying_type_name(type);
        if (underlying.empty()) return unavailable_value(std::move(typeName), std::string("enum result underlying type unavailable: ") + std::string(methodName));
        return enum_from_pointer(std::move(typeName), underlying, raw);
    }
    return scalar_from_pointer(std::move(typeName), raw);
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo& method, const std::vector<ValueInfo>& arguments = {}) {
    detail::clear_error();
    if (!method.handle) return unavailable_value(method.return_type, "Unity Inspect::InvokeMethod failed: method handle is null");
    if (arguments.size() != method.parameters.size()) return unavailable_value(method.return_type, std::string("Unity Inspect::InvokeMethod failed: argument count mismatch for ") + method.name);
    if (!method.is_static && !object.handle()) return unavailable_value(method.return_type, std::string("Unity Inspect::InvokeMethod failed: target object is null for instance method: ") + method.name);
    std::vector<WriteStorage> storage(arguments.size());
    std::vector<void*> argv(arguments.size(), nullptr);
    for (std::size_t i = 0; i < arguments.size(); ++i)
        if (!method_argument_pointer(method.parameters[i], arguments[i], storage[i], argv[i]))
            return unavailable_value(method.return_type, detail::fallback_error() ? detail::fallback_error() : "method argument conversion failed");
    void* result = nullptr;
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(method.handle, method.is_static ? nullptr : object.handle(), argv.empty() ? nullptr : argv.data(), &result, &ex) || ex) { detail::set_error(std::string("Unity Inspect::InvokeMethod failed: runtime_invoke threw or failed: ") + method.name); detail::append_backend_error(); return unavailable_value(method.return_type, detail::fallback_error() ? detail::fallback_error() : "method invocation failed"); }
    return invoke_result_value(method.return_type, method.return_type_handle, result, method.name);
}
inline bool write_field_raw(Object object, const FieldInfo& field, void* value) {
    if (!field.handle) { detail::set_error("Unity Inspect::SetField failed: field handle is null"); return false; }
    if (field.is_static) {
        if (!field.declaring_type.handle) { detail::set_error("Unity Inspect::SetField failed: static field declaring type is unavailable"); return false; }
        if (!detail::Backend::field_static_set_value(field.declaring_type.handle, field.handle, value)) { detail::set_error(std::string("Unity Inspect::SetField failed: static field write failed: ") + field.name); detail::append_backend_error(); return false; }
        return true;
    }
    if (!object.handle()) { detail::set_error("Unity Inspect::SetField failed: target object is null"); return false; }
    if (!detail::Backend::field_set_value(object.handle(), field.handle, value)) { detail::set_error(std::string("Unity Inspect::SetField failed: field write failed: ") + field.name); detail::append_backend_error(); return false; }
    return true;
}
inline bool read_field_scalar_pointer(Object object, const FieldInfo& field, std::string_view typeName, WriteStorage& storage, void*& pointer) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") { pointer = &storage.b; return read_field_raw(object, field, pointer); }
    if (normalized == "system.sbyte") { pointer = &storage.i8; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int16") { pointer = &storage.i16; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int32") { pointer = &storage.i32; return read_field_raw(object, field, pointer); }
    if (normalized == "system.int64") { pointer = &storage.i64; return read_field_raw(object, field, pointer); }
    if (normalized == "system.byte") { pointer = &storage.u8; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint16" || normalized == "system.char") { pointer = &storage.u16; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint32") { pointer = &storage.u32; return read_field_raw(object, field, pointer); }
    if (normalized == "system.uint64") { pointer = &storage.u64; return read_field_raw(object, field, pointer); }
    detail::set_error(std::string("Unity Inspect::ReadField failed: unsupported scalar field type: ") + std::string(typeName));
    return false;
}
inline ValueInfo ReadField(Object object, const FieldInfo& field) {
    detail::clear_error();
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (normalized == "system.string") { void* ref = nullptr; return read_field_raw(object, field, &ref) ? string_value(field.type_name, ref) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (!field.is_value_type) { void* ref = nullptr; return read_field_raw(object, field, &ref) ? object_reference_value(field.type_name, ref) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty()) return unavailable_value(field.type_name, std::string("enum underlying type unavailable: ") + field.name);
        WriteStorage storage{};
        void* raw = nullptr;
        return read_field_scalar_pointer(object, field, underlying, storage, raw) ? enum_from_pointer(field.type_name, underlying, raw) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "enum field read failed");
    }
    if (normalized == "system.boolean") { bool value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int16") { std::int16_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int32") { std::int32_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.int64") { std::int64_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.sbyte") { std::int8_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint16" || normalized == "system.char") { std::uint16_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint32") { std::uint32_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.uint64") { std::uint64_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.byte") { std::uint8_t value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.single") { float value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    if (normalized == "system.double") { double value{}; return read_field_raw(object, field, &value) ? scalar_from_pointer(field.type_name, &value) : unavailable_value(field.type_name, detail::fallback_error() ? detail::fallback_error() : "field read failed"); }
    return value_type_placeholder(field.type_name);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo& property) {
    detail::clear_error();
    if (!property.can_read || !property.get_method) return unavailable_value(property.type_name, std::string("property is not readable: ") + property.name);
    void* result = nullptr;
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.get_method, object.handle(), nullptr, &result, &ex) || ex) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: getter threw or could not be invoked: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "property read failed"); }
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (normalized == "system.string") return string_value(property.type_name, result);
    if (!property.is_value_type) return object_reference_value(property.type_name, result);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty()) return unavailable_value(property.type_name, std::string("enum underlying type unavailable: ") + property.name);
        void* raw = detail::Backend::object_unbox(result);
        if (!raw) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: enum object_unbox failed: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "enum property unbox failed"); }
        return enum_from_pointer(property.type_name, underlying, raw);
    }
    void* raw = detail::Backend::object_unbox(result);
    if (!raw) { detail::set_error(std::string("Unity Inspect::ReadProperty failed: object_unbox failed: ") + property.name); detail::append_backend_error(); return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error() : "property unbox failed"); }
    return scalar_from_pointer(property.type_name, raw);
}
inline bool SetField(Object object, const FieldInfo& field, const ValueInfo& value) {
    detail::clear_error();
    if (!field.handle) { detail::set_error("Unity Inspect::SetField failed: field handle is null"); return false; }
    WriteStorage storage{};
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::SetField failed: enum underlying type unavailable: ") + field.type_name); return false; }
        void* pointer = nullptr;
        if (!scalar_write_pointer(underlying, value, storage, pointer, "SetField")) return false;
        return write_field_raw(object, field, pointer);
    }
    if (normalized == "system.string" || !field.is_value_type) {
        if (!reference_write_value(field.type_name, value, storage, "SetField")) return false;
        return write_field_raw(object, field,
                               detail::Backend::field_reference_write_pointer(storage.reference));
    }
    void* pointer = nullptr;
    if (!scalar_write_pointer(field.type_name, value, storage, pointer, "SetField")) return false;
    return write_field_raw(object, field, pointer);
}
inline bool SetProperty(Object object, const PropertyInfo& property, const ValueInfo& value) {
    detail::clear_error();
    if (!property.can_write || !property.set_method) { detail::set_error(std::string("Unity Inspect::SetProperty failed: property is not writable: ") + property.name); return false; }
    WriteStorage storage{};
    void* arg = nullptr;
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty()) { detail::set_error(std::string("Unity Inspect::SetProperty failed: enum underlying type unavailable: ") + property.type_name); return false; }
        if (!scalar_write_pointer(underlying, value, storage, arg, "SetProperty")) return false;
    } else
    if (normalized == "system.string" || !property.is_value_type) {
        if (!reference_write_value(property.type_name, value, storage, "SetProperty")) return false;
        arg = storage.reference;
    } else if (!scalar_write_pointer(property.type_name, value, storage, arg, "SetProperty")) {
        return false;
    }
    void* args[] = {arg};
    void* ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.set_method, object.handle(), args, nullptr, &ex) || ex) { detail::set_error(std::string("Unity Inspect::SetProperty failed: setter threw or could not be invoked: ") + property.name); detail::append_backend_error(); return false; }
    return true;
}
inline void DumpFields(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& f : Fields(type)) { char line[512]{}; std::snprintf(line, sizeof(line), "field %s : %s flags=0x%08x", f.name.c_str(), f.type_name.c_str(), f.flags); emit(sink, line); } }
inline void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& m : Methods(type)) { std::string line = "method " + m.name + "("; for (std::size_t i = 0; i < m.parameters.size(); ++i) { if (i) line += ", "; line += m.parameters[i].type_name.empty() ? "<unavailable>" : m.parameters[i].type_name; } line += ") -> "; line += m.return_type.empty() ? "<unavailable>" : m.return_type; emit(sink, line.c_str()); } }
inline void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr) { for (const auto& p : Properties(type)) { char line[512]{}; std::snprintf(line, sizeof(line), "property %s : %s flags=0x%08x", p.name.c_str(), p.type_name.c_str(), p.flags); emit(sink, line); } }
}

}
namespace Unity {
// URK_UNITY_INSPECT_ALIASES_BEGIN
namespace Inspect {
using FieldInfo = URK::Unity::Inspect::FieldInfo;
using MethodInfo = URK::Unity::Inspect::MethodInfo;
using MethodParamInfo = URK::Unity::Inspect::MethodParamInfo;
using ObjectRefInfo = URK::Unity::Inspect::ObjectRefInfo;
using ObjectHandle = URK::Unity::Inspect::ObjectHandle;
using PropertyInfo = URK::Unity::Inspect::PropertyInfo;
using TypeInfo = URK::Unity::Inspect::TypeInfo;
using ValueInfo = URK::Unity::Inspect::ValueInfo;
using ValueKind = URK::Unity::Inspect::ValueKind;
inline TypeInfo DescribeClass(const void* klass) {
    return URK::Unity::Inspect::DescribeClass(klass);
}
inline TypeInfo TypeOf(Object object) {
    return URK::Unity::Inspect::TypeOf(object);
}
inline ObjectRefInfo DescribeObject(Object object) {
    return URK::Unity::Inspect::DescribeObject(object);
}
inline ObjectRefInfo ExpandValue(const ValueInfo& value) {
    return URK::Unity::Inspect::ExpandValue(value);
}
inline ObjectHandle PinObject(Object object, bool pinned = false) {
    return URK::Unity::Inspect::PinObject(object, pinned);
}
inline ObjectHandle PinValue(const ValueInfo& value, bool pinned = false) {
    return URK::Unity::Inspect::PinValue(value, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    return URK::Unity::Inspect::WeakObject(object, trackResurrection);
}
inline Object ResolveObjectHandle(const ObjectHandle& handle) {
    return URK::Unity::Inspect::ResolveObjectHandle(handle);
}
inline void FreeObjectHandle(ObjectHandle& handle) {
    URK::Unity::Inspect::FreeObjectHandle(handle);
}
inline std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(type, includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(type, includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(type, includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo& object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline ValueInfo ReadField(Object object, const FieldInfo& field) {
    return URK::Unity::Inspect::ReadField(object, field);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo& property) {
    return URK::Unity::Inspect::ReadProperty(object, property);
}
inline ValueInfo ReadArrayElement(const ValueInfo& array, std::size_t index) {
    return URK::Unity::Inspect::ReadArrayElement(array, index);
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo& method, const std::vector<ValueInfo>& arguments = {}) {
    return URK::Unity::Inspect::InvokeMethod(object, method, arguments);
}
inline bool SetField(Object object, const FieldInfo& field, const ValueInfo& value) {
    return URK::Unity::Inspect::SetField(object, field, value);
}
inline bool SetProperty(Object object, const PropertyInfo& property, const ValueInfo& value) {
    return URK::Unity::Inspect::SetProperty(object, property, value);
}
inline bool SetArrayElement(const ValueInfo& array, std::size_t index, const ValueInfo& value) {
    return URK::Unity::Inspect::SetArrayElement(array, index, value);
}
inline void DumpFields(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpFields(type, sink);
}
inline void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpMethods(type, sink);
}
inline void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr) {
    URK::Unity::Inspect::DumpProperties(type, sink);
}
}
}
