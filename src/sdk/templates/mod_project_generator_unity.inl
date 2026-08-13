// Internal Unity SDK templates. Included by mod_project_generator_common.cpp.

std::string UnityCoreModuleFull(const ModuleProjectOptions &options) {
    const bool mono = options.backendNamespace == "URK::mono";
    const char *backendImport =
        mono ? "#include \"../mono/mono_runtime.h\"\n" : "#include \"../il2cpp/il2cpp_runtime.h\"\n";
    const char *backendNs = mono ? "mono" : "il2cpp";
    const char *methodObjectHelper =
        mono ? "    static void* method_get_object(const void* method, const void*) { return "
               "URK::mono::method_get_object(static_cast<const URK::mono::Method*>(method), nullptr); }\n"
             : "    static void* method_get_object(const void* method, const void* refClass) { return "
               "URK::il2cpp::method_get_object(static_cast<const URK::il2cpp::Method*>(method), static_cast<const "
               "URK::il2cpp::Class*>(refClass)); }\n";
    const char *valueBoxHelper =
        mono ? "    static void* value_box(const void* klass, void* data) { return "
               "URK::mono::value_box(static_cast<const URK::mono::Class*>(klass), data); }\n"
             : "    static void* value_box(const void* klass, void* data) { return "
               "URK::il2cpp::value_box(static_cast<const URK::il2cpp::Class*>(klass), data); }\n";
    const char *methodParameterCountHelper =
        mono ? "    static std::size_t method_get_param_count(const void* method) { const auto* signature = "
               "URK::mono::method_signature(static_cast<const URK::mono::Method*>(method)); return signature ? "
               "URK::mono::signature_get_param_count(signature) : 0; }\n"
             : "    static std::size_t method_get_param_count(const void* method) { return method ? "
               "URK::il2cpp::method_get_param_count(static_cast<const URK::il2cpp::Method*>(method)) : 0; }\n";
    std::ostringstream out;
    out << R"URKUNITY(#pragma once

#include "../runtime_api.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <tuple>
#include <type_traits>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>
)URKUNITY"
        << backendImport << R"URKUNITY(

// URK_UNITY_NAMESPACE_BEGIN
namespace URK::Unity {
struct Object;
struct TypeObject;
struct Component;
struct Behaviour;
struct MonoBehaviour;
struct GameObject;
struct Scene;
struct ScriptableObject;
struct Transform;
struct Camera;
struct Light;
struct Renderer;
struct SkinnedMeshRenderer;
struct Collider;
struct RectTransform;
struct Rigidbody;
struct Rigidbody2D;
struct AudioSource;
struct Animator;
struct Canvas;
struct CanvasRenderer;
struct CanvasGroup;
struct CanvasScaler;
struct Graphic;
struct GraphicRaycaster;
struct Selectable;
struct Image;
struct RawImage;
struct Text;
struct TextMeshProUGUI;
struct TmpInputField;
struct TmpDropdown;
struct Button;
struct Toggle;
struct Slider;
struct Scrollbar;
struct Dropdown;
struct InputField;
struct Mask;
struct RectMask2D;
struct ScrollRect;
struct LayoutElement;
struct HorizontalLayoutGroup;
struct VerticalLayoutGroup;
struct GridLayoutGroup;
struct ContentSizeFitter;
struct AspectRatioFitter;
struct EventSystem;
struct BaseInputModule;
struct StandaloneInputModule;
struct InputSystemUIInputModule;
struct MeshRenderer;
struct MeshFilter;
struct MeshCollider;
struct Mesh;
struct Material;
struct Texture;
struct Texture2D;
struct Shader;
struct Sprite;
struct AssetBundle;

struct Vector2 {
    float x{};
    float y{};
    constexpr Vector2() = default;
    constexpr Vector2(float x_, float y_) : x(x_), y(y_) {
    }
    constexpr Vector2 operator-() const {
        return {-x, -y};
    }
    constexpr Vector2 operator+(Vector2 rhs) const {
        return {x + rhs.x, y + rhs.y};
    }
    constexpr Vector2 operator-(Vector2 rhs) const {
        return {x - rhs.x, y - rhs.y};
    }
    constexpr Vector2 operator*(float s) const {
        return {x * s, y * s};
    }
    constexpr Vector2 operator/(float s) const {
        return s != 0.0f ? Vector2{x / s, y / s} : Vector2{};
    }
    Vector2 &operator+=(Vector2 rhs) {
        x += rhs.x;
        y += rhs.y;
        return *this;
    }
    Vector2 &operator-=(Vector2 rhs) {
        x -= rhs.x;
        y -= rhs.y;
        return *this;
    }
    Vector2 &operator*=(float s) {
        x *= s;
        y *= s;
        return *this;
    }
    Vector2 &operator/=(float s) {
        if (s != 0.0f) {
            x /= s;
            y /= s;
        } else {
            x = 0.0f;
            y = 0.0f;
        }
        return *this;
    }
    float sqr_magnitude() const {
        return x * x + y * y;
    }
    float magnitude() const {
        return std::sqrt(sqr_magnitude());
    }
    Vector2 normalized() const {
        const float m = magnitude();
        return m > 0.000001f ? (*this / m) : Vector2{};
    }
    Vector2 &normalize() {
        const float m = magnitude();
        if (m > 0.000001f) {
            x /= m;
            y /= m;
        } else {
            x = 0.0f;
            y = 0.0f;
        }
        return *this;
    }
    bool nearly_zero(float epsilon = 0.000001f) const {
        return sqr_magnitude() <= epsilon * epsilon;
    }
    static Vector2 normalize(Vector2 value) {
        return value.normalized();
    }
    static float dot(Vector2 a, Vector2 b) {
        return a.x * b.x + a.y * b.y;
    }
    static float distance(Vector2 a, Vector2 b) {
        return (a - b).magnitude();
    }
};
struct Vector3 {
    float x{};
    float y{};
    float z{};
    constexpr Vector3() = default;
    constexpr Vector3(float x_, float y_, float z_) : x(x_), y(y_), z(z_) {
    }
    constexpr Vector3 operator-() const {
        return {-x, -y, -z};
    }
    constexpr Vector3 operator+(Vector3 rhs) const {
        return {x + rhs.x, y + rhs.y, z + rhs.z};
    }
    constexpr Vector3 operator-(Vector3 rhs) const {
        return {x - rhs.x, y - rhs.y, z - rhs.z};
    }
    constexpr Vector3 operator*(float s) const {
        return {x * s, y * s, z * s};
    }
    constexpr Vector3 operator/(float s) const {
        return s != 0.0f ? Vector3{x / s, y / s, z / s} : Vector3{};
    }
    Vector3 &operator+=(Vector3 rhs) {
        x += rhs.x;
        y += rhs.y;
        z += rhs.z;
        return *this;
    }
    Vector3 &operator-=(Vector3 rhs) {
        x -= rhs.x;
        y -= rhs.y;
        z -= rhs.z;
        return *this;
    }
    Vector3 &operator*=(float s) {
        x *= s;
        y *= s;
        z *= s;
        return *this;
    }
    Vector3 &operator/=(float s) {
        if (s != 0.0f) {
            x /= s;
            y /= s;
            z /= s;
        } else {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
        }
        return *this;
    }
    float sqr_magnitude() const {
        return x * x + y * y + z * z;
    }
    float magnitude() const {
        return std::sqrt(sqr_magnitude());
    }
    Vector3 normalized() const {
        const float m = magnitude();
        return m > 0.000001f ? (*this / m) : Vector3{};
    }
    Vector3 &normalize() {
        const float m = magnitude();
        if (m > 0.000001f) {
            x /= m;
            y /= m;
            z /= m;
        } else {
            x = 0.0f;
            y = 0.0f;
            z = 0.0f;
        }
        return *this;
    }
    bool nearly_zero(float epsilon = 0.000001f) const {
        return sqr_magnitude() <= epsilon * epsilon;
    }
    static Vector3 normalize(Vector3 value) {
        return value.normalized();
    }
    static float dot(Vector3 a, Vector3 b) {
        return a.x * b.x + a.y * b.y + a.z * b.z;
    }
    static Vector3 cross(Vector3 a, Vector3 b) {
        return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
    }
    static float distance(Vector3 a, Vector3 b) {
        return (a - b).magnitude();
    }
};
struct Quaternion {
    float x{};
    float y{};
    float z{};
    float w{};
};
struct Vector4 {
    float x{};
    float y{};
    float z{};
    float w{};
};
struct Color {
    float r{};
    float g{};
    float b{};
    float a{1.0f};
};
struct Color32 {
    std::uint8_t r{};
    std::uint8_t g{};
    std::uint8_t b{};
    std::uint8_t a{255};
};
struct Vector2Int {
    int x{};
    int y{};
};
struct Vector3Int {
    int x{};
    int y{};
    int z{};
};
struct Rect {
    float x{};
    float y{};
    float width{};
    float height{};
};
struct Bounds {
    Vector3 center{};
    Vector3 extents{};
    Vector3 size() const {
        return extents * 2.0f;
    }
    Vector3 min() const {
        return center - extents;
    }
    Vector3 max() const {
        return center + extents;
    }
};
struct Ray {
    Vector3 origin{};
    Vector3 direction{};
};
struct ProjectionResult {
    bool valid = false;
    bool in_front = false;
    bool on_screen = false;
    Vector2 screen{};
    Vector2 clamped_screen{};
    Vector2 screen_center{};
    Vector2 direction{};
    Vector3 world{};
    Vector3 screen3{};
    Vector3 viewport{};
    float depth = 0.0f;
    float distance = 0.0f;
    float facing = 0.0f;
};
enum class FindObjectsSortMode : int {
    None = 0,
    InstanceID = 1
};
enum class ObjectFilterFlags : std::uint32_t {
    None = 0,
    IncludeInactive = 1u << 0,
    IncludeHidden = 1u << 1,
    IncludeDontDestroyOnLoad = 1u << 2
};
enum class MouseButton : int {
    Left = 0,
    Right = 1,
    Middle = 2
};
enum class ShadowCastingMode : int {
    Off = 0,
    On = 1,
    TwoSided = 2,
    ShadowsOnly = 3
};
enum class MotionVectorGenerationMode : int {
    Camera = 0,
    Object = 1,
    ForceNoMotion = 2
};
enum class LightProbeUsage : int {
    Off = 0,
    BlendProbes = 1,
    UseProxyVolume = 2,
    CustomProvided = 3
};
enum class ReflectionProbeUsage : int {
    Off = 0,
    BlendProbes = 1,
    BlendProbesAndSkybox = 2,
    Simple = 3
};
enum class SkinQuality : int {
    Auto = 0,
    Bone1 = 1,
    Bone2 = 2,
    Bone4 = 4
};
enum class AnimatorCullingMode : int {
    AlwaysAnimate = 0,
    CullUpdateTransforms = 1,
    CullCompletely = 2
};
enum class AnimatorUpdateMode : int {
    Normal = 0,
    AnimatePhysics = 1,
    UnscaledTime = 2
};
enum class LightType : int {
    Spot = 0,
    Directional = 1,
    Point = 2,
    Area = 3,
    Rectangle = 3,
    Disc = 4
};
enum class LightShadows : int {
    None = 0,
    Hard = 1,
    Soft = 2
};
enum class LightRenderMode : int {
    Auto = 0,
    Important = 1,
    NotImportant = 2
};
enum class LightShadowResolution : int {
    FromQualitySettings = -1,
    Low = 0,
    Medium = 1,
    High = 2,
    VeryHigh = 3
};
enum class FontStyle : int {
    Normal = 0,
    Bold = 1,
    Italic = 2,
    BoldAndItalic = 3
};
enum class TextAnchor : int {
    UpperLeft = 0,
    UpperCenter = 1,
    UpperRight = 2,
    MiddleLeft = 3,
    MiddleCenter = 4,
    MiddleRight = 5,
    LowerLeft = 6,
    LowerCenter = 7,
    LowerRight = 8
};
enum class ImageType : int {
    Simple = 0,
    Sliced = 1,
    Tiled = 2,
    Filled = 3
};
enum class ImageFillMethod : int {
    Horizontal = 0,
    Vertical = 1,
    Radial90 = 2,
    Radial180 = 3,
    Radial360 = 4
};
enum class ButtonTransition : int {
    None = 0,
    ColorTint = 1,
    SpriteSwap = 2,
    Animation = 3
};
enum class SelectableTransition : int {
    None = 0,
    ColorTint = 1,
    SpriteSwap = 2,
    Animation = 3
};
enum class CanvasRenderMode : int {
    ScreenSpaceOverlay = 0,
    ScreenSpaceCamera = 1,
    WorldSpace = 2
};
enum class RectTransformAxis : int {
    Horizontal = 0,
    Vertical = 1
};
enum class RectTransformEdge : int {
    Left = 0,
    Right = 1,
    Top = 2,
    Bottom = 3
};
enum class ContentSizeFitterFitMode : int {
    Unconstrained = 0,
    MinSize = 1,
    PreferredSize = 2
};
enum class AspectRatioFitterMode : int {
    None = 0,
    WidthControlsHeight = 1,
    HeightControlsWidth = 2,
    FitInParent = 3,
    EnvelopeParent = 4
};
enum class GraphicRaycasterBlockingObjects : int {
    None = 0,
    TwoD = 1,
    ThreeD = 2,
    All = 3
};
enum class SliderDirection : int {
    LeftToRight = 0,
    RightToLeft = 1,
    BottomToTop = 2,
    TopToBottom = 3
};
enum class ScrollbarDirection : int {
    LeftToRight = 0,
    RightToLeft = 1,
    BottomToTop = 2,
    TopToBottom = 3
};
enum class InputFieldContentType : int {
    Standard = 0,
    Autocorrected = 1,
    IntegerNumber = 2,
    DecimalNumber = 3,
    Alphanumeric = 4,
    Name = 5,
    EmailAddress = 6,
    Password = 7,
    Pin = 8,
    Custom = 9
};
enum class InputFieldLineType : int {
    SingleLine = 0,
    MultiLineSubmit = 1,
    MultiLineNewline = 2
};
enum class CanvasScaleMode : int {
    ConstantPixelSize = 0,
    ScaleWithScreenSize = 1,
    ConstantPhysicalSize = 2
};
enum class CanvasScreenMatchMode : int {
    MatchWidthOrHeight = 0,
    Expand = 1,
    Shrink = 2
};
enum class ScrollRectMovementType : int {
    Unrestricted = 0,
    Elastic = 1,
    Clamped = 2
};
enum class GridLayoutConstraint : int {
    Flexible = 0,
    FixedColumnCount = 1,
    FixedRowCount = 2
};
enum class GridLayoutAxis : int {
    Horizontal = 0,
    Vertical = 1
};
enum class GridLayoutCorner : int {
    UpperLeft = 0,
    UpperRight = 1,
    LowerLeft = 2,
    LowerRight = 3
};
enum class TmpFontStyles : int {
    Normal = 0,
    Bold = 1,
    Italic = 2,
    Underline = 4,
    LowerCase = 8,
    UpperCase = 16,
    SmallCaps = 32,
    Strikethrough = 64,
    Superscript = 128,
    Subscript = 256,
    Highlight = 512
};
enum class TmpInputFieldContentType : int {
    Standard = 0,
    Autocorrected = 1,
    IntegerNumber = 2,
    DecimalNumber = 3,
    Alphanumeric = 4,
    Name = 5,
    EmailAddress = 6,
    Password = 7,
    Pin = 8,
    Custom = 9
};
enum class TmpInputFieldLineType : int {
    SingleLine = 0,
    MultiLineSubmit = 1,
    MultiLineNewline = 2
};
enum class KeyCode : int {
    None = 0,
    Backspace = 8,
    Tab = 9,
    Return = 13,
    Escape = 27,
    Space = 32,
    Alpha0 = 48,
    Alpha1 = 49,
    Alpha2 = 50,
    Alpha3 = 51,
    Alpha4 = 52,
    Alpha5 = 53,
    Alpha6 = 54,
    Alpha7 = 55,
    Alpha8 = 56,
    Alpha9 = 57,
    A = 97,
    B = 98,
    C = 99,
    D = 100,
    E = 101,
    F = 102,
    G = 103,
    H = 104,
    I = 105,
    J = 106,
    K = 107,
    L = 108,
    M = 109,
    N = 110,
    O = 111,
    P = 112,
    Q = 113,
    R = 114,
    S = 115,
    T = 116,
    U = 117,
    V = 118,
    W = 119,
    X = 120,
    Y = 121,
    Z = 122,
    Delete = 127,
    UpArrow = 273,
    DownArrow = 274,
    RightArrow = 275,
    LeftArrow = 276,
    Insert = 277,
    Home = 278,
    End = 279,
    PageUp = 280,
    PageDown = 281,
    F1 = 282,
    F2 = 283,
    F3 = 284,
    F4 = 285,
    F5 = 286,
    F6 = 287,
    F7 = 288,
    F8 = 289,
    F9 = 290,
    F10 = 291,
    F11 = 292,
    F12 = 293,
    LeftShift = 304,
    RightShift = 303,
    LeftControl = 306,
    RightControl = 305,
    LeftAlt = 308,
    RightAlt = 307,
    Mouse0 = 323,
    Mouse1 = 324,
    Mouse2 = 325,
    Mouse3 = 326,
    Mouse4 = 327,
    Mouse5 = 328,
    Mouse6 = 329
};
using DiagnosticSink = void (*)(const char *);

namespace detail {
inline std::string &error_slot() {
    static std::string value;
    return value;
}
inline std::mutex &cache_mutex() {
    static std::mutex value;
    return value;
}
inline std::unordered_map<std::string, const void *> &class_cache() {
    static std::unordered_map<std::string, const void *> value;
    return value;
}
inline std::unordered_map<std::string, void *> &type_cache() {
    static std::unordered_map<std::string, void *> value;
    return value;
}
inline std::unordered_map<std::string, const void *> &method_cache() {
    static std::unordered_map<std::string, const void *> value;
    return value;
}
inline std::unordered_map<std::string, const void *> &field_cache() {
    static std::unordered_map<std::string, const void *> value;
    return value;
}
inline void clear_error() {
    error_slot().clear();
}
inline void set_error(std::string_view text) {
    error_slot() = std::string(text);
}
inline const char *fallback_error() {
    return error_slot().empty() ? nullptr : error_slot().c_str();
}
inline std::string z(std::string_view v) {
    return std::string(v);
}
inline std::string signature_text(std::string_view methodName, const std::vector<const char *> &parameterTypeNames) {
    std::string s(methodName);
    s += "(";
    for (std::size_t i = 0; i < parameterTypeNames.size(); ++i) {
        if (i)
            s += ", ";
        s += parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>";
    }
    s += ")";
    return s;
}
inline std::string type_cache_key(std::string_view image, std::string_view namespc, std::string_view name) {
    std::string key(image);
    key.push_back('|');
    key.append(namespc);
    key.push_back('|');
    key.append(name);
    return key;
}
inline std::string member_cache_key(const void *klass, std::string_view name, int argc) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('|');
    key.append(std::to_string(argc));
    return key;
}
inline std::string member_cache_key(const void *klass, std::string_view name,
                                    const std::vector<const char *> &parameterTypeNames) {
    std::string key = std::to_string(reinterpret_cast<std::uintptr_t>(klass));
    key.push_back('|');
    key.append(name);
    key.push_back('(');
    for (std::size_t i = 0; i < parameterTypeNames.size(); ++i) {
        if (i)
            key.push_back(',');
        key.append(parameterTypeNames[i] ? parameterTypeNames[i] : "<unknown>");
    }
    key.push_back(')');
    return key;
}
inline std::string normalized_type_name(std::string_view type) {
    std::string out(type);
    if (out.rfind("class ", 0) == 0)
        out.erase(0, 6);
    if (out.rfind("struct ", 0) == 0)
        out.erase(0, 7);
    std::transform(out.begin(), out.end(), out.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (out == "bool" || out == "boolean" || out == "system.boolean")
        return "system.boolean";
    if (out == "int" || out == "int32" || out == "system.int32")
        return "system.int32";
    if (out == "uint" || out == "uint32" || out == "system.uint32")
        return "system.uint32";
    if (out == "short" || out == "int16" || out == "system.int16")
        return "system.int16";
    if (out == "ushort" || out == "uint16" || out == "system.uint16")
        return "system.uint16";
    if (out == "long" || out == "int64" || out == "system.int64")
        return "system.int64";
    if (out == "ulong" || out == "uint64" || out == "system.uint64")
        return "system.uint64";
    if (out == "float" || out == "single" || out == "system.single")
        return "system.single";
    if (out == "double" || out == "system.double")
        return "system.double";
    if (out == "byte" || out == "system.byte")
        return "system.byte";
    if (out == "sbyte" || out == "system.sbyte")
        return "system.sbyte";
    if (out == "char" || out == "system.char")
        return "system.char";
    if (out == "string" || out == "system.string")
        return "system.string";
    if (out == "object" || out == "system.object")
        return "system.object";
    if (out == "type" || out == "system.type")
        return "system.type";
    if (out == "void" || out == "system.void")
        return "system.void";
    return out;
}
inline bool type_name_matches(std::string_view actual, const char *requested) {
    return requested && normalized_type_name(actual) == normalized_type_name(requested);
}
struct Backend {
    static bool available() {
        return URK::
)URKUNITY"
        << backendNs << R"URKUNITY(::available(); }
    static const char* backend_last_error() { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::last_error(); }
    static const char* last_error() {
        // Backend diagnostics are copied into the local slot at the failure
        // site. Returning that slot prevents a stale backend error from
        // turning a successful false/null Unity result into a failure.
        thread_local std::string snapshot;
        snapshot = fallback_error() ? fallback_error() : "";
        return snapshot.empty() ? nullptr : snapshot.c_str();
    }
    static const void* find_class(std::string_view image, std::string_view ns, std::string_view name) { auto i=z(image), n=z(ns), c=z(name); return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::find_class(i.c_str(), n.c_str(), c.c_str()); }
    static const void* object_get_class(void* object) { return object ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::object_get_class(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object)) : nullptr; }
    static const char* class_get_name(const void* klass) { return klass ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::class_get_name(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass)) : nullptr; }
    static const char* class_get_namespace(const void* klass) { return klass ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::class_get_namespace(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass)) : nullptr; }
    static const void* class_get_parent(const void* klass) { return klass ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::class_get_parent(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass)) : nullptr; }
    static const void* class_get_methods(const void* klass, void** iterator) { return klass ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::class_get_methods(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass), iterator) : nullptr; }
    static const void* find_method(const void* klass, std::string_view name, int argc) { auto n=z(name); return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::resolve_method(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass), n.c_str(), argc); }
    static const char* method_get_name(const void* method) { return method ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::method_get_name(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Method*>(method)) : nullptr; }
    static bool method_is_generic(const void* method) { return method && URK::)URKUNITY"
        << backendNs << R"URKUNITY(::method_is_generic(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Method*>(method)); }
    static const void* find_method_exact(const void* klass, std::string_view name, const std::vector<const char*>& parameterTypeNames);
    static int runtime_invoke(const void* method, void* object, void** params, void** result, void** exception) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::runtime_invoke(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Method*>(method), static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object), params, result, exception); }
    static void* object_unbox(void* object) { return object ? URK::)URKUNITY"
        << backendNs << R"URKUNITY(::object_unbox(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object)) : nullptr; }
    static void* object_new(const void* klass) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::object_new(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass)); }
    static void runtime_object_init(void* object) { URK::)URKUNITY"
        << backendNs << R"URKUNITY(::runtime_object_init(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object)); }
    static void* new_string(std::string_view text) {
        if (text.size() > static_cast<std::size_t>(std::numeric_limits<std::uint32_t>::max())) {
            set_error("Unity managed string exceeds the runtime uint32 length range");
            return nullptr;
        }
        return URK::)URKUNITY"
        << backendNs << (mono
                              ? "::new_string(std::string(text).c_str()); }\n"
                              : "::string_new_len(text.data(), static_cast<std::uint32_t>(text.size())); }\n")
        << R"URKUNITY(    static bool string_to_utf8(void* string, char* output, std::size_t outputSize) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::string_to_utf8(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::String*>(string), output, outputSize); }
    static const void* find_field(const void* klass, std::string_view name) { auto n=z(name); return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::find_field(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(klass), n.c_str()); }
    static bool field_get_value(void* object, const void* field, void* output) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::field_get_value(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object), static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Field*>(field), output); }
    static void* field_reference_write_pointer(void*& reference) { return )URKUNITY"
        << (mono ? "&reference" : "reference") << R"URKUNITY(; }
    static bool field_set_value(void* object, const void* field, void* value) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::field_set_value(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Object*>(object), static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Field*>(field), value); }
    static bool field_static_get_value(const void* klass, const void* field, void* output);
    static bool field_static_set_value(const void* klass, const void* field, void* value);
    static std::size_t array_length(void* array) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::array_length(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Array*>(array)); }
    static bool has_array_length() { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::has_array_length(); }
    static bool has_array_ref_at() { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::has_array_ref_at(); }
    static void* array_ref_at(void* array, std::size_t index) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::array_ref_at(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Array*>(array), index); }
    static void* array_addr_with_size(void* array, int elementSize, std::size_t index) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::array_addr_with_size(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Array*>(array), elementSize, index); }
    static bool has_array_set_ref() { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::has_array_set_ref(); }
    static bool array_set_ref(void* array, std::size_t index, void* value) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::array_set_ref(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Array*>(array), index, value); }
    static std::uint32_t gchandle_new(void* object, int pinned) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::gchandle_new(object, pinned); }
    static std::uint32_t gchandle_new_weakref(void* object, int trackResurrection) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::gchandle_new_weakref(object, trackResurrection); }
    static void* gchandle_get_target(std::uint32_t handle) { return URK::)URKUNITY"
        << backendNs << R"URKUNITY(::gchandle_get_target(handle); }
    static void gchandle_free(std::uint32_t handle) { URK::)URKUNITY"
        << backendNs << R"URKUNITY(::gchandle_free(handle); }
    static void* type_object_for_class(std::string_view image, std::string_view ns, std::string_view name) { const void* k=find_class(image, ns, name); if (!k) return nullptr; auto* t = URK::)URKUNITY"
        << backendNs << R"URKUNITY(::class_get_type(static_cast<const URK::)URKUNITY" << backendNs
        << R"URKUNITY(::Class*>(k)); return t ? URK::)URKUNITY" << backendNs
        << R"URKUNITY(::type_get_object(t) : nullptr; }
    )URKUNITY"
        << methodObjectHelper << methodParameterCountHelper << valueBoxHelper << R"URKUNITY(
    static std::int64_t string_length(void* string) { return static_cast<std::int64_t>(URK::)URKUNITY"
        << backendNs << R"URKUNITY(::string_length(static_cast<URK::)URKUNITY" << backendNs
        << R"URKUNITY(::String*>(string)));
}
}
;
inline void append_backend_error() {
    const char *e = Backend::backend_last_error();
    if (e && e[0]) {
        if (!error_slot().empty())
            error_slot() += "; backend: ";
        error_slot() += e;
    }
}
inline std::string managed_string_to_utf8(void *value) {
    if (!value)
        return {};
    constexpr std::int64_t kMaxInspectorStringUnits = 1024 * 1024;
    const std::int64_t rawLength = Backend::string_length(value);
    if (rawLength >= 0 && rawLength <= kMaxInspectorStringUnits) {
        const std::size_t capacity = static_cast<std::size_t>(rawLength) * 4 + 1;
        std::string buffer(capacity, '\0');
        if (Backend::string_to_utf8(value, buffer.data(), buffer.size())) {
            buffer.resize(std::strlen(buffer.c_str()));
            return buffer;
        }
    }
    if (!fallback_error())
        set_error(rawLength > kMaxInspectorStringUnits ? "Unity string conversion rejected an oversized managed string"
                                                       : "Unity string conversion failed: invalid length or backend "
                                                         "conversion failure");
    append_backend_error();
    return {};
}
inline std::string class_display_name(const void *klass) {
    if (!klass)
        return {};
    const char *ns = Backend::class_get_namespace(klass);
    const char *name = Backend::class_get_name(klass);
    if (ns && ns[0])
        return std::string(ns) + "." + (name ? name : "<unnamed>");
    return name && name[0] ? std::string(name) : std::string{};
}

// User-authored wrappers normally derive from Object, Component,
// MonoBehaviour, or another generated wrapper. Recognize that inheritance
// automatically so custom game types work everywhere a managed reference is
// accepted, including fields, arguments, and return values.
template <class T> struct is_wrapper : std::bool_constant<std::is_base_of_v<Object, std::remove_cvref_t<T>>> {};
template <class T> inline constexpr bool is_wrapper_v = is_wrapper<std::remove_cvref_t<T>>::value;
}

inline const char *last_error() {
    return detail::Backend::last_error();
}
inline void clear_error() {
    detail::clear_error();
}

inline constexpr std::array<std::string_view, 16> common_type_images{
    "UnityEngine.CoreModule.dll",
    "UnityEngine.PhysicsModule.dll",
    "UnityEngine.Physics2DModule.dll",
    "UnityEngine.AudioModule.dll",
    "UnityEngine.AnimationModule.dll",
    "UnityEngine.UIModule.dll",
    "UnityEngine.UI.dll",
    "UnityEngine.ImageConversionModule.dll",
    "UnityEngine.TextRenderingModule.dll",
    "Unity.TextMeshPro.dll",
    "Unity.InputSystem.dll",
    "UnityEngine.AssetBundleModule.dll",
    "UnityEngine.dll",
    "mscorlib.dll",
    "System.Private.CoreLib.dll",
    "netstandard.dll",
};

struct TypeRef {
    std::string_view image;
    std::string_view namespc;
    std::string_view name;
    const void *resolve_class() const {
        const std::string key = detail::type_cache_key(image, namespc, name);
        {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            if (const auto found = detail::class_cache().find(key); found != detail::class_cache().end())
                return found->second;
        }
        const void *resolved = nullptr;
        if (image.empty()) {
            for (const std::string_view candidate : common_type_images) {
                if (auto *klass = detail::Backend::find_class(candidate, namespc, name)) {
                    resolved = klass;
                    break;
                }
            }
        } else {
            resolved = detail::Backend::find_class(image, namespc, name);
        }
        if (resolved) {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            detail::class_cache()[key] = resolved;
        }
        return resolved;
    }
    void *resolve_type_object() const {
        const std::string key = detail::type_cache_key(image, namespc, name);
        {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            if (const auto found = detail::type_cache().find(key); found != detail::type_cache().end())
                return found->second;
        }
        void *resolved = nullptr;
        if (image.empty()) {
            for (const std::string_view candidate : common_type_images) {
                if (void *type = detail::Backend::type_object_for_class(candidate, namespc, name)) {
                    resolved = type;
                    break;
                }
            }
        } else {
            resolved = detail::Backend::type_object_for_class(image, namespc, name);
        }
        if (resolved) {
            std::lock_guard<std::mutex> lock(detail::cache_mutex());
            detail::type_cache()[key] = resolved;
        }
        return resolved;
    }
};

inline constexpr TypeRef UnityObjectType{"", "UnityEngine", "Object"};
inline constexpr TypeRef GameObjectType{"", "UnityEngine", "GameObject"};
inline constexpr TypeRef ComponentType{"", "UnityEngine", "Component"};
inline constexpr TypeRef BehaviourType{"", "UnityEngine", "Behaviour"};
inline constexpr TypeRef MonoBehaviourType{"", "UnityEngine", "MonoBehaviour"};
inline constexpr TypeRef ScriptableObjectType{"", "UnityEngine", "ScriptableObject"};
inline constexpr TypeRef TransformType{"", "UnityEngine", "Transform"};
inline constexpr TypeRef CameraType{"", "UnityEngine", "Camera"};
inline constexpr TypeRef LightTypeRef{"", "UnityEngine", "Light"};
inline constexpr TypeRef RendererType{"", "UnityEngine", "Renderer"};
inline constexpr TypeRef SkinnedMeshRendererType{"", "UnityEngine", "SkinnedMeshRenderer"};
inline constexpr TypeRef ColliderType{"", "UnityEngine", "Collider"};
inline constexpr TypeRef RectTransformType{"", "UnityEngine", "RectTransform"};
inline constexpr TypeRef RigidbodyType{"", "UnityEngine", "Rigidbody"};
inline constexpr TypeRef Rigidbody2DType{"", "UnityEngine", "Rigidbody2D"};
inline constexpr TypeRef AudioSourceType{"", "UnityEngine", "AudioSource"};
inline constexpr TypeRef AnimatorType{"", "UnityEngine", "Animator"};
inline constexpr TypeRef CanvasType{"", "UnityEngine", "Canvas"};
inline constexpr TypeRef CanvasRendererType{"", "UnityEngine", "CanvasRenderer"};
inline constexpr TypeRef CanvasGroupType{"", "UnityEngine", "CanvasGroup"};
inline constexpr TypeRef CanvasScalerType{"", "UnityEngine.UI", "CanvasScaler"};
inline constexpr TypeRef GraphicType{"", "UnityEngine.UI", "Graphic"};
inline constexpr TypeRef GraphicRaycasterType{"", "UnityEngine.UI", "GraphicRaycaster"};
inline constexpr TypeRef SelectableType{"", "UnityEngine.UI", "Selectable"};
inline constexpr TypeRef ImageTypeRef{"", "UnityEngine.UI", "Image"};
inline constexpr TypeRef RawImageType{"", "UnityEngine.UI", "RawImage"};
inline constexpr TypeRef TextType{"", "UnityEngine.UI", "Text"};
inline constexpr TypeRef TextMeshProUGUIType{"", "TMPro", "TextMeshProUGUI"};
inline constexpr TypeRef TmpInputFieldType{"", "TMPro", "TMP_InputField"};
inline constexpr TypeRef TmpDropdownType{"", "TMPro", "TMP_Dropdown"};
inline constexpr TypeRef ButtonType{"", "UnityEngine.UI", "Button"};
inline constexpr TypeRef ToggleType{"", "UnityEngine.UI", "Toggle"};
inline constexpr TypeRef SliderType{"", "UnityEngine.UI", "Slider"};
inline constexpr TypeRef ScrollbarType{"", "UnityEngine.UI", "Scrollbar"};
inline constexpr TypeRef DropdownType{"", "UnityEngine.UI", "Dropdown"};
inline constexpr TypeRef InputFieldType{"", "UnityEngine.UI", "InputField"};
inline constexpr TypeRef MaskType{"", "UnityEngine.UI", "Mask"};
inline constexpr TypeRef RectMask2DType{"", "UnityEngine.UI", "RectMask2D"};
inline constexpr TypeRef ScrollRectType{"", "UnityEngine.UI", "ScrollRect"};
inline constexpr TypeRef LayoutElementType{"", "UnityEngine.UI", "LayoutElement"};
inline constexpr TypeRef HorizontalLayoutGroupType{"", "UnityEngine.UI", "HorizontalLayoutGroup"};
inline constexpr TypeRef VerticalLayoutGroupType{"", "UnityEngine.UI", "VerticalLayoutGroup"};
inline constexpr TypeRef GridLayoutGroupType{"", "UnityEngine.UI", "GridLayoutGroup"};
inline constexpr TypeRef ContentSizeFitterType{"", "UnityEngine.UI", "ContentSizeFitter"};
inline constexpr TypeRef AspectRatioFitterType{"", "UnityEngine.UI", "AspectRatioFitter"};
inline constexpr TypeRef EventSystemType{"", "UnityEngine.EventSystems", "EventSystem"};
inline constexpr TypeRef BaseInputModuleType{"", "UnityEngine.EventSystems", "BaseInputModule"};
inline constexpr TypeRef StandaloneInputModuleType{"", "UnityEngine.EventSystems", "StandaloneInputModule"};
inline constexpr TypeRef InputSystemUIInputModuleType{"Unity.InputSystem.dll", "UnityEngine.InputSystem.UI",
                                                      "InputSystemUIInputModule"};
inline constexpr TypeRef MeshRendererType{"", "UnityEngine", "MeshRenderer"};
inline constexpr TypeRef MeshFilterType{"", "UnityEngine", "MeshFilter"};
inline constexpr TypeRef MeshColliderType{"", "UnityEngine", "MeshCollider"};
inline constexpr TypeRef MeshType{"", "UnityEngine", "Mesh"};
inline constexpr TypeRef MaterialType{"", "UnityEngine", "Material"};
inline constexpr TypeRef TextureType{"", "UnityEngine", "Texture"};
inline constexpr TypeRef Texture2DType{"", "UnityEngine", "Texture2D"};
inline constexpr TypeRef ShaderType{"", "UnityEngine", "Shader"};
inline constexpr TypeRef SpriteType{"", "UnityEngine", "Sprite"};
inline constexpr TypeRef AssetBundleType{"", "UnityEngine", "AssetBundle"};
inline constexpr TypeRef ScreenType{"", "UnityEngine", "Screen"};
inline constexpr TypeRef TimeType{"", "UnityEngine", "Time"};
inline constexpr TypeRef ResourcesType{"", "UnityEngine", "Resources"};
inline constexpr TypeRef DebugType{"", "UnityEngine", "Debug"};

namespace detail {
// IL2CPP field setters take the address of raw value-type storage, but take a
// managed object directly for reference fields. Field getters still write a
// reference into an output slot, so FieldOut keeps its pointer-to-pointer form.
template <class T> struct FieldArg {
    T storage;
    void *ptr;
    FieldArg(T v) : storage(v), ptr(&storage) {
    }
};
template <class T>
    requires is_wrapper_v<T>
struct FieldArg<T> {
    void *storage;
    void *ptr;
    FieldArg(T v) : storage(v.handle()), ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <> struct FieldArg<void *> {
    void *storage;
    void *ptr;
    FieldArg(void *v) : storage(v), ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <> struct FieldArg<const char *> {
    void *storage;
    void *ptr;
    FieldArg(const char *v)
        : storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})),
          ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <std::size_t N> struct FieldArg<char[N]> {
    void *storage;
    void *ptr;
    FieldArg(const char (&v)[N])
        : storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),
          ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <std::size_t N> struct FieldArg<const char[N]> {
    void *storage;
    void *ptr;
    FieldArg(const char (&v)[N])
        : storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))),
          ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <> struct FieldArg<std::string> {
    void *storage;
    void *ptr;
    FieldArg(const std::string &v)
        : storage(Backend::new_string(v)), ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <> struct FieldArg<std::string_view> {
    void *storage;
    void *ptr;
    FieldArg(std::string_view v)
        : storage(Backend::new_string(v)), ptr(Backend::field_reference_write_pointer(storage)) {
    }
};
template <class T> struct FieldOut {
    T value{};
    void *ptr() {
        return &value;
    }
    T get() {
        return value;
    }
};
template <class T>
    requires is_wrapper_v<T>
struct FieldOut<T> {
    void *value = nullptr;
    void *ptr() {
        return &value;
    }
    T get() {
        return T{value};
    }
};
template <> struct FieldOut<void *> {
    void *value = nullptr;
    void *ptr() {
        return &value;
    }
    void *get() {
        return value;
    }
};
template <> struct FieldOut<std::string> {
    void *value = nullptr;
    void *ptr() {
        return &value;
    }
    std::string get() {
        return managed_string_to_utf8(value);
    }
};
template <class T> void *field_value(T &v) {
    FieldArg<std::remove_cvref_t<T>> a(v);
    return a.ptr;
}
template <class Ret, class... Args> Ret InvokeStatic(TypeRef type, std::string_view methodName, Args &&...args);
template <class T, class... Args>
std::vector<T> StaticArrayCall(TypeRef type, std::string_view methodName, Args &&...args);
template <class T, class... ExtraArgs>
std::vector<T> FindObjectsUsing(TypeRef owner, std::string_view methodName, std::string_view image,
                                std::string_view namespc, std::string_view className, ExtraArgs &&...extraArgs);
}
struct TypeObject {
    void *handle_ = nullptr;
    explicit TypeObject(void *h = nullptr) : handle_(h) {
    }
    void *handle() const {
        return handle_;
    }
    explicit operator bool() const {
        return handle_ != nullptr;
    }
};

struct Object {
    void *handle_ = nullptr;
    Object() = default;
    explicit Object(void *h) : handle_(h) {
    }
    void *handle() const {
        return handle_;
    }
    explicit operator bool() const {
        return handle_ != nullptr;
    }
    static constexpr TypeRef unity_type() {
        return UnityObjectType;
    }
    bool alive() const {
        return handle_ ? detail::InvokeStatic<bool>(UnityObjectType, "op_Implicit", *this) : false;
    }
    std::string name() const;
    std::string ToString() const;
    std::string runtime_class_name() const;
    int hideFlags() const {
        return GetProperty<int>("hideFlags");
    }
    int GetInstanceID() const {
        return Call<int>("GetInstanceID");
    }

    template <class T = Object>
    static std::vector<T> FindObjectsOfType(std::string_view image, std::string_view namespc,
                                            std::string_view className) {
        return detail::FindObjectsUsing<T>(UnityObjectType, "FindObjectsOfType", image, namespc, className);
    }
    template <class T = Object>
    static std::vector<T> FindObjectsByType(std::string_view image, std::string_view namespc,
                                            std::string_view className,
                                            FindObjectsSortMode sortMode = FindObjectsSortMode::None) {
        return detail::FindObjectsUsing<T>(UnityObjectType, "FindObjectsByType", image, namespc, className, sortMode);
    }
    template <class T = Object>
    static std::vector<T> FindObjectsOfTypeAll(std::string_view image, std::string_view namespc,
                                               std::string_view className) {
        return detail::FindObjectsUsing<T>(ResourcesType, "FindObjectsOfTypeAll", image, namespc, className);
    }
    template <class T = Object>
    static T FindObjectOfType(std::string_view image, std::string_view namespc, std::string_view className) {
        auto all = FindObjectsOfType<T>(image, namespc, className);
        return all.empty() ? T{} : all.front();
    }
    template <class T = Object>
    static T FindObjectOfTypeAll(std::string_view image, std::string_view namespc, std::string_view className) {
        auto all = FindObjectsOfTypeAll<T>(image, namespc, className);
        return all.empty() ? T{} : all.front();
    }
    template <class T = Object>
    static T FindObject(std::string_view image, std::string_view namespc, std::string_view className) {
        return FindObjectOfType<T>(image, namespc, className);
    }
    template <class T = Object>
    static T FindInstance(std::string_view image, std::string_view namespc, std::string_view className) {
        return FindObjectOfType<T>(image, namespc, className);
    }
    template <class T = Object>
    static std::vector<T> FindInstances(std::string_view image, std::string_view namespc, std::string_view className) {
        return FindObjectsOfType<T>(image, namespc, className);
    }
    template <class T = Object>
    static std::vector<T> FindAllInstances(std::string_view image, std::string_view namespc,
                                           std::string_view className) {
        return FindObjectsOfTypeAll<T>(image, namespc, className);
    }
    template <class T> static std::vector<T> FindObjectsOfType() {
        const TypeRef type = T::unity_type();
        return FindObjectsOfType<T>(type.image, type.namespc, type.name);
    }
    template <class T>
    static std::vector<T> FindObjectsByType(FindObjectsSortMode sortMode = FindObjectsSortMode::None) {
        const TypeRef type = T::unity_type();
        return FindObjectsByType<T>(type.image, type.namespc, type.name, sortMode);
    }
    template <class T> static std::vector<T> FindObjectsOfTypeAll() {
        const TypeRef type = T::unity_type();
        return FindObjectsOfTypeAll<T>(type.image, type.namespc, type.name);
    }
    template <class T> static T FindObjectOfType() {
        auto all = FindObjectsOfType<T>();
        return all.empty() ? T{} : all.front();
    }
    template <class T> static T FindObjectOfTypeAll() {
        auto all = FindObjectsOfTypeAll<T>();
        return all.empty() ? T{} : all.front();
    }
    template <class T> static T FindObject() {
        return FindObjectOfType<T>();
    }
    template <class T> static T FindInstance() {
        return FindObjectOfType<T>();
    }
    template <class T> static std::vector<T> FindInstances() {
        return FindObjectsOfType<T>();
    }
    template <class T> static std::vector<T> FindAllInstances() {
        return FindObjectsOfTypeAll<T>();
    }
    template <class T = Object> static T Instantiate(const T &original) {
        return T{detail::InvokeStatic<void *>(UnityObjectType, "Instantiate", original)};
    }
    template <class T = Object> static T Instantiate(const T &original, const Transform &parent) {
        return T{detail::InvokeStatic<void *>(UnityObjectType, "Instantiate", original, parent)};
    }
    template <class T = Object>
    static T Instantiate(const T &original, const Transform &parent, bool instantiateInWorldSpace) {
        return T{
            detail::InvokeStatic<void *>(UnityObjectType, "Instantiate", original, parent, instantiateInWorldSpace)};
    }
    template <class T = Object> static T Instantiate(const T &original, Vector3 position, Quaternion rotation) {
        return T{detail::InvokeStatic<void *>(UnityObjectType, "Instantiate", original, position, rotation)};
    }
    template <class T = Object>
    static T Instantiate(const T &original, Vector3 position, Quaternion rotation, const Transform &parent) {
        return T{detail::InvokeStatic<void *>(UnityObjectType, "Instantiate", original, position, rotation, parent)};
    }
    static void Destroy(const Object &object) {
        detail::InvokeStatic<void>(UnityObjectType, "Destroy", object);
    }
    static void Destroy(const Object &object, float delaySeconds) {
        detail::InvokeStatic<void>(UnityObjectType, "Destroy", object, delaySeconds);
    }
    static void DestroyImmediate(const Object &object, bool allowDestroyingAssets = false) {
        detail::InvokeStatic<void>(UnityObjectType, "DestroyImmediate", object, allowDestroyingAssets);
    }
    static void DontDestroyOnLoad(const Object &object) {
        detail::InvokeStatic<void>(UnityObjectType, "DontDestroyOnLoad", object);
    }

    template <class Ret = void, class... Args> Ret Call(std::string_view methodName, Args &&...args) const;
    template <class T> T GetField(std::string_view fieldName) const;
    template <class T> void SetField(std::string_view fieldName, T value) const;
    template <class T> static T StaticGetField(TypeRef type, std::string_view fieldName);
    template <class T> static void StaticSetField(TypeRef type, std::string_view fieldName, T value);
    template <class Ret = void>
    Ret CallExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                  void **rawArgs) const;
    template <class Ret = void, class... Args>
    Ret CallExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                  Args &&...args) const;
    template <class Ret = void, class... Args>
    Ret InvokeGeneric(std::string_view methodName, const std::vector<TypeObject> &genericTypes, Args &&...args) const;
    template <class T = Object>
    std::vector<T> CallArrayExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                                  void **rawArgs) const;
    template <class T = Object, class... Args>
    std::vector<T> CallArrayExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                                  Args &&...args) const;
    std::vector<std::string> CallStringArrayExact(std::string_view methodName,
                                                  const std::vector<const char *> &parameterTypeNames) const;
    template <class T>
    void SetReferenceArrayProperty(std::string_view propertyName, const std::vector<T> &values) const;
    template <class T> T GetProperty(std::string_view propertyName) const {
        return Call<T>(std::string("get_") + std::string(propertyName));
    }
    template <class T> void SetProperty(std::string_view propertyName, T value) const {
        Call<void>(std::string("set_") + std::string(propertyName), value);
    }
};

struct GameObject;
struct Scene;
struct Transform;
struct Camera;
struct Light;
struct Renderer;
struct SkinnedMeshRenderer;
struct Collider;
struct RectTransform;
struct Rigidbody;
struct Rigidbody2D;
struct Animator;
struct Canvas;
struct CanvasGroup;
struct CanvasScaler;
struct Graphic;
struct Image;
struct RawImage;
struct Text;
struct TextMeshProUGUI;
struct Button;
struct Toggle;
struct Slider;
struct Scrollbar;
struct Dropdown;
struct InputField;
struct Mask;
struct ScrollRect;
struct LayoutElement;
struct HorizontalLayoutGroup;
struct VerticalLayoutGroup;
struct GridLayoutGroup;
struct MeshRenderer;
struct MeshFilter;
struct MeshCollider;
struct Mesh;
struct Material;
struct Texture;
struct Texture2D;
struct Shader;
struct Sprite;
// URK_UNITY_COMPONENTS_BEGIN
struct Component : Object {
    Component() = default;
    explicit Component(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return ComponentType;
    }
    GameObject gameObject() const;
    Transform transform() const;
    template <class T> T GetComponent() const;
    template <class T> T GetComponent(const char *name) const;
    Object GetComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template <class T> T GetComponentInChildren(bool includeInactive = false) const;
    Object GetComponentInChildren(std::string_view image, std::string_view namespc, std::string_view className,
                                  bool includeInactive = false) const;
    template <class T> T GetComponentInParent(bool includeInactive = false) const;
    Object GetComponentInParent(std::string_view image, std::string_view namespc, std::string_view className,
                                bool includeInactive = false) const;
    template <class T = Object> std::vector<T> GetComponents() const;
    template <class T = Object> std::vector<T> GetComponentsInChildren(bool includeInactive = false) const;
    template <class T = Object> std::vector<T> GetComponentsInParent(bool includeInactive = false) const;
    template <class T> T AddComponent() const;
    Object AddComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template <class T> bool HasComponent() const;
    bool HasComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
    template <class T> T GetOrAddComponent() const;
    Object GetOrAddComponent(std::string_view image, std::string_view namespc, std::string_view className) const;
};
struct Behaviour : Component {
    Behaviour() = default;
    explicit Behaviour(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return BehaviourType;
    }
    bool enabled() const {
        return GetProperty<bool>("enabled");
    }
    void set_enabled(bool value) const {
        SetProperty("enabled", value);
    }
    bool isActiveAndEnabled() const {
        return GetProperty<bool>("isActiveAndEnabled");
    }
};
struct MonoBehaviour : Behaviour {
    MonoBehaviour() = default;
    explicit MonoBehaviour(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return MonoBehaviourType;
    }
    bool useGUILayout() const {
        return GetProperty<bool>("useGUILayout");
    }
    void set_useGUILayout(bool value) const {
        SetProperty("useGUILayout", value);
    }
};
struct ScriptableObject : Object {
    ScriptableObject() = default;
    explicit ScriptableObject(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return ScriptableObjectType;
    }
    static ScriptableObject CreateInstance(std::string_view image, std::string_view namespc,
                                           std::string_view className) {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity ScriptableObject::CreateInstance "
                                          "failed: class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return ScriptableObject{detail::InvokeStatic<void *>(ScriptableObjectType, "CreateInstance", TypeObject{type})};
    }
    static ScriptableObject CreateInstance(TypeRef type) {
        return CreateInstance(type.image, type.namespc, type.name);
    }
    template <class T> static T CreateInstance() {
        const TypeRef type = T::unity_type();
        return T{CreateInstance(type.image, type.namespc, type.name).handle()};
    }
};
struct Transform : Component {
    Transform() = default;
    explicit Transform(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return TransformType;
    }
    Vector3 position() const {
        return GetProperty<Vector3>("position");
    }
    void set_position(Vector3 v) const {
        SetProperty("position", v);
    }
    Vector3 localPosition() const {
        return GetProperty<Vector3>("localPosition");
    }
    void set_localPosition(Vector3 v) const {
        SetProperty("localPosition", v);
    }
    Vector3 eulerAngles() const {
        return GetProperty<Vector3>("eulerAngles");
    }
    void set_eulerAngles(Vector3 v) const {
        SetProperty("eulerAngles", v);
    }
    Quaternion rotation() const {
        return GetProperty<Quaternion>("rotation");
    }
    void set_rotation(Quaternion q) const {
        SetProperty("rotation", q);
    }
    Vector3 localScale() const {
        return GetProperty<Vector3>("localScale");
    }
    void set_localScale(Vector3 value) const {
        SetProperty("localScale", value);
    }
    Vector3 forward() const {
        return GetProperty<Vector3>("forward");
    }
    Vector3 right() const {
        return GetProperty<Vector3>("right");
    }
    Vector3 up() const {
        return GetProperty<Vector3>("up");
    }
    Vector3 lossyScale() const {
        return GetProperty<Vector3>("lossyScale");
    }
    Transform parent() const {
        return GetProperty<Transform>("parent");
    }
    void set_parent(Transform value) const {
        SetProperty("parent", value);
    }
    void SetParent(Transform value, bool worldPositionStays = true) const {
        CallExact<void>("SetParent", {"UnityEngine.Transform", "System.Boolean"}, value, worldPositionStays);
    }
    Transform root() const {
        return GetProperty<Transform>("root");
    }
    int childCount() const {
        return GetProperty<int>("childCount");
    }
    Transform GetChild(int index) const {
        return CallExact<Transform>("GetChild", {"System.Int32"}, index);
    }
    Transform Find(std::string_view path) const {
        return Call<Transform>("Find", path);
    }
};
struct Camera : Behaviour {
    Camera() = default;
    explicit Camera(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return CameraType;
    }
    static Camera main() {
        return detail::InvokeStatic<Camera>(CameraType, "get_main");
    }
    static Camera current() {
        return detail::InvokeStatic<Camera>(CameraType, "get_current");
    }
    float fieldOfView() const {
        return GetProperty<float>("fieldOfView");
    }
    void set_fieldOfView(float value) const {
        SetProperty("fieldOfView", value);
    }
    float nearClipPlane() const {
        return GetProperty<float>("nearClipPlane");
    }
    float farClipPlane() const {
        return GetProperty<float>("farClipPlane");
    }
    float aspect() const {
        return GetProperty<float>("aspect");
    }
    int pixelWidth() const {
        return GetProperty<int>("pixelWidth");
    }
    int pixelHeight() const {
        return GetProperty<int>("pixelHeight");
    }
    Vector3 WorldToScreenPoint(Vector3 world) const {
        return CallExact<Vector3>("WorldToScreenPoint", {"UnityEngine.Vector3"}, world);
    }
    Vector3 ScreenToWorldPoint(Vector3 screen) const {
        return CallExact<Vector3>("ScreenToWorldPoint", {"UnityEngine.Vector3"}, screen);
    }
    Vector3 WorldToViewportPoint(Vector3 world) const {
        return CallExact<Vector3>("WorldToViewportPoint", {"UnityEngine.Vector3"}, world);
    }
    Vector3 ViewportToWorldPoint(Vector3 viewport) const {
        return CallExact<Vector3>("ViewportToWorldPoint", {"UnityEngine.Vector3"}, viewport);
    }
    Ray ScreenPointToRay(Vector3 screen) const {
        return CallExact<Ray>("ScreenPointToRay", {"UnityEngine.Vector3"}, screen);
    }
};
struct Mesh : Object {
    Mesh() = default;
    explicit Mesh(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return MeshType;
    }
};
struct Texture : Object {
    Texture() = default;
    explicit Texture(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return TextureType;
    }
    int width() const {
        return GetProperty<int>("width");
    }
    int height() const {
        return GetProperty<int>("height");
    }
    int anisoLevel() const {
        return GetProperty<int>("anisoLevel");
    }
    void set_anisoLevel(int value) const {
        SetProperty("anisoLevel", value);
    }
    float mipMapBias() const {
        return GetProperty<float>("mipMapBias");
    }
    void set_mipMapBias(float value) const {
        SetProperty("mipMapBias", value);
    }
};
struct Texture2D : Texture {
    Texture2D() = default;
    explicit Texture2D(void *h) : Texture(h) {
    }
    static constexpr TypeRef unity_type() {
        return Texture2DType;
    }
    int mipmapCount() const {
        return GetProperty<int>("mipmapCount");
    }
    Color GetPixel(int x, int y) const {
        return CallExact<Color>("GetPixel", {"System.Int32", "System.Int32"}, x, y);
    }
    void SetPixel(int x, int y, Color color) const {
        CallExact<void>("SetPixel", {"System.Int32", "System.Int32", "UnityEngine.Color"}, x, y, color);
    }
    bool Resize(int width, int height) const {
        return CallExact<bool>("Resize", {"System.Int32", "System.Int32"}, width, height);
    }
    void Apply() const {
        Apply(true, false);
    }
    void Apply(bool updateMipmaps, bool makeNoLongerReadable = false) const {
        CallExact<void>("Apply", {"System.Boolean", "System.Boolean"}, updateMipmaps, makeNoLongerReadable);
    }
};
struct Shader : Object {
    Shader() = default;
    explicit Shader(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return ShaderType;
    }
    static Shader Find(std::string_view name) {
        return detail::InvokeStatic<Shader>(ShaderType, "Find", name);
    }
    static int PropertyToID(std::string_view name) {
        return detail::InvokeStatic<int>(ShaderType, "PropertyToID", name);
    }
    bool isSupported() const {
        return GetProperty<bool>("isSupported");
    }
    int maximumLOD() const {
        return GetProperty<int>("maximumLOD");
    }
    void set_maximumLOD(int value) const {
        SetProperty("maximumLOD", value);
    }
    int renderQueue() const {
        return GetProperty<int>("renderQueue");
    }
};
struct Material : Object {
    Material() = default;
    explicit Material(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return MaterialType;
    }
    Shader shader() const {
        return GetProperty<Shader>("shader");
    }
    void set_shader(Shader value) const {
        SetProperty("shader", value);
    }
    Color color() const {
        return GetProperty<Color>("color");
    }
    void set_color(Color value) const {
        SetProperty("color", value);
    }
    Texture mainTexture() const {
        return GetProperty<Texture>("mainTexture");
    }
    void set_mainTexture(Texture value) const {
        SetProperty("mainTexture", value);
    }
    float GetFloat(std::string_view name) const {
        return CallExact<float>("GetFloat", {"System.String"}, name);
    }
    void SetFloat(std::string_view name, float value) const {
        CallExact<void>("SetFloat", {"System.String", "System.Single"}, name, value);
    }
    Color GetColor(std::string_view name) const {
        return CallExact<Color>("GetColor", {"System.String"}, name);
    }
    void SetColor(std::string_view name, Color value) const {
        CallExact<void>("SetColor", {"System.String", "UnityEngine.Color"}, name, value);
    }
    Texture GetTexture(std::string_view name) const {
        return CallExact<Texture>("GetTexture", {"System.String"}, name);
    }
    void SetTexture(std::string_view name, Texture value) const {
        CallExact<void>("SetTexture", {"System.String", "UnityEngine.Texture"}, name, value);
    }
    bool HasProperty(std::string_view name) const {
        return CallExact<bool>("HasProperty", {"System.String"}, name);
    }
};
struct Light : Behaviour {
    Light() = default;
    explicit Light(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return LightTypeRef;
    }
    LightType type() const {
        return GetProperty<LightType>("type");
    }
    void set_type(LightType value) const {
        CallExact<void>("set_type", {"UnityEngine.LightType"}, value);
    }
    Color color() const {
        return GetProperty<Color>("color");
    }
    void set_color(Color value) const {
        SetProperty("color", value);
    }
    float colorTemperature() const {
        return GetProperty<float>("colorTemperature");
    }
    void set_colorTemperature(float value) const {
        SetProperty("colorTemperature", value);
    }
    bool useColorTemperature() const {
        return GetProperty<bool>("useColorTemperature");
    }
    void set_useColorTemperature(bool value) const {
        SetProperty("useColorTemperature", value);
    }
    float intensity() const {
        return GetProperty<float>("intensity");
    }
    void set_intensity(float value) const {
        SetProperty("intensity", value);
    }
    float bounceIntensity() const {
        return GetProperty<float>("bounceIntensity");
    }
    void set_bounceIntensity(float value) const {
        SetProperty("bounceIntensity", value);
    }
    float range() const {
        return GetProperty<float>("range");
    }
    void set_range(float value) const {
        SetProperty("range", value);
    }
    float spotAngle() const {
        return GetProperty<float>("spotAngle");
    }
    void set_spotAngle(float value) const {
        SetProperty("spotAngle", value);
    }
    float innerSpotAngle() const {
        return GetProperty<float>("innerSpotAngle");
    }
    void set_innerSpotAngle(float value) const {
        SetProperty("innerSpotAngle", value);
    }
    Texture cookie() const {
        return GetProperty<Texture>("cookie");
    }
    void set_cookie(Texture value) const {
        SetProperty("cookie", value);
    }
    float cookieSize() const {
        return GetProperty<float>("cookieSize");
    }
    void set_cookieSize(float value) const {
        SetProperty("cookieSize", value);
    }
    LightShadows shadows() const {
        return GetProperty<LightShadows>("shadows");
    }
    void set_shadows(LightShadows value) const {
        CallExact<void>("set_shadows", {"UnityEngine.LightShadows"}, value);
    }
    float shadowStrength() const {
        return GetProperty<float>("shadowStrength");
    }
    void set_shadowStrength(float value) const {
        SetProperty("shadowStrength", value);
    }
    LightShadowResolution shadowResolution() const {
        return GetProperty<LightShadowResolution>("shadowResolution");
    }
    void set_shadowResolution(LightShadowResolution value) const {
        CallExact<void>("set_shadowResolution", {"UnityEngine.LightShadowResolution"}, value);
    }
    float shadowBias() const {
        return GetProperty<float>("shadowBias");
    }
    void set_shadowBias(float value) const {
        SetProperty("shadowBias", value);
    }
    float shadowNormalBias() const {
        return GetProperty<float>("shadowNormalBias");
    }
    void set_shadowNormalBias(float value) const {
        SetProperty("shadowNormalBias", value);
    }
    float shadowNearPlane() const {
        return GetProperty<float>("shadowNearPlane");
    }
    void set_shadowNearPlane(float value) const {
        SetProperty("shadowNearPlane", value);
    }
    int cullingMask() const {
        return GetProperty<int>("cullingMask");
    }
    void set_cullingMask(int value) const {
        SetProperty("cullingMask", value);
    }
    LightRenderMode renderMode() const {
        return GetProperty<LightRenderMode>("renderMode");
    }
    void set_renderMode(LightRenderMode value) const {
        CallExact<void>("set_renderMode", {"UnityEngine.LightRenderMode"}, value);
    }
};
struct Renderer : Component {
    Renderer() = default;
    explicit Renderer(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return RendererType;
    }
    Bounds bounds() const {
        return GetProperty<Bounds>("bounds");
    }
    Bounds localBounds() const {
        return GetProperty<Bounds>("localBounds");
    }
    void set_localBounds(Bounds value) const {
        SetProperty("localBounds", value);
    }
    bool enabled() const {
        return GetProperty<bool>("enabled");
    }
    void set_enabled(bool value) const {
        SetProperty("enabled", value);
    }
    bool isVisible() const {
        return GetProperty<bool>("isVisible");
    }
    bool forceRenderingOff() const {
        return GetProperty<bool>("forceRenderingOff");
    }
    void set_forceRenderingOff(bool value) const {
        SetProperty("forceRenderingOff", value);
    }
    bool receiveShadows() const {
        return GetProperty<bool>("receiveShadows");
    }
    void set_receiveShadows(bool value) const {
        SetProperty("receiveShadows", value);
    }
    bool allowOcclusionWhenDynamic() const {
        return GetProperty<bool>("allowOcclusionWhenDynamic");
    }
    void set_allowOcclusionWhenDynamic(bool value) const {
        SetProperty("allowOcclusionWhenDynamic", value);
    }
    int sortingLayerID() const {
        return GetProperty<int>("sortingLayerID");
    }
    void set_sortingLayerID(int value) const {
        SetProperty("sortingLayerID", value);
    }
    int sortingOrder() const {
        return GetProperty<int>("sortingOrder");
    }
    void set_sortingOrder(int value) const {
        SetProperty("sortingOrder", value);
    }
    Material material() const {
        return GetProperty<Material>("material");
    }
    void set_material(Material value) const {
        SetProperty("material", value);
    }
    Material sharedMaterial() const {
        return GetProperty<Material>("sharedMaterial");
    }
    void set_sharedMaterial(Material value) const {
        SetProperty("sharedMaterial", value);
    }
    std::vector<Material> materials() const {
        return CallArrayExact<Material>("get_materials", {});
    }
    std::vector<Material> sharedMaterials() const {
        return CallArrayExact<Material>("get_sharedMaterials", {});
    }
    void set_materials(const std::vector<Material> &values) const {
        SetReferenceArrayProperty("materials", values);
    }
    void set_sharedMaterials(const std::vector<Material> &values) const {
        SetReferenceArrayProperty("sharedMaterials", values);
    }
    ShadowCastingMode shadowCastingMode() const {
        return GetProperty<ShadowCastingMode>("shadowCastingMode");
    }
    void set_shadowCastingMode(ShadowCastingMode value) const {
        CallExact<void>("set_shadowCastingMode", {"UnityEngine.Rendering.ShadowCastingMode"}, value);
    }
    MotionVectorGenerationMode motionVectorGenerationMode() const {
        return GetProperty<MotionVectorGenerationMode>("motionVectorGenerationMode");
    }
    void set_motionVectorGenerationMode(MotionVectorGenerationMode value) const {
        CallExact<void>("set_motionVectorGenerationMode", {"UnityEngine.MotionVectorGenerationMode"}, value);
    }
    LightProbeUsage lightProbeUsage() const {
        return GetProperty<LightProbeUsage>("lightProbeUsage");
    }
    void set_lightProbeUsage(LightProbeUsage value) const {
        CallExact<void>("set_lightProbeUsage", {"UnityEngine.Rendering.LightProbeUsage"}, value);
    }
    ReflectionProbeUsage reflectionProbeUsage() const {
        return GetProperty<ReflectionProbeUsage>("reflectionProbeUsage");
    }
    void set_reflectionProbeUsage(ReflectionProbeUsage value) const {
        CallExact<void>("set_reflectionProbeUsage", {"UnityEngine.Rendering.ReflectionProbeUsage"}, value);
    }
};
struct SkinnedMeshRenderer : Renderer {
    SkinnedMeshRenderer() = default;
    explicit SkinnedMeshRenderer(void *h) : Renderer(h) {
    }
    static constexpr TypeRef unity_type() {
        return SkinnedMeshRendererType;
    }
    Mesh sharedMesh() const {
        return GetProperty<Mesh>("sharedMesh");
    }
    void set_sharedMesh(Mesh value) const {
        SetProperty("sharedMesh", value);
    }
    std::vector<Transform> bones() const {
        return CallArrayExact<Transform>("get_bones", {});
    }
    void set_bones(const std::vector<Transform> &values) const {
        SetReferenceArrayProperty("bones", values);
    }
    Transform rootBone() const {
        return GetProperty<Transform>("rootBone");
    }
    void set_rootBone(Transform value) const {
        SetProperty("rootBone", value);
    }
    int blendShapeCount() const {
        return GetProperty<int>("blendShapeCount");
    }
    float GetBlendShapeWeight(int index) const {
        return CallExact<float>("GetBlendShapeWeight", {"System.Int32"}, index);
    }
    void SetBlendShapeWeight(int index, float value) const {
        CallExact<void>("SetBlendShapeWeight", {"System.Int32", "System.Single"}, index, value);
    }
    SkinQuality quality() const {
        return GetProperty<SkinQuality>("quality");
    }
    void set_quality(SkinQuality value) const {
        CallExact<void>("set_quality", {"UnityEngine.SkinQuality"}, value);
    }
    bool updateWhenOffscreen() const {
        return GetProperty<bool>("updateWhenOffscreen");
    }
    void set_updateWhenOffscreen(bool value) const {
        SetProperty("updateWhenOffscreen", value);
    }
    bool forceMatrixRecalculationPerRender() const {
        return GetProperty<bool>("forceMatrixRecalculationPerRender");
    }
    void set_forceMatrixRecalculationPerRender(bool value) const {
        SetProperty("forceMatrixRecalculationPerRender", value);
    }
    bool skinnedMotionVectors() const {
        return GetProperty<bool>("skinnedMotionVectors");
    }
    void set_skinnedMotionVectors(bool value) const {
        SetProperty("skinnedMotionVectors", value);
    }
    void BakeMesh(Mesh mesh) const {
        CallExact<void>("BakeMesh", {"UnityEngine.Mesh"}, mesh);
    }
};
struct MeshRenderer : Renderer {
    MeshRenderer() = default;
    explicit MeshRenderer(void *h) : Renderer(h) {
    }
    static constexpr TypeRef unity_type() {
        return MeshRendererType;
    }
};
struct MeshFilter : Component {
    MeshFilter() = default;
    explicit MeshFilter(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return MeshFilterType;
    }
    Mesh mesh() const {
        return GetProperty<Mesh>("mesh");
    }
    void set_mesh(Mesh value) const {
        SetProperty("mesh", value);
    }
    Mesh sharedMesh() const {
        return GetProperty<Mesh>("sharedMesh");
    }
    void set_sharedMesh(Mesh value) const {
        SetProperty("sharedMesh", value);
    }
};
struct Collider : Component {
    Collider() = default;
    explicit Collider(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return ColliderType;
    }
    Bounds bounds() const {
        return GetProperty<Bounds>("bounds");
    }
    bool enabled() const {
        return GetProperty<bool>("enabled");
    }
    void set_enabled(bool value) const {
        SetProperty("enabled", value);
    }
};
struct MeshCollider : Collider {
    MeshCollider() = default;
    explicit MeshCollider(void *h) : Collider(h) {
    }
    static constexpr TypeRef unity_type() {
        return MeshColliderType;
    }
    Mesh sharedMesh() const {
        return GetProperty<Mesh>("sharedMesh");
    }
    void set_sharedMesh(Mesh value) const {
        SetProperty("sharedMesh", value);
    }
    bool convex() const {
        return GetProperty<bool>("convex");
    }
    void set_convex(bool value) const {
        SetProperty("convex", value);
    }
};
struct RectTransform : Transform {
    RectTransform() = default;
    explicit RectTransform(void *h) : Transform(h) {
    }
    static constexpr TypeRef unity_type() {
        return RectTransformType;
    }
    Vector2 anchoredPosition() const {
        return GetProperty<Vector2>("anchoredPosition");
    }
    void set_anchoredPosition(Vector2 value) const {
        SetProperty("anchoredPosition", value);
    }
    Vector3 anchoredPosition3D() const {
        return GetProperty<Vector3>("anchoredPosition3D");
    }
    void set_anchoredPosition3D(Vector3 value) const {
        SetProperty("anchoredPosition3D", value);
    }
    Vector2 anchorMin() const {
        return GetProperty<Vector2>("anchorMin");
    }
    void set_anchorMin(Vector2 value) const {
        SetProperty("anchorMin", value);
    }
    Vector2 anchorMax() const {
        return GetProperty<Vector2>("anchorMax");
    }
    void set_anchorMax(Vector2 value) const {
        SetProperty("anchorMax", value);
    }
    Vector2 pivot() const {
        return GetProperty<Vector2>("pivot");
    }
    void set_pivot(Vector2 value) const {
        SetProperty("pivot", value);
    }
    Vector2 sizeDelta() const {
        return GetProperty<Vector2>("sizeDelta");
    }
    void set_sizeDelta(Vector2 value) const {
        SetProperty("sizeDelta", value);
    }
    Vector2 offsetMin() const {
        return GetProperty<Vector2>("offsetMin");
    }
    void set_offsetMin(Vector2 value) const {
        SetProperty("offsetMin", value);
    }
    Vector2 offsetMax() const {
        return GetProperty<Vector2>("offsetMax");
    }
    void set_offsetMax(Vector2 value) const {
        SetProperty("offsetMax", value);
    }
    Rect rect() const {
        return GetProperty<Rect>("rect");
    }
    void SetInsetAndSizeFromParentEdge(RectTransformEdge edge, float inset, float size) const {
        CallExact<void>("SetInsetAndSizeFromParentEdge",
                        {"UnityEngine.RectTransform+Edge", "System.Single", "System.Single"}, edge, inset, size);
    }
    void SetSizeWithCurrentAnchors(RectTransformAxis axis, float size) const {
        CallExact<void>("SetSizeWithCurrentAnchors", {"UnityEngine.RectTransform+Axis", "System.Single"}, axis, size);
    }
};
struct Rigidbody : Component {
    Rigidbody() = default;
    explicit Rigidbody(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return RigidbodyType;
    }
    Vector3 velocity() const {
        return GetProperty<Vector3>("velocity");
    }
    void set_velocity(Vector3 value) const {
        SetProperty("velocity", value);
    }
    float angularVelocity() const {
        return GetProperty<float>("angularVelocity");
    }
};
struct Rigidbody2D : Component {
    Rigidbody2D() = default;
    explicit Rigidbody2D(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return Rigidbody2DType;
    }
    Vector2 velocity() const {
        return GetProperty<Vector2>("velocity");
    }
    void set_velocity(Vector2 value) const {
        SetProperty("velocity", value);
    }
    float angularVelocity() const {
        return GetProperty<float>("angularVelocity");
    }
};
struct AudioSource : Behaviour {
    AudioSource() = default;
    explicit AudioSource(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return AudioSourceType;
    }
    Object clip() const {
        return GetProperty<Object>("clip");
    }
    void set_clip(Object value) const {
        SetProperty("clip", value);
    }
    float volume() const {
        return GetProperty<float>("volume");
    }
    void set_volume(float value) const {
        SetProperty("volume", value);
    }
    float pitch() const {
        return GetProperty<float>("pitch");
    }
    void set_pitch(float value) const {
        SetProperty("pitch", value);
    }
    float spatialBlend() const {
        return GetProperty<float>("spatialBlend");
    }
    void set_spatialBlend(float value) const {
        SetProperty("spatialBlend", value);
    }
    float time() const {
        return GetProperty<float>("time");
    }
    void set_time(float value) const {
        SetProperty("time", value);
    }
    bool loop() const {
        return GetProperty<bool>("loop");
    }
    void set_loop(bool value) const {
        SetProperty("loop", value);
    }
    bool mute() const {
        return GetProperty<bool>("mute");
    }
    void set_mute(bool value) const {
        SetProperty("mute", value);
    }
    bool playOnAwake() const {
        return GetProperty<bool>("playOnAwake");
    }
    void set_playOnAwake(bool value) const {
        SetProperty("playOnAwake", value);
    }
    bool isPlaying() const {
        return GetProperty<bool>("isPlaying");
    }
    void Play() const {
        Call<void>("Play");
    }
    void PlayDelayed(float delaySeconds) const {
        CallExact<void>("PlayDelayed", {"System.Single"}, delaySeconds);
    }
    void PlayOneShot(Object audioClip) const {
        CallExact<void>("PlayOneShot", {"UnityEngine.AudioClip"}, audioClip);
    }
    void PlayOneShot(Object audioClip, float volumeScale) const {
        CallExact<void>("PlayOneShot", {"UnityEngine.AudioClip", "System.Single"}, audioClip, volumeScale);
    }
    void Pause() const {
        Call<void>("Pause");
    }
    void UnPause() const {
        Call<void>("UnPause");
    }
    void Stop() const {
        Call<void>("Stop");
    }
};
struct Animator : Behaviour {
    Animator() = default;
    explicit Animator(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return AnimatorType;
    }
    float speed() const {
        return GetProperty<float>("speed");
    }
    void set_speed(float value) const {
        SetProperty("speed", value);
    }
    bool applyRootMotion() const {
        return GetProperty<bool>("applyRootMotion");
    }
    void set_applyRootMotion(bool value) const {
        SetProperty("applyRootMotion", value);
    }
    Vector3 deltaPosition() const {
        return GetProperty<Vector3>("deltaPosition");
    }
    Quaternion deltaRotation() const {
        return GetProperty<Quaternion>("deltaRotation");
    }
    bool isHuman() const {
        return GetProperty<bool>("isHuman");
    }
    bool hasRootMotion() const {
        return GetProperty<bool>("hasRootMotion");
    }
    bool isInitialized() const {
        return GetProperty<bool>("isInitialized");
    }
    int layerCount() const {
        return GetProperty<int>("layerCount");
    }
    AnimatorCullingMode cullingMode() const {
        return GetProperty<AnimatorCullingMode>("cullingMode");
    }
    void set_cullingMode(AnimatorCullingMode value) const {
        CallExact<void>("set_cullingMode", {"UnityEngine.AnimatorCullingMode"}, value);
    }
    AnimatorUpdateMode updateMode() const {
        return GetProperty<AnimatorUpdateMode>("updateMode");
    }
    void set_updateMode(AnimatorUpdateMode value) const {
        CallExact<void>("set_updateMode", {"UnityEngine.AnimatorUpdateMode"}, value);
    }
    bool fireEvents() const {
        return GetProperty<bool>("fireEvents");
    }
    void set_fireEvents(bool value) const {
        SetProperty("fireEvents", value);
    }
    Object avatar() const {
        return GetProperty<Object>("avatar");
    }
    void set_avatar(Object value) const {
        SetProperty("avatar", value);
    }
    Object runtimeAnimatorController() const {
        return GetProperty<Object>("runtimeAnimatorController");
    }
    void set_runtimeAnimatorController(Object value) const {
        SetProperty("runtimeAnimatorController", value);
    }
    float GetFloat(std::string_view name) const {
        return CallExact<float>("GetFloat", {"System.String"}, name);
    }
    float GetFloat(int id) const {
        return CallExact<float>("GetFloat", {"System.Int32"}, id);
    }
    void SetFloat(std::string_view name, float value) const {
        CallExact<void>("SetFloat", {"System.String", "System.Single"}, name, value);
    }
    void SetFloat(int id, float value) const {
        CallExact<void>("SetFloat", {"System.Int32", "System.Single"}, id, value);
    }
    int GetInteger(std::string_view name) const {
        return CallExact<int>("GetInteger", {"System.String"}, name);
    }
    int GetInteger(int id) const {
        return CallExact<int>("GetInteger", {"System.Int32"}, id);
    }
    void SetInteger(std::string_view name, int value) const {
        CallExact<void>("SetInteger", {"System.String", "System.Int32"}, name, value);
    }
    void SetInteger(int id, int value) const {
        CallExact<void>("SetInteger", {"System.Int32", "System.Int32"}, id, value);
    }
    bool GetBool(std::string_view name) const {
        return CallExact<bool>("GetBool", {"System.String"}, name);
    }
    bool GetBool(int id) const {
        return CallExact<bool>("GetBool", {"System.Int32"}, id);
    }
    void SetBool(std::string_view name, bool value) const {
        CallExact<void>("SetBool", {"System.String", "System.Boolean"}, name, value);
    }
    void SetBool(int id, bool value) const {
        CallExact<void>("SetBool", {"System.Int32", "System.Boolean"}, id, value);
    }
    void SetTrigger(std::string_view name) const {
        CallExact<void>("SetTrigger", {"System.String"}, name);
    }
    void SetTrigger(int id) const {
        CallExact<void>("SetTrigger", {"System.Int32"}, id);
    }
    void ResetTrigger(std::string_view name) const {
        CallExact<void>("ResetTrigger", {"System.String"}, name);
    }
    void ResetTrigger(int id) const {
        CallExact<void>("ResetTrigger", {"System.Int32"}, id);
    }
    int GetLayerIndex(std::string_view name) const {
        return CallExact<int>("GetLayerIndex", {"System.String"}, name);
    }
    std::string GetLayerName(int index) const {
        return CallExact<std::string>("GetLayerName", {"System.Int32"}, index);
    }
    float GetLayerWeight(int index) const {
        return CallExact<float>("GetLayerWeight", {"System.Int32"}, index);
    }
    void SetLayerWeight(int index, float value) const {
        CallExact<void>("SetLayerWeight", {"System.Int32", "System.Single"}, index, value);
    }
    void Play(std::string_view stateName, int layer = -1,
              float normalizedTime = float(-std::numeric_limits<float>::infinity())) const {
        CallExact<void>("Play", {"System.String", "System.Int32", "System.Single"}, stateName, layer, normalizedTime);
    }
    void Play(int stateHash, int layer = -1,
              float normalizedTime = float(-std::numeric_limits<float>::infinity())) const {
        CallExact<void>("Play", {"System.Int32", "System.Int32", "System.Single"}, stateHash, layer, normalizedTime);
    }
    void CrossFade(std::string_view stateName, float transitionDuration, int layer = -1,
                   float normalizedTime = float(-std::numeric_limits<float>::infinity())) const {
        CallExact<void>("CrossFade", {"System.String", "System.Single", "System.Int32", "System.Single"}, stateName,
                        transitionDuration, layer, normalizedTime);
    }
    void CrossFade(int stateHash, float transitionDuration, int layer = -1,
                   float normalizedTime = float(-std::numeric_limits<float>::infinity())) const {
        CallExact<void>("CrossFade", {"System.Int32", "System.Single", "System.Int32", "System.Single"}, stateHash,
                        transitionDuration, layer, normalizedTime);
    }
    static int StringToHash(std::string_view name) {
        return detail::InvokeStatic<int>(AnimatorType, "StringToHash", name);
    }
    void Update(float deltaTime) const {
        CallExact<void>("Update", {"System.Single"}, deltaTime);
    }
    void Rebind() const {
        Call<void>("Rebind");
    }
};
struct Canvas : Behaviour {
    Canvas() = default;
    explicit Canvas(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return CanvasType;
    }
    static void ForceUpdateCanvases() {
        detail::InvokeStatic<void>(CanvasType, "ForceUpdateCanvases");
    }
    CanvasRenderMode renderMode() const {
        return GetProperty<CanvasRenderMode>("renderMode");
    }
    void set_renderMode(CanvasRenderMode value) const {
        SetProperty("renderMode", value);
    }
    Camera worldCamera() const {
        return GetProperty<Camera>("worldCamera");
    }
    void set_worldCamera(Camera value) const {
        SetProperty("worldCamera", value);
    }
    float planeDistance() const {
        return GetProperty<float>("planeDistance");
    }
    void set_planeDistance(float value) const {
        SetProperty("planeDistance", value);
    }
    bool pixelPerfect() const {
        return GetProperty<bool>("pixelPerfect");
    }
    void set_pixelPerfect(bool value) const {
        SetProperty("pixelPerfect", value);
    }
    float scaleFactor() const {
        return GetProperty<float>("scaleFactor");
    }
    void set_scaleFactor(float value) const {
        SetProperty("scaleFactor", value);
    }
    int sortingOrder() const {
        return GetProperty<int>("sortingOrder");
    }
    void set_sortingOrder(int value) const {
        SetProperty("sortingOrder", value);
    }
    bool overrideSorting() const {
        return GetProperty<bool>("overrideSorting");
    }
    void set_overrideSorting(bool value) const {
        SetProperty("overrideSorting", value);
    }
    int targetDisplay() const {
        return GetProperty<int>("targetDisplay");
    }
    void set_targetDisplay(int value) const {
        SetProperty("targetDisplay", value);
    }
};
struct CanvasRenderer : Component {
    CanvasRenderer() = default;
    explicit CanvasRenderer(void *h) : Component(h) {
    }
    static constexpr TypeRef unity_type() {
        return CanvasRendererType;
    }
    bool cull() const {
        return GetProperty<bool>("cull");
    }
    void set_cull(bool value) const {
        SetProperty("cull", value);
    }
    float GetAlpha() const {
        return Call<float>("GetAlpha");
    }
    void SetAlpha(float value) const {
        CallExact<void>("SetAlpha", {"System.Single"}, value);
    }
};
struct GraphicRaycaster : Behaviour {
    GraphicRaycaster() = default;
    explicit GraphicRaycaster(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return GraphicRaycasterType;
    }
    bool ignoreReversedGraphics() const {
        return GetProperty<bool>("ignoreReversedGraphics");
    }
    void set_ignoreReversedGraphics(bool value) const {
        SetProperty("ignoreReversedGraphics", value);
    }
    GraphicRaycasterBlockingObjects blockingObjects() const {
        return GetProperty<GraphicRaycasterBlockingObjects>("blockingObjects");
    }
    void set_blockingObjects(GraphicRaycasterBlockingObjects value) const {
        SetProperty("blockingObjects", value);
    }
    int blockingMask() const {
        return GetProperty<int>("blockingMask");
    }
    void set_blockingMask(int value) const {
        SetProperty("blockingMask", value);
    }
};
struct Sprite : Object {
    Sprite() = default;
    explicit Sprite(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return SpriteType;
    }
};
struct Graphic : Behaviour {
    Graphic() = default;
    explicit Graphic(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return GraphicType;
    }
    Color color() const {
        return GetProperty<Color>("color");
    }
    void set_color(Color value) const {
        SetProperty("color", value);
    }
    Material material() const {
        return GetProperty<Material>("material");
    }
    void set_material(Material value) const {
        SetProperty("material", value);
    }
    bool raycastTarget() const {
        return GetProperty<bool>("raycastTarget");
    }
    void set_raycastTarget(bool value) const {
        SetProperty("raycastTarget", value);
    }
};
struct Image : Graphic {
    Image() = default;
    explicit Image(void *h) : Graphic(h) {
    }
    static constexpr TypeRef unity_type() {
        return ImageTypeRef;
    }
    Sprite sprite() const {
        return GetProperty<Sprite>("sprite");
    }
    void set_sprite(Sprite value) const {
        SetProperty("sprite", value);
    }
    Sprite overrideSprite() const {
        return GetProperty<Sprite>("overrideSprite");
    }
    void set_overrideSprite(Sprite value) const {
        SetProperty("overrideSprite", value);
    }
    ImageType type() const {
        return GetProperty<ImageType>("type");
    }
    void set_type(ImageType value) const {
        SetProperty("type", value);
    }
    bool preserveAspect() const {
        return GetProperty<bool>("preserveAspect");
    }
    void set_preserveAspect(bool value) const {
        SetProperty("preserveAspect", value);
    }
    float fillAmount() const {
        return GetProperty<float>("fillAmount");
    }
    void set_fillAmount(float value) const {
        SetProperty("fillAmount", value);
    }
    ImageFillMethod fillMethod() const {
        return GetProperty<ImageFillMethod>("fillMethod");
    }
    void set_fillMethod(ImageFillMethod value) const {
        SetProperty("fillMethod", value);
    }
    int fillOrigin() const {
        return GetProperty<int>("fillOrigin");
    }
    void set_fillOrigin(int value) const {
        SetProperty("fillOrigin", value);
    }
    bool fillClockwise() const {
        return GetProperty<bool>("fillClockwise");
    }
    void set_fillClockwise(bool value) const {
        SetProperty("fillClockwise", value);
    }
};
struct Text : Graphic {
    Text() = default;
    explicit Text(void *h) : Graphic(h) {
    }
    static constexpr TypeRef unity_type() {
        return TextType;
    }
    std::string text() const {
        return GetProperty<std::string>("text");
    }
    void set_text(std::string_view value) const {
        SetProperty("text", value);
    }
    Object font() const {
        return GetProperty<Object>("font");
    }
    void set_font(Object value) const {
        SetProperty("font", value);
    }
    int fontSize() const {
        return GetProperty<int>("fontSize");
    }
    void set_fontSize(int value) const {
        SetProperty("fontSize", value);
    }
    FontStyle fontStyle() const {
        return GetProperty<FontStyle>("fontStyle");
    }
    void set_fontStyle(FontStyle value) const {
        SetProperty("fontStyle", value);
    }
    TextAnchor alignment() const {
        return GetProperty<TextAnchor>("alignment");
    }
    void set_alignment(TextAnchor value) const {
        SetProperty("alignment", value);
    }
    bool supportRichText() const {
        return GetProperty<bool>("supportRichText");
    }
    void set_supportRichText(bool value) const {
        SetProperty("supportRichText", value);
    }
    float lineSpacing() const {
        return GetProperty<float>("lineSpacing");
    }
    void set_lineSpacing(float value) const {
        SetProperty("lineSpacing", value);
    }
    bool resizeTextForBestFit() const {
        return GetProperty<bool>("resizeTextForBestFit");
    }
    void set_resizeTextForBestFit(bool value) const {
        SetProperty("resizeTextForBestFit", value);
    }
};
struct Selectable : Behaviour {
    Selectable() = default;
    explicit Selectable(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return SelectableType;
    }
    bool interactable() const {
        return GetProperty<bool>("interactable");
    }
    void set_interactable(bool value) const {
        SetProperty("interactable", value);
    }
    SelectableTransition transition() const {
        return GetProperty<SelectableTransition>("transition");
    }
    void set_transition(SelectableTransition value) const {
        SetProperty("transition", value);
    }
    Graphic targetGraphic() const {
        return GetProperty<Graphic>("targetGraphic");
    }
    void set_targetGraphic(Graphic value) const {
        SetProperty("targetGraphic", value);
    }
    bool IsInteractable() const {
        return Call<bool>("IsInteractable");
    }
    void Select() const {
        Call<void>("Select");
    }
};
struct Button : Selectable {
    Button() = default;
    explicit Button(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return ButtonType;
    }
    Image image() const {
        return GetProperty<Image>("image");
    }
    Object onClick() const {
        return GetProperty<Object>("onClick");
    }
    void Click() const {
        onClick().Call<void>("Invoke");
    }
};
struct RawImage : Graphic {
    RawImage() = default;
    explicit RawImage(void *h) : Graphic(h) {
    }
    static constexpr TypeRef unity_type() {
        return RawImageType;
    }
    Texture texture() const {
        return GetProperty<Texture>("texture");
    }
    void set_texture(Texture value) const {
        SetProperty("texture", value);
    }
    Rect uvRect() const {
        return GetProperty<Rect>("uvRect");
    }
    void set_uvRect(Rect value) const {
        SetProperty("uvRect", value);
    }
};
struct TextMeshProUGUI : Behaviour {
    TextMeshProUGUI() = default;
    explicit TextMeshProUGUI(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return TextMeshProUGUIType;
    }
    std::string text() const {
        return GetProperty<std::string>("text");
    }
    void set_text(std::string_view value) const {
        SetProperty("text", value);
    }
    Color color() const {
        return GetProperty<Color>("color");
    }
    void set_color(Color value) const {
        SetProperty("color", value);
    }
    float fontSize() const {
        return GetProperty<float>("fontSize");
    }
    void set_fontSize(float value) const {
        SetProperty("fontSize", value);
    }
    TmpFontStyles fontStyle() const {
        return GetProperty<TmpFontStyles>("fontStyle");
    }
    void set_fontStyle(TmpFontStyles value) const {
        SetProperty("fontStyle", value);
    }
    int alignment() const {
        return GetProperty<int>("alignment");
    }
    void set_alignment(int value) const {
        SetProperty("alignment", value);
    }
    bool enableWordWrapping() const {
        return GetProperty<bool>("enableWordWrapping");
    }
    void set_enableWordWrapping(bool value) const {
        SetProperty("enableWordWrapping", value);
    }
    bool richText() const {
        return GetProperty<bool>("richText");
    }
    void set_richText(bool value) const {
        SetProperty("richText", value);
    }
};
struct Toggle : Selectable {
    Toggle() = default;
    explicit Toggle(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return ToggleType;
    }
    bool isOn() const {
        return GetProperty<bool>("isOn");
    }
    void set_isOn(bool value) const {
        SetProperty("isOn", value);
    }
    Graphic graphic() const {
        return GetProperty<Graphic>("graphic");
    }
    void set_graphic(Graphic value) const {
        SetProperty("graphic", value);
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    void SetIsOnWithoutNotify(bool value) const {
        CallExact<void>("SetIsOnWithoutNotify", {"System.Boolean"}, value);
    }
};
struct Slider : Selectable {
    Slider() = default;
    explicit Slider(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return SliderType;
    }
    float value() const {
        return GetProperty<float>("value");
    }
    void set_value(float value) const {
        SetProperty("value", value);
    }
    float minValue() const {
        return GetProperty<float>("minValue");
    }
    void set_minValue(float value) const {
        SetProperty("minValue", value);
    }
    float maxValue() const {
        return GetProperty<float>("maxValue");
    }
    void set_maxValue(float value) const {
        SetProperty("maxValue", value);
    }
    bool wholeNumbers() const {
        return GetProperty<bool>("wholeNumbers");
    }
    void set_wholeNumbers(bool value) const {
        SetProperty("wholeNumbers", value);
    }
    SliderDirection direction() const {
        return GetProperty<SliderDirection>("direction");
    }
    void set_direction(SliderDirection value) const {
        SetProperty("direction", value);
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    void SetValueWithoutNotify(float value) const {
        CallExact<void>("SetValueWithoutNotify", {"System.Single"}, value);
    }
};
struct Scrollbar : Selectable {
    Scrollbar() = default;
    explicit Scrollbar(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return ScrollbarType;
    }
    float value() const {
        return GetProperty<float>("value");
    }
    void set_value(float value) const {
        SetProperty("value", value);
    }
    float size() const {
        return GetProperty<float>("size");
    }
    void set_size(float value) const {
        SetProperty("size", value);
    }
    int numberOfSteps() const {
        return GetProperty<int>("numberOfSteps");
    }
    void set_numberOfSteps(int value) const {
        SetProperty("numberOfSteps", value);
    }
    ScrollbarDirection direction() const {
        return GetProperty<ScrollbarDirection>("direction");
    }
    void set_direction(ScrollbarDirection value) const {
        SetProperty("direction", value);
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    void SetValueWithoutNotify(float value) const {
        CallExact<void>("SetValueWithoutNotify", {"System.Single"}, value);
    }
};
struct Dropdown : Selectable {
    Dropdown() = default;
    explicit Dropdown(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return DropdownType;
    }
    int value() const {
        return GetProperty<int>("value");
    }
    void set_value(int value) const {
        SetProperty("value", value);
    }
    Text captionText() const {
        return GetProperty<Text>("captionText");
    }
    Image captionImage() const {
        return GetProperty<Image>("captionImage");
    }
    Text itemText() const {
        return GetProperty<Text>("itemText");
    }
    Image itemImage() const {
        return GetProperty<Image>("itemImage");
    }
    RectTransform templateTransform() const {
        return GetProperty<RectTransform>("template");
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    void SetValueWithoutNotify(int value) const {
        CallExact<void>("SetValueWithoutNotify", {"System.Int32"}, value);
    }
    void ClearOptions() const {
        Call<void>("ClearOptions");
    }
    void RefreshShownValue() const {
        Call<void>("RefreshShownValue");
    }
    void Show() const {
        Call<void>("Show");
    }
    void Hide() const {
        Call<void>("Hide");
    }
};
struct InputField : Selectable {
    InputField() = default;
    explicit InputField(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return InputFieldType;
    }
    std::string text() const {
        return GetProperty<std::string>("text");
    }
    void set_text(std::string_view value) const {
        SetProperty("text", value);
    }
    int characterLimit() const {
        return GetProperty<int>("characterLimit");
    }
    void set_characterLimit(int value) const {
        SetProperty("characterLimit", value);
    }
    InputFieldContentType contentType() const {
        return GetProperty<InputFieldContentType>("contentType");
    }
    void set_contentType(InputFieldContentType value) const {
        SetProperty("contentType", value);
    }
    InputFieldLineType lineType() const {
        return GetProperty<InputFieldLineType>("lineType");
    }
    void set_lineType(InputFieldLineType value) const {
        SetProperty("lineType", value);
    }
    bool readOnly() const {
        return GetProperty<bool>("readOnly");
    }
    void set_readOnly(bool value) const {
        SetProperty("readOnly", value);
    }
    Graphic placeholder() const {
        return GetProperty<Graphic>("placeholder");
    }
    Text textComponent() const {
        return GetProperty<Text>("textComponent");
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    Object onEndEdit() const {
        return GetProperty<Object>("onEndEdit");
    }
    void SetTextWithoutNotify(std::string_view value) const {
        CallExact<void>("SetTextWithoutNotify", {"System.String"}, value);
    }
    void ActivateInputField() const {
        Call<void>("ActivateInputField");
    }
    void DeactivateInputField() const {
        Call<void>("DeactivateInputField");
    }
    void SelectAll() const {
        Call<void>("SelectAll");
    }
};
struct TmpInputField : Selectable {
    TmpInputField() = default;
    explicit TmpInputField(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return TmpInputFieldType;
    }
    std::string text() const {
        return GetProperty<std::string>("text");
    }
    void set_text(std::string_view value) const {
        SetProperty("text", value);
    }
    int characterLimit() const {
        return GetProperty<int>("characterLimit");
    }
    void set_characterLimit(int value) const {
        SetProperty("characterLimit", value);
    }
    TmpInputFieldContentType contentType() const {
        return GetProperty<TmpInputFieldContentType>("contentType");
    }
    void set_contentType(TmpInputFieldContentType value) const {
        SetProperty("contentType", value);
    }
    TmpInputFieldLineType lineType() const {
        return GetProperty<TmpInputFieldLineType>("lineType");
    }
    void set_lineType(TmpInputFieldLineType value) const {
        SetProperty("lineType", value);
    }
    bool readOnly() const {
        return GetProperty<bool>("readOnly");
    }
    void set_readOnly(bool value) const {
        SetProperty("readOnly", value);
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    Object onEndEdit() const {
        return GetProperty<Object>("onEndEdit");
    }
    void SetTextWithoutNotify(std::string_view value) const {
        CallExact<void>("SetTextWithoutNotify", {"System.String"}, value);
    }
    void ActivateInputField() const {
        Call<void>("ActivateInputField");
    }
    void DeactivateInputField() const {
        Call<void>("DeactivateInputField");
    }
    void SelectAll() const {
        Call<void>("SelectAll");
    }
};
struct TmpDropdown : Selectable {
    TmpDropdown() = default;
    explicit TmpDropdown(void *h) : Selectable(h) {
    }
    static constexpr TypeRef unity_type() {
        return TmpDropdownType;
    }
    int value() const {
        return GetProperty<int>("value");
    }
    void set_value(int value) const {
        SetProperty("value", value);
    }
    Object onValueChanged() const {
        return GetProperty<Object>("onValueChanged");
    }
    void SetValueWithoutNotify(int value) const {
        CallExact<void>("SetValueWithoutNotify", {"System.Int32"}, value);
    }
    void Show() const {
        Call<void>("Show");
    }
    void Hide() const {
        Call<void>("Hide");
    }
};
struct CanvasGroup : Behaviour {
    CanvasGroup() = default;
    explicit CanvasGroup(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return CanvasGroupType;
    }
    float alpha() const {
        return GetProperty<float>("alpha");
    }
    void set_alpha(float value) const {
        SetProperty("alpha", value);
    }
    bool interactable() const {
        return GetProperty<bool>("interactable");
    }
    void set_interactable(bool value) const {
        SetProperty("interactable", value);
    }
    bool blocksRaycasts() const {
        return GetProperty<bool>("blocksRaycasts");
    }
    void set_blocksRaycasts(bool value) const {
        SetProperty("blocksRaycasts", value);
    }
    bool ignoreParentGroups() const {
        return GetProperty<bool>("ignoreParentGroups");
    }
    void set_ignoreParentGroups(bool value) const {
        SetProperty("ignoreParentGroups", value);
    }
};
struct CanvasScaler : Behaviour {
    CanvasScaler() = default;
    explicit CanvasScaler(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return CanvasScalerType;
    }
    CanvasScaleMode uiScaleMode() const {
        return GetProperty<CanvasScaleMode>("uiScaleMode");
    }
    void set_uiScaleMode(CanvasScaleMode value) const {
        SetProperty("uiScaleMode", value);
    }
    Vector2 referenceResolution() const {
        return GetProperty<Vector2>("referenceResolution");
    }
    void set_referenceResolution(Vector2 value) const {
        SetProperty("referenceResolution", value);
    }
    CanvasScreenMatchMode screenMatchMode() const {
        return GetProperty<CanvasScreenMatchMode>("screenMatchMode");
    }
    void set_screenMatchMode(CanvasScreenMatchMode value) const {
        SetProperty("screenMatchMode", value);
    }
    float matchWidthOrHeight() const {
        return GetProperty<float>("matchWidthOrHeight");
    }
    void set_matchWidthOrHeight(float value) const {
        SetProperty("matchWidthOrHeight", value);
    }
    float scaleFactor() const {
        return GetProperty<float>("scaleFactor");
    }
    void set_scaleFactor(float value) const {
        SetProperty("scaleFactor", value);
    }
    float referencePixelsPerUnit() const {
        return GetProperty<float>("referencePixelsPerUnit");
    }
    void set_referencePixelsPerUnit(float value) const {
        SetProperty("referencePixelsPerUnit", value);
    }
};
struct Mask : Behaviour {
    Mask() = default;
    explicit Mask(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return MaskType;
    }
    bool showMaskGraphic() const {
        return GetProperty<bool>("showMaskGraphic");
    }
    void set_showMaskGraphic(bool value) const {
        SetProperty("showMaskGraphic", value);
    }
};
struct RectMask2D : Behaviour {
    RectMask2D() = default;
    explicit RectMask2D(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return RectMask2DType;
    }
    Vector4 padding() const {
        return GetProperty<Vector4>("padding");
    }
    void set_padding(Vector4 value) const {
        SetProperty("padding", value);
    }
    Rect canvasRect() const {
        return GetProperty<Rect>("canvasRect");
    }
    void PerformClipping() const {
        Call<void>("PerformClipping");
    }
};
struct ScrollRect : Behaviour {
    ScrollRect() = default;
    explicit ScrollRect(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return ScrollRectType;
    }
    RectTransform content() const {
        return GetProperty<RectTransform>("content");
    }
    void set_content(RectTransform value) const {
        SetProperty("content", value);
    }
    RectTransform viewport() const {
        return GetProperty<RectTransform>("viewport");
    }
    void set_viewport(RectTransform value) const {
        SetProperty("viewport", value);
    }
    bool horizontal() const {
        return GetProperty<bool>("horizontal");
    }
    void set_horizontal(bool value) const {
        SetProperty("horizontal", value);
    }
    bool vertical() const {
        return GetProperty<bool>("vertical");
    }
    void set_vertical(bool value) const {
        SetProperty("vertical", value);
    }
    ScrollRectMovementType movementType() const {
        return GetProperty<ScrollRectMovementType>("movementType");
    }
    void set_movementType(ScrollRectMovementType value) const {
        SetProperty("movementType", value);
    }
    float elasticity() const {
        return GetProperty<float>("elasticity");
    }
    void set_elasticity(float value) const {
        SetProperty("elasticity", value);
    }
    bool inertia() const {
        return GetProperty<bool>("inertia");
    }
    void set_inertia(bool value) const {
        SetProperty("inertia", value);
    }
    float decelerationRate() const {
        return GetProperty<float>("decelerationRate");
    }
    void set_decelerationRate(float value) const {
        SetProperty("decelerationRate", value);
    }
    float scrollSensitivity() const {
        return GetProperty<float>("scrollSensitivity");
    }
    void set_scrollSensitivity(float value) const {
        SetProperty("scrollSensitivity", value);
    }
    float horizontalNormalizedPosition() const {
        return GetProperty<float>("horizontalNormalizedPosition");
    }
    void set_horizontalNormalizedPosition(float value) const {
        SetProperty("horizontalNormalizedPosition", value);
    }
    float verticalNormalizedPosition() const {
        return GetProperty<float>("verticalNormalizedPosition");
    }
    void set_verticalNormalizedPosition(float value) const {
        SetProperty("verticalNormalizedPosition", value);
    }
    Vector2 normalizedPosition() const {
        return GetProperty<Vector2>("normalizedPosition");
    }
    void set_normalizedPosition(Vector2 value) const {
        SetProperty("normalizedPosition", value);
    }
    void StopMovement() const {
        Call<void>("StopMovement");
    }
};
struct LayoutElement : Behaviour {
    LayoutElement() = default;
    explicit LayoutElement(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return LayoutElementType;
    }
    bool ignoreLayout() const {
        return GetProperty<bool>("ignoreLayout");
    }
    void set_ignoreLayout(bool value) const {
        SetProperty("ignoreLayout", value);
    }
    float minWidth() const {
        return GetProperty<float>("minWidth");
    }
    void set_minWidth(float value) const {
        SetProperty("minWidth", value);
    }
    float minHeight() const {
        return GetProperty<float>("minHeight");
    }
    void set_minHeight(float value) const {
        SetProperty("minHeight", value);
    }
    float preferredWidth() const {
        return GetProperty<float>("preferredWidth");
    }
    void set_preferredWidth(float value) const {
        SetProperty("preferredWidth", value);
    }
    float preferredHeight() const {
        return GetProperty<float>("preferredHeight");
    }
    void set_preferredHeight(float value) const {
        SetProperty("preferredHeight", value);
    }
    float flexibleWidth() const {
        return GetProperty<float>("flexibleWidth");
    }
    void set_flexibleWidth(float value) const {
        SetProperty("flexibleWidth", value);
    }
    float flexibleHeight() const {
        return GetProperty<float>("flexibleHeight");
    }
    void set_flexibleHeight(float value) const {
        SetProperty("flexibleHeight", value);
    }
    int layoutPriority() const {
        return GetProperty<int>("layoutPriority");
    }
    void set_layoutPriority(int value) const {
        SetProperty("layoutPriority", value);
    }
};
struct HorizontalLayoutGroup : Behaviour {
    HorizontalLayoutGroup() = default;
    explicit HorizontalLayoutGroup(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return HorizontalLayoutGroupType;
    }
    float spacing() const {
        return GetProperty<float>("spacing");
    }
    void set_spacing(float value) const {
        SetProperty("spacing", value);
    }
    TextAnchor childAlignment() const {
        return GetProperty<TextAnchor>("childAlignment");
    }
    void set_childAlignment(TextAnchor value) const {
        SetProperty("childAlignment", value);
    }
    bool childControlWidth() const {
        return GetProperty<bool>("childControlWidth");
    }
    void set_childControlWidth(bool value) const {
        SetProperty("childControlWidth", value);
    }
    bool childControlHeight() const {
        return GetProperty<bool>("childControlHeight");
    }
    void set_childControlHeight(bool value) const {
        SetProperty("childControlHeight", value);
    }
    bool childForceExpandWidth() const {
        return GetProperty<bool>("childForceExpandWidth");
    }
    void set_childForceExpandWidth(bool value) const {
        SetProperty("childForceExpandWidth", value);
    }
    bool childForceExpandHeight() const {
        return GetProperty<bool>("childForceExpandHeight");
    }
    void set_childForceExpandHeight(bool value) const {
        SetProperty("childForceExpandHeight", value);
    }
};
struct VerticalLayoutGroup : Behaviour {
    VerticalLayoutGroup() = default;
    explicit VerticalLayoutGroup(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return VerticalLayoutGroupType;
    }
    float spacing() const {
        return GetProperty<float>("spacing");
    }
    void set_spacing(float value) const {
        SetProperty("spacing", value);
    }
    TextAnchor childAlignment() const {
        return GetProperty<TextAnchor>("childAlignment");
    }
    void set_childAlignment(TextAnchor value) const {
        SetProperty("childAlignment", value);
    }
    bool childControlWidth() const {
        return GetProperty<bool>("childControlWidth");
    }
    void set_childControlWidth(bool value) const {
        SetProperty("childControlWidth", value);
    }
    bool childControlHeight() const {
        return GetProperty<bool>("childControlHeight");
    }
    void set_childControlHeight(bool value) const {
        SetProperty("childControlHeight", value);
    }
    bool childForceExpandWidth() const {
        return GetProperty<bool>("childForceExpandWidth");
    }
    void set_childForceExpandWidth(bool value) const {
        SetProperty("childForceExpandWidth", value);
    }
    bool childForceExpandHeight() const {
        return GetProperty<bool>("childForceExpandHeight");
    }
    void set_childForceExpandHeight(bool value) const {
        SetProperty("childForceExpandHeight", value);
    }
};
struct GridLayoutGroup : Behaviour {
    GridLayoutGroup() = default;
    explicit GridLayoutGroup(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return GridLayoutGroupType;
    }
    Vector2 cellSize() const {
        return GetProperty<Vector2>("cellSize");
    }
    void set_cellSize(Vector2 value) const {
        SetProperty("cellSize", value);
    }
    Vector2 spacing() const {
        return GetProperty<Vector2>("spacing");
    }
    void set_spacing(Vector2 value) const {
        SetProperty("spacing", value);
    }
    GridLayoutCorner startCorner() const {
        return GetProperty<GridLayoutCorner>("startCorner");
    }
    void set_startCorner(GridLayoutCorner value) const {
        SetProperty("startCorner", value);
    }
    GridLayoutAxis startAxis() const {
        return GetProperty<GridLayoutAxis>("startAxis");
    }
    void set_startAxis(GridLayoutAxis value) const {
        SetProperty("startAxis", value);
    }
    GridLayoutConstraint constraint() const {
        return GetProperty<GridLayoutConstraint>("constraint");
    }
    void set_constraint(GridLayoutConstraint value) const {
        SetProperty("constraint", value);
    }
    int constraintCount() const {
        return GetProperty<int>("constraintCount");
    }
    void set_constraintCount(int value) const {
        SetProperty("constraintCount", value);
    }
};
struct ContentSizeFitter : Behaviour {
    ContentSizeFitter() = default;
    explicit ContentSizeFitter(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return ContentSizeFitterType;
    }
    ContentSizeFitterFitMode horizontalFit() const {
        return GetProperty<ContentSizeFitterFitMode>("horizontalFit");
    }
    void set_horizontalFit(ContentSizeFitterFitMode value) const {
        SetProperty("horizontalFit", value);
    }
    ContentSizeFitterFitMode verticalFit() const {
        return GetProperty<ContentSizeFitterFitMode>("verticalFit");
    }
    void set_verticalFit(ContentSizeFitterFitMode value) const {
        SetProperty("verticalFit", value);
    }
};
struct AspectRatioFitter : Behaviour {
    AspectRatioFitter() = default;
    explicit AspectRatioFitter(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return AspectRatioFitterType;
    }
    AspectRatioFitterMode aspectMode() const {
        return GetProperty<AspectRatioFitterMode>("aspectMode");
    }
    void set_aspectMode(AspectRatioFitterMode value) const {
        SetProperty("aspectMode", value);
    }
    float aspectRatio() const {
        return GetProperty<float>("aspectRatio");
    }
    void set_aspectRatio(float value) const {
        SetProperty("aspectRatio", value);
    }
};
struct EventSystem : Behaviour {
    EventSystem() = default;
    explicit EventSystem(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return EventSystemType;
    }
    static EventSystem EnsureStandalone();
    static EventSystem current() {
        return detail::InvokeStatic<EventSystem>(EventSystemType, "get_current");
    }
    GameObject firstSelectedGameObject() const;
    void set_firstSelectedGameObject(GameObject value) const;
    GameObject currentSelectedGameObject() const;
    bool sendNavigationEvents() const {
        return GetProperty<bool>("sendNavigationEvents");
    }
    void set_sendNavigationEvents(bool value) const {
        SetProperty("sendNavigationEvents", value);
    }
    int pixelDragThreshold() const {
        return GetProperty<int>("pixelDragThreshold");
    }
    void set_pixelDragThreshold(int value) const {
        SetProperty("pixelDragThreshold", value);
    }
    void SetSelectedGameObject(GameObject value) const;
    bool IsPointerOverGameObject() const {
        return Call<bool>("IsPointerOverGameObject");
    }
};
struct BaseInputModule : Behaviour {
    BaseInputModule() = default;
    explicit BaseInputModule(void *h) : Behaviour(h) {
    }
    static constexpr TypeRef unity_type() {
        return BaseInputModuleType;
    }
    EventSystem eventSystem() const {
        return GetProperty<EventSystem>("eventSystem");
    }
};
struct StandaloneInputModule : BaseInputModule {
    StandaloneInputModule() = default;
    explicit StandaloneInputModule(void *h) : BaseInputModule(h) {
    }
    static constexpr TypeRef unity_type() {
        return StandaloneInputModuleType;
    }
    float inputActionsPerSecond() const {
        return GetProperty<float>("inputActionsPerSecond");
    }
    void set_inputActionsPerSecond(float value) const {
        SetProperty("inputActionsPerSecond", value);
    }
    float repeatDelay() const {
        return GetProperty<float>("repeatDelay");
    }
    void set_repeatDelay(float value) const {
        SetProperty("repeatDelay", value);
    }
};
struct InputSystemUIInputModule : BaseInputModule {
    InputSystemUIInputModule() = default;
    explicit InputSystemUIInputModule(void *h) : BaseInputModule(h) {
    }
    static constexpr TypeRef unity_type() {
        return InputSystemUIInputModuleType;
    }
};
struct LayoutRebuilder {
    static void ForceRebuildLayoutImmediate(RectTransform value) {
        detail::InvokeStatic<void>(TypeRef{"", "UnityEngine.UI", "LayoutRebuilder"}, "ForceRebuildLayoutImmediate",
                                   value);
    }
    static void MarkLayoutForRebuild(RectTransform value) {
        detail::InvokeStatic<void>(TypeRef{"", "UnityEngine.UI", "LayoutRebuilder"}, "MarkLayoutForRebuild", value);
    }
};
struct AssetBundle : Object {
    AssetBundle() = default;
    explicit AssetBundle(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return AssetBundleType;
    }
    // This wrapper is a borrowed managed reference. Unity owns the bundle and
    // its assets; do not use this or assets returned from it after Unload().
    static AssetBundle LoadFromFile(std::string_view path) {
        AssetBundle bundle = detail::InvokeStatic<AssetBundle>(AssetBundleType, "LoadFromFile", path);
        if (!bundle && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadFromFile returned null for path: ") +
                              std::string(path));
        return bundle;
    }
    bool Contains(std::string_view assetName) const {
        return CallExact<bool>("Contains", {"System.String"}, assetName);
    }
    Object LoadAsset(std::string_view assetName) const {
        Object asset = CallExact<Object>("LoadAsset", {"System.String"}, assetName);
        if (!asset && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadAsset returned null for asset: ") +
                              std::string(assetName));
        return asset;
    }
    Object LoadAsset(std::string_view assetName, TypeRef type) const {
        detail::clear_error();
        void *typeObject = type.resolve_type_object();
        if (!typeObject) {
            detail::set_error(std::string("Unity AssetBundle::LoadAsset failed: "
                                          "requested type not found: ") +
                              std::string(type.image) + ":" + std::string(type.namespc) + "." + std::string(type.name));
            detail::append_backend_error();
            return {};
        }
        Object asset =
            CallExact<Object>("LoadAsset", {"System.String", "System.Type"}, assetName, TypeObject{typeObject});
        if (!asset && !detail::fallback_error())
            detail::set_error(std::string("Unity AssetBundle::LoadAsset returned null for asset/type: ") +
                              std::string(assetName) + " / " + std::string(type.namespc) + "." +
                              std::string(type.name));
        return asset;
    }
    template <class T> T LoadAsset(std::string_view assetName) const {
        return T{LoadAsset(assetName, T::unity_type()).handle()};
    }
    std::vector<Object> LoadAllAssets() const {
        return CallArrayExact<Object>("LoadAllAssets", {});
    }
    std::vector<Object> LoadAllAssets(TypeRef type) const {
        detail::clear_error();
        void *typeObject = type.resolve_type_object();
        if (!typeObject) {
            detail::set_error(std::string("Unity AssetBundle::LoadAllAssets failed: "
                                          "requested type not found: ") +
                              std::string(type.image) + ":" + std::string(type.namespc) + "." + std::string(type.name));
            detail::append_backend_error();
            return {};
        }
        return CallArrayExact<Object>("LoadAllAssets", {"System.Type"}, TypeObject{typeObject});
    }
    template <class T> std::vector<T> LoadAllAssets() const {
        const std::vector<Object> assets = LoadAllAssets(T::unity_type());
        std::vector<T> typed;
        typed.reserve(assets.size());
        for (const Object &asset : assets)
            typed.emplace_back(asset.handle());
        return typed;
    }
    std::vector<std::string> GetAllAssetNames() const {
        return CallStringArrayExact("GetAllAssetNames", {});
    }
    std::vector<std::string> GetAllScenePaths() const {
        return CallStringArrayExact("GetAllScenePaths", {});
    }
    void Unload(bool unloadAllLoadedObjects) const {
        CallExact<void>("Unload", {"System.Boolean"}, unloadAllLoadedObjects);
    }
};
struct Scene {
    void *boxed_ = nullptr;
    Scene() = default;
    explicit Scene(void *boxed) : boxed_(boxed) {
    }
    void *handle() const {
        return boxed_;
    }
    explicit operator bool() const {
        return boxed_ != nullptr;
    }
    bool IsValid() const {
        return boxed_ ? Object{boxed_}.Call<bool>("IsValid") : false;
    }
    bool isLoaded() const {
        return boxed_ ? Object{boxed_}.GetProperty<bool>("isLoaded") : false;
    }
    int buildIndex() const {
        return boxed_ ? Object{boxed_}.GetProperty<int>("buildIndex") : -1;
    }
    int handle_value() const {
        return boxed_ ? Object{boxed_}.GetProperty<int>("handle") : 0;
    }
    std::string name() const {
        return boxed_ ? Object{boxed_}.GetProperty<std::string>("name") : std::string{};
    }
    std::string path() const {
        return boxed_ ? Object{boxed_}.GetProperty<std::string>("path") : std::string{};
    }
    bool isDontDestroyOnLoad() const {
        return name() == "DontDestroyOnLoad";
    }
    std::vector<GameObject> GetRootGameObjects() const;
    int rootCount() const;
};
struct GameObject : Object {
    GameObject() = default;
    explicit GameObject(void *h) : Object(h) {
    }
    static constexpr TypeRef unity_type() {
        return GameObjectType;
    }
    static GameObject Find(std::string_view name) {
        return detail::InvokeStatic<GameObject>(GameObjectType, "Find", name);
    }
    static GameObject FindWithTag(std::string_view tag) {
        return detail::InvokeStatic<GameObject>(GameObjectType, "FindWithTag", tag);
    }
    static std::vector<GameObject> FindGameObjectsWithTag(std::string_view tag) {
        return detail::StaticArrayCall<GameObject>(GameObjectType, "FindGameObjectsWithTag", tag);
    }
    static GameObject Create() {
        detail::clear_error();
        auto *k = GameObjectType.resolve_class();
        if (!k) {
            detail::set_error("Unity GameObject::Create failed: GameObject class not found");
            detail::append_backend_error();
            return {};
        }
        void *o = detail::Backend::object_new(k);
        if (!o) {
            detail::set_error("Unity GameObject::Create failed: object allocation failed");
            detail::append_backend_error();
            return {};
        }
        const void *c = detail::Backend::find_method(k, ".ctor", 0);
        if (!c) {
            detail::set_error("Unity GameObject::Create failed: default constructor not found");
            detail::append_backend_error();
            return {};
        }
        void *ex = nullptr;
        if (!detail::Backend::runtime_invoke(c, o, nullptr, nullptr, &ex) || ex) {
            detail::set_error("Unity GameObject::Create failed: constructor threw or "
                              "could not be invoked");
            detail::append_backend_error();
            return {};
        }
        return GameObject{o};
    }
    static GameObject Create(std::string_view name) {
        detail::clear_error();
        auto *k = GameObjectType.resolve_class();
        if (!k) {
            detail::set_error("Unity GameObject::Create failed: GameObject class not found");
            detail::append_backend_error();
            return {};
        }
        void *o = detail::Backend::object_new(k);
        if (!o) {
            detail::set_error("Unity GameObject::Create failed: object allocation failed");
            detail::append_backend_error();
            return {};
        }
        const std::vector<const char *> sig{"System.String"};
        const void *c = detail::Backend::find_method_exact(k, ".ctor", sig);
        if (!c) {
            detail::set_error(std::string("Unity GameObject::Create failed: string "
                                          "constructor not found: ") +
                              detail::signature_text(".ctor", sig));
            detail::append_backend_error();
            return {};
        }
        void *s = detail::Backend::new_string(name);
        if (!s) {
            detail::set_error("Unity GameObject::Create failed: name string allocation failed");
            detail::append_backend_error();
            return {};
        }
        void *args[] = {s};
        void *ex = nullptr;
        if (!detail::Backend::runtime_invoke(c, o, args, nullptr, &ex) || ex) {
            detail::set_error("Unity GameObject::Create failed: constructor threw or "
                              "could not be invoked");
            detail::append_backend_error();
            return {};
        }
        return GameObject{o};
    }
    static GameObject CreateUi(std::string_view name);
    static GameObject New() {
        return Create();
    }
    static GameObject New(std::string_view name) {
        return Create(name);
    }
    Transform transform() const {
        return Call<Transform>("get_transform");
    }
    bool activeSelf() const {
        return GetProperty<bool>("activeSelf");
    }
    bool activeInHierarchy() const {
        return GetProperty<bool>("activeInHierarchy");
    }
    void SetActive(bool value) const {
        CallExact<void>("SetActive", {"System.Boolean"}, value);
    }
    Scene scene() const {
        return Scene{Call<void *>("get_scene")};
    }
    std::string tag() const;
    template <class T> T GetComponent() const {
        return T{GetComponent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name).handle()};
    }
    template <class T> T GetComponent(const char *name) const {
        void *result = CallExact<void *>("GetComponent", {"System.String"}, name);
        return T{result};
    }
    template <class T> T GetComponentInChildren(bool includeInactive = false) const {
        return T{GetComponentInChildren(T::unity_type().image, T::unity_type().namespc, T::unity_type().name,
                                        includeInactive)
                     .handle()};
    }
    template <class T> T GetComponentInParent(bool includeInactive = false) const {
        return T{
            GetComponentInParent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name, includeInactive)
                .handle()};
    }
    Object GetComponent(std::string_view image, std::string_view namespc, std::string_view className) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponent failed: "
                                          "component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return CallExact<Object>("GetComponent", {"System.Type"}, TypeObject{type});
    }
    Object GetComponentInChildren(std::string_view image, std::string_view namespc, std::string_view className,
                                  bool includeInactive = false) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponentInChildren "
                                          "failed: component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return includeInactive ? CallExact<Object>("GetComponentInChildren", {"System.Type", "System.Boolean"},
                                                   TypeObject{type}, includeInactive)
                               : CallExact<Object>("GetComponentInChildren", {"System.Type"}, TypeObject{type});
    }
    Object GetComponentInParent(std::string_view image, std::string_view namespc, std::string_view className,
                                bool includeInactive = false) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponentInParent "
                                          "failed: component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return includeInactive ? CallExact<Object>("GetComponentInParent", {"System.Type", "System.Boolean"},
                                                   TypeObject{type}, includeInactive)
                               : CallExact<Object>("GetComponentInParent", {"System.Type"}, TypeObject{type});
    }
    template <class T = Object> std::vector<T> GetComponents() const {
        const TypeRef type = T::unity_type();
        return GetComponents<T>(type.image, type.namespc, type.name);
    }
    template <class T = Object>
    std::vector<T> GetComponents(std::string_view image, std::string_view namespc, std::string_view className) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponents failed: "
                                          "component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return CallArrayExact<T>("GetComponents", {"System.Type"}, TypeObject{type});
    }
    template <class T = Object> std::vector<T> GetComponentsInChildren(bool includeInactive = false) const {
        const TypeRef type = T::unity_type();
        return GetComponentsInChildren<T>(type.image, type.namespc, type.name, includeInactive);
    }
    template <class T = Object>
    std::vector<T> GetComponentsInChildren(std::string_view image, std::string_view namespc, std::string_view className,
                                           bool includeInactive = false) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponentsInChildren "
                                          "failed: component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return includeInactive ? CallArrayExact<T>("GetComponentsInChildren", {"System.Type", "System.Boolean"},
                                                   TypeObject{type}, includeInactive)
                               : CallArrayExact<T>("GetComponentsInChildren", {"System.Type"}, TypeObject{type});
    }
    template <class T = Object> std::vector<T> GetComponentsInParent(bool includeInactive = false) const {
        const TypeRef type = T::unity_type();
        return GetComponentsInParent<T>(type.image, type.namespc, type.name, includeInactive);
    }
    template <class T = Object>
    std::vector<T> GetComponentsInParent(std::string_view image, std::string_view namespc, std::string_view className,
                                         bool includeInactive = false) const {
        detail::clear_error();
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::GetComponentsInParent "
                                          "failed: component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return includeInactive ? CallArrayExact<T>("GetComponentsInParent", {"System.Type", "System.Boolean"},
                                                   TypeObject{type}, includeInactive)
                               : CallArrayExact<T>("GetComponentsInParent", {"System.Type"}, TypeObject{type});
    }
    template <class T> T AddComponent() const {
        if constexpr (std::is_same_v<T, Transform>)
            return T{};
        else
            return T{AddComponent(T::unity_type().image, T::unity_type().namespc, T::unity_type().name).handle()};
    }
    Object AddComponent(std::string_view image, std::string_view namespc, std::string_view className) const {
        detail::clear_error();
        if (className == "Transform" && (namespc.empty() || namespc == "UnityEngine")) {
            detail::set_error("Unity GameObject::AddComponent failed: Transform is "
                              "owned by GameObject and cannot be added");
            return {};
        }
        void *type = TypeRef{image, namespc, className}.resolve_type_object();
        if (!type) {
            detail::set_error(std::string("Unity GameObject::AddComponent failed: "
                                          "component class not found: ") +
                              std::string(image) + ":" + std::string(namespc) + "." + std::string(className));
            detail::append_backend_error();
            return {};
        }
        return CallExact<Object>("AddComponent", {"System.Type"}, TypeObject{type});
    }
    template <class T> bool HasComponent() const {
        return static_cast<bool>(GetComponent<T>());
    }
    bool HasComponent(std::string_view image, std::string_view namespc, std::string_view className) const {
        return static_cast<bool>(GetComponent(image, namespc, className));
    }
    template <class T> T GetOrAddComponent() const {
        T c = GetComponent<T>();
        return c ? c : AddComponent<T>();
    }
    Object GetOrAddComponent(std::string_view image, std::string_view namespc, std::string_view className) const {
        Object c = GetComponent(image, namespc, className);
        return c ? c : AddComponent(image, namespc, className);
    }
};

inline std::vector<GameObject> Scene::GetRootGameObjects() const {
    return boxed_ ? Object{boxed_}.CallArrayExact<GameObject>("GetRootGameObjects", std::vector<const char *>{})
                  : std::vector<GameObject>{};
}

inline int Scene::rootCount() const {
    return static_cast<int>(GetRootGameObjects().size());
}

struct CanvasRoot {
    GameObject gameObject;
    RectTransform rectTransform;
    Canvas canvas;
    CanvasScaler scaler;
    GraphicRaycaster raycaster;
    explicit operator bool() const {
        return static_cast<bool>(gameObject) && static_cast<bool>(canvas);
    }
};

inline CanvasRoot CreateOverlayCanvas(std::string_view name, bool addRaycaster = true) {
    CanvasRoot root{};
    root.gameObject = GameObject::CreateUi(name);
    if (!root.gameObject)
        return root;
    root.rectTransform = root.gameObject.transform().GetComponent<RectTransform>();
    root.canvas = root.gameObject.AddComponent<Canvas>();
    if (!root.canvas)
        return root;
    root.canvas.set_renderMode(CanvasRenderMode::ScreenSpaceOverlay);
    root.scaler = root.gameObject.AddComponent<CanvasScaler>();
    if (addRaycaster)
        root.raycaster = root.gameObject.AddComponent<GraphicRaycaster>();
    return root;
}

inline EventSystem EventSystem::EnsureStandalone() {
    EventSystem existing = current();
    if (existing)
        return existing;
    GameObject gameObject = GameObject::Create("EventSystem");
    if (!gameObject)
        return {};
    EventSystem eventSystem = gameObject.AddComponent<EventSystem>();
    if (!eventSystem)
        return {};
    if (!gameObject.AddComponent<StandaloneInputModule>())
        return {};
    return eventSystem;
}

inline GameObject EventSystem::firstSelectedGameObject() const {
    return GetProperty<GameObject>("firstSelectedGameObject");
}

inline void EventSystem::set_firstSelectedGameObject(GameObject value) const {
    SetProperty("firstSelectedGameObject", value);
}

inline GameObject EventSystem::currentSelectedGameObject() const {
    return GetProperty<GameObject>("currentSelectedGameObject");
}

inline void EventSystem::SetSelectedGameObject(GameObject value) const {
    CallExact<void>("SetSelectedGameObject", {"UnityEngine.GameObject"}, value);
}

// URK_UNITY_INVOKE_BEGIN
template <> struct detail::is_wrapper<Object> : std::true_type {};
template <> struct detail::is_wrapper<TypeObject> : std::true_type {};
template <> struct detail::is_wrapper<Component> : std::true_type {};
template <> struct detail::is_wrapper<Behaviour> : std::true_type {};
template <> struct detail::is_wrapper<MonoBehaviour> : std::true_type {};
template <> struct detail::is_wrapper<ScriptableObject> : std::true_type {};
template <> struct detail::is_wrapper<GameObject> : std::true_type {};
template <> struct detail::is_wrapper<Transform> : std::true_type {};
template <> struct detail::is_wrapper<Camera> : std::true_type {};
template <> struct detail::is_wrapper<Light> : std::true_type {};
template <> struct detail::is_wrapper<Renderer> : std::true_type {};
template <> struct detail::is_wrapper<SkinnedMeshRenderer> : std::true_type {};
template <> struct detail::is_wrapper<Collider> : std::true_type {};
template <> struct detail::is_wrapper<RectTransform> : std::true_type {};
template <> struct detail::is_wrapper<Rigidbody> : std::true_type {};
template <> struct detail::is_wrapper<Rigidbody2D> : std::true_type {};
template <> struct detail::is_wrapper<AudioSource> : std::true_type {};
template <> struct detail::is_wrapper<Animator> : std::true_type {};
template <> struct detail::is_wrapper<Canvas> : std::true_type {};
template <> struct detail::is_wrapper<Graphic> : std::true_type {};
template <> struct detail::is_wrapper<Image> : std::true_type {};
template <> struct detail::is_wrapper<Text> : std::true_type {};
template <> struct detail::is_wrapper<Button> : std::true_type {};
template <> struct detail::is_wrapper<Mesh> : std::true_type {};
template <> struct detail::is_wrapper<Material> : std::true_type {};
template <> struct detail::is_wrapper<Texture> : std::true_type {};
template <> struct detail::is_wrapper<Sprite> : std::true_type {};
template <> struct detail::is_wrapper<AssetBundle> : std::true_type {};
template <> struct detail::is_wrapper<Scene> : std::true_type {};
template <> struct detail::is_wrapper<CanvasGroup> : std::true_type {};
template <> struct detail::is_wrapper<CanvasScaler> : std::true_type {};
template <> struct detail::is_wrapper<CanvasRenderer> : std::true_type {};
template <> struct detail::is_wrapper<GraphicRaycaster> : std::true_type {};
template <> struct detail::is_wrapper<Selectable> : std::true_type {};
template <> struct detail::is_wrapper<RawImage> : std::true_type {};
template <> struct detail::is_wrapper<TextMeshProUGUI> : std::true_type {};
template <> struct detail::is_wrapper<TmpInputField> : std::true_type {};
template <> struct detail::is_wrapper<TmpDropdown> : std::true_type {};
template <> struct detail::is_wrapper<Toggle> : std::true_type {};
template <> struct detail::is_wrapper<Slider> : std::true_type {};
template <> struct detail::is_wrapper<Scrollbar> : std::true_type {};
template <> struct detail::is_wrapper<Dropdown> : std::true_type {};
template <> struct detail::is_wrapper<InputField> : std::true_type {};
template <> struct detail::is_wrapper<Mask> : std::true_type {};
template <> struct detail::is_wrapper<RectMask2D> : std::true_type {};
template <> struct detail::is_wrapper<ScrollRect> : std::true_type {};
template <> struct detail::is_wrapper<LayoutElement> : std::true_type {};
template <> struct detail::is_wrapper<HorizontalLayoutGroup> : std::true_type {};
template <> struct detail::is_wrapper<VerticalLayoutGroup> : std::true_type {};
template <> struct detail::is_wrapper<GridLayoutGroup> : std::true_type {};
template <> struct detail::is_wrapper<ContentSizeFitter> : std::true_type {};
template <> struct detail::is_wrapper<AspectRatioFitter> : std::true_type {};
template <> struct detail::is_wrapper<EventSystem> : std::true_type {};
template <> struct detail::is_wrapper<BaseInputModule> : std::true_type {};
template <> struct detail::is_wrapper<StandaloneInputModule> : std::true_type {};
template <> struct detail::is_wrapper<InputSystemUIInputModule> : std::true_type {};
template <> struct detail::is_wrapper<MeshRenderer> : std::true_type {};
template <> struct detail::is_wrapper<MeshFilter> : std::true_type {};
template <> struct detail::is_wrapper<MeshCollider> : std::true_type {};
template <> struct detail::is_wrapper<Texture2D> : std::true_type {};
template <> struct detail::is_wrapper<Shader> : std::true_type {};
inline GameObject Component::gameObject() const {
    return Call<GameObject>("get_gameObject");
}
inline Transform Component::transform() const {
    return Call<Transform>("get_transform");
}
template <class T> inline T Component::GetComponent() const {
    return gameObject().GetComponent<T>();
}
template <class T> inline T Component::GetComponent(const char *name) const {
    return gameObject().GetComponent<T>(name);
}
inline Object Component::GetComponent(std::string_view image, std::string_view namespc,
                                      std::string_view className) const {
    return gameObject().GetComponent(image, namespc, className);
}
template <class T> inline T Component::GetComponentInChildren(bool includeInactive) const {
    return gameObject().GetComponentInChildren<T>(includeInactive);
}
inline Object Component::GetComponentInChildren(std::string_view image, std::string_view namespc,
                                                std::string_view className, bool includeInactive) const {
    return gameObject().GetComponentInChildren(image, namespc, className, includeInactive);
}
template <class T> inline T Component::GetComponentInParent(bool includeInactive) const {
    return gameObject().GetComponentInParent<T>(includeInactive);
}
inline Object Component::GetComponentInParent(std::string_view image, std::string_view namespc,
                                              std::string_view className, bool includeInactive) const {
    return gameObject().GetComponentInParent(image, namespc, className, includeInactive);
}
template <class T> inline std::vector<T> Component::GetComponents() const {
    return gameObject().GetComponents<T>();
}
template <class T> inline std::vector<T> Component::GetComponentsInChildren(bool includeInactive) const {
    return gameObject().GetComponentsInChildren<T>(includeInactive);
}
template <class T> inline std::vector<T> Component::GetComponentsInParent(bool includeInactive) const {
    return gameObject().GetComponentsInParent<T>(includeInactive);
}
template <class T> inline T Component::AddComponent() const {
    return gameObject().AddComponent<T>();
}
inline Object Component::AddComponent(std::string_view image, std::string_view namespc,
                                      std::string_view className) const {
    return gameObject().AddComponent(image, namespc, className);
}
template <class T> inline bool Component::HasComponent() const {
    return gameObject().HasComponent<T>();
}
inline bool Component::HasComponent(std::string_view image, std::string_view namespc,
                                    std::string_view className) const {
    return gameObject().HasComponent(image, namespc, className);
}
template <class T> inline T Component::GetOrAddComponent() const {
    return gameObject().GetOrAddComponent<T>();
}
inline Object Component::GetOrAddComponent(std::string_view image, std::string_view namespc,
                                           std::string_view className) const {
    return gameObject().GetOrAddComponent(image, namespc, className);
}
inline std::string Object::runtime_class_name() const {
    return detail::class_display_name(detail::Backend::object_get_class(handle_));
}
inline std::string Object::ToString() const {
    return detail::managed_string_to_utf8(Call<void *>("ToString"));
}
inline std::string Object::name() const {
    return detail::managed_string_to_utf8(Call<void *>("get_name"));
}
inline std::string GameObject::tag() const {
    return detail::managed_string_to_utf8(Call<void *>("get_tag"));
}

namespace detail {
// runtime_invoke expects value-type params as pointers to local storage, but
// managed reference/object/string params as the managed pointer itself.
template <class T> struct Arg {
    T storage;
    void *ptr;
    bool valid;
    explicit Arg(T value) noexcept(std::is_nothrow_move_constructible_v<T>)
        : storage(std::move(value)), ptr(&storage), valid(true) {
    }
    Arg(const Arg &other) noexcept(std::is_nothrow_copy_constructible_v<T>)
        : storage(other.storage), ptr(&storage), valid(other.valid) {
    }
    Arg(Arg &&other) noexcept(std::is_nothrow_move_constructible_v<T>)
        : storage(std::move(other.storage)), ptr(&storage), valid(other.valid) {
    }
    Arg &operator=(const Arg &other) noexcept(std::is_nothrow_copy_assignable_v<T>) {
        if (this != &other) {
            storage = other.storage;
            ptr = &storage;
            valid = other.valid;
        }
        return *this;
    }
    Arg &operator=(Arg &&other) noexcept(std::is_nothrow_move_assignable_v<T>) {
        if (this != &other) {
            storage = std::move(other.storage);
            ptr = &storage;
            valid = other.valid;
        }
        return *this;
    }
};
template <class T>
    requires is_wrapper_v<T>
struct Arg<T> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(T v) : storage(v.handle()), ptr(storage), valid(true) {
    }
};
template <> struct Arg<void *> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(void *v) : storage(v), ptr(storage), valid(true) {
    }
};
template <> struct Arg<const char *> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(const char *v)
        : storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})), ptr(storage),
          valid(storage != nullptr) {
    }
};
template <> struct Arg<char *> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(char *v)
        : storage(Backend::new_string(v ? std::string_view(v) : std::string_view{})), ptr(storage),
          valid(storage != nullptr) {
    }
};
template <std::size_t N> struct Arg<char[N]> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(const char (&v)[N])
        : storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))), ptr(storage),
          valid(storage != nullptr) {
    }
};
template <std::size_t N> struct Arg<const char[N]> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(const char (&v)[N])
        : storage(Backend::new_string(std::string_view(v, N > 0 && v[N - 1] == '\0' ? N - 1 : N))), ptr(storage),
          valid(storage != nullptr) {
    }
};
template <> struct Arg<std::string> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(const std::string &v) : storage(Backend::new_string(v)), ptr(storage), valid(storage != nullptr) {
    }
};
template <> struct Arg<std::string_view> {
    void *storage;
    void *ptr;
    bool valid;
    Arg(std::string_view v) : storage(Backend::new_string(v)), ptr(storage), valid(storage != nullptr) {
    }
};
template <class T> inline std::optional<const char *> parameter_type_name() {
    if constexpr (std::is_array_v<T> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<T>>, char>)
        return "System.String";
    else
        return std::nullopt;
}
template <> inline std::optional<const char *> parameter_type_name<bool>() {
    return "System.Boolean";
}
template <> inline std::optional<const char *> parameter_type_name<int>() {
    return "System.Int32";
}
template <> inline std::optional<const char *> parameter_type_name<unsigned int>() {
    return "System.UInt32";
}
template <> inline std::optional<const char *> parameter_type_name<short>() {
    return "System.Int16";
}
template <> inline std::optional<const char *> parameter_type_name<unsigned short>() {
    return "System.UInt16";
}
template <> inline std::optional<const char *> parameter_type_name<long long>() {
    return "System.Int64";
}
template <> inline std::optional<const char *> parameter_type_name<unsigned long long>() {
    return "System.UInt64";
}
template <> inline std::optional<const char *> parameter_type_name<float>() {
    return "System.Single";
}
template <> inline std::optional<const char *> parameter_type_name<double>() {
    return "System.Double";
}
template <> inline std::optional<const char *> parameter_type_name<void *>() {
    return "System.Object";
}
template <> inline std::optional<const char *> parameter_type_name<std::string_view>() {
    return "System.String";
}
template <> inline std::optional<const char *> parameter_type_name<std::string>() {
    return "System.String";
}
template <> inline std::optional<const char *> parameter_type_name<const char *>() {
    return "System.String";
}
template <> inline std::optional<const char *> parameter_type_name<char *>() {
    return "System.String";
}
template <> inline std::optional<const char *> parameter_type_name<Vector2>() {
    return "UnityEngine.Vector2";
}
template <> inline std::optional<const char *> parameter_type_name<Vector3>() {
    return "UnityEngine.Vector3";
}
template <> inline std::optional<const char *> parameter_type_name<Quaternion>() {
    return "UnityEngine.Quaternion";
}
template <> inline std::optional<const char *> parameter_type_name<Color>() {
    return "UnityEngine.Color";
}
template <> inline std::optional<const char *> parameter_type_name<Rect>() {
    return "UnityEngine.Rect";
}
template <> inline std::optional<const char *> parameter_type_name<Bounds>() {
    return "UnityEngine.Bounds";
}
template <> inline std::optional<const char *> parameter_type_name<Ray>() {
    return "UnityEngine.Ray";
}
template <> inline std::optional<const char *> parameter_type_name<TypeObject>() {
    return "System.Type";
}
template <> inline std::optional<const char *> parameter_type_name<FindObjectsSortMode>() {
    return "UnityEngine.FindObjectsSortMode";
}
template <> inline std::optional<const char *> parameter_type_name<Object>() {
    return "UnityEngine.Object";
}
template <> inline std::optional<const char *> parameter_type_name<Component>() {
    return "UnityEngine.Component";
}
template <> inline std::optional<const char *> parameter_type_name<Behaviour>() {
    return "UnityEngine.Behaviour";
}
template <> inline std::optional<const char *> parameter_type_name<MonoBehaviour>() {
    return "UnityEngine.MonoBehaviour";
}
template <> inline std::optional<const char *> parameter_type_name<ScriptableObject>() {
    return "UnityEngine.ScriptableObject";
}
template <> inline std::optional<const char *> parameter_type_name<GameObject>() {
    return "UnityEngine.GameObject";
}
template <> inline std::optional<const char *> parameter_type_name<Transform>() {
    return "UnityEngine.Transform";
}
template <> inline std::optional<const char *> parameter_type_name<Camera>() {
    return "UnityEngine.Camera";
}
template <> inline std::optional<const char *> parameter_type_name<Light>() {
    return "UnityEngine.Light";
}
template <> inline std::optional<const char *> parameter_type_name<SkinnedMeshRenderer>() {
    return "UnityEngine.SkinnedMeshRenderer";
}
template <> inline std::optional<const char *> parameter_type_name<RectTransform>() {
    return "UnityEngine.RectTransform";
}
template <> inline std::optional<const char *> parameter_type_name<Rigidbody>() {
    return "UnityEngine.Rigidbody";
}
template <> inline std::optional<const char *> parameter_type_name<Rigidbody2D>() {
    return "UnityEngine.Rigidbody2D";
}
template <> inline std::optional<const char *> parameter_type_name<AudioSource>() {
    return "UnityEngine.AudioSource";
}
template <> inline std::optional<const char *> parameter_type_name<Animator>() {
    return "UnityEngine.Animator";
}
template <> inline std::optional<const char *> parameter_type_name<Canvas>() {
    return "UnityEngine.Canvas";
}
template <> inline std::optional<const char *> parameter_type_name<CanvasRenderer>() {
    return "UnityEngine.CanvasRenderer";
}
template <> inline std::optional<const char *> parameter_type_name<Graphic>() {
    return "UnityEngine.UI.Graphic";
}
template <> inline std::optional<const char *> parameter_type_name<GraphicRaycaster>() {
    return "UnityEngine.UI.GraphicRaycaster";
}
template <> inline std::optional<const char *> parameter_type_name<Selectable>() {
    return "UnityEngine.UI.Selectable";
}
template <> inline std::optional<const char *> parameter_type_name<Image>() {
    return "UnityEngine.UI.Image";
}
template <> inline std::optional<const char *> parameter_type_name<Text>() {
    return "UnityEngine.UI.Text";
}
template <> inline std::optional<const char *> parameter_type_name<Button>() {
    return "UnityEngine.UI.Button";
}
template <> inline std::optional<const char *> parameter_type_name<Mesh>() {
    return "UnityEngine.Mesh";
}
template <> inline std::optional<const char *> parameter_type_name<Material>() {
    return "UnityEngine.Material";
}
template <> inline std::optional<const char *> parameter_type_name<Texture>() {
    return "UnityEngine.Texture";
}
template <> inline std::optional<const char *> parameter_type_name<Sprite>() {
    return "UnityEngine.Sprite";
}
template <> inline std::optional<const char *> parameter_type_name<AssetBundle>() {
    return "UnityEngine.AssetBundle";
}
template <> inline std::optional<const char *> parameter_type_name<Renderer>() {
    return "UnityEngine.Renderer";
}
template <> inline std::optional<const char *> parameter_type_name<Collider>() {
    return "UnityEngine.Collider";
}
template <> inline std::optional<const char *> parameter_type_name<CanvasGroup>() {
    return "UnityEngine.CanvasGroup";
}
template <> inline std::optional<const char *> parameter_type_name<CanvasScaler>() {
    return "UnityEngine.UI.CanvasScaler";
}
template <> inline std::optional<const char *> parameter_type_name<RawImage>() {
    return "UnityEngine.UI.RawImage";
}
template <> inline std::optional<const char *> parameter_type_name<TextMeshProUGUI>() {
    return "TMPro.TextMeshProUGUI";
}
template <> inline std::optional<const char *> parameter_type_name<TmpInputField>() {
    return "TMPro.TMP_InputField";
}
template <> inline std::optional<const char *> parameter_type_name<TmpDropdown>() {
    return "TMPro.TMP_Dropdown";
}
template <> inline std::optional<const char *> parameter_type_name<Toggle>() {
    return "UnityEngine.UI.Toggle";
}
template <> inline std::optional<const char *> parameter_type_name<Slider>() {
    return "UnityEngine.UI.Slider";
}
template <> inline std::optional<const char *> parameter_type_name<Scrollbar>() {
    return "UnityEngine.UI.Scrollbar";
}
template <> inline std::optional<const char *> parameter_type_name<Dropdown>() {
    return "UnityEngine.UI.Dropdown";
}
template <> inline std::optional<const char *> parameter_type_name<InputField>() {
    return "UnityEngine.UI.InputField";
}
template <> inline std::optional<const char *> parameter_type_name<Mask>() {
    return "UnityEngine.UI.Mask";
}
template <> inline std::optional<const char *> parameter_type_name<RectMask2D>() {
    return "UnityEngine.UI.RectMask2D";
}
template <> inline std::optional<const char *> parameter_type_name<ScrollRect>() {
    return "UnityEngine.UI.ScrollRect";
}
template <> inline std::optional<const char *> parameter_type_name<LayoutElement>() {
    return "UnityEngine.UI.LayoutElement";
}
template <> inline std::optional<const char *> parameter_type_name<HorizontalLayoutGroup>() {
    return "UnityEngine.UI.HorizontalLayoutGroup";
}
template <> inline std::optional<const char *> parameter_type_name<VerticalLayoutGroup>() {
    return "UnityEngine.UI.VerticalLayoutGroup";
}
template <> inline std::optional<const char *> parameter_type_name<GridLayoutGroup>() {
    return "UnityEngine.UI.GridLayoutGroup";
}
template <> inline std::optional<const char *> parameter_type_name<ContentSizeFitter>() {
    return "UnityEngine.UI.ContentSizeFitter";
}
template <> inline std::optional<const char *> parameter_type_name<AspectRatioFitter>() {
    return "UnityEngine.UI.AspectRatioFitter";
}
template <> inline std::optional<const char *> parameter_type_name<EventSystem>() {
    return "UnityEngine.EventSystems.EventSystem";
}
template <> inline std::optional<const char *> parameter_type_name<BaseInputModule>() {
    return "UnityEngine.EventSystems.BaseInputModule";
}
template <> inline std::optional<const char *> parameter_type_name<StandaloneInputModule>() {
    return "UnityEngine.EventSystems.StandaloneInputModule";
}
template <> inline std::optional<const char *> parameter_type_name<InputSystemUIInputModule>() {
    return "UnityEngine.InputSystem.UI.InputSystemUIInputModule";
}
template <> inline std::optional<const char *> parameter_type_name<MeshRenderer>() {
    return "UnityEngine.MeshRenderer";
}
template <> inline std::optional<const char *> parameter_type_name<MeshFilter>() {
    return "UnityEngine.MeshFilter";
}
template <> inline std::optional<const char *> parameter_type_name<MeshCollider>() {
    return "UnityEngine.MeshCollider";
}
template <> inline std::optional<const char *> parameter_type_name<Texture2D>() {
    return "UnityEngine.Texture2D";
}
template <> inline std::optional<const char *> parameter_type_name<Shader>() {
    return "UnityEngine.Shader";
}
template <class... Args> std::vector<const char *> inferred_parameter_types() {
    std::vector<const char *> names;
    names.reserve(sizeof...(Args));
    bool all = true;
    (
        [&] {
            auto n = parameter_type_name<std::remove_cvref_t<Args>>();
            if (n)
                names.push_back(*n);
            else
                all = false;
        }(),
        ...);
    if (!all)
        names.clear();
    return names;
}
template <class Ret> Ret from_result(void *r) {
    if constexpr (std::is_void_v<Ret>)
        return;
    else if constexpr (std::is_same_v<std::remove_cvref_t<Ret>, std::string>)
        return managed_string_to_utf8(r);
    else if constexpr (is_wrapper_v<Ret>)
        return Ret{r};
    else if constexpr (std::is_pointer_v<Ret>)
        return static_cast<Ret>(r);
    else {
        if (!r) {
            if (!fallback_error())
                set_error("Unity conversion failed: managed return object is null");
            return Ret{};
        }
        void *p = Backend::object_unbox(r);
        if (!p) {
            if (!fallback_error())
                set_error("Unity conversion failed: object_unbox failed for value type "
                          "return");
            append_backend_error();
            return Ret{};
        }
        return *static_cast<Ret *>(p);
    }
}
}

template <class Ret, class... Args> Ret Object::Call(std::string_view methodName, Args &&...args) const {
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::Call failed: target object is null");
        return detail::from_result<Ret>(nullptr);
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::Call failed: object_get_class failed");
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    auto inferred = detail::inferred_parameter_types<Args...>();
    const bool exact = !inferred.empty() || sizeof...(Args) == 0;
    const void *m = exact ? detail::Backend::find_method_exact(k, methodName, inferred)
                          : detail::Backend::find_method(k, methodName, sizeof...(Args));
    if (!m) {
        const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : "";
        detail::set_error(std::string("Unity Object::Call failed: method not found or ambiguous: ") +
                          (exact ? detail::signature_text(methodName, inferred)
                                 : std::string(methodName) + "/" + std::to_string(sizeof...(Args))) +
                          (lookupDetail.empty() ? std::string{} : std::string("; detail: ") + lookupDetail));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
        detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void *, sizeof...(Args)> argv{};
    std::size_t i = 0;
    bool argsValid = true;
    std::apply([&](auto &...a) { ((argsValid = argsValid && a.valid, argv[i++] = a.ptr), ...); }, pack);
    if (!argsValid) {
        detail::set_error(std::string("Unity Object::Call failed: managed string "
                                      "argument allocation failed in ") +
                          std::string(methodName));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    void *result = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(m, handle_, argv.data(), &result, &ex) || ex) {
        detail::set_error(std::string("Unity Object::Call failed: runtime_invoke exception in ") +
                          std::string(methodName));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    return detail::from_result<Ret>(result);
}

template <class Ret, class... Args>
Ret detail::InvokeStatic(TypeRef type, std::string_view methodName, Args &&...args) {
    clear_error();
    const void *k = type.resolve_class();
    if (!k) {
        set_error(std::string("Unity static call failed: class not found for ") + std::string(type.namespc) + "." +
                  std::string(type.name));
        append_backend_error();
        return from_result<Ret>(nullptr);
    }
    auto inferred = inferred_parameter_types<Args...>();
    const bool exact = !inferred.empty() || sizeof...(Args) == 0;
    const void *m = exact ? Backend::find_method_exact(k, methodName, inferred)
                          : Backend::find_method(k, methodName, sizeof...(Args));
    if (!m) {
        const std::string lookupDetail = fallback_error() ? fallback_error() : "";
        set_error(std::string("Unity static call failed: Unity method not found or ambiguous: ") +
                  std::string(type.namespc) + "." + std::string(type.name) + "." +
                  (exact ? signature_text(methodName, inferred)
                         : std::string(methodName) + "/" + std::to_string(sizeof...(Args))) +
                  (lookupDetail.empty() ? std::string{} : std::string("; detail: ") + lookupDetail));
        append_backend_error();
        return from_result<Ret>(nullptr);
    }
    auto pack =
        std::tuple<Arg<std::remove_cvref_t<Args>>...>(Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void *, sizeof...(Args)> argv{};
    std::size_t i = 0;
    bool argsValid = true;
    std::apply([&](auto &...a) { ((argsValid = argsValid && a.valid, argv[i++] = a.ptr), ...); }, pack);
    if (!argsValid) {
        set_error(std::string("Unity static call failed: managed string argument "
                              "allocation failed in ") +
                  std::string(methodName));
        append_backend_error();
        return from_result<Ret>(nullptr);
    }
    void *result = nullptr;
    void *ex = nullptr;
    if (!Backend::runtime_invoke(m, nullptr, argv.data(), &result, &ex) || ex) {
        set_error(std::string("Unity static call failed: runtime_invoke exception in ") + std::string(methodName));
        append_backend_error();
        return from_result<Ret>(nullptr);
    }
    return from_result<Ret>(result);
}

template <class T, class... Args>
std::vector<T> detail::StaticArrayCall(TypeRef type, std::string_view methodName, Args &&...args) {
    std::vector<T> out;
    void *array = InvokeStatic<void *>(type, methodName, std::forward<Args>(args)...);
    if (!array) {
        if (!fallback_error())
            set_error(std::string("Unity array call returned null: ") + std::string(methodName));
        return out;
    }
    if (!Backend::has_array_length()) {
        set_error("Unity array length unavailable: backend array_length API is "
                  "unavailable");
        append_backend_error();
        return out;
    }
    const std::size_t count = Backend::array_length(array);
    if (count == 0) {
        append_backend_error();
        return out;
    }
    if (!Backend::has_array_ref_at()) {
        set_error("Unity array element access unavailable: backend array_ref_at "
                  "API is unavailable");
        append_backend_error();
        return out;
    }
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        void *item = Backend::array_ref_at(array, i);
        if (item)
            out.emplace_back(item);
    }
    return out;
}

template <class T, class... ExtraArgs>
std::vector<T> detail::FindObjectsUsing(TypeRef owner, std::string_view methodName, std::string_view image,
                                        std::string_view namespc, std::string_view className,
                                        ExtraArgs &&...extraArgs) {
    std::vector<T> out;
    clear_error();
    TypeRef target{image, namespc, className};
    void *type = target.resolve_type_object();
    if (!type) {
        set_error(std::string("Unity object finding failed: class not found: ") + std::string(image) + ":" +
                  std::string(namespc) + "." + std::string(className));
        append_backend_error();
        return out;
    }
    void *array = InvokeStatic<void *>(owner, methodName, TypeObject{type}, std::forward<ExtraArgs>(extraArgs)...);
    if (!array && methodName == "FindObjectsOfType" && sizeof...(ExtraArgs) == 0) {
        clear_error();
        array = InvokeStatic<void *>(owner, "FindObjectsByType", TypeObject{type}, FindObjectsSortMode::None);
    }
    if (!array) {
        if (!fallback_error())
            set_error(std::string("Unity object finding failed: returned array was null: ") + std::string(methodName));
        return out;
    }
    if (!Backend::has_array_length()) {
        set_error("Unity object finding failed: backend array_length API is unavailable");
        append_backend_error();
        return out;
    }
    const std::size_t count = Backend::array_length(array);
    if (count == 0) {
        append_backend_error();
        return out;
    }
    if (!Backend::has_array_ref_at()) {
        set_error("Unity object finding failed: Unity array element access "
                  "unavailable: backend array_ref_at API is unavailable");
        append_backend_error();
        return out;
    }
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        void *item = Backend::array_ref_at(array, i);
        if (item)
            out.emplace_back(item);
    }
    if (out.empty())
        set_error(std::string("Unity object finding failed: no non-null instances found: ") + std::string(image) + ":" +
                  std::string(namespc) + "." + std::string(className));
    return out;
}

template <class T> T Object::GetField(std::string_view fieldName) const {
    detail::clear_error();
    detail::FieldOut<std::remove_cvref_t<T>> out{};
    if (!handle_) {
        detail::set_error("Unity Object::GetField failed: target object is null");
        return out.get();
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::GetField failed: object_get_class failed");
        detail::append_backend_error();
        return out.get();
    }
    const void *f = detail::Backend::find_field(k, fieldName);
    if (!f) {
        detail::set_error(std::string("Unity Object::GetField failed: field not found: ") + std::string(fieldName));
        detail::append_backend_error();
        return out.get();
    }
    if (!detail::Backend::field_get_value(handle_, f, out.ptr())) {
        detail::set_error(std::string("Unity Object::GetField failed: field read failed: ") + std::string(fieldName));
        detail::append_backend_error();
    }
    return out.get();
}
template <class T> void Object::SetField(std::string_view fieldName, T value) const {
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::SetField failed: target object is null");
        return;
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::SetField failed: object_get_class failed");
        detail::append_backend_error();
        return;
    }
    const void *f = detail::Backend::find_field(k, fieldName);
    if (!f) {
        detail::set_error(std::string("Unity Object::SetField failed: field not found: ") + std::string(fieldName));
        detail::append_backend_error();
        return;
    }
    detail::FieldArg<std::remove_cvref_t<T>> arg(value);
    if (!detail::Backend::field_set_value(handle_, f, arg.ptr)) {
        detail::set_error(std::string("Unity Object::SetField failed: field write failed: ") + std::string(fieldName));
        detail::append_backend_error();
    }
}
template <class T> T Object::StaticGetField(TypeRef type, std::string_view fieldName) {
    detail::clear_error();
    detail::FieldOut<std::remove_cvref_t<T>> out{};
    const void *k = type.resolve_class();
    if (!k) {
        detail::set_error(std::string("Unity Object::StaticGetField failed: class not found for ") +
                          std::string(type.namespc) + "." + std::string(type.name));
        detail::append_backend_error();
        return out.get();
    }
    const void *f = detail::Backend::find_field(k, fieldName);
    if (!f) {
        detail::set_error(std::string("Unity Object::StaticGetField failed: field not found: ") +
                          std::string(fieldName));
        detail::append_backend_error();
        return out.get();
    }
    if (!detail::Backend::field_static_get_value(k, f, out.ptr())) {
        detail::set_error(std::string("Unity Object::StaticGetField failed: field read failed: ") +
                          std::string(fieldName));
        detail::append_backend_error();
    }
    return out.get();
}
template <class T> void Object::StaticSetField(TypeRef type, std::string_view fieldName, T value) {
    detail::clear_error();
    const void *k = type.resolve_class();
    if (!k) {
        detail::set_error(std::string("Unity Object::StaticSetField failed: class not found for ") +
                          std::string(type.namespc) + "." + std::string(type.name));
        detail::append_backend_error();
        return;
    }
    const void *f = detail::Backend::find_field(k, fieldName);
    if (!f) {
        detail::set_error(std::string("Unity Object::StaticSetField failed: field not found: ") +
                          std::string(fieldName));
        detail::append_backend_error();
        return;
    }
    detail::FieldArg<std::remove_cvref_t<T>> arg(value);
    if (!detail::Backend::field_static_set_value(k, f, arg.ptr)) {
        detail::set_error(std::string("Unity Object::StaticSetField failed: field write failed: ") +
                          std::string(fieldName));
        detail::append_backend_error();
    }
}
template <class Ret>
Ret Object::CallExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                      void **rawArgs) const {
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::CallExact failed: target object is null");
        return detail::from_result<Ret>(nullptr);
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::CallExact failed: object_get_class failed");
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    const void *m = detail::Backend::find_method_exact(k, methodName, parameterTypeNames);
    if (!m) {
        const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : "";
        detail::set_error(std::string("Unity Object::CallExact failed: exact method not found: ") +
                          detail::signature_text(methodName, parameterTypeNames) +
                          (lookupDetail.empty() ? std::string{} : std::string("; detail: ") + lookupDetail));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    void *result = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(m, handle_, rawArgs, &result, &ex) || ex) {
        detail::set_error(std::string("Unity Object::CallExact failed: runtime_invoke exception in ") +
                          detail::signature_text(methodName, parameterTypeNames));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    return detail::from_result<Ret>(result);
}
template <class Ret, class... Args>
Ret Object::CallExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                      Args &&...args) const {
    auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
        detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void *, sizeof...(Args)> argv{};
    std::size_t i = 0;
    bool argsValid = true;
    std::apply([&](auto &...a) { ((argsValid = argsValid && a.valid, argv[i++] = a.ptr), ...); }, pack);
    if (!argsValid) {
        detail::clear_error();
        detail::set_error(std::string("Unity Object::CallExact failed: managed "
                                      "string argument allocation failed in ") +
                          std::string(methodName));
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    return CallExact<Ret>(methodName, parameterTypeNames, argv.data());
}
namespace detail {
inline TypeRef reflection_type_ref(std::string_view typeName) {
    const std::size_t separator = typeName.rfind('.');
    if (separator == std::string_view::npos)
        return {"", "", typeName};
    return {"", typeName.substr(0, separator), typeName.substr(separator + 1)};
}
template <class T> void *reflection_argument(Arg<T> &argument) {
    using Value = std::remove_cvref_t<T>;
    constexpr bool charArray =
        std::is_array_v<Value> && std::is_same_v<std::remove_cv_t<std::remove_extent_t<Value>>, char>;
    if constexpr (is_wrapper_v<Value> || std::is_pointer_v<Value> || std::is_same_v<Value, std::string> ||
                  std::is_same_v<Value, std::string_view> || charArray) {
        return argument.ptr;
    } else {
        const auto typeName = parameter_type_name<Value>();
        if (!typeName) {
            set_error("Unity Object::InvokeGeneric failed: no managed type mapping "
                      "exists for a value-type argument");
            return nullptr;
        }
        const TypeRef type = reflection_type_ref(*typeName);
        const void *klass = type.resolve_class();
        if (!klass) {
            set_error(std::string("Unity Object::InvokeGeneric failed: value-type "
                                  "class not found: ") +
                      *typeName);
            append_backend_error();
            return nullptr;
        }
        void *boxed = Backend::value_box(klass, argument.ptr);
        if (!boxed) {
            set_error(std::string("Unity Object::InvokeGeneric failed: value-type "
                                  "boxing failed: ") +
                      *typeName);
            append_backend_error();
        }
        return boxed;
    }
}
inline void *make_reflection_array(void *elementType, const std::vector<void *> &values) {
    if (!elementType) {
        set_error("Unity reflection array creation failed: element type is null");
        return nullptr;
    }
    TypeRef arrayTypeRef{"mscorlib", "System", "Array"};
    // Array.CreateInstance expects the element Type. Passing MakeArrayType()
    // here creates a jagged array (for example Type[][] instead of Type[]),
    // so reference writes either fail or corrupt the reflection invocation.
    void *array =
        InvokeStatic<void *>(arrayTypeRef, "CreateInstance", TypeObject{elementType}, static_cast<int>(values.size()));
    if (!array) {
        if (!fallback_error())
            set_error("Unity reflection array creation failed: Array.CreateInstance "
                      "returned null");
        return nullptr;
    }
    if (!Backend::has_array_length() || !Backend::has_array_ref_at() || !Backend::has_array_set_ref()) {
        set_error("Unity reflection array creation failed: required array APIs are unavailable");
        append_backend_error();
        return nullptr;
    }
    if (Backend::array_length(array) != values.size()) {
        set_error("Unity reflection array creation failed: Array.CreateInstance returned an unexpected length");
        return nullptr;
    }
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!Backend::array_set_ref(array, i, values[i])) {
            set_error("Unity reflection array creation failed: array_set_ref "
                      "rejected an element");
            append_backend_error();
            return nullptr;
        }
        if (Backend::array_ref_at(array, i) != values[i]) {
            set_error("Unity reflection array creation failed: written array element did not round-trip");
            return nullptr;
        }
    }
    return array;
}

struct ResolvedGenericMethod {
    const void *method = nullptr;
    const void *declaringClass = nullptr;
    void *reflectionMethod = nullptr;
    void *genericParameterArray = nullptr;

    explicit operator bool() const noexcept {
        return method && declaringClass && reflectionMethod && genericParameterArray;
    }
};

inline ResolvedGenericMethod find_generic_method(const void *klass, std::string_view methodName,
                                                 std::size_t runtimeArgumentCount,
                                                 std::size_t genericArgumentCount) {
    if (!klass) {
        set_error("Unity generic method lookup failed: class is null");
        return {};
    }

    const std::string requestedName(methodName);
    const void *current = klass;
    while (current) {
        ResolvedGenericMethod match{};
        std::size_t matches = 0;
        void *iterator = nullptr;
        while (const void *candidate = Backend::class_get_methods(current, &iterator)) {
            const char *candidateName = Backend::method_get_name(candidate);
            if (!candidateName || requestedName != candidateName ||
                Backend::method_get_param_count(candidate) != runtimeArgumentCount ||
                !Backend::method_is_generic(candidate)) {
                continue;
            }

            void *methodInfo = Backend::method_get_object(candidate, current);
            if (!methodInfo)
                continue;

            // A runtime can surface an inflated MethodInfo while enumerating a
            // generic definition. Normalize it before validating generic arity.
            Object reflectionMethod{methodInfo};
            void *genericDefinition = reflectionMethod.CallExact<void *>("GetGenericMethodDefinition", {});
            if (!genericDefinition)
                continue;

            reflectionMethod = Object{genericDefinition};
            void *genericParameters = reflectionMethod.CallExact<void *>("GetGenericArguments", {});
            if (!genericParameters || !Backend::has_array_length() || !Backend::has_array_ref_at() ||
                !Backend::has_array_set_ref() || Backend::array_length(genericParameters) != genericArgumentCount) {
                continue;
            }

            match = {candidate, current, genericDefinition, genericParameters};
            ++matches;
        }

        if (matches > 1) {
            set_error(std::string("Unity generic method lookup failed: ambiguous overload: ") + requestedName);
            return {};
        }
        if (match)
            return match;
        current = Backend::class_get_parent(current);
    }

    set_error(std::string("Unity generic method lookup failed: no overload with ") +
              std::to_string(genericArgumentCount) + " generic argument(s): " + requestedName);
    return {};
}
}
inline GameObject GameObject::CreateUi(std::string_view name) {
    detail::clear_error();
    const void *gameObjectClass = GameObjectType.resolve_class();
    if (!gameObjectClass) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject class not found");
        detail::append_backend_error();
        return {};
    }
    void *rectTransformType = RectTransformType.resolve_type_object();
    void *systemType = TypeRef{"mscorlib", "System", "Type"}.resolve_type_object();
    if (!rectTransformType || !systemType) {
        detail::set_error("Unity GameObject::CreateUi failed: RectTransform or "
                          "System.Type metadata is unavailable");
        detail::append_backend_error();
        return {};
    }
    void *componentTypes = detail::make_reflection_array(systemType, {rectTransformType});
    if (!componentTypes)
        return {};
    const std::vector<const char *> signature{"System.String", "System.Type[]"};
    const void *constructor = detail::Backend::find_method_exact(gameObjectClass, ".ctor", signature);
    if (!constructor) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject(String, "
                          "Type[]) constructor is unavailable");
        detail::append_backend_error();
        return {};
    }
    void *object = detail::Backend::object_new(gameObjectClass);
    void *managedName = detail::Backend::new_string(name);
    if (!object || !managedName) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject or "
                          "managed name allocation failed");
        detail::append_backend_error();
        return {};
    }
    void *arguments[] = {managedName, componentTypes};
    void *exception = nullptr;
    if (!detail::Backend::runtime_invoke(constructor, object, arguments, nullptr, &exception) || exception) {
        detail::set_error("Unity GameObject::CreateUi failed: GameObject(String, "
                          "Type[]) constructor threw or could not be invoked");
        detail::append_backend_error();
        return {};
    }
    return GameObject{object};
}
template <class T>
void Object::SetReferenceArrayProperty(std::string_view propertyName, const std::vector<T> &values) const {
    static_assert(detail::is_wrapper_v<T>);
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::SetReferenceArrayProperty failed: target "
                          "object is null");
        return;
    }
    const TypeRef elementType = T::unity_type();
    void *elementTypeObject = elementType.resolve_type_object();
    if (!elementTypeObject) {
        detail::set_error(std::string("Unity Object::SetReferenceArrayProperty "
                                      "failed: element type not found: ") +
                          std::string(elementType.image) + ":" + std::string(elementType.namespc) + "." +
                          std::string(elementType.name));
        detail::append_backend_error();
        return;
    }
    std::vector<void *> handles;
    handles.reserve(values.size());
    for (const T &value : values)
        handles.push_back(value.handle());
    void *array = detail::make_reflection_array(elementTypeObject, handles);
    if (!array)
        return;
    const std::string parameterType = std::string(elementType.namespc) + "." + std::string(elementType.name) + "[]";
    void *args[] = {array};
    CallExact<void>(std::string("set_") + std::string(propertyName), {parameterType.c_str()}, args);
}
template <class Ret, class... Args>
Ret Object::InvokeGeneric(std::string_view methodName, const std::vector<TypeObject> &genericTypes,
                          Args &&...args) const {
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::InvokeGeneric failed: target object is null");
        return detail::from_result<Ret>(nullptr);
    }
    const void *klass = detail::Backend::object_get_class(handle_);
    if (!klass) {
        detail::set_error("Unity Object::InvokeGeneric failed: object_get_class failed");
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    if (genericTypes.empty()) {
        detail::set_error("Unity Object::InvokeGeneric failed: at least one "
                          "generic type is required");
        return detail::from_result<Ret>(nullptr);
    }

    const detail::ResolvedGenericMethod resolved =
        detail::find_generic_method(klass, methodName, sizeof...(Args), genericTypes.size());
    if (!resolved) {
        const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : "unknown lookup failure";
        detail::set_error(std::string("Unity Object::InvokeGeneric failed: generic method not found: ") +
                          std::string(methodName) + "; detail: " + lookupDetail);
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }

    // GetGenericArguments() provides a correctly typed, fresh Type[] for the
    // selected definition. Reuse it instead of synthesizing a reflection array.
    void *typeArray = resolved.genericParameterArray;
    if (!detail::Backend::has_array_length() || !detail::Backend::has_array_ref_at() ||
        !detail::Backend::has_array_set_ref() || detail::Backend::array_length(typeArray) != genericTypes.size()) {
        detail::set_error("Unity Object::InvokeGeneric failed: generic parameter array is invalid");
        detail::append_backend_error();
        return detail::from_result<Ret>(nullptr);
    }
    for (std::size_t i = 0; i < genericTypes.size(); ++i) {
        void *typeObject = genericTypes[i].handle();
        if (!typeObject || !detail::Backend::array_set_ref(typeArray, i, typeObject) ||
            detail::Backend::array_ref_at(typeArray, i) != typeObject) {
            detail::set_error("Unity Object::InvokeGeneric failed: could not prepare generic type argument array");
            detail::append_backend_error();
            return detail::from_result<Ret>(nullptr);
        }
    }

    Object reflectionMethod{resolved.reflectionMethod};
    void *inflated = reflectionMethod.CallExact<void *>("MakeGenericMethod", {"System.Type[]"}, typeArray);
    if (!inflated)
        return detail::from_result<Ret>(nullptr);
    auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
        detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::vector<void *> arguments;
    arguments.reserve(sizeof...(Args));
    std::apply([&](auto &...a) { (arguments.push_back(detail::reflection_argument(a)), ...); }, pack);
    if (detail::fallback_error())
        return detail::from_result<Ret>(nullptr);
    void *argumentArray = nullptr;
    if constexpr (sizeof...(Args) != 0) {
        void *objectType = TypeRef{"mscorlib", "System", "Object"}.resolve_type_object();
        argumentArray = detail::make_reflection_array(objectType, arguments);
        if (!argumentArray)
            return detail::from_result<Ret>(nullptr);
    }
    Object inflatedMethod{inflated};
    void *result =
        inflatedMethod.CallExact<void *>("Invoke", {"System.Object", "System.Object[]"}, handle_, argumentArray);
    return detail::from_result<Ret>(result);
}
template <class T>
std::vector<T> Object::CallArrayExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                                      void **rawArgs) const {
    std::vector<T> out;
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::CallArrayExact failed: target object is null");
        return out;
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::CallArrayExact failed: object_get_class failed");
        detail::append_backend_error();
        return out;
    }
    const void *m = detail::Backend::find_method_exact(k, methodName, parameterTypeNames);
    if (!m) {
        const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : "";
        detail::set_error(std::string("Unity Object::CallArrayExact failed: exact method not found: ") +
                          detail::signature_text(methodName, parameterTypeNames) +
                          (lookupDetail.empty() ? std::string{} : std::string("; detail: ") + lookupDetail));
        detail::append_backend_error();
        return out;
    }
    void *array = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(m, handle_, rawArgs, &array, &ex) || ex) {
        detail::set_error(std::string("Unity Object::CallArrayExact failed: "
                                      "runtime_invoke exception in ") +
                          detail::signature_text(methodName, parameterTypeNames));
        detail::append_backend_error();
        return out;
    }
    if (!array) {
        detail::set_error(std::string("Unity Object::CallArrayExact failed: returned array was null: ") +
                          std::string(methodName));
        detail::append_backend_error();
        return out;
    }
    if (!detail::Backend::has_array_length()) {
        detail::set_error("Unity Object::CallArrayExact failed: backend "
                          "array_length API is unavailable");
        detail::append_backend_error();
        return out;
    }
    const std::size_t count = detail::Backend::array_length(array);
    if (count == 0)
        return out;
    if (!detail::Backend::has_array_ref_at()) {
        detail::set_error("Unity Object::CallArrayExact failed: backend "
                          "array_ref_at API is unavailable");
        detail::append_backend_error();
        return out;
    }
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        void *item = detail::Backend::array_ref_at(array, i);
        if (item)
            out.emplace_back(item);
    }
    return out;
}
template <class T, class... Args>
std::vector<T> Object::CallArrayExact(std::string_view methodName, const std::vector<const char *> &parameterTypeNames,
                                      Args &&...args) const {
    auto pack = std::tuple<detail::Arg<std::remove_cvref_t<Args>>...>(
        detail::Arg<std::remove_cvref_t<Args>>(std::forward<Args>(args))...);
    std::array<void *, sizeof...(Args)> argv{};
    std::size_t i = 0;
    bool argsValid = true;
    std::apply([&](auto &...a) { ((argsValid = argsValid && a.valid, argv[i++] = a.ptr), ...); }, pack);
    if (!argsValid) {
        detail::clear_error();
        detail::set_error(std::string("Unity Object::CallArrayExact failed: managed string "
                                      "argument allocation failed in ") +
                          std::string(methodName));
        detail::append_backend_error();
        return {};
    }
    return CallArrayExact<T>(methodName, parameterTypeNames, argv.data());
}
inline std::vector<std::string> Object::CallStringArrayExact(
    std::string_view methodName, const std::vector<const char *> &parameterTypeNames) const {
    std::vector<std::string> out;
    detail::clear_error();
    if (!handle_) {
        detail::set_error("Unity Object::CallStringArrayExact failed: target object is null");
        return out;
    }
    const void *k = detail::Backend::object_get_class(handle_);
    if (!k) {
        detail::set_error("Unity Object::CallStringArrayExact failed: object_get_class failed");
        detail::append_backend_error();
        return out;
    }
    const void *m = detail::Backend::find_method_exact(k, methodName, parameterTypeNames);
    if (!m) {
        const std::string lookupDetail = detail::fallback_error() ? detail::fallback_error() : "";
        detail::set_error(std::string("Unity Object::CallStringArrayExact failed: "
                                      "exact method not found: ") +
                          detail::signature_text(methodName, parameterTypeNames) +
                          (lookupDetail.empty() ? std::string{} : std::string("; detail: ") + lookupDetail));
        detail::append_backend_error();
        return out;
    }
    void *array = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(m, handle_, nullptr, &array, &ex) || ex) {
        detail::set_error(std::string("Unity Object::CallStringArrayExact failed: "
                                      "runtime_invoke exception in ") +
                          detail::signature_text(methodName, parameterTypeNames));
        detail::append_backend_error();
        return out;
    }
    if (!array) {
        detail::set_error(std::string("Unity Object::CallStringArrayExact failed: "
                                      "returned array was null: ") +
                          std::string(methodName));
        detail::append_backend_error();
        return out;
    }
    if (!detail::Backend::has_array_length()) {
        detail::set_error("Unity Object::CallStringArrayExact failed: backend "
                          "array_length API is unavailable");
        detail::append_backend_error();
        return out;
    }
    const std::size_t count = detail::Backend::array_length(array);
    if (count == 0)
        return out;
    if (!detail::Backend::has_array_ref_at()) {
        detail::set_error("Unity Object::CallStringArrayExact failed: backend "
                          "array_ref_at API is unavailable");
        detail::append_backend_error();
        return out;
    }
    out.reserve(count);
    for (std::size_t i = 0; i < count; ++i) {
        void *item = detail::Backend::array_ref_at(array, i);
        if (!item) {
            detail::set_error(std::string("Unity Object::CallStringArrayExact failed: string array "
                                          "contains a null element in ") +
                              std::string(methodName));
            detail::append_backend_error();
            out.clear();
            return out;
        }
        out.emplace_back(detail::managed_string_to_utf8(item));
        if (detail::fallback_error()) {
            out.clear();
            return out;
        }
    }
    return out;
}
// URK_UNITY_SHORTCUTS_BEGIN
namespace Debug {
inline void Log(std::string_view message) {
    detail::InvokeStatic<void>(DebugType, "Log", message);
}
inline void LogWarning(std::string_view message) {
    detail::InvokeStatic<void>(DebugType, "LogWarning", message);
}
inline void LogError(std::string_view message) {
    detail::InvokeStatic<void>(DebugType, "LogError", message);
}
}
namespace Screen {
inline int width() {
    return detail::InvokeStatic<int>(ScreenType, "get_width");
}
inline int height() {
    return detail::InvokeStatic<int>(ScreenType, "get_height");
}
inline float dpi() {
    return detail::InvokeStatic<float>(ScreenType, "get_dpi");
}
}
namespace ObjectFilter {
inline constexpr int kHideFlagsHierarchyMask = 1;
inline constexpr int kHideFlagsImplicitRuntimeMask = 4 | 16 | 32;
inline bool has(std::uint32_t flags, ObjectFilterFlags flag) {
    return (flags & static_cast<std::uint32_t>(flag)) != 0;
}
inline bool include_scene(Scene scene, std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    if (!scene || !scene.IsValid() || !scene.isLoaded())
        return false;
    if (!has(flags, ObjectFilterFlags::IncludeDontDestroyOnLoad) && scene.isDontDestroyOnLoad())
        return false;
    return true;
}
inline bool include_game_object(GameObject object,
                                std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    if (!object || !object.alive())
        return false;
    if (!has(flags, ObjectFilterFlags::IncludeInactive) && !object.activeInHierarchy())
        return false;
    const int hide_flags = object.hideFlags();
    if (!has(flags, ObjectFilterFlags::IncludeHidden) &&
        (hide_flags & (kHideFlagsHierarchyMask | kHideFlagsImplicitRuntimeMask)) != 0) {
        return false;
    }
    return include_scene(object.scene(), flags);
}
inline std::vector<GameObject> canonicalize_game_objects(
    const std::vector<GameObject> &objects, std::uint32_t flags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    std::vector<GameObject> filtered;
    filtered.reserve(objects.size());
    std::unordered_set<void *> seen;
    for (const GameObject &object : objects) {
        void *handle = object.handle();
        if (!handle || !seen.insert(handle).second)
            continue;
        if (include_game_object(object, flags))
            filtered.push_back(object);
    }
    return filtered;
}
}
namespace SceneManager {
inline TypeRef type() {
    return {"", "UnityEngine.SceneManagement", "SceneManager"};
}
inline Scene GetActiveScene() {
    return Scene{detail::InvokeStatic<void *>(type(), "GetActiveScene")};
}
inline int sceneCount() {
    return detail::InvokeStatic<int>(type(), "get_sceneCount");
}
inline Scene GetSceneAt(int index) {
    return Scene{detail::InvokeStatic<void *>(type(), "GetSceneAt", index)};
}
inline std::vector<Scene> GetLoadedScenes() {
    std::vector<Scene> scenes;
    const int count = sceneCount();
    if (count <= 0)
        return scenes;
    scenes.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        Scene scene = GetSceneAt(i);
        if (scene && scene.IsValid())
            scenes.push_back(scene);
    }
    return scenes;
}
inline std::vector<GameObject> GetLoadedSceneRootsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    std::vector<GameObject> roots;
    for (const Scene &scene : GetLoadedScenes()) {
        if (!ObjectFilter::include_scene(scene, filterFlags))
            continue;
        std::vector<GameObject> sceneRoots = scene.GetRootGameObjects();
        roots.insert(roots.end(), sceneRoots.begin(), sceneRoots.end());
    }
    return ObjectFilter::canonicalize_game_objects(roots, filterFlags);
}
inline std::vector<GameObject> GetLoadedSceneRoots() {
    return GetLoadedSceneRootsFiltered();
}
inline std::vector<GameObject> FindSceneGameObjectsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    const bool includeInactive = ObjectFilter::has(filterFlags, ObjectFilterFlags::IncludeInactive);
    std::vector<GameObject> objects;
    if (!includeInactive)
        objects = Object::FindObjectsByType<GameObject>("", "UnityEngine", "GameObject", FindObjectsSortMode::None);
    else
        objects = Object::FindObjectsOfTypeAll<GameObject>("", "UnityEngine", "GameObject");
    return ObjectFilter::canonicalize_game_objects(objects, filterFlags);
}
inline std::vector<GameObject> FindSceneGameObjects(bool includeInactive = true) {
    return FindSceneGameObjectsFiltered(includeInactive ? static_cast<std::uint32_t>(ObjectFilterFlags::IncludeInactive)
                                                        : static_cast<std::uint32_t>(ObjectFilterFlags::None));
}
}
namespace Time {
inline float time() {
    return detail::InvokeStatic<float>(TimeType, "get_time");
}
inline float deltaTime() {
    return detail::InvokeStatic<float>(TimeType, "get_deltaTime");
}
inline float unscaledDeltaTime() {
    return detail::InvokeStatic<float>(TimeType, "get_unscaledDeltaTime");
}
inline float timeScale() {
    return detail::InvokeStatic<float>(TimeType, "get_timeScale");
}
inline void set_timeScale(float value) {
    detail::InvokeStatic<void>(TimeType, "set_timeScale", value);
}
}
namespace Input {
inline bool available() {
    return URK::has_input();
}
inline bool GetKey(int keyCode) {
    return URK::input_get_key(keyCode);
}
inline bool GetKeyDown(int keyCode) {
    return URK::input_get_key_down(keyCode);
}
inline bool GetKeyUp(int keyCode) {
    return URK::input_get_key_up(keyCode);
}
inline bool GetMouseButton(int button) {
    return URK::input_get_mouse_button(button);
}
inline bool GetMouseButtonDown(int button) {
    return URK::input_get_mouse_button_down(button);
}
inline bool GetMouseButtonUp(int button) {
    return URK::input_get_mouse_button_up(button);
}
inline bool GetKey(KeyCode keyCode) {
    return GetKey(static_cast<int>(keyCode));
}
inline bool GetKeyDown(KeyCode keyCode) {
    return GetKeyDown(static_cast<int>(keyCode));
}
inline bool GetKeyUp(KeyCode keyCode) {
    return GetKeyUp(static_cast<int>(keyCode));
}
inline bool GetMouseButton(MouseButton button) {
    return GetMouseButton(static_cast<int>(button));
}
inline bool GetMouseButtonDown(MouseButton button) {
    return GetMouseButtonDown(static_cast<int>(button));
}
inline bool GetMouseButtonUp(MouseButton button) {
    return GetMouseButtonUp(static_cast<int>(button));
}
}
inline Vector2 screen_size(Camera camera = {}) {
    int width_value = Screen::width();
    int height_value = Screen::height();
    if ((width_value <= 0 || height_value <= 0) && static_cast<bool>(camera)) {
        const int pixel_width = camera.pixelWidth();
        const int pixel_height = camera.pixelHeight();
        if (pixel_width > 0)
            width_value = pixel_width;
        if (pixel_height > 0)
            height_value = pixel_height;
    }
    return {static_cast<float>(width_value > 0 ? width_value : 0),
            static_cast<float>(height_value > 0 ? height_value : 0)};
}
inline Vector2 screen_center(Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    return {size.x * 0.5f, size.y * 0.5f};
}
inline bool screen_contains(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    return size.x > 0.0f && size.y > 0.0f && point.x >= padding && point.y >= padding &&
           point.x <= (size.x - padding) && point.y <= (size.y - padding);
}
inline Vector2 clamp_to_screen(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    const float max_x = size.x > 0.0f ? std::max(padding, size.x - padding) : padding;
    const float max_y = size.y > 0.0f ? std::max(padding, size.y - padding) : padding;
    return {std::clamp(point.x, padding, max_x), std::clamp(point.y, padding, max_y)};
}
inline Vector2 direction_to_screen_edge(Vector2 direction, float padding = 24.0f, Camera camera = {}) {
    const Vector2 size = screen_size(camera);
    const Vector2 center = screen_center(camera);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return center;

    Vector2 dir = direction.normalized();
    if (dir.nearly_zero())
        dir = {0.0f, -1.0f};

    const float half_w = std::max(1.0f, size.x * 0.5f - padding);
    const float half_h = std::max(1.0f, size.y * 0.5f - padding);
    float tx = std::numeric_limits<float>::infinity();
    float ty = std::numeric_limits<float>::infinity();
    if (std::fabs(dir.x) > 0.000001f)
        tx = half_w / std::fabs(dir.x);
    if (std::fabs(dir.y) > 0.000001f)
        ty = half_h / std::fabs(dir.y);

    const float scale = std::min(tx, ty);
    return {center.x + dir.x * scale, center.y + dir.y * scale};
}
inline ProjectionResult project_world(Camera camera, Vector3 world, float edge_padding = 24.0f) {
    ProjectionResult result{};
    result.world = world;
    result.screen_center = screen_center(camera);
    if (!static_cast<bool>(camera))
        return result;

    const Vector2 size = screen_size(camera);
    if (size.x <= 0.0f || size.y <= 0.0f)
        return result;

    const Transform camera_transform = camera.transform();
    Vector3 camera_position{};
    Vector3 camera_forward{0.0f, 0.0f, 1.0f};
    Vector3 camera_right{1.0f, 0.0f, 0.0f};
    Vector3 camera_up{0.0f, 1.0f, 0.0f};
    bool have_basis = false;
    if (static_cast<bool>(camera_transform)) {
        camera_position = camera_transform.position();
        camera_forward = camera_transform.forward().normalized();
        camera_right = camera_transform.right().normalized();
        camera_up = camera_transform.up().normalized();
        have_basis = true;
    }

    const Vector3 offset = world - camera_position;
    result.distance = offset.magnitude();
    if (have_basis && result.distance > 0.000001f)
        result.facing = Vector3::dot(offset / result.distance, camera_forward);

    result.screen3 = camera.WorldToScreenPoint(world);
    result.viewport = camera.WorldToViewportPoint(world);
    result.depth = result.screen3.z;
    result.in_front = result.depth > 0.01f;
    result.screen = {result.screen3.x, size.y - result.screen3.y};
    result.on_screen = result.in_front && result.viewport.x >= 0.0f && result.viewport.x <= 1.0f &&
                       result.viewport.y >= 0.0f && result.viewport.y <= 1.0f;

    Vector2 direction = result.screen - result.screen_center;
    if (!result.on_screen && have_basis && result.distance > 0.000001f) {
        const Vector3 offset_dir = offset / result.distance;
        direction = {Vector3::dot(offset_dir, camera_right), -Vector3::dot(offset_dir, camera_up)};
        if (!result.in_front)
            direction *= -1.0f;
    } else if (!result.in_front) {
        direction *= -1.0f;
    }

    if (direction.nearly_zero())
        direction = {0.0f, -1.0f};

    result.direction = direction.normalized();
    result.clamped_screen = direction_to_screen_edge(result.direction, edge_padding, camera);
    result.valid = true;
    return result;
}
inline ProjectionResult project_world(Vector3 world, float edge_padding = 24.0f) {
    return project_world(Camera::main(), world, edge_padding);
}
inline ProjectionResult project_transform(Camera camera, Transform transform, float edge_padding = 24.0f) {
    return static_cast<bool>(transform) ? project_world(camera, transform.position(), edge_padding)
                                        : ProjectionResult{};
}
inline ProjectionResult project_transform(Transform transform, float edge_padding = 24.0f) {
    return project_transform(Camera::main(), transform, edge_padding);
}
inline bool world_to_overlay(Camera camera, Vector3 world, Vector2 *out) {
    if (!out)
        return false;
    const ProjectionResult projection = project_world(camera, world, 0.0f);
    if (!projection.on_screen)
        return false;
    *out = projection.screen;
    return true;
}
inline bool world_to_overlay(Vector3 world, Vector2 *out) {
    return world_to_overlay(Camera::main(), world, out);
}
inline bool world_visible(Camera camera, Vector3 world, float min_facing = 0.01f) {
    const ProjectionResult projection = project_world(camera, world, 0.0f);
    return projection.on_screen && projection.facing >= min_facing;
}
inline bool world_visible(Vector3 world, float min_facing = 0.01f) {
    return world_visible(Camera::main(), world, min_facing);
}
// URK_UNITY_INSPECT_BEGIN
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
    std::uint32_t handle = 0;
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
}
}

// URK_UNITY_ALIASES_BEGIN
namespace Unity {
using AudioSource = URK::Unity::AudioSource;
using AssetBundle = URK::Unity::AssetBundle;
using Behaviour = URK::Unity::Behaviour;
using Button = URK::Unity::Button;
using ButtonTransition = URK::Unity::ButtonTransition;
using Animator = URK::Unity::Animator;
using AnimatorCullingMode = URK::Unity::AnimatorCullingMode;
using AnimatorUpdateMode = URK::Unity::AnimatorUpdateMode;
using Bounds = URK::Unity::Bounds;
using Camera = URK::Unity::Camera;
using Canvas = URK::Unity::Canvas;
using CanvasRoot = URK::Unity::CanvasRoot;
using CanvasRenderMode = URK::Unity::CanvasRenderMode;
using CanvasRenderer = URK::Unity::CanvasRenderer;
using CanvasGroup = URK::Unity::CanvasGroup;
using CanvasScaler = URK::Unity::CanvasScaler;
using CanvasScaleMode = URK::Unity::CanvasScaleMode;
using CanvasScreenMatchMode = URK::Unity::CanvasScreenMatchMode;
using Collider = URK::Unity::Collider;
using Color = URK::Unity::Color;
using Color32 = URK::Unity::Color32;
using Component = URK::Unity::Component;
using ContentSizeFitter = URK::Unity::ContentSizeFitter;
using ContentSizeFitterFitMode = URK::Unity::ContentSizeFitterFitMode;
using CursorLockState = URK::CursorLockState;
using CursorState = URK::CursorState;
using DiagnosticSink = URK::Unity::DiagnosticSink;
using Dropdown = URK::Unity::Dropdown;
using EventSystem = URK::Unity::EventSystem;
using FindObjectsSortMode = URK::Unity::FindObjectsSortMode;
using FontStyle = URK::Unity::FontStyle;
using GameObject = URK::Unity::GameObject;
using Graphic = URK::Unity::Graphic;
using GraphicRaycaster = URK::Unity::GraphicRaycaster;
using GraphicRaycasterBlockingObjects = URK::Unity::GraphicRaycasterBlockingObjects;
using GridLayoutAxis = URK::Unity::GridLayoutAxis;
using GridLayoutConstraint = URK::Unity::GridLayoutConstraint;
using GridLayoutCorner = URK::Unity::GridLayoutCorner;
using GridLayoutGroup = URK::Unity::GridLayoutGroup;
using HorizontalLayoutGroup = URK::Unity::HorizontalLayoutGroup;
using Image = URK::Unity::Image;
using ImageFillMethod = URK::Unity::ImageFillMethod;
using ImageType = URK::Unity::ImageType;
using InputField = URK::Unity::InputField;
using InputFieldContentType = URK::Unity::InputFieldContentType;
using InputFieldLineType = URK::Unity::InputFieldLineType;
using InputSystemUIInputModule = URK::Unity::InputSystemUIInputModule;
using KeyCode = URK::Unity::KeyCode;
using LightProbeUsage = URK::Unity::LightProbeUsage;
using Light = URK::Unity::Light;
using LightRenderMode = URK::Unity::LightRenderMode;
using LightShadowResolution = URK::Unity::LightShadowResolution;
using LightShadows = URK::Unity::LightShadows;
using LightType = URK::Unity::LightType;
using LayoutElement = URK::Unity::LayoutElement;
using LayoutRebuilder = URK::Unity::LayoutRebuilder;
using Material = URK::Unity::Material;
using Mask = URK::Unity::Mask;
using AspectRatioFitter = URK::Unity::AspectRatioFitter;
using AspectRatioFitterMode = URK::Unity::AspectRatioFitterMode;
using Mesh = URK::Unity::Mesh;
using MeshCollider = URK::Unity::MeshCollider;
using MeshFilter = URK::Unity::MeshFilter;
using MeshRenderer = URK::Unity::MeshRenderer;
using MonoBehaviour = URK::Unity::MonoBehaviour;
using MouseButton = URK::Unity::MouseButton;
using MotionVectorGenerationMode = URK::Unity::MotionVectorGenerationMode;
using Object = URK::Unity::Object;
using ObjectFilterFlags = URK::Unity::ObjectFilterFlags;
using ProjectionResult = URK::Unity::ProjectionResult;
using Quaternion = URK::Unity::Quaternion;
using Ray = URK::Unity::Ray;
using Rect = URK::Unity::Rect;
using RectTransform = URK::Unity::RectTransform;
using RectTransformAxis = URK::Unity::RectTransformAxis;
using RectTransformEdge = URK::Unity::RectTransformEdge;
using RectMask2D = URK::Unity::RectMask2D;
using RawImage = URK::Unity::RawImage;
using ReflectionProbeUsage = URK::Unity::ReflectionProbeUsage;
using Renderer = URK::Unity::Renderer;
using Rigidbody = URK::Unity::Rigidbody;
using Rigidbody2D = URK::Unity::Rigidbody2D;
using Scene = URK::Unity::Scene;
using Scrollbar = URK::Unity::Scrollbar;
using ScrollbarDirection = URK::Unity::ScrollbarDirection;
using ScrollRect = URK::Unity::ScrollRect;
using ScrollRectMovementType = URK::Unity::ScrollRectMovementType;
using ScriptableObject = URK::Unity::ScriptableObject;
using Selectable = URK::Unity::Selectable;
using SelectableTransition = URK::Unity::SelectableTransition;
using ShadowCastingMode = URK::Unity::ShadowCastingMode;
using SkinQuality = URK::Unity::SkinQuality;
using SkinnedMeshRenderer = URK::Unity::SkinnedMeshRenderer;
using Sprite = URK::Unity::Sprite;
using Shader = URK::Unity::Shader;
using Slider = URK::Unity::Slider;
using SliderDirection = URK::Unity::SliderDirection;
using Text = URK::Unity::Text;
using TextAnchor = URK::Unity::TextAnchor;
using Texture = URK::Unity::Texture;
using Texture2D = URK::Unity::Texture2D;
using TextMeshProUGUI = URK::Unity::TextMeshProUGUI;
using TmpInputField = URK::Unity::TmpInputField;
using TmpInputFieldContentType = URK::Unity::TmpInputFieldContentType;
using TmpInputFieldLineType = URK::Unity::TmpInputFieldLineType;
using TmpDropdown = URK::Unity::TmpDropdown;
using TmpFontStyles = URK::Unity::TmpFontStyles;
using BaseInputModule = URK::Unity::BaseInputModule;
using StandaloneInputModule = URK::Unity::StandaloneInputModule;
using Toggle = URK::Unity::Toggle;
using VerticalLayoutGroup = URK::Unity::VerticalLayoutGroup;
using Transform = URK::Unity::Transform;
using TypeObject = URK::Unity::TypeObject;
using TypeRef = URK::Unity::TypeRef;
using Vector2 = URK::Unity::Vector2;
using Vector2Int = URK::Unity::Vector2Int;
using Vector3 = URK::Unity::Vector3;
using Vector3Int = URK::Unity::Vector3Int;
using Vector4 = URK::Unity::Vector4;

inline const char *last_error() {
    return URK::Unity::last_error();
}
inline void clear_error() {
    URK::Unity::clear_error();
}
inline CanvasRoot CreateOverlayCanvas(std::string_view name, bool addRaycaster = true) {
    return URK::Unity::CreateOverlayCanvas(name, addRaycaster);
}

namespace Debug {
inline void Log(std::string_view message) {
    URK::Unity::Debug::Log(message);
}
inline void LogWarning(std::string_view message) {
    URK::Unity::Debug::LogWarning(message);
}
inline void LogError(std::string_view message) {
    URK::Unity::Debug::LogError(message);
}
}

namespace Screen {
inline int width() {
    return URK::Unity::Screen::width();
}
inline int height() {
    return URK::Unity::Screen::height();
}
inline float dpi() {
    return URK::Unity::Screen::dpi();
}
inline Vector2 size(Camera camera = {}) {
    return URK::Unity::screen_size(camera);
}
inline Vector2 center(Camera camera = {}) {
    return URK::Unity::screen_center(camera);
}
inline bool contains(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    return URK::Unity::screen_contains(point, padding, camera);
}
inline Vector2 clamp(Vector2 point, float padding = 0.0f, Camera camera = {}) {
    return URK::Unity::clamp_to_screen(point, padding, camera);
}
}

namespace SceneManager {
inline Scene GetActiveScene() {
    return URK::Unity::SceneManager::GetActiveScene();
}
inline int sceneCount() {
    return URK::Unity::SceneManager::sceneCount();
}
inline Scene GetSceneAt(int index) {
    return URK::Unity::SceneManager::GetSceneAt(index);
}
inline std::vector<Scene> GetLoadedScenes() {
    return URK::Unity::SceneManager::GetLoadedScenes();
}
inline std::vector<GameObject> GetLoadedSceneRootsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    return URK::Unity::SceneManager::GetLoadedSceneRootsFiltered(filterFlags);
}
inline std::vector<GameObject> GetLoadedSceneRoots() {
    return URK::Unity::SceneManager::GetLoadedSceneRoots();
}
inline std::vector<GameObject> FindSceneGameObjectsFiltered(
    std::uint32_t filterFlags = static_cast<std::uint32_t>(ObjectFilterFlags::None)) {
    return URK::Unity::SceneManager::FindSceneGameObjectsFiltered(filterFlags);
}
inline std::vector<GameObject> FindSceneGameObjects(bool includeInactive = true) {
    return URK::Unity::SceneManager::FindSceneGameObjects(includeInactive);
}
}

namespace Time {
inline float time() {
    return URK::Unity::Time::time();
}
inline float deltaTime() {
    return URK::Unity::Time::deltaTime();
}
inline float unscaledDeltaTime() {
    return URK::Unity::Time::unscaledDeltaTime();
}
inline float timeScale() {
    return URK::Unity::Time::timeScale();
}
inline void set_timeScale(float value) {
    URK::Unity::Time::set_timeScale(value);
}
}

namespace Input {
inline bool available() {
    return URK::Unity::Input::available();
}
inline bool GetKey(int keyCode) {
    return URK::Unity::Input::GetKey(keyCode);
}
inline bool GetKeyDown(int keyCode) {
    return URK::Unity::Input::GetKeyDown(keyCode);
}
inline bool GetKeyUp(int keyCode) {
    return URK::Unity::Input::GetKeyUp(keyCode);
}
inline bool GetMouseButton(int button) {
    return URK::Unity::Input::GetMouseButton(button);
}
inline bool GetMouseButtonDown(int button) {
    return URK::Unity::Input::GetMouseButtonDown(button);
}
inline bool GetMouseButtonUp(int button) {
    return URK::Unity::Input::GetMouseButtonUp(button);
}
inline bool GetKey(KeyCode keyCode) {
    return URK::Unity::Input::GetKey(keyCode);
}
inline bool GetKeyDown(KeyCode keyCode) {
    return URK::Unity::Input::GetKeyDown(keyCode);
}
inline bool GetKeyUp(KeyCode keyCode) {
    return URK::Unity::Input::GetKeyUp(keyCode);
}
inline bool GetMouseButton(MouseButton button) {
    return URK::Unity::Input::GetMouseButton(button);
}
inline bool GetMouseButtonDown(MouseButton button) {
    return URK::Unity::Input::GetMouseButtonDown(button);
}
inline bool GetMouseButtonUp(MouseButton button) {
    return URK::Unity::Input::GetMouseButtonUp(button);
}
}

inline Vector2 direction_to_screen_edge(Vector2 direction, float padding = 24.0f, Camera camera = {}) {
    return URK::Unity::direction_to_screen_edge(direction, padding, camera);
}
inline ProjectionResult project_world(Camera camera, Vector3 world, float edge_padding = 24.0f) {
    return URK::Unity::project_world(camera, world, edge_padding);
}
inline ProjectionResult project_world(Vector3 world, float edge_padding = 24.0f) {
    return URK::Unity::project_world(world, edge_padding);
}
inline ProjectionResult project_transform(Camera camera, Transform transform, float edge_padding = 24.0f) {
    return URK::Unity::project_transform(camera, transform, edge_padding);
}
inline ProjectionResult project_transform(Transform transform, float edge_padding = 24.0f) {
    return URK::Unity::project_transform(transform, edge_padding);
}
inline bool world_to_overlay(Camera camera, Vector3 world, Vector2 *out) {
    return URK::Unity::world_to_overlay(camera, world, out);
}
inline bool world_to_overlay(Vector3 world, Vector2 *out) {
    return URK::Unity::world_to_overlay(world, out);
}
inline bool world_visible(Camera camera, Vector3 world, float min_facing = 0.01f) {
    return URK::Unity::world_visible(camera, world, min_facing);
}
inline bool world_visible(Vector3 world, float min_facing = 0.01f) {
    return URK::Unity::world_visible(world, min_facing);
}

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
inline TypeInfo DescribeClass(const void *klass) {
    return URK::Unity::Inspect::DescribeClass(klass);
}
inline TypeInfo TypeOf(Object object) {
    return URK::Unity::Inspect::TypeOf(object);
}
inline ObjectRefInfo DescribeObject(Object object) {
    return URK::Unity::Inspect::DescribeObject(object);
}
inline ObjectRefInfo ExpandValue(const ValueInfo &value) {
    return URK::Unity::Inspect::ExpandValue(value);
}
inline ObjectHandle PinObject(Object object, bool pinned = false) {
    return URK::Unity::Inspect::PinObject(object, pinned);
}
inline ObjectHandle PinValue(const ValueInfo &value, bool pinned = false) {
    return URK::Unity::Inspect::PinValue(value, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    return URK::Unity::Inspect::WeakObject(object, trackResurrection);
}
inline Object ResolveObjectHandle(const ObjectHandle &handle) {
    return URK::Unity::Inspect::ResolveObjectHandle(handle);
}
inline void FreeObjectHandle(ObjectHandle &handle) {
    URK::Unity::Inspect::FreeObjectHandle(handle);
}
inline std::vector<FieldInfo> Fields(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(type, includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo &object, bool includeInherited = true) {
    return URK::Unity::Inspect::Fields(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(type, includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo &object, bool includeInherited = true) {
    return URK::Unity::Inspect::Methods(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(type, includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo &object, bool includeInherited = true) {
    return URK::Unity::Inspect::Properties(object, includeInherited);
}
inline ValueInfo ReadField(Object object, const FieldInfo &field) {
    return URK::Unity::Inspect::ReadField(object, field);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo &property) {
    return URK::Unity::Inspect::ReadProperty(object, property);
}
inline ValueInfo ReadArrayElement(const ValueInfo &array, std::size_t index) {
    return URK::Unity::Inspect::ReadArrayElement(array, index);
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo &method, const std::vector<ValueInfo> &arguments = {}) {
    return URK::Unity::Inspect::InvokeMethod(object, method, arguments);
}
inline bool SetField(Object object, const FieldInfo &field, const ValueInfo &value) {
    return URK::Unity::Inspect::SetField(object, field, value);
}
inline bool SetProperty(Object object, const PropertyInfo &property, const ValueInfo &value) {
    return URK::Unity::Inspect::SetProperty(object, property, value);
}
inline bool SetArrayElement(const ValueInfo &array, std::size_t index, const ValueInfo &value) {
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
)URKUNITY";
    std::string text = out.str();
    const std::string genericFindMethod =
        std::string("    static const void* find_method(const void* klass, "
                    "std::string_view name, int argc) { auto n=z(name); return URK::") +
        backendNs + "::resolve_method(static_cast<const URK::" + backendNs + "::Class*>(klass), n.c_str(), argc); }";
    const std::string genericInspect =
        R"URKUNITY(namespace Inspect {
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
    std::uint32_t handle = 0;
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
)URKUNITY";
    const std::string genericFindExact = "    static const void* find_method_exact(const void* klass, "
                                         "std::string_view name, const std::vector<const char*>& "
                                         "parameterTypeNames);";
    const std::string genericFieldStaticGet = "    static bool field_static_get_value(const void* klass, const void* "
                                              "field, void* output);";
    const std::string genericFieldStaticSet = "    static bool field_static_set_value(const void* klass, const void* "
                                              "field, void* value);";
    const std::string genericFindField = std::string("    static const void* find_field(const void* klass, "
                                                     "std::string_view name) { auto n=z(name); return URK::") +
                                         backendNs + "::find_field(static_cast<const URK::" + backendNs +
                                         "::Class*>(klass), n.c_str()); }";
    const std::string replacementFindMethod =
        mono ? R"URKUNITY(static const void *find_method(const void *klass, std::string_view name, int argc) {
    if (!klass) {
        set_error("Unity method lookup failed: class is null");
        return nullptr;
    }
    const std::string cacheKey = member_cache_key(klass, name, argc);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = method_cache().find(cacheKey);
        if (found != method_cache().end())
            return found->second;
    }
    auto n = z(name);
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        const void *match = nullptr;
        int matches = 0;
        while (const auto *m = URK::mono::class_get_methods(static_cast<const URK::mono::Class *>(current), &it)) {
            const char *mn = URK::mono::method_get_name(m);
            if (!mn || n != mn)
                continue;
            const auto *sig = URK::mono::method_signature(m);
            if (argc < 0 || (sig && static_cast<int>(URK::mono::signature_get_param_count(sig)) == argc)) {
                match = m;
                ++matches;
            }
        }
        if (matches > 1) {
            set_error(std::string("Unity method lookup failed: ambiguous overload by name/argc: ") + std::string(name));
            return nullptr;
        }
        if (match) {
            std::lock_guard<std::mutex> lock(cache_mutex());
            method_cache()[cacheKey] = match;
            return match;
        }
        current = URK::mono::class_get_parent(static_cast<const URK::mono::Class *>(current));
    }
    return nullptr;
}
)URKUNITY"
             : R"URKUNITY(static const void *find_method(const void *klass, std::string_view name, int argc) {
    if (!klass) {
        set_error("Unity method lookup failed: class is null");
        return nullptr;
    }
    const std::string cacheKey = member_cache_key(klass, name, argc);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = method_cache().find(cacheKey);
        if (found != method_cache().end())
            return found->second;
    }
    auto n = z(name);
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        const void *match = nullptr;
        int matches = 0;
        while (const auto *m = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class *>(current), &it)) {
            const char *mn = URK::il2cpp::method_get_name(m);
            if (!mn || n != mn)
                continue;
            if (argc < 0 || static_cast<int>(URK::il2cpp::method_get_param_count(m)) == argc) {
                match = m;
                ++matches;
            }
        }
        if (matches > 1) {
            set_error(std::string("Unity method lookup failed: ambiguous overload by name/argc: ") + std::string(name));
            return nullptr;
        }
        if (match) {
            std::lock_guard<std::mutex> lock(cache_mutex());
            method_cache()[cacheKey] = match;
            return match;
        }
        current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class *>(current));
    }
    return nullptr;
}
)URKUNITY";
    // Keep the large Inspect implementation backend-neutral. These tokens cover the
    // small metadata API differences that cannot be expressed by a namespace swap.
    const std::string replacementInspect = [mono] {
        std::string text = R"URKUNITY(namespace Inspect {
inline constexpr std::uint32_t kStaticMemberFlag = 0x0010u;
// Metadata names are normally UTF-8, but an obfuscator can intentionally
// return invalid byte sequences. Preserve valid names and give malformed names
// stable printable IDs so UI consumers can distinguish members.
inline bool metadata_name_is_valid_utf8(std::string_view text) {
    for (std::size_t index = 0; index < text.size();) {
        const unsigned char first = static_cast<unsigned char>(text[index]);
        if (first <= 0x7Fu) {
            ++index;
            continue;
        }
        const auto continuation = [&](std::size_t offset) {
            return index + offset < text.size() && (static_cast<unsigned char>(text[index + offset]) & 0xC0u) == 0x80u;
        };
        if (first >= 0xC2u && first <= 0xDFu && continuation(1)) {
            index += 2;
            continue;
        }
        if (first >= 0xE0u && first <= 0xEFu && continuation(1) && continuation(2)) {
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            if ((first != 0xE0u || second >= 0xA0u) && (first != 0xEDu || second <= 0x9Fu)) {
                index += 3;
                continue;
            }
        }
        if (first >= 0xF0u && first <= 0xF4u && continuation(1) && continuation(2) && continuation(3)) {
            const unsigned char second = static_cast<unsigned char>(text[index + 1]);
            if ((first != 0xF0u || second >= 0x90u) && (first != 0xF4u || second <= 0x8Fu)) {
                index += 4;
                continue;
            }
        }
        return false;
    }
    return true;
}
inline std::string metadata_display_name(const char *value) {
    if (!value || !value[0])
        return {};
    const std::string raw(value);
    if (metadata_name_is_valid_utf8(raw))
        return raw;
    constexpr char hex[] = "0123456789ABCDEF";
    constexpr std::size_t maxBytes = 24;
    const std::size_t byteCount = (std::min)(raw.size(), maxBytes);
    std::string display = "obf_";
    display.reserve(display.size() + byteCount * 2 + 12);
    for (std::size_t index = 0; index < byteCount; ++index) {
        const unsigned char byte = static_cast<unsigned char>(raw[index]);
        display.push_back(hex[byte >> 4u]);
        display.push_back(hex[byte & 0x0Fu]);
    }
    if (raw.size() > byteCount)
        display += "...";
    std::uint32_t hash = 2166136261u;
    for (const unsigned char byte : raw) {
        hash ^= byte;
        hash *= 16777619u;
    }
    display.push_back('_');
    for (int shift = 28; shift >= 0; shift -= 4)
        display.push_back(hex[(hash >> static_cast<unsigned>(shift)) & 0x0Fu]);
    return display;
}
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
    std::uint32_t handle = 0;
    bool weak = false;
    bool pinned = false;
};
inline void emit(DiagnosticSink sink, const char *text) {
    if (sink)
        sink(text);
}
inline std::string type_name(const void *type) {
    char out[256]{};
    return URK::@BACKEND @ ::type_get_name(static_cast<const URK::@BACKEND @ ::Type *>(type), out, sizeof(out))
               ? std::string(out)
               : std::string{};
}
inline TypeInfo DescribeClass(const void *klass) {
    TypeInfo out{};
    out.handle = klass;
    if (!klass)
        return out;
    const auto *k = static_cast<const URK::@BACKEND @ ::Class *>(klass);
    const char *ns = URK::@BACKEND @ ::class_get_namespace(k);
    const char *name = URK::@BACKEND @ ::class_get_name(k);
    out.namespc = ns ? ns : "";
    out.name = name ? name : "";
    out.full_name = out.namespc.empty() ? out.name : out.namespc + "." + out.name;
    out.flags = URK::@BACKEND @ ::class_get_flags(k);
    out.is_value_type = URK::@BACKEND @ ::class_is_valuetype(k);
    out.is_enum = URK::@BACKEND @ ::class_is_enum(k);
    return out;
}
inline TypeInfo DescribeType(const void *type) {
    return DescribeClass(URK::@BACKEND @ ::@TYPE_GET_CLASS @(static_cast<const URK::@BACKEND @ ::Type *>(type)));
}
inline TypeInfo TypeOf(Object object) {
    detail::clear_error();
    const void *klass = detail::Backend::object_get_class(object.handle());
    if (!klass) {
        detail::set_error("Unity Inspect::TypeOf failed: object_get_class failed");
        detail::append_backend_error();
        return {};
    }
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
inline ObjectRefInfo ExpandValue(const ValueInfo &value) {
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
    if (!object.handle()) {
        detail::set_error("Unity Inspect::PinObject failed: object is null");
        return out;
    }
    out.handle = detail::Backend::gchandle_new(object.handle(), pinned ? 1 : 0);
    if (!out.handle) {
        detail::set_error("Unity Inspect::PinObject failed: backend gchandle_new "
                          "API is unavailable or failed");
        detail::append_backend_error();
    }
    return out;
}
inline ObjectHandle PinValue(const ValueInfo &value, bool pinned = false) {
    if ((value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference &&
         value.kind != ValueKind::String) ||
        !value.object) {
        detail::set_error("Unity Inspect::PinValue failed: value is not a managed "
                          "object reference");
        return {};
    }
    return PinObject(Object{value.object}, pinned);
}
inline ObjectHandle WeakObject(Object object, bool trackResurrection = false) {
    detail::clear_error();
    ObjectHandle out{};
    out.weak = true;
    if (!object.handle()) {
        detail::set_error("Unity Inspect::WeakObject failed: object is null");
        return out;
    }
    out.handle = detail::Backend::gchandle_new_weakref(object.handle(), trackResurrection ? 1 : 0);
    if (!out.handle) {
        detail::set_error("Unity Inspect::WeakObject failed: backend "
                          "gchandle_new_weakref API is unavailable or failed");
        detail::append_backend_error();
    }
    return out;
}
inline Object ResolveObjectHandle(const ObjectHandle &handle) {
    detail::clear_error();
    if (!handle.handle) {
        detail::set_error("Unity Inspect::ResolveObjectHandle failed: handle is empty");
        return {};
    }
    void *object = detail::Backend::gchandle_get_target(handle.handle);
    if (!object) {
        detail::set_error("Unity Inspect::ResolveObjectHandle failed: backend "
                          "gchandle_get_target API is unavailable, failed, or "
                          "target was collected");
        detail::append_backend_error();
        return {};
    }
    return Object{object};
}
inline void FreeObjectHandle(ObjectHandle &handle) {
    detail::clear_error();
    if (!handle.handle)
        return;
    detail::Backend::gchandle_free(handle.handle);
    handle.handle = 0;
    handle.weak = false;
    handle.pinned = false;
}
inline std::vector<FieldInfo> fields_from_class(const URK::@BACKEND @ ::Class *klass, bool includeInherited) {
    std::vector<FieldInfo> out;
    for (const void *current = klass; current;
         current = includeInherited ? URK::@BACKEND
                       @ ::class_get_parent(static_cast<const URK::@BACKEND @ ::Class *>(current))
                                    : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void *it = nullptr;
        while (const auto *field =
                   URK::@BACKEND @ ::class_get_fields(static_cast<const URK::@BACKEND @ ::Class *>(current), &it)) {
            FieldInfo info{};
            info.handle = field;
            info.declaring_type = declaring;
            const char *name = URK::@BACKEND @ ::field_get_name(field);
            info.name = name ? name : "";
            const void *fieldType = URK::@BACKEND @ ::field_get_type(field);
            const TypeInfo fieldTypeInfo = DescribeType(fieldType);
            info.type = fieldType;
            info.type_name = type_name(fieldType);
            info.flags = URK::@BACKEND @ ::field_get_flags(field);
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
    const void *klass = type.resolve_class();
    if (!klass) {
        detail::set_error("Unity Inspect::Fields failed: type lookup failure");
        detail::append_backend_error();
        return {};
    }
    return fields_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void *klass = detail::Backend::object_get_class(object.handle());
    if (!klass) {
        detail::set_error("Unity Inspect::Fields failed: object_get_class failed");
        detail::append_backend_error();
        return {};
    }
    return fields_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<FieldInfo> Fields(const ObjectRefInfo &object, bool includeInherited = true) {
    return object.handle ? Fields(Object{object.handle}, includeInherited) : std::vector<FieldInfo>{};
}
inline MethodInfo method_info(const URK::@BACKEND @ ::Method *method, TypeInfo declaring) {
    MethodInfo info{};
    info.handle = method;
    info.declaring_type = std::move(declaring);
    const char *name = URK::@BACKEND @ ::method_get_name(method);
    info.name = metadata_display_name(name);
    info.flags = URK::@BACKEND @ ::method_get_flags(method, &info.iflags);
    info.is_static = (info.flags & kStaticMemberFlag) != 0;
    info.return_type_handle = URK::@BACKEND @ ::method_get_return_type(method);
    info.return_type = type_name(info.return_type_handle);
    @METHOD_PARAMETERS @ return info;
}
inline std::vector<MethodInfo> methods_from_class(const URK::@BACKEND @ ::Class *klass, bool includeInherited) {
    std::vector<MethodInfo> out;
    for (const void *current = klass; current;
         current = includeInherited ? URK::@BACKEND
                       @ ::class_get_parent(static_cast<const URK::@BACKEND @ ::Class *>(current))
                                    : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void *it = nullptr;
        while (const auto *method =
                   URK::@BACKEND @ ::class_get_methods(static_cast<const URK::@BACKEND @ ::Class *>(current), &it))
            out.push_back(method_info(method, declaring));
    }
    return out;
}
inline std::vector<MethodInfo> Methods(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void *klass = type.resolve_class();
    if (!klass) {
        detail::set_error("Unity Inspect::Methods failed: type lookup failure");
        detail::append_backend_error();
        return {};
    }
    return methods_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void *klass = detail::Backend::object_get_class(object.handle());
    if (!klass) {
        detail::set_error("Unity Inspect::Methods failed: object_get_class failed");
        detail::append_backend_error();
        return {};
    }
    return methods_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<MethodInfo> Methods(const ObjectRefInfo &object, bool includeInherited = true) {
    return object.handle ? Methods(Object{object.handle}, includeInherited) : std::vector<MethodInfo>{};
}
inline PropertyInfo property_info(const URK::@BACKEND @ ::Property *property, TypeInfo declaring) {
    PropertyInfo info{};
    info.handle = property;
    info.declaring_type = std::move(declaring);
    const char *name = URK::@BACKEND @ ::property_get_name(property);
    info.name = name ? name : "";
    info.flags = URK::@BACKEND @ ::property_get_flags(property);
    info.get_method = URK::@BACKEND @ ::property_get_get_method(property);
    info.set_method = URK::@BACKEND @ ::property_get_set_method(property);
    info.can_read = info.get_method != nullptr;
    info.can_write = info.set_method != nullptr;
    const void *propertyType =
        info.get_method ? URK::@BACKEND
            @ ::method_get_return_type(static_cast<const URK::@BACKEND @ ::Method *>(info.get_method))
                        : (info.set_method ? URK::@BACKEND @ ::@METHOD_GET_PARAM
                               @(static_cast<const URK::@BACKEND @ ::Method *>(info.set_method), 0)
                                           : nullptr);
    const TypeInfo propertyTypeInfo = DescribeType(propertyType);
    info.type = propertyType;
    info.type_name = type_name(propertyType);
    info.is_value_type = propertyTypeInfo.is_value_type;
    info.is_enum = propertyTypeInfo.is_enum;
    return info;
}
inline std::vector<PropertyInfo> properties_from_class(const URK::@BACKEND @ ::Class *klass, bool includeInherited) {
    std::vector<PropertyInfo> out;
    for (const void *current = klass; current;
         current = includeInherited ? URK::@BACKEND
                       @ ::class_get_parent(static_cast<const URK::@BACKEND @ ::Class *>(current))
                                    : nullptr) {
        const TypeInfo declaring = DescribeClass(current);
        void *it = nullptr;
        while (const auto *property =
                   URK::@BACKEND @ ::class_get_properties(static_cast<const URK::@BACKEND @ ::Class *>(current), &it))
            out.push_back(property_info(property, declaring));
    }
    return out;
}
inline std::vector<PropertyInfo> Properties(TypeRef type, bool includeInherited = true) {
    detail::clear_error();
    const void *klass = type.resolve_class();
    if (!klass) {
        detail::set_error("Unity Inspect::Properties failed: type lookup failure");
        detail::append_backend_error();
        return {};
    }
    return properties_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(Object object, bool includeInherited = true) {
    detail::clear_error();
    const void *klass = detail::Backend::object_get_class(object.handle());
    if (!klass) {
        detail::set_error("Unity Inspect::Properties failed: object_get_class failed");
        detail::append_backend_error();
        return {};
    }
    return properties_from_class(static_cast<const URK::@BACKEND @ ::Class *>(klass), includeInherited);
}
inline std::vector<PropertyInfo> Properties(const ObjectRefInfo &object, bool includeInherited = true) {
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
inline std::string enum_underlying_type_name(const void *type) {
    const void *klass =
        type ? URK::@BACKEND @ ::@TYPE_GET_CLASS @(static_cast<const URK::@BACKEND @ ::Type *>(type)) : nullptr;
    if (!klass)
        return {};
    @ENUM_BASETYPE @ void *it = nullptr;
    while (const auto *field =
               URK::@BACKEND @ ::class_get_fields(static_cast<const URK::@BACKEND @ ::Class *>(klass), &it)) {
        const char *name = URK::@BACKEND @ ::field_get_name(field);
        if (name && std::string_view{name} == "value__")
            return type_name(URK::@BACKEND @ ::field_get_type(field));
    }
    return {};
}
inline ValueInfo scalar_from_pointer(std::string typeName, void *data);
inline ValueInfo enum_from_pointer(std::string typeName, std::string underlyingTypeName, void *data) {
    ValueInfo out = scalar_from_pointer(underlyingTypeName, data);
    if (!out.readable)
        return unavailable_value(std::move(typeName),
                                 std::string("enum underlying type is unsupported: ") + underlyingTypeName);
    out.kind = ValueKind::Enum;
    out.type_name = std::move(typeName);
    return out;
}
inline bool type_name_looks_array(std::string_view typeName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.array")
        return true;
    if (normalized.size() >= 2 && normalized.compare(normalized.size() - 2, 2, "[]") == 0)
        return true;
    const std::size_t open = normalized.rfind('[');
    return open != std::string::npos && !normalized.empty() && normalized.back() == ']' &&
           open + 1 < normalized.size() && normalized[open + 1] == ',';
}
inline ValueInfo array_reference_value(std::string typeName, void *object) {
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
inline ValueInfo object_reference_value(std::string typeName, void *object) {
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.object = object;
    out.readable = true;
    if (!object) {
        out.kind = ValueKind::Null;
        out.display = "null";
        return out;
    }
    if (type_name_looks_array(out.type_name))
        return array_reference_value(out.type_name, object);
    out.kind = ValueKind::ObjectReference;
    out.display = detail::class_display_name(detail::Backend::object_get_class(object));
    if (out.display.empty())
        out.display = "<object>";
    return out;
}
inline ValueInfo string_value(std::string typeName, void *object) {
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
inline ValueInfo scalar_from_pointer(std::string typeName, void *data) {
    if (!data)
        return unavailable_value(std::move(typeName), "value data is null");
    const std::string normalized = detail::normalized_type_name(typeName);
    ValueInfo out{};
    out.type_name = std::move(typeName);
    out.readable = true;
    if (normalized == "system.boolean") {
        out.kind = ValueKind::Boolean;
        out.bool_value = *static_cast<bool *>(data);
        out.display = out.bool_value ? "true" : "false";
        return out;
    }
    if (normalized == "system.int16") {
        out.kind = ValueKind::SignedInteger;
        out.signed_value = *static_cast<std::int16_t *>(data);
        out.display = std::to_string(out.signed_value);
        return out;
    }
    if (normalized == "system.int32") {
        out.kind = ValueKind::SignedInteger;
        out.signed_value = *static_cast<std::int32_t *>(data);
        out.display = std::to_string(out.signed_value);
        return out;
    }
    if (normalized == "system.int64") {
        out.kind = ValueKind::SignedInteger;
        out.signed_value = *static_cast<std::int64_t *>(data);
        out.display = std::to_string(out.signed_value);
        return out;
    }
    if (normalized == "system.sbyte") {
        out.kind = ValueKind::SignedInteger;
        out.signed_value = *static_cast<std::int8_t *>(data);
        out.display = std::to_string(out.signed_value);
        return out;
    }
    if (normalized == "system.uint16" || normalized == "system.char") {
        out.kind = ValueKind::UnsignedInteger;
        out.unsigned_value = *static_cast<std::uint16_t *>(data);
        out.display = std::to_string(out.unsigned_value);
        return out;
    }
    if (normalized == "system.uint32") {
        out.kind = ValueKind::UnsignedInteger;
        out.unsigned_value = *static_cast<std::uint32_t *>(data);
        out.display = std::to_string(out.unsigned_value);
        return out;
    }
    if (normalized == "system.uint64") {
        out.kind = ValueKind::UnsignedInteger;
        out.unsigned_value = *static_cast<std::uint64_t *>(data);
        out.display = std::to_string(out.unsigned_value);
        return out;
    }
    if (normalized == "system.byte") {
        out.kind = ValueKind::UnsignedInteger;
        out.unsigned_value = *static_cast<std::uint8_t *>(data);
        out.display = std::to_string(out.unsigned_value);
        return out;
    }
    if (normalized == "system.single") {
        out.kind = ValueKind::FloatingPoint;
        out.floating_value = *static_cast<float *>(data);
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.6g", out.floating_value);
        out.display = text;
        return out;
    }
    if (normalized == "system.double") {
        out.kind = ValueKind::FloatingPoint;
        out.floating_value = *static_cast<double *>(data);
        char text[64]{};
        std::snprintf(text, sizeof(text), "%.12g", out.floating_value);
        out.display = text;
        return out;
    }
    return value_type_placeholder(out.type_name);
}
inline bool read_field_raw(Object object, const FieldInfo &field, void *output) {
    if (!field.handle) {
        detail::set_error("Unity Inspect::ReadField failed: field handle is null");
        return false;
    }
    if (field.is_static) {
        if (!field.declaring_type.handle) {
            detail::set_error("Unity Inspect::ReadField failed: static field "
                              "declaring type is unavailable");
            return false;
        }
        if (!detail::Backend::field_static_get_value(field.declaring_type.handle, field.handle, output)) {
            detail::set_error(std::string("Unity Inspect::ReadField failed: static field read failed: ") + field.name);
            detail::append_backend_error();
            return false;
        }
        return true;
    }
    if (!object.handle()) {
        detail::set_error("Unity Inspect::ReadField failed: target object is null");
        return false;
    }
    if (!detail::Backend::field_get_value(object.handle(), field.handle, output)) {
        detail::set_error(std::string("Unity Inspect::ReadField failed: field read failed: ") + field.name);
        detail::append_backend_error();
        return false;
    }
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
    void *reference{};
};
inline const char *value_kind_name(ValueKind kind) {
    switch (kind) {
        case ValueKind::Unavailable:
            return "Unavailable";
        case ValueKind::Null:
            return "Null";
        case ValueKind::Boolean:
            return "Boolean";
        case ValueKind::SignedInteger:
            return "SignedInteger";
        case ValueKind::UnsignedInteger:
            return "UnsignedInteger";
        case ValueKind::FloatingPoint:
            return "FloatingPoint";
        case ValueKind::String:
            return "String";
        case ValueKind::ObjectReference:
            return "ObjectReference";
        case ValueKind::ArrayReference:
            return "ArrayReference";
        case ValueKind::Enum:
            return "Enum";
        case ValueKind::ValueType:
            return "ValueType";
    }
    return "<unknown>";
}
template <class T>
inline bool assign_integral_value(const ValueInfo &value, T &output, std::string_view targetType,
                                  std::string_view context) {
    if (value.kind == ValueKind::UnsignedInteger || (value.kind == ValueKind::Enum && std::is_unsigned_v<T>)) {
        const std::uint64_t unsignedValue = value.unsigned_value;
        if constexpr (std::is_signed_v<T>) {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                  " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (unsignedValue > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                  " failed: unsigned integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(unsignedValue);
        return true;
    }
    if (value.kind == ValueKind::SignedInteger || value.kind == ValueKind::Enum) {
        const std::int64_t signedValue = value.signed_value;
        if constexpr (std::is_unsigned_v<T>) {
            if (signedValue < 0 ||
                static_cast<std::uint64_t>(signedValue) > static_cast<std::uint64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                  " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        } else {
            if (signedValue < static_cast<std::int64_t>(std::numeric_limits<T>::min()) ||
                signedValue > static_cast<std::int64_t>(std::numeric_limits<T>::max())) {
                detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                                  " failed: signed integer is out of range for " + std::string(targetType));
                return false;
            }
        }
        output = static_cast<T>(signedValue);
        return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected integer value for " +
                      std::string(targetType) + ", got " + value_kind_name(value.kind));
    return false;
}
inline bool scalar_write_pointer(std::string_view typeName, const ValueInfo &value, WriteStorage &storage,
                                 void *&pointer, std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") {
        if (value.kind != ValueKind::Boolean) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) + " failed: expected Boolean for " +
                              std::string(typeName) + ", got " + value_kind_name(value.kind));
            return false;
        }
        storage.b = value.bool_value;
        pointer = &storage.b;
        return true;
    }
    if (normalized == "system.sbyte") {
        if (!assign_integral_value(value, storage.i8, typeName, context))
            return false;
        pointer = &storage.i8;
        return true;
    }
    if (normalized == "system.int16") {
        if (!assign_integral_value(value, storage.i16, typeName, context))
            return false;
        pointer = &storage.i16;
        return true;
    }
    if (normalized == "system.int32") {
        if (!assign_integral_value(value, storage.i32, typeName, context))
            return false;
        pointer = &storage.i32;
        return true;
    }
    if (normalized == "system.int64") {
        if (!assign_integral_value(value, storage.i64, typeName, context))
            return false;
        pointer = &storage.i64;
        return true;
    }
    if (normalized == "system.byte") {
        if (!assign_integral_value(value, storage.u8, typeName, context))
            return false;
        pointer = &storage.u8;
        return true;
    }
    if (normalized == "system.uint16" || normalized == "system.char") {
        if (!assign_integral_value(value, storage.u16, typeName, context))
            return false;
        pointer = &storage.u16;
        return true;
    }
    if (normalized == "system.uint32") {
        if (!assign_integral_value(value, storage.u32, typeName, context))
            return false;
        pointer = &storage.u32;
        return true;
    }
    if (normalized == "system.uint64") {
        if (!assign_integral_value(value, storage.u64, typeName, context))
            return false;
        pointer = &storage.u64;
        return true;
    }
    if (normalized == "system.single") {
        if (value.kind != ValueKind::FloatingPoint) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: expected FloatingPoint for " + std::string(typeName) + ", got " +
                              value_kind_name(value.kind));
            return false;
        }
        if (value.floating_value > static_cast<double>(std::numeric_limits<float>::max()) ||
            value.floating_value < -static_cast<double>(std::numeric_limits<float>::max())) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: floating point value is out of range for " + std::string(typeName));
            return false;
        }
        storage.f32 = static_cast<float>(value.floating_value);
        pointer = &storage.f32;
        return true;
    }
    if (normalized == "system.double") {
        if (value.kind != ValueKind::FloatingPoint) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: expected FloatingPoint for " + std::string(typeName) + ", got " +
                              value_kind_name(value.kind));
            return false;
        }
        storage.f64 = value.floating_value;
        pointer = &storage.f64;
        return true;
    }
    detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                      " failed: unsupported value type write: " + std::string(typeName));
    return false;
}
inline bool reference_write_value(std::string_view typeName, const ValueInfo &value, WriteStorage &storage,
                                  std::string_view context) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (value.kind == ValueKind::Null) {
        storage.reference = nullptr;
        return true;
    }
    if (normalized == "system.string") {
        if (value.kind != ValueKind::String) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: expected String or Null for " + std::string(typeName) + ", got " +
                              value_kind_name(value.kind));
            return false;
        }
        storage.reference = value.object ? value.object : detail::Backend::new_string(value.display);
        if (!storage.reference) {
            detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                              " failed: managed string allocation failed");
            detail::append_backend_error();
            return false;
        }
        return true;
    }
    if (value.kind != ValueKind::ObjectReference && value.kind != ValueKind::ArrayReference) {
        detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                          " failed: expected ObjectReference, ArrayReference, or Null for " + std::string(typeName) +
                          ", got " + value_kind_name(value.kind));
        return false;
    }
    if (!value.object) {
        detail::set_error(std::string("Unity Inspect::") + std::string(context) +
                          " failed: reference value object is null but kind is " + value_kind_name(value.kind));
        return false;
    }
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
    if (normalized == "system.boolean" || normalized == "system.byte" || normalized == "system.sbyte")
        return 1;
    if (normalized == "system.int16" || normalized == "system.uint16" || normalized == "system.char")
        return 2;
    if (normalized == "system.int32" || normalized == "system.uint32" || normalized == "system.single")
        return 4;
    if (normalized == "system.int64" || normalized == "system.uint64" || normalized == "system.double")
        return 8;
    return 0;
}
inline bool validate_array_element_access(const ValueInfo &array, std::size_t index, std::size_t &length) {
    if (array.kind != ValueKind::ArrayReference || !array.object) {
        detail::set_error("Unity Inspect array element access failed: value is not "
                          "an array reference");
        return false;
    }
    if (!detail::Backend::has_array_length()) {
        detail::set_error("Unity Inspect array element access failed: backend "
                          "array_length API is unavailable");
        detail::append_backend_error();
        return false;
    }
    length = detail::Backend::array_length(array.object);
    if (index >= length) {
        detail::set_error("Unity Inspect array element access failed: index out of range");
        return false;
    }
    return true;
}
inline ValueInfo ReadArrayElement(const ValueInfo &array, std::size_t index) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length))
        return unavailable_value(array.type_name,
                                 detail::fallback_error() ? detail::fallback_error() : "array element access failed");
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty())
        return unavailable_value(array.type_name,
                                 std::string("array element type unsupported or not single-dimensional: ") +
                                     array.type_name);
    const int elementSize = scalar_element_size(elementType);
    if (elementSize > 0) {
        void *slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
        if (!slot) {
            detail::set_error(std::string("Unity Inspect::ReadArrayElement failed: "
                                          "array_addr_with_size failed for ") +
                              elementType);
            detail::append_backend_error();
            return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error()
                                                                           : "array element address failed");
        }
        return scalar_from_pointer(elementType, slot);
    }
    if (!detail::Backend::has_array_ref_at()) {
        detail::set_error("Unity Inspect::ReadArrayElement failed: backend "
                          "array_ref_at API is unavailable");
        detail::append_backend_error();
        return unavailable_value(elementType, detail::fallback_error() ? detail::fallback_error()
                                                                       : "array reference element access failed");
    }
    void *ref = detail::Backend::array_ref_at(array.object, index);
    return detail::normalized_type_name(elementType) == "system.string" ? string_value(elementType, ref)
                                                                        : object_reference_value(elementType, ref);
}
inline bool SetArrayElement(const ValueInfo &array, std::size_t index, const ValueInfo &value) {
    detail::clear_error();
    std::size_t length = 0;
    if (!validate_array_element_access(array, index, length))
        return false;
    (void)length;
    const std::string elementType = array_element_type_name(array.type_name);
    if (elementType.empty()) {
        detail::set_error(std::string("Unity Inspect::SetArrayElement failed: array element type "
                                      "unsupported or not single-dimensional: ") +
                          array.type_name);
        return false;
    }
    WriteStorage storage{};
    const int elementSize = scalar_element_size(elementType);
    if (elementSize <= 0) {
        if (!detail::Backend::has_array_set_ref()) {
            detail::set_error(std::string("Unity Inspect::SetArrayElement failed: backend "
                                          "array_set_ref API is unavailable for ") +
                              array.type_name);
            detail::append_backend_error();
            return false;
        }
        if (!reference_write_value(elementType, value, storage, "SetArrayElement"))
            return false;
        if (!detail::Backend::array_set_ref(array.object, index, storage.reference)) {
            detail::set_error(std::string("Unity Inspect::SetArrayElement failed: "
                                          "reference array write failed for ") +
                              array.type_name);
            detail::append_backend_error();
            return false;
        }
        return true;
    }
    void *source = nullptr;
    if (!scalar_write_pointer(elementType, value, storage, source, "SetArrayElement"))
        return false;
    void *slot = detail::Backend::array_addr_with_size(array.object, elementSize, index);
    if (!slot) {
        detail::set_error(std::string("Unity Inspect::SetArrayElement failed: "
                                      "array_addr_with_size failed for ") +
                          elementType);
        detail::append_backend_error();
        return false;
    }
    std::memcpy(slot, source, static_cast<std::size_t>(elementSize));
    return true;
}
inline bool method_argument_pointer(const MethodParamInfo &parameter, const ValueInfo &value, WriteStorage &storage,
                                    void *&pointer) {
    const TypeInfo type = DescribeType(parameter.type);
    if (!type.handle && parameter.type_name.empty()) {
        detail::set_error("Unity Inspect::InvokeMethod failed: parameter type "
                          "metadata is unavailable");
        return false;
    }
    const std::string normalized = detail::normalized_type_name(parameter.type_name);
    if (type.is_enum) {
        const std::string underlying = enum_underlying_type_name(parameter.type);
        if (underlying.empty()) {
            detail::set_error(std::string("Unity Inspect::InvokeMethod failed: enum "
                                          "parameter underlying type unavailable: ") +
                              parameter.name);
            return false;
        }
        return scalar_write_pointer(underlying, value, storage, pointer, "InvokeMethod");
    }
    if (normalized == "system.string" || !type.is_value_type)
        return reference_write_value(parameter.type_name, value, storage, "InvokeMethod")
                   ? (pointer = storage.reference, true)
                   : false;
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
inline ValueInfo invoke_result_value(std::string typeName, const void *type, void *result,
                                     std::string_view methodName) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.void" || typeName.empty())
        return void_value();
    if (normalized == "system.string")
        return string_value(std::move(typeName), result);
    const TypeInfo resultType = DescribeType(type);
    if (!resultType.is_value_type)
        return object_reference_value(std::move(typeName), result);
    if (!result)
        return unavailable_value(std::move(typeName),
                                 std::string("Unity Inspect::InvokeMethod failed: value-type result is null: ") +
                                     std::string(methodName));
    void *raw = detail::Backend::object_unbox(result);
    if (!raw) {
        detail::set_error(std::string("Unity Inspect::InvokeMethod failed: "
                                      "object_unbox failed for result: ") +
                          std::string(methodName));
        detail::append_backend_error();
        return unavailable_value(std::move(typeName),
                                 detail::fallback_error() ? detail::fallback_error() : "method result unbox failed");
    }
    if (resultType.is_enum) {
        const std::string underlying = enum_underlying_type_name(type);
        if (underlying.empty())
            return unavailable_value(std::move(typeName), std::string("enum result underlying type unavailable: ") +
                                                              std::string(methodName));
        return enum_from_pointer(std::move(typeName), underlying, raw);
    }
    return scalar_from_pointer(std::move(typeName), raw);
}
inline ValueInfo InvokeMethod(Object object, const MethodInfo &method, const std::vector<ValueInfo> &arguments = {}) {
    detail::clear_error();
    if (!method.handle)
        return unavailable_value(method.return_type, "Unity Inspect::InvokeMethod failed: method handle is null");
    if (arguments.size() != method.parameters.size())
        return unavailable_value(method.return_type, std::string("Unity Inspect::InvokeMethod failed: "
                                                                 "argument count mismatch for ") +
                                                         method.name);
    if (!method.is_static && !object.handle())
        return unavailable_value(method.return_type,
                                 std::string("Unity Inspect::InvokeMethod failed: target object is null "
                                             "for instance method: ") +
                                     method.name);
    std::vector<WriteStorage> storage(arguments.size());
    std::vector<void *> argv(arguments.size(), nullptr);
    for (std::size_t i = 0; i < arguments.size(); ++i)
        if (!method_argument_pointer(method.parameters[i], arguments[i], storage[i], argv[i]))
            return unavailable_value(method.return_type, detail::fallback_error()
                                                             ? detail::fallback_error()
                                                             : "method argument conversion failed");
    void *result = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(method.handle, method.is_static ? nullptr : object.handle(),
                                         argv.empty() ? nullptr : argv.data(), &result, &ex) ||
        ex) {
        detail::set_error(std::string("Unity Inspect::InvokeMethod failed: "
                                      "runtime_invoke threw or failed: ") +
                          method.name);
        detail::append_backend_error();
        return unavailable_value(method.return_type,
                                 detail::fallback_error() ? detail::fallback_error() : "method invocation failed");
    }
    return invoke_result_value(method.return_type, method.return_type_handle, result, method.name);
}
inline bool write_field_raw(Object object, const FieldInfo &field, void *value) {
    if (!field.handle) {
        detail::set_error("Unity Inspect::SetField failed: field handle is null");
        return false;
    }
    if (field.is_static) {
        if (!field.declaring_type.handle) {
            detail::set_error("Unity Inspect::SetField failed: static field "
                              "declaring type is unavailable");
            return false;
        }
        if (!detail::Backend::field_static_set_value(field.declaring_type.handle, field.handle, value)) {
            detail::set_error(std::string("Unity Inspect::SetField failed: static field write failed: ") + field.name);
            detail::append_backend_error();
            return false;
        }
        return true;
    }
    if (!object.handle()) {
        detail::set_error("Unity Inspect::SetField failed: target object is null");
        return false;
    }
    if (!detail::Backend::field_set_value(object.handle(), field.handle, value)) {
        detail::set_error(std::string("Unity Inspect::SetField failed: field write failed: ") + field.name);
        detail::append_backend_error();
        return false;
    }
    return true;
}
inline bool read_field_scalar_pointer(Object object, const FieldInfo &field, std::string_view typeName,
                                      WriteStorage &storage, void *&pointer) {
    const std::string normalized = detail::normalized_type_name(typeName);
    if (normalized == "system.boolean") {
        pointer = &storage.b;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.sbyte") {
        pointer = &storage.i8;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.int16") {
        pointer = &storage.i16;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.int32") {
        pointer = &storage.i32;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.int64") {
        pointer = &storage.i64;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.byte") {
        pointer = &storage.u8;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.uint16" || normalized == "system.char") {
        pointer = &storage.u16;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.uint32") {
        pointer = &storage.u32;
        return read_field_raw(object, field, pointer);
    }
    if (normalized == "system.uint64") {
        pointer = &storage.u64;
        return read_field_raw(object, field, pointer);
    }
    detail::set_error(std::string("Unity Inspect::ReadField failed: unsupported scalar field type: ") +
                      std::string(typeName));
    return false;
}
inline ValueInfo ReadField(Object object, const FieldInfo &field) {
    detail::clear_error();
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (normalized == "system.string") {
        void *ref = nullptr;
        return read_field_raw(object, field, &ref)
                   ? string_value(field.type_name, ref)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (!field.is_value_type) {
        void *ref = nullptr;
        return read_field_raw(object, field, &ref)
                   ? object_reference_value(field.type_name, ref)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty())
            return unavailable_value(field.type_name, std::string("enum underlying type unavailable: ") + field.name);
        WriteStorage storage{};
        void *raw = nullptr;
        return read_field_scalar_pointer(object, field, underlying, storage, raw)
                   ? enum_from_pointer(field.type_name, underlying, raw)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "enum field read failed");
    }
    if (normalized == "system.boolean") {
        bool value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.int16") {
        std::int16_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.int32") {
        std::int32_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.int64") {
        std::int64_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.sbyte") {
        std::int8_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.uint16" || normalized == "system.char") {
        std::uint16_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.uint32") {
        std::uint32_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.uint64") {
        std::uint64_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.byte") {
        std::uint8_t value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.single") {
        float value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    if (normalized == "system.double") {
        double value{};
        return read_field_raw(object, field, &value)
                   ? scalar_from_pointer(field.type_name, &value)
                   : unavailable_value(field.type_name,
                                       detail::fallback_error() ? detail::fallback_error() : "field read failed");
    }
    return value_type_placeholder(field.type_name);
}
inline ValueInfo ReadProperty(Object object, const PropertyInfo &property) {
    detail::clear_error();
    if (!property.can_read || !property.get_method)
        return unavailable_value(property.type_name, std::string("property is not readable: ") + property.name);
    void *result = nullptr;
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.get_method, object.handle(), nullptr, &result, &ex) || ex) {
        detail::set_error(std::string("Unity Inspect::ReadProperty failed: getter "
                                      "threw or could not be invoked: ") +
                          property.name);
        detail::append_backend_error();
        return unavailable_value(property.type_name,
                                 detail::fallback_error() ? detail::fallback_error() : "property read failed");
    }
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (normalized == "system.string")
        return string_value(property.type_name, result);
    if (!property.is_value_type)
        return object_reference_value(property.type_name, result);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty())
            return unavailable_value(property.type_name,
                                     std::string("enum underlying type unavailable: ") + property.name);
        void *raw = detail::Backend::object_unbox(result);
        if (!raw) {
            detail::set_error(std::string("Unity Inspect::ReadProperty failed: enum "
                                          "object_unbox failed: ") +
                              property.name);
            detail::append_backend_error();
            return unavailable_value(property.type_name, detail::fallback_error() ? detail::fallback_error()
                                                                                  : "enum property unbox failed");
        }
        return enum_from_pointer(property.type_name, underlying, raw);
    }
    void *raw = detail::Backend::object_unbox(result);
    if (!raw) {
        detail::set_error(std::string("Unity Inspect::ReadProperty failed: object_unbox failed: ") + property.name);
        detail::append_backend_error();
        return unavailable_value(property.type_name,
                                 detail::fallback_error() ? detail::fallback_error() : "property unbox failed");
    }
    return scalar_from_pointer(property.type_name, raw);
}
inline bool SetField(Object object, const FieldInfo &field, const ValueInfo &value) {
    detail::clear_error();
    if (!field.handle) {
        detail::set_error("Unity Inspect::SetField failed: field handle is null");
        return false;
    }
    WriteStorage storage{};
    const std::string normalized = detail::normalized_type_name(field.type_name);
    if (field.is_enum) {
        const std::string underlying = enum_underlying_type_name(field.type);
        if (underlying.empty()) {
            detail::set_error(std::string("Unity Inspect::SetField failed: enum "
                                          "underlying type unavailable: ") +
                              field.type_name);
            return false;
        }
        void *pointer = nullptr;
        if (!scalar_write_pointer(underlying, value, storage, pointer, "SetField"))
            return false;
        return write_field_raw(object, field, pointer);
    }
    if (normalized == "system.string" || !field.is_value_type) {
        if (!reference_write_value(field.type_name, value, storage, "SetField"))
            return false;
        return write_field_raw(object, field, detail::Backend::field_reference_write_pointer(storage.reference));
    }
    void *pointer = nullptr;
    if (!scalar_write_pointer(field.type_name, value, storage, pointer, "SetField"))
        return false;
    return write_field_raw(object, field, pointer);
}
inline bool SetProperty(Object object, const PropertyInfo &property, const ValueInfo &value) {
    detail::clear_error();
    if (!property.can_write || !property.set_method) {
        detail::set_error(std::string("Unity Inspect::SetProperty failed: property is not writable: ") + property.name);
        return false;
    }
    WriteStorage storage{};
    void *arg = nullptr;
    const std::string normalized = detail::normalized_type_name(property.type_name);
    if (property.is_enum) {
        const std::string underlying = enum_underlying_type_name(property.type);
        if (underlying.empty()) {
            detail::set_error(std::string("Unity Inspect::SetProperty failed: enum "
                                          "underlying type unavailable: ") +
                              property.type_name);
            return false;
        }
        if (!scalar_write_pointer(underlying, value, storage, arg, "SetProperty"))
            return false;
    } else if (normalized == "system.string" || !property.is_value_type) {
        if (!reference_write_value(property.type_name, value, storage, "SetProperty"))
            return false;
        arg = storage.reference;
    } else if (!scalar_write_pointer(property.type_name, value, storage, arg, "SetProperty")) {
        return false;
    }
    void *args[] = {arg};
    void *ex = nullptr;
    if (!detail::Backend::runtime_invoke(property.set_method, object.handle(), args, nullptr, &ex) || ex) {
        detail::set_error(std::string("Unity Inspect::SetProperty failed: setter "
                                      "threw or could not be invoked: ") +
                          property.name);
        detail::append_backend_error();
        return false;
    }
    return true;
}
inline void DumpFields(TypeRef type, DiagnosticSink sink = nullptr) {
    for (const auto &f : Fields(type)) {
        char line[512]{};
        std::snprintf(line, sizeof(line), "field %s : %s flags=0x%08x", f.name.c_str(), f.type_name.c_str(), f.flags);
        emit(sink, line);
    }
}
inline void DumpMethods(TypeRef type, DiagnosticSink sink = nullptr) {
    for (const auto &m : Methods(type)) {
        std::string line = "method " + m.name + "(";
        for (std::size_t i = 0; i < m.parameters.size(); ++i) {
            if (i)
                line += ", ";
            line += m.parameters[i].type_name.empty() ? "<unavailable>" : m.parameters[i].type_name;
        }
        line += ") -> ";
        line += m.return_type.empty() ? "<unavailable>" : m.return_type;
        emit(sink, line.c_str());
    }
}
inline void DumpProperties(TypeRef type, DiagnosticSink sink = nullptr) {
    for (const auto &p : Properties(type)) {
        char line[512]{};
        std::snprintf(line, sizeof(line), "property %s : %s flags=0x%08x", p.name.c_str(), p.type_name.c_str(),
                      p.flags);
        emit(sink, line);
    }
}
} // namespace Inspect
)URKUNITY";
        const std::string methodParameters = mono ? R"URKUNITY(    const auto* sig = URK::mono::method_signature(method);
    const std::uint32_t count = sig ? URK::mono::signature_get_param_count(sig) : 0;
    info.parameters.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const void* paramType = URK::mono::method_get_param_type(method, i);
        info.parameters.push_back(MethodParamInfo{paramType, {}, type_name(paramType)});
    }
)URKUNITY"
                                                  : R"URKUNITY(    const std::uint32_t count = URK::il2cpp::method_get_param_count(method);
    info.parameters.reserve(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        const void* paramType = URK::il2cpp::method_get_param(method, i);
        const char* paramName = URK::il2cpp::method_get_param_name(method, i);
        info.parameters.push_back(MethodParamInfo{paramType, metadata_display_name(paramName), type_name(paramType)});
    }
)URKUNITY";
        const std::string enumBaseType = mono ? std::string{} : R"URKUNITY(
    const void* underlying = URK::il2cpp::class_enum_basetype(static_cast<const URK::il2cpp::Class*>(klass));
    if (underlying) return type_name(underlying);)URKUNITY";
        const auto replaceAll = [&text](std::string_view needle, std::string_view replacement) {
            std::size_t position = 0;
            while ((position = text.find(needle, position)) != std::string::npos) {
                text.replace(position, needle.size(), replacement);
                position += replacement.size();
            }
        };
        replaceAll("@BACKEND@", mono ? "mono" : "il2cpp");
        replaceAll("@TYPE_GET_CLASS@", mono ? "type_get_class" : "type_get_class_or_element_class");
        replaceAll("@METHOD_GET_PARAM@", mono ? "method_get_param_type" : "method_get_param");
        replaceAll("@METHOD_PARAMETERS@", methodParameters);
        replaceAll("@ENUM_BASETYPE@", enumBaseType);
        return text;
    }();
    const std::string replacementFindExact =
        mono
            ? R"URKUNITY(static const void *find_method_exact(const void *klass, std::string_view name,
                                     const std::vector<const char *> &parameterTypeNames) {
    if (!klass) {
        set_error("Unity exact method lookup failed: class is null");
        return nullptr;
    }
    const std::string cacheKey = member_cache_key(klass, name, parameterTypeNames);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = method_cache().find(cacheKey);
        if (found != method_cache().end())
            return found->second;
    }
    auto n = z(name);
    int same_arity = 0;
    std::string first_mismatch;
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        const void *match = nullptr;
        int matches = 0;
        while (const auto *m = URK::mono::class_get_methods(static_cast<const URK::mono::Class *>(current), &it)) {
            const char *mn = URK::mono::method_get_name(m);
            if (!mn || n != mn)
                continue;
            const auto *sig = URK::mono::method_signature(m);
            if (!sig || URK::mono::signature_get_param_count(sig) != parameterTypeNames.size())
                continue;
            ++same_arity;
            bool ok = true;
            for (std::uint32_t i = 0; i < parameterTypeNames.size(); ++i) {
                char buf[256]{};
                const auto *pt = URK::mono::method_get_param_type(m, i);
                const char *want = parameterTypeNames[i];
                const bool named = URK::mono::type_get_name(pt, buf, sizeof(buf));
                std::string_view got(named ? buf : "");
                if (!want || !named || (!type_name_matches(got, want))) {
                    if (first_mismatch.empty())
                        first_mismatch = std::string("; first mismatch param=") + std::to_string(i) +
                                         " requested=" + (want ? want : "<null>") +
                                         " actual=" + (got.empty() ? "<unavailable>" : std::string(got));
                    ok = false;
                    break;
                }
            }
            if (ok) {
                match = m;
                ++matches;
            }
        }
        if (matches > 1) {
            set_error(std::string("Unity exact method lookup failed: ambiguous exact overload: ") +
                      signature_text(name, parameterTypeNames));
            return nullptr;
        }
        if (match) {
            std::lock_guard<std::mutex> lock(cache_mutex());
            method_cache()[cacheKey] = match;
            return match;
        }
        current = URK::mono::class_get_parent(static_cast<const URK::mono::Class *>(current));
    }
    set_error(std::string("Unity exact method lookup failed: no overload matched ") +
              signature_text(name, parameterTypeNames) + "; same-arity candidates=" + std::to_string(same_arity) +
              first_mismatch);
    return nullptr;
}
)URKUNITY"
            : R"URKUNITY(static const void *find_method_exact(const void *klass, std::string_view name,
                                     const std::vector<const char *> &parameterTypeNames) {
    if (!klass) {
        set_error("Unity exact method lookup failed: class is null");
        return nullptr;
    }
    const std::string cacheKey = member_cache_key(klass, name, parameterTypeNames);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = method_cache().find(cacheKey);
        if (found != method_cache().end())
            return found->second;
    }
    auto n = z(name);
    int same_arity = 0;
    std::string first_mismatch;
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        const void *match = nullptr;
        int matches = 0;
        while (const auto *m = URK::il2cpp::class_get_methods(static_cast<const URK::il2cpp::Class *>(current), &it)) {
            const char *mn = URK::il2cpp::method_get_name(m);
            if (!mn || n != mn || URK::il2cpp::method_get_param_count(m) != parameterTypeNames.size())
                continue;
            ++same_arity;
            bool ok = true;
            for (std::uint32_t i = 0; i < parameterTypeNames.size(); ++i) {
                char buf[256]{};
                const auto *pt = URK::il2cpp::method_get_param(m, i);
                const char *want = parameterTypeNames[i];
                const bool named = URK::il2cpp::type_get_name(pt, buf, sizeof(buf));
                std::string_view got(named ? buf : "");
                if (!want || !named || (!type_name_matches(got, want))) {
                    if (first_mismatch.empty())
                        first_mismatch = std::string("; first mismatch param=") + std::to_string(i) +
                                         " requested=" + (want ? want : "<null>") +
                                         " actual=" + (got.empty() ? "<unavailable>" : std::string(got));
                    ok = false;
                    break;
                }
            }
            if (ok) {
                match = m;
                ++matches;
            }
        }
        if (matches > 1) {
            set_error(std::string("Unity exact method lookup failed: ambiguous exact overload: ") +
                      signature_text(name, parameterTypeNames));
            return nullptr;
        }
        if (match) {
            std::lock_guard<std::mutex> lock(cache_mutex());
            method_cache()[cacheKey] = match;
            return match;
        }
        current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class *>(current));
    }
    set_error(std::string("Unity exact method lookup failed: no overload matched ") +
              signature_text(name, parameterTypeNames) + "; same-arity candidates=" + std::to_string(same_arity) +
              first_mismatch);
    return nullptr;
}
)URKUNITY";
    const std::string replacementFieldStaticGet =
        mono
            ? R"URKUNITY(static bool field_static_get_value(const void *klass, const void *field, void *output) {
    return URK::mono::field_static_get_value(static_cast<const URK::mono::Class *>(klass),
                                             static_cast<const URK::mono::Field *>(field), output);
}
)URKUNITY"
            : R"URKUNITY(static bool field_static_get_value(const void *klass, const void *field, void *output) {
    (void)klass;
    return URK::il2cpp::field_static_get_value(static_cast<const URK::il2cpp::Field *>(field), output);
}
)URKUNITY";
    const std::string replacementFieldStaticSet =
        mono
            ? R"URKUNITY(static bool field_static_set_value(const void *klass, const void *field, void *value) {
    return URK::mono::field_static_set_value(static_cast<const URK::mono::Class *>(klass),
                                             static_cast<const URK::mono::Field *>(field), value);
}
)URKUNITY"
            : R"URKUNITY(static bool field_static_set_value(const void *klass, const void *field, void *value) {
    (void)klass;
    return URK::il2cpp::field_static_set_value(static_cast<const URK::il2cpp::Field *>(field), value);
}
)URKUNITY";
    const std::string replacementFindField =
        mono ? R"URKUNITY(static const void *find_field(const void *klass, std::string_view name) {
    const std::string cacheKey = member_cache_key(klass, name, -1);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = field_cache().find(cacheKey);
        if (found != field_cache().end())
            return found->second;
    }
    auto n = z(name);
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        while (const auto *f = URK::mono::class_get_fields(static_cast<const URK::mono::Class *>(current), &it)) {
            const char *fn = URK::mono::field_get_name(f);
            if (fn && n == fn) {
                std::lock_guard<std::mutex> lock(cache_mutex());
                field_cache()[cacheKey] = f;
                return f;
            }
        }
        current = URK::mono::class_get_parent(static_cast<const URK::mono::Class *>(current));
    }
    return nullptr;
}
)URKUNITY"
             : R"URKUNITY(static const void *find_field(const void *klass, std::string_view name) {
    const std::string cacheKey = member_cache_key(klass, name, -1);
    {
        std::lock_guard<std::mutex> lock(cache_mutex());
        const auto found = field_cache().find(cacheKey);
        if (found != field_cache().end())
            return found->second;
    }
    auto n = z(name);
    const void *current = klass;
    while (current) {
        void *it = nullptr;
        while (const auto *f = URK::il2cpp::class_get_fields(static_cast<const URK::il2cpp::Class *>(current), &it)) {
            const char *fn = URK::il2cpp::field_get_name(f);
            if (fn && n == fn) {
                std::lock_guard<std::mutex> lock(cache_mutex());
                field_cache()[cacheKey] = f;
                return f;
            }
        }
        current = URK::il2cpp::class_get_parent(static_cast<const URK::il2cpp::Class *>(current));
    }
    return nullptr;
}
)URKUNITY";
    if (const auto pos = text.find(genericInspect); pos != std::string::npos)
        text.replace(pos, genericInspect.size(), replacementInspect);
    if (const auto pos = text.find(genericFindMethod); pos != std::string::npos)
        text.replace(pos, genericFindMethod.size(), replacementFindMethod);
    if (const auto pos = text.find(genericFindExact); pos != std::string::npos)
        text.replace(pos, genericFindExact.size(), replacementFindExact);
    if (const auto pos = text.find(genericFieldStaticGet); pos != std::string::npos)
        text.replace(pos, genericFieldStaticGet.size(), replacementFieldStaticGet);
    if (const auto pos = text.find(genericFieldStaticSet); pos != std::string::npos)
        text.replace(pos, genericFieldStaticSet.size(), replacementFieldStaticSet);
    if (const auto pos = text.find(genericFindField); pos != std::string::npos)
        text.replace(pos, genericFindField.size(), replacementFindField);
    return text;
}

struct UnityModuleSet {
    std::string types;
    std::string invoke;
    std::string components;
    std::string inspect;
    std::string shortcuts;
    std::string publicHeader;
};

UnityModuleSet BuildUnityModuleSet(const ModuleProjectOptions &options) {
    const std::string full = UnityCoreModuleFull(options);
    const std::string markerNamespace = "// URK_UNITY_NAMESPACE_BEGIN\n";
    const std::string markerComponents = "// URK_UNITY_COMPONENTS_BEGIN\n";
    const std::string markerInvoke = "// URK_UNITY_INVOKE_BEGIN\n";
    const std::string markerShortcuts = "// URK_UNITY_SHORTCUTS_BEGIN\n";
    const std::string markerInspect = "// URK_UNITY_INSPECT_BEGIN\n";
    const std::string markerAliases = "// URK_UNITY_ALIASES_BEGIN\n";
    const std::string markerInspectAliases = "// URK_UNITY_INSPECT_ALIASES_BEGIN\n";

    const std::size_t namespacePos = full.find(markerNamespace);
    const std::size_t componentsPos = full.find(markerComponents);
    const std::size_t invokePos = full.find(markerInvoke);
    const std::size_t shortcutsPos = full.find(markerShortcuts);
    const std::size_t inspectPos = full.find(markerInspect);
    const std::size_t aliasesPos = full.find(markerAliases);
    const std::size_t inspectAliasesPos = full.find(markerInspectAliases);
    if (namespacePos == std::string::npos || componentsPos == std::string::npos || invokePos == std::string::npos ||
        shortcutsPos == std::string::npos || inspectPos == std::string::npos || aliasesPos == std::string::npos ||
        inspectAliasesPos == std::string::npos ||
        !(namespacePos < componentsPos && componentsPos < invokePos && invokePos < shortcutsPos &&
          shortcutsPos < inspectPos && inspectPos < aliasesPos && aliasesPos < inspectAliasesPos)) {
        throw std::runtime_error("Unity SDK module split markers are missing or out of order");
    }

    const auto chained = [](const char *include, const std::string &body) {
        return std::string("#pragma once\n#include \"") + include + "\"\n" + "\nnamespace URK::Unity {\n" + body +
               "\n}\n";
    };

    UnityModuleSet modules;
    modules.types = full.substr(0, componentsPos) + "\n}\n";
    modules.components = chained("unity_types.h", full.substr(componentsPos, invokePos - componentsPos));
    modules.invoke = chained("unity_components.h", full.substr(invokePos, shortcutsPos - invokePos));
    modules.shortcuts = chained("unity_invoke.h", full.substr(shortcutsPos, inspectPos - shortcutsPos));
    modules.shortcuts += full.substr(aliasesPos, inspectAliasesPos - aliasesPos);
    modules.shortcuts += "\n}\n";
    std::string inspectBody = full.substr(inspectPos, aliasesPos - inspectPos);
    const std::string namespaceClose = "\n}\n\n";
    if (inspectBody.size() < namespaceClose.size() ||
        inspectBody.compare(inspectBody.size() - namespaceClose.size(), namespaceClose.size(), namespaceClose) != 0) {
        throw std::runtime_error("Unity SDK inspect module namespace boundary is invalid");
    }
    inspectBody.erase(inspectBody.size() - namespaceClose.size());
    modules.inspect = chained("unity_shortcuts.h", inspectBody);
    modules.inspect += "namespace Unity {\n" + full.substr(inspectAliasesPos);
    modules.publicHeader =
        "#pragma once\n\n// Canonical public Unity surface for normal mod code.\n#include \"unity_shortcuts.h\"\n";
    return modules;
}
