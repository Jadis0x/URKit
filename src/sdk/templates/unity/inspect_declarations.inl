    out << R"URKUNITY(// URK_UNITY_INSPECT_BEGIN
namespace Inspect {
struct TypeInfo {
    const void *handle = nullptr;
    std::string namespc;
    std::string name;
    std::string full_name;
    std::uint32_t flags = 0;
    bool is_value_type = false;
    bool is_enum = false;
};
struct FieldInfo {
    const void *handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void *type = nullptr;
    std::string type_name;
    std::uint32_t flags = 0;
    bool is_static = false;
    bool is_value_type = false;
    bool is_enum = false;
};
struct MethodParamInfo {
    const void *type = nullptr;
    std::string name;
    std::string type_name;
};
struct MethodInfo {
    const void *handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void *return_type_handle = nullptr;
    std::string return_type;
    std::vector<MethodParamInfo> parameters;
    std::uint32_t flags = 0;
    std::uint32_t iflags = 0;
    bool is_static = false;
};
struct PropertyInfo {
    const void *handle = nullptr;
    TypeInfo declaring_type;
    std::string name;
    const void *type = nullptr;
    std::string type_name;
    const void *get_method = nullptr;
    const void *set_method = nullptr;
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
    void *object = nullptr;
    bool bool_value = false;
    std::int64_t signed_value = 0;
    std::uint64_t unsigned_value = 0;
    double floating_value = 0.0;
    std::size_t array_length = 0;
    bool readable = false;
};
struct ObjectRefInfo {
    void *handle = nullptr;
    TypeInfo type;
    std::string display;
    bool is_null = true;
    bool expandable = false;
};
struct ObjectHandle {
    std::uintptr_t handle = 0;
    bool weak = false;
    bool pinned = false;
};
TypeInfo DescribeClass(const void *klass);
TypeInfo TypeOf(Object object);
ObjectRefInfo DescribeObject(Object object);
ObjectRefInfo ExpandValue(const ValueInfo &value);
ObjectHandle PinObject(Object object, bool pinned = false);
ObjectHandle PinValue(const ValueInfo &value, bool pinned = false);
ObjectHandle WeakObject(Object object, bool trackResurrection = false);
Object ResolveObjectHandle(const ObjectHandle &handle);
void FreeObjectHandle(ObjectHandle &handle);
std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true);
std::vector<FieldInfo> Fields(Object object, bool includeInherited = true);
std::vector<FieldInfo> Fields(const ObjectRefInfo &object, bool includeInherited = true);
std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true);
std::vector<MethodInfo> Methods(Object object, bool includeInherited = true);
std::vector<MethodInfo> Methods(const ObjectRefInfo &object, bool includeInherited = true);
std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true);
std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true);
std::vector<PropertyInfo> Properties(const ObjectRefInfo &object, bool includeInherited = true);
ValueInfo ReadField(Object object, const FieldInfo &field);
ValueInfo ReadProperty(Object object, const PropertyInfo &property);
ValueInfo ReadArrayElement(const ValueInfo &array, std::size_t index);
ValueInfo InvokeMethod(Object object, const MethodInfo &method, const std::vector<ValueInfo> &arguments = {});
bool SetField(Object object, const FieldInfo &field, const ValueInfo &value);
bool SetProperty(Object object, const PropertyInfo &property, const ValueInfo &value);
bool SetArrayElement(const ValueInfo &array, std::size_t index, const ValueInfo &value);
void DumpFields(TypeRef type, DiagnosticSink sink = nullptr);
void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr);
void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr);
} // namespace Inspect
}

)URKUNITY";

