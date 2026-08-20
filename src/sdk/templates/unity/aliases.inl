    out << R"URKUNITY(// URK_UNITY_ALIASES_BEGIN
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
inline bool is_main_thread() {
    return URK::Unity::is_main_thread();
}
inline bool require_main_thread(std::string_view operation = "Unity operation") {
    return URK::Unity::require_main_thread(operation);
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

