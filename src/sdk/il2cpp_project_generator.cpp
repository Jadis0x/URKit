#include "il2cpp_sdk_generator.h"
#include "mod_project_generator_common.h"
#include "mod_project_generator_profiles.h"

#include "sdk_generator_contract.h"

#include <filesystem>
#include <sstream>
#include <string>
#include <system_error>

namespace {
namespace fs = std::filesystem;
namespace mpg = ModProjectGenerator;
namespace sdk = SdkGenerator;

std::string Il2CppRuntimeModule() {
    return R"URKIL2CPP(#pragma once

#include "../runtime_api.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <limits>
#include <type_traits>

namespace URK::il2cpp {
using Domain = void;
using Assembly = void;
using Image = void;
using Class = void;
using Method = void;
using Field = void;
using Property = void;
using Type = void;
using Object = void;
using String = void;
using Array = void;
using Thread = void;

inline const URK_Il2CppApi *&ApiSlot() {
    static const URK_Il2CppApi *api = nullptr;
    return api;
}
inline bool has_field(const URK_Il2CppApi *a, std::size_t end) {
    return URK::has_runtime_capability(URK::runtime_cap_il2cpp_api) && a && a->version >= URK_IL2CPP_API_VERSION &&
           a->size >= end;
}
#define URK_IL2CPP_HAS(member) (has_field(a, offsetof(URK_Il2CppApi, member) + sizeof(a->member)))

inline bool init(const URK::ModContext *ctx) {
    URK::set_context(ctx);
    ApiSlot() = nullptr;
    if (!ctx || ctx->version < URK_SDK_VERSION || ctx->size < sizeof(URK_ModContext) ||
        !URK::has_runtime_capability(URK::runtime_cap_il2cpp_api) ||
        ctx->runtimeBackend != URK::runtime_backend_il2cpp || !ctx->il2cpp ||
        ctx->il2cpp->version < URK_IL2CPP_API_VERSION || ctx->il2cpp->size < sizeof(URK_Il2CppApi))
        return false;
    ApiSlot() = ctx->il2cpp;
    const auto *a = ApiSlot();
    return a && URK_IL2CPP_HAS(is_available) && a->is_available && a->is_available() != 0;
}
inline const URK_Il2CppApi *api() {
    if (const URK::ModContext *ctx = URK::context()) {
        if (ctx->runtimeBackend == URK::runtime_backend_il2cpp &&
            URK::has_runtime_capability(URK::runtime_cap_il2cpp_api) && ctx->il2cpp &&
            ctx->il2cpp->version >= URK_IL2CPP_API_VERSION && ctx->il2cpp->size >= sizeof(URK_Il2CppApi))
            return ctx->il2cpp;
    }
    return ApiSlot();
}
inline bool available() {
    const auto *a = api();
    return URK::has_runtime_capability(URK::runtime_cap_il2cpp_api) && a && a->version >= URK_IL2CPP_API_VERSION &&
           a->size >= sizeof(URK_Il2CppApi) && URK_IL2CPP_HAS(is_available) && a->is_available &&
           a->is_available() != 0;
}
inline const char *last_error() {
    const auto *a = api();
    return URK::has_runtime_capability(URK::runtime_cap_il2cpp_api) && URK_IL2CPP_HAS(last_error) && a->last_error
               ? a->last_error()
               : nullptr;
}
inline const Image *find_image(const char *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(find_image) && a->find_image ? static_cast<const Image *>(a->find_image(image))
                                                                      : nullptr;
}
inline const Class *find_class(const char *image, const char *namespc, const char *name) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(find_class) && a->find_class
               ? static_cast<const Class *>(a->find_class(image, namespc, name))
               : nullptr;
}
inline const Method *resolve_method(const Class *klass, const char *name, int argc) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(find_method) && a->find_method
               ? static_cast<const Method *>(a->find_method(klass, name, argc))
               : nullptr;
}
inline const Method *resolve_method_exact(const Class *klass, const char *name, const char *const *parameter_types,
                                          int parameter_count) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(find_method_exact) && a->find_method_exact
               ? static_cast<const Method *>(a->find_method_exact(klass, name, parameter_types, parameter_count))
               : nullptr;
}
inline const Method *resolve_method_exact(const char *image, const char *namespc, const char *klass, const char *name,
                                          const char *const *parameter_types, int parameter_count) {
    const Class *c = find_class(image, namespc, klass);
    return c ? resolve_method_exact(c, name, parameter_types, parameter_count) : nullptr;
}
inline const Field *resolve_field(const Class *klass, const char *name) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(find_field) && a->find_field
               ? static_cast<const Field *>(a->find_field(klass, name))
               : nullptr;
}
inline const Field *find_field(const Class *klass, const char *name) {
    return resolve_field(klass, name);
}
inline const Field *resolve_field(const char *image, const char *namespc, const char *klass, const char *name) {
    const Class *c = find_class(image, namespc, klass);
    return c ? resolve_field(c, name) : nullptr;
}
inline void *method_pointer(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_pointer) && a->method_pointer ? a->method_pointer(method) : nullptr;
}
inline URK_Il2CppManagedMethodDesc managed_method_desc(const char *image, const char *namespc, const char *klass,
                                                       const char *method, const char *const *parameter_types = nullptr,
                                                       int parameter_count = 0) {
    return URK_Il2CppManagedMethodDesc{
        sizeof(URK_Il2CppManagedMethodDesc), image, namespc, klass, method, parameter_types, parameter_count};
}
inline URK_Il2CppManagedHookResult managed_hook_result() {
    return URK_Il2CppManagedHookResult{sizeof(URK_Il2CppManagedHookResult), nullptr, nullptr, nullptr};
}
inline int attach_managed_method_hook(const URK_Il2CppManagedMethodDesc *method, void **original, void *detour,
                                      const URK_HookOptions *options, URK_Il2CppManagedHookResult *result) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(attach_managed_method_hook) && a->attach_managed_method_hook
               ? a->attach_managed_method_hook(method, original, detour, options, result)
               : 0;
}
inline const char *class_get_name(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_name) && a->class_get_name ? a->class_get_name(klass) : nullptr;
}
inline const char *class_get_namespace(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_namespace) && a->class_get_namespace ? a->class_get_namespace(klass)
                                                                                        : nullptr;
}
inline const char *method_get_name(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_name) && a->method_get_name ? a->method_get_name(method) : nullptr;
}
inline bool type_get_name(const Type *type, char *output, std::size_t output_size) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(type_get_name) && a->type_get_name
               ? a->type_get_name(type, output, output_size) != 0
               : false;
}
inline String *string_new(const char *utf8) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_new) && a->string_new ? static_cast<String *>(a->string_new(utf8))
                                                                      : nullptr;
}
inline const Domain *domain_get() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(domain_get) && a->domain_get ? static_cast<const Domain *>(a->domain_get())
                                                                      : nullptr;
}
inline std::size_t domain_get_assembly_count() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(domain_get_assembly_count) && a->domain_get_assembly_count
               ? a->domain_get_assembly_count()
               : 0;
}
inline const Assembly *domain_get_assembly(std::size_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(domain_get_assembly) && a->domain_get_assembly
               ? static_cast<const Assembly *>(a->domain_get_assembly(index))
               : nullptr;
}
inline const Image *assembly_get_image(const Assembly *assembly) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(assembly_get_image) && a->assembly_get_image
               ? static_cast<const Image *>(a->assembly_get_image(assembly))
               : nullptr;
}
inline const char *image_get_name(const Image *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_name) && a->image_get_name ? a->image_get_name(image) : nullptr;
}
inline std::size_t image_get_class_count(const Image *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_class_count) && a->image_get_class_count
               ? a->image_get_class_count(image)
               : 0;
}
inline const Class *image_get_class(const Image *image, std::size_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_class) && a->image_get_class
               ? static_cast<const Class *>(a->image_get_class(image, index))
               : nullptr;
}
inline const Class *class_get_parent(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_parent) && a->class_get_parent
               ? static_cast<const Class *>(a->class_get_parent(klass))
               : nullptr;
}
inline std::uint32_t class_get_flags(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_flags) && a->class_get_flags ? a->class_get_flags(klass) : 0;
}
inline bool class_is_valuetype(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_valuetype) && a->class_is_valuetype
               ? a->class_is_valuetype(klass) != 0
               : false;
}
inline bool class_is_enum(const Class *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_enum) && a->class_is_enum ? a->class_is_enum(klass) != 0 : false;
}
inline const Field *class_get_fields(const Class *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_fields) && a->class_get_fields
               ? static_cast<const Field *>(a->class_get_fields(klass, iterator))
               : nullptr;
}
inline const Method *class_get_methods(const Class *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_methods) && a->class_get_methods
               ? static_cast<const Method *>(a->class_get_methods(klass, iterator))
               : nullptr;
}
inline const Property *class_get_properties(const Class *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_properties) && a->class_get_properties
               ? static_cast<const Property *>(a->class_get_properties(klass, iterator))
               : nullptr;
}
inline const Class *class_get_nested_types(const Class *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_nested_types) && a->class_get_nested_types
               ? static_cast<const Class *>(a->class_get_nested_types(klass, iterator))
               : nullptr;
}
inline const Class *class_get_interfaces(const Class *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_interfaces) && a->class_get_interfaces
               ? static_cast<const Class *>(a->class_get_interfaces(klass, iterator))
               : nullptr;
}
inline const Class *method_get_declaring_class(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_declaring_class) && a->method_get_declaring_class
               ? static_cast<const Class *>(a->method_get_declaring_class(method))
               : nullptr;
}
inline std::uint32_t method_get_param_count(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_param_count) && a->method_get_param_count
               ? a->method_get_param_count(method)
               : 0;
}
inline const Type *method_get_param(const Method *method, std::uint32_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_param) && a->method_get_param
               ? static_cast<const Type *>(a->method_get_param(method, index))
               : nullptr;
}
inline const Type *method_get_return_type(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_return_type) && a->method_get_return_type
               ? static_cast<const Type *>(a->method_get_return_type(method))
               : nullptr;
}
inline const char *field_get_name(const Field *field) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_name) && a->field_get_name ? a->field_get_name(field) : nullptr;
}
inline const Type *field_get_type(const Field *field) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_type) && a->field_get_type
               ? static_cast<const Type *>(a->field_get_type(field))
               : nullptr;
}
inline bool field_get_value(Object *object, const Field *field, void *output) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_value) && a->field_get_value
               ? a->field_get_value(object, field, output) != 0
               : false;
}
inline bool field_set_value(Object *object, const Field *field, void *value) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_set_value) && a->field_set_value
               ? a->field_set_value(object, field, value) != 0
               : false;
}
inline bool field_static_get_value(const Field *field, void *output) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_static_get_value) && a->field_static_get_value
               ? a->field_static_get_value(field, output) != 0
               : false;
}
inline bool field_static_set_value(const Field *field, void *value) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_static_set_value) && a->field_static_set_value
               ? a->field_static_set_value(field, value) != 0
               : false;
}
inline const char *property_get_name(const Property *property) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(property_get_name) && a->property_get_name ? a->property_get_name(property)
                                                                                    : nullptr;
}
inline const Method *property_get_get_method(const Property *property) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(property_get_get_method) && a->property_get_get_method
               ? static_cast<const Method *>(a->property_get_get_method(property))
               : nullptr;
}
inline const Method *property_get_set_method(const Property *property) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(property_get_set_method) && a->property_get_set_method
               ? static_cast<const Method *>(a->property_get_set_method(property))
               : nullptr;
}
inline const Class *type_get_class_or_element_class(const Type *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(type_get_class_or_element_class) && a->type_get_class_or_element_class
               ? static_cast<const Class *>(a->type_get_class_or_element_class(type))
               : nullptr;
}
inline const Class *object_get_class(Object *object) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_get_class) && a->object_get_class
               ? static_cast<const Class *>(a->object_get_class(object))
               : nullptr;
}
inline void *object_unbox(Object *object) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_unbox) && a->object_unbox ? a->object_unbox(object) : nullptr;
}
inline bool string_to_utf8(String *string, char *output, std::size_t output_size) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_to_utf8) && a->string_to_utf8
               ? a->string_to_utf8(string, output, output_size) != 0
               : false;
}
inline std::size_t array_length(Array *array) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_length) && a->array_length ? a->array_length(array) : 0;
}
inline bool has_array_length() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_length) && a->array_length;
}
inline void *array_addr_with_size(Array *array, int element_size, std::size_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_addr_with_size) && a->array_addr_with_size
               ? a->array_addr_with_size(array, element_size, index)
               : nullptr;
}
inline void *array_ref_at(Array *array, std::size_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_ref_at) && a->array_ref_at ? a->array_ref_at(array, index) : nullptr;
}
inline bool has_array_ref_at() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_ref_at) && a->array_ref_at;
}
inline bool array_set_ref(Array *array, std::size_t index, void *value) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_set_ref) && a->array_set_ref ? a->array_set_ref(array, index, value) != 0
                                                                            : false;
}
inline bool has_array_set_ref() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_set_ref) && a->array_set_ref;
}
inline std::uint32_t method_get_flags(const Method *method, std::uint32_t *iflags = nullptr) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_flags) && a->method_get_flags ? a->method_get_flags(method, iflags)
                                                                                  : 0;
}
inline std::uint32_t method_get_token(const Method *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_token) && a->method_get_token ? a->method_get_token(method) : 0;
}
inline std::int32_t field_offset(const Field *field) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_offset) && a->field_offset ? a->field_offset(field) : -1;
}
inline std::uint32_t field_get_flags(const Field *field) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_flags) && a->field_get_flags ? a->field_get_flags(field) : 0;
}
inline std::uint32_t property_get_flags(const Property *property) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(property_get_flags) && a->property_get_flags ? a->property_get_flags(property)
                                                                                      : 0;
}
inline std::int32_t type_get_type(const Type *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(type_get_type) && a->type_get_type ? a->type_get_type(type) : 0;
}
inline std::uint32_t type_get_attrs(const Type *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(type_get_attrs) && a->type_get_attrs ? a->type_get_attrs(type) : 0;
}
inline int runtime_invoke(const Method *method, Object *object, void **params, void **result = nullptr,
                          void **exception = nullptr) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(runtime_invoke) && a->runtime_invoke
               ? a->runtime_invoke(method, object, params, result, exception)
               : 0;
}
inline const Thread *thread_current() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_current) && a->thread_current
               ? static_cast<const Thread *>(a->thread_current())
               : nullptr;
}
inline const Thread *thread_attach(const Domain *domain = nullptr) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_attach) && a->thread_attach
               ? static_cast<const Thread *>(a->thread_attach(domain ? domain : domain_get()))
               : nullptr;
}
inline void thread_detach(const Thread *thread) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(thread_detach) && a->thread_detach && thread)
        a->thread_detach(thread);
}
inline void *alloc(std::size_t size) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(alloc) && a->alloc ? a->alloc(size) : nullptr;
}

inline void free(void *ptr) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(free) && a->free && ptr)
        a->free(ptr);
}

inline void init(const char *domain_name) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(init) && a->init)
        a->init(domain_name);
}
inline void shutdown() {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(shutdown) && a->shutdown)
        a->shutdown();
}
inline void set_config_dir(const char *config_path) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(set_config_dir) && a->set_config_dir)
        a->set_config_dir(config_path);
}
inline void set_data_dir(const char *data_path) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(set_data_dir) && a->set_data_dir)
        a->set_data_dir(data_path);
}
inline void set_commandline_arguments(int argc, const char *argv[], const char *basedir) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(set_commandline_arguments) && a->set_commandline_arguments)
        a->set_commandline_arguments(argc, argv, basedir);
}
inline void set_memory_callbacks(void *callbacks) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(set_memory_callbacks) && a->set_memory_callbacks)
        a->set_memory_callbacks(callbacks);
}
inline void set_find_plugin_callback(void *method) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(set_find_plugin_callback) && a->set_find_plugin_callback)
        a->set_find_plugin_callback(method);
}
inline void add_internal_call(const char *name, void *method) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(add_internal_call) && a->add_internal_call)
        a->add_internal_call(name, method);
}
inline void *resolve_icall(const char *name) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(resolve_icall) && a->resolve_icall ? a->resolve_icall(name) : nullptr;
}
inline const void *domain_assembly_open(const void *domain, const char *name) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(domain_assembly_open) && a->domain_assembly_open
               ? a->domain_assembly_open(domain, name)
               : nullptr;
}
inline const void *get_corlib() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(get_corlib) && a->get_corlib ? a->get_corlib() : nullptr;
}
inline const void *image_get_assembly(const void *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_assembly) && a->image_get_assembly ? a->image_get_assembly(image)
                                                                                      : nullptr;
}
inline const char *image_get_filename(const void *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_filename) && a->image_get_filename ? a->image_get_filename(image)
                                                                                      : nullptr;
}
inline const void *image_get_entry_point(const void *image) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(image_get_entry_point) && a->image_get_entry_point
               ? a->image_get_entry_point(image)
               : nullptr;
}
inline const void *class_from_type(const void *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_from_type) && a->class_from_type ? a->class_from_type(type) : nullptr;
}
inline const void *class_from_il2cpp_type(const void *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_from_il2cpp_type) && a->class_from_il2cpp_type
               ? a->class_from_il2cpp_type(type)
               : nullptr;
}
inline const void *class_from_system_type(void *reflection_type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_from_system_type) && a->class_from_system_type
               ? a->class_from_system_type(reflection_type)
               : nullptr;
}
inline const char *class_get_assemblyname(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_assemblyname) && a->class_get_assemblyname
               ? a->class_get_assemblyname(klass)
               : nullptr;
}
inline const void *class_get_image(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_image) && a->class_get_image ? a->class_get_image(klass) : nullptr;
}
inline const void *class_get_declaring_type(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_declaring_type) && a->class_get_declaring_type
               ? a->class_get_declaring_type(klass)
               : nullptr;
}
inline const void *class_get_element_class(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_element_class) && a->class_get_element_class
               ? a->class_get_element_class(klass)
               : nullptr;
}
inline const void *class_get_type(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_type) && a->class_get_type ? a->class_get_type(klass) : nullptr;
}
inline std::uint32_t class_get_type_token(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_type_token) && a->class_get_type_token
               ? a->class_get_type_token(klass)
               : 0;
}
inline int class_get_rank(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_rank) && a->class_get_rank ? a->class_get_rank(klass) : 0;
}
inline std::int32_t class_instance_size(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_instance_size) && a->class_instance_size ? a->class_instance_size(klass)
                                                                                        : 0;
}
inline std::int32_t class_value_size(const void *klass, std::uint32_t *align) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_value_size) && a->class_value_size ? a->class_value_size(klass, align)
                                                                                  : 0;
}
inline std::size_t class_num_fields(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_num_fields) && a->class_num_fields ? a->class_num_fields(klass) : 0;
}
inline int class_array_element_size(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_array_element_size) && a->class_array_element_size
               ? a->class_array_element_size(klass)
               : 0;
}
inline int class_is_generic(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_generic) && a->class_is_generic ? a->class_is_generic(klass) : 0;
}
inline int class_is_inflated(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_inflated) && a->class_is_inflated ? a->class_is_inflated(klass) : 0;
}
inline int class_is_abstract(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_abstract) && a->class_is_abstract ? a->class_is_abstract(klass) : 0;
}
inline int class_is_interface(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_interface) && a->class_is_interface ? a->class_is_interface(klass)
                                                                                      : 0;
}
inline int class_is_subclass_of(const void *klass, const void *klassc, int check_interfaces) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_subclass_of) && a->class_is_subclass_of
               ? a->class_is_subclass_of(klass, klassc, check_interfaces)
               : 0;
}
inline int class_is_assignable_from(const void *klass, const void *oklass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_is_assignable_from) && a->class_is_assignable_from
               ? a->class_is_assignable_from(klass, oklass)
               : 0;
}
inline int class_has_parent(const void *klass, const void *klassc) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_has_parent) && a->class_has_parent ? a->class_has_parent(klass, klassc)
                                                                                  : 0;
}
inline int class_has_attribute(const void *klass, const void *attr_class) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_has_attribute) && a->class_has_attribute
               ? a->class_has_attribute(klass, attr_class)
               : 0;
}
inline int class_has_references(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_has_references) && a->class_has_references
               ? a->class_has_references(klass)
               : 0;
}
inline const void *class_enum_basetype(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_enum_basetype) && a->class_enum_basetype ? a->class_enum_basetype(klass)
                                                                                        : nullptr;
}
inline const void *class_get_property_from_name(const void *klass, const char *name) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_property_from_name) && a->class_get_property_from_name
               ? a->class_get_property_from_name(klass, name)
               : nullptr;
}
inline const void *class_get_events(const void *klass, void **iterator) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_events) && a->class_get_events ? a->class_get_events(klass, iterator)
                                                                                  : nullptr;
}
inline std::size_t class_get_bitmap_size(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(class_get_bitmap_size) && a->class_get_bitmap_size
               ? a->class_get_bitmap_size(klass)
               : 0;
}
inline void class_get_bitmap(const void *klass, std::size_t *bitmap) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(class_get_bitmap) && a->class_get_bitmap)
        a->class_get_bitmap(klass, bitmap);
}
inline const void *method_get_declaring_type(const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_declaring_type) && a->method_get_declaring_type
               ? a->method_get_declaring_type(method)
               : nullptr;
}
inline void *method_get_object(const void *method, const void *refclass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_object) && a->method_get_object
               ? a->method_get_object(method, refclass)
               : nullptr;
}
inline int method_is_generic(const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_is_generic) && a->method_is_generic ? a->method_is_generic(method) : 0;
}
inline int method_is_inflated(const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_is_inflated) && a->method_is_inflated ? a->method_is_inflated(method)
                                                                                      : 0;
}
inline int method_is_instance(const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_is_instance) && a->method_is_instance ? a->method_is_instance(method)
                                                                                      : 0;
}
inline int method_has_attribute(const void *method, const void *attr_class) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_has_attribute) && a->method_has_attribute
               ? a->method_has_attribute(method, attr_class)
               : 0;
}
inline const char *method_get_param_name(const void *method, std::uint32_t index) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(method_get_param_name) && a->method_get_param_name
               ? a->method_get_param_name(method, index)
               : nullptr;
}
inline const void *field_get_parent(const void *field) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_parent) && a->field_get_parent ? a->field_get_parent(field)
                                                                                  : nullptr;
}
inline void *field_get_value_object(const void *field, void *object) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_get_value_object) && a->field_get_value_object
               ? a->field_get_value_object(field, object)
               : nullptr;
}
inline int field_has_attribute(const void *field, const void *attr_class) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(field_has_attribute) && a->field_has_attribute
               ? a->field_has_attribute(field, attr_class)
               : 0;
}
inline const void *property_get_parent(const void *property) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(property_get_parent) && a->property_get_parent
               ? a->property_get_parent(property)
               : nullptr;
}
inline void *type_get_object(const void *type) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(type_get_object) && a->type_get_object ? a->type_get_object(type) : nullptr;
}
inline std::uint32_t object_get_size(void *object) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_get_size) && a->object_get_size ? a->object_get_size(object) : 0;
}
inline const void *object_get_virtual_method(void *object, const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_get_virtual_method) && a->object_get_virtual_method
               ? a->object_get_virtual_method(object, method)
               : nullptr;
}
inline void *object_new(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_new) && a->object_new ? a->object_new(klass) : nullptr;
}
inline void *object_is_inst(void *object, const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_is_inst) && a->object_is_inst ? a->object_is_inst(object, klass)
                                                                              : nullptr;
}
inline void *value_box(const void *klass, void *data) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(value_box) && a->value_box ? a->value_box(klass, data) : nullptr;
}
inline void *string_new_len(const char *str, std::uint32_t length) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_new_len) && a->string_new_len ? a->string_new_len(str, length)
                                                                              : nullptr;
}
inline void *string_new_utf16(const uint16_t *text, std::int32_t length) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_new_utf16) && a->string_new_utf16 ? a->string_new_utf16(text, length)
                                                                                  : nullptr;
}
inline void *string_new_wrapper(const char *str) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_new_wrapper) && a->string_new_wrapper ? a->string_new_wrapper(str)
                                                                                      : nullptr;
}
inline std::int32_t string_length(void *string) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_length) && a->string_length ? a->string_length(string) : 0;
}
inline const uint16_t *string_chars(void *string) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_chars) && a->string_chars ? a->string_chars(string) : nullptr;
}
inline void *string_intern(void *string) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_intern) && a->string_intern ? a->string_intern(string) : nullptr;
}
inline void *string_is_interned(void *string) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(string_is_interned) && a->string_is_interned ? a->string_is_interned(string)
                                                                                      : nullptr;
}
inline const void *array_class_get(const void *element_class, std::uint32_t rank) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_class_get) && a->array_class_get
               ? a->array_class_get(element_class, rank)
               : nullptr;
}
inline const void *bounded_array_class_get(const void *element_class, std::uint32_t rank, int bounded) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(bounded_array_class_get) && a->bounded_array_class_get
               ? a->bounded_array_class_get(element_class, rank, bounded)
               : nullptr;
}
inline std::size_t array_get_byte_length(void *array) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_get_byte_length) && a->array_get_byte_length
               ? a->array_get_byte_length(array)
               : 0;
}
inline int array_element_size(const void *array_class) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_element_size) && a->array_element_size
               ? a->array_element_size(array_class)
               : 0;
}
inline void *array_new(const void *element_class, std::size_t length) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_new) && a->array_new ? a->array_new(element_class, length) : nullptr;
}
inline void *array_new_specific(const void *array_class, std::size_t length) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_new_specific) && a->array_new_specific
               ? a->array_new_specific(array_class, length)
               : nullptr;
}
inline void *array_new_full(const void *array_class, std::size_t *lengths, std::size_t *lower_bounds) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_new_full) && a->array_new_full
               ? a->array_new_full(array_class, lengths, lower_bounds)
               : nullptr;
}
inline std::uint32_t object_header_size() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(object_header_size) && a->object_header_size ? a->object_header_size() : 0;
}
inline std::uint32_t array_object_header_size() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(array_object_header_size) && a->array_object_header_size
               ? a->array_object_header_size()
               : 0;
}
inline std::uint32_t offset_of_array_length_in_array_object_header() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(offset_of_array_length_in_array_object_header) &&
                   a->offset_of_array_length_in_array_object_header
               ? a->offset_of_array_length_in_array_object_header()
               : 0;
}
inline std::uint32_t offset_of_array_bounds_in_array_object_header() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(offset_of_array_bounds_in_array_object_header) &&
                   a->offset_of_array_bounds_in_array_object_header
               ? a->offset_of_array_bounds_in_array_object_header()
               : 0;
}
inline std::uint32_t allocation_granularity() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(allocation_granularity) && a->allocation_granularity
               ? a->allocation_granularity()
               : 0;
}
inline void *runtime_invoke_convert_args(const void *method, void *object, void **params, int param_count,
                                         void **exception) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(runtime_invoke_convert_args) && a->runtime_invoke_convert_args
               ? a->runtime_invoke_convert_args(method, object, params, param_count, exception)
               : nullptr;
}
inline void runtime_class_init(const void *klass) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(runtime_class_init) && a->runtime_class_init)
        a->runtime_class_init(klass);
}
inline void runtime_object_init(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(runtime_object_init) && a->runtime_object_init)
        a->runtime_object_init(object);
}
inline void runtime_object_init_exception(void *object, void **exception) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(runtime_object_init_exception) && a->runtime_object_init_exception)
        a->runtime_object_init_exception(object, exception);
}
inline void runtime_unhandled_exception_policy_set(int value) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(runtime_unhandled_exception_policy_set) &&
        a->runtime_unhandled_exception_policy_set)
        a->runtime_unhandled_exception_policy_set(value);
}
inline void raise_exception(void *exception) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(raise_exception) && a->raise_exception)
        a->raise_exception(exception);
}
inline void *exception_from_name_msg(const void *image, const char *name_space, const char *name, const char *msg) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(exception_from_name_msg) && a->exception_from_name_msg
               ? a->exception_from_name_msg(image, name_space, name, msg)
               : nullptr;
}
inline void *get_exception_argument_null(const char *arg) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(get_exception_argument_null) && a->get_exception_argument_null
               ? a->get_exception_argument_null(arg)
               : nullptr;
}
inline void format_exception(const void *exception, char *message, int message_size) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(format_exception) && a->format_exception)
        a->format_exception(exception, message, message_size);
}
inline void format_stack_trace(const void *exception, char *output, int output_size) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(format_stack_trace) && a->format_stack_trace)
        a->format_stack_trace(exception, output, output_size);
}
inline void unhandled_exception(void *exception) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(unhandled_exception) && a->unhandled_exception)
        a->unhandled_exception(exception);
}
inline void gc_collect(int max_generations) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(gc_collect) && a->gc_collect)
        a->gc_collect(max_generations);
}
inline std::int64_t gc_get_used_size() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(gc_get_used_size) && a->gc_get_used_size ? a->gc_get_used_size() : 0;
}
inline std::int64_t gc_get_heap_size() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(gc_get_heap_size) && a->gc_get_heap_size ? a->gc_get_heap_size() : 0;
}
inline std::uintptr_t gchandle_new(void *object, int pinned) {
    const auto *a = api();
    if (!available())
        return 0;
    if (URK_IL2CPP_HAS(gchandle_new_v2) && a->gchandle_new_v2)
        return a->gchandle_new_v2(object, pinned);
    return URK_IL2CPP_HAS(gchandle_new) && a->gchandle_new ? a->gchandle_new(object, pinned) : 0;
}
inline std::uintptr_t gchandle_new_weakref(void *object, int track_resurrection) {
    const auto *a = api();
    if (!available())
        return 0;
    if (URK_IL2CPP_HAS(gchandle_new_weakref_v2) && a->gchandle_new_weakref_v2)
        return a->gchandle_new_weakref_v2(object, track_resurrection);
    return URK_IL2CPP_HAS(gchandle_new_weakref) && a->gchandle_new_weakref
               ? a->gchandle_new_weakref(object, track_resurrection) : 0;
}
inline void *gchandle_get_target(std::uintptr_t gchandle) {
    const auto *a = api();
    if (!available())
        return nullptr;
    if (URK_IL2CPP_HAS(gchandle_get_target_v2) && a->gchandle_get_target_v2)
        return a->gchandle_get_target_v2(gchandle);
    if (gchandle > static_cast<std::uintptr_t>((std::numeric_limits<std::uint32_t>::max)()))
        return nullptr;
    return URK_IL2CPP_HAS(gchandle_get_target) && a->gchandle_get_target
               ? a->gchandle_get_target(static_cast<std::uint32_t>(gchandle)) : nullptr;
}
inline void gchandle_free(std::uintptr_t gchandle) {
    const auto *a = api();
    if (!available())
        return;
    if (URK_IL2CPP_HAS(gchandle_free_v2) && a->gchandle_free_v2) {
        a->gchandle_free_v2(gchandle);
        return;
    }
    if (gchandle <= static_cast<std::uintptr_t>((std::numeric_limits<std::uint32_t>::max)()) &&
        URK_IL2CPP_HAS(gchandle_free) && a->gchandle_free)
        a->gchandle_free(static_cast<std::uint32_t>(gchandle));
}
inline char *thread_get_name(const void *thread, std::uint32_t *length) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_get_name) && a->thread_get_name ? a->thread_get_name(thread, length)
                                                                                : nullptr;
}
inline const void **thread_get_all_attached_threads(std::size_t *size) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_get_all_attached_threads) && a->thread_get_all_attached_threads
               ? a->thread_get_all_attached_threads(size)
               : nullptr;
}
inline int is_vm_thread(const void *thread) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(is_vm_thread) && a->is_vm_thread ? a->is_vm_thread(thread) : 0;
}
inline void current_thread_walk_frame_stack(void *callback, void *user_data) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(current_thread_walk_frame_stack) && a->current_thread_walk_frame_stack)
        a->current_thread_walk_frame_stack(callback, user_data);
}
inline void thread_walk_frame_stack(const void *thread, void *callback, void *user_data) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(thread_walk_frame_stack) && a->thread_walk_frame_stack)
        a->thread_walk_frame_stack(thread, callback, user_data);
}
inline int current_thread_get_top_frame(void *frame) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(current_thread_get_top_frame) && a->current_thread_get_top_frame
               ? a->current_thread_get_top_frame(frame)
               : 0;
}
inline int thread_get_top_frame(const void *thread, void *frame) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_get_top_frame) && a->thread_get_top_frame
               ? a->thread_get_top_frame(thread, frame)
               : 0;
}
inline int current_thread_get_frame_at(std::int32_t offset, void *frame) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(current_thread_get_frame_at) && a->current_thread_get_frame_at
               ? a->current_thread_get_frame_at(offset, frame)
               : 0;
}
inline int thread_get_frame_at(const void *thread, std::int32_t offset, void *frame) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_get_frame_at) && a->thread_get_frame_at
               ? a->thread_get_frame_at(thread, offset, frame)
               : 0;
}
inline std::int32_t current_thread_get_stack_depth() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(current_thread_get_stack_depth) && a->current_thread_get_stack_depth
               ? a->current_thread_get_stack_depth()
               : 0;
}
inline std::int32_t thread_get_stack_depth(const void *thread) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(thread_get_stack_depth) && a->thread_get_stack_depth
               ? a->thread_get_stack_depth(thread)
               : 0;
}
inline void monitor_enter(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(monitor_enter) && a->monitor_enter)
        a->monitor_enter(object);
}
inline int monitor_try_enter(void *object, std::uint32_t timeout) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(monitor_try_enter) && a->monitor_try_enter
               ? a->monitor_try_enter(object, timeout)
               : 0;
}
inline void monitor_exit(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(monitor_exit) && a->monitor_exit)
        a->monitor_exit(object);
}
inline void monitor_pulse(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(monitor_pulse) && a->monitor_pulse)
        a->monitor_pulse(object);
}
inline void monitor_pulse_all(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(monitor_pulse_all) && a->monitor_pulse_all)
        a->monitor_pulse_all(object);
}
inline void monitor_wait(void *object) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(monitor_wait) && a->monitor_wait)
        a->monitor_wait(object);
}
inline int monitor_try_wait(void *object, std::uint32_t timeout) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(monitor_try_wait) && a->monitor_try_wait ? a->monitor_try_wait(object, timeout)
                                                                                  : 0;
}
inline void *delegate_begin_invoke(void *delegate_obj, void **params, void *async_callback, void *state) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(delegate_begin_invoke) && a->delegate_begin_invoke
               ? a->delegate_begin_invoke(delegate_obj, params, async_callback, state)
               : nullptr;
}
inline void *delegate_end_invoke(void *async_result, void **out_args) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(delegate_end_invoke) && a->delegate_end_invoke
               ? a->delegate_end_invoke(async_result, out_args)
               : nullptr;
}
inline void profiler_install(void *profiler, void *shutdown_callback) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(profiler_install) && a->profiler_install)
        a->profiler_install(profiler, shutdown_callback);
}
inline void profiler_set_events(int events) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(profiler_set_events) && a->profiler_set_events)
        a->profiler_set_events(events);
}
inline void profiler_install_enter_leave(void *enter, void *leave) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(profiler_install_enter_leave) && a->profiler_install_enter_leave)
        a->profiler_install_enter_leave(enter, leave);
}
inline void profiler_install_allocation(void *callback) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(profiler_install_allocation) && a->profiler_install_allocation)
        a->profiler_install_allocation(callback);
}
inline void profiler_install_gc(void *callback, void *heap_resize_callback) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(profiler_install_gc) && a->profiler_install_gc)
        a->profiler_install_gc(callback, heap_resize_callback);
}
inline void *unity_liveness_calculation_begin(const void *filter, int max_object_count, void *callback, void *userdata,
                                              void *on_world_started, void *on_world_stopped) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(unity_liveness_calculation_begin) && a->unity_liveness_calculation_begin
               ? a->unity_liveness_calculation_begin(filter, max_object_count, callback, userdata, on_world_started,
                                                     on_world_stopped)
               : nullptr;
}
inline void unity_liveness_calculation_end(void *state) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(unity_liveness_calculation_end) && a->unity_liveness_calculation_end)
        a->unity_liveness_calculation_end(state);
}
inline void unity_liveness_calculation_from_root(void *root, void *state) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(unity_liveness_calculation_from_root) && a->unity_liveness_calculation_from_root)
        a->unity_liveness_calculation_from_root(root, state);
}
inline void unity_liveness_calculation_from_statics(void *state) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(unity_liveness_calculation_from_statics) &&
        a->unity_liveness_calculation_from_statics)
        a->unity_liveness_calculation_from_statics(state);
}
inline int stats_dump_to_file(const char *path) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(stats_dump_to_file) && a->stats_dump_to_file ? a->stats_dump_to_file(path) : 0;
}
inline std::uint64_t stats_get_value(int stat) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(stats_get_value) && a->stats_get_value ? a->stats_get_value(stat) : 0;
}
inline void *capture_memory_snapshot() {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(capture_memory_snapshot) && a->capture_memory_snapshot
               ? a->capture_memory_snapshot()
               : nullptr;
}
inline void free_captured_memory_snapshot(void *snapshot) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(free_captured_memory_snapshot) && a->free_captured_memory_snapshot)
        a->free_captured_memory_snapshot(snapshot);
}
inline const void *debug_get_class_info(const void *klass) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_get_class_info) && a->debug_get_class_info
               ? a->debug_get_class_info(klass)
               : nullptr;
}
inline const void *debug_class_get_document(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_class_get_document) && a->debug_class_get_document
               ? a->debug_class_get_document(info)
               : nullptr;
}
inline const char *debug_document_get_filename(const void *document) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_document_get_filename) && a->debug_document_get_filename
               ? a->debug_document_get_filename(document)
               : nullptr;
}
inline const char *debug_document_get_directory(const void *document) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_document_get_directory) && a->debug_document_get_directory
               ? a->debug_document_get_directory(document)
               : nullptr;
}
inline const void *debug_get_method_info(const void *method) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_get_method_info) && a->debug_get_method_info
               ? a->debug_get_method_info(method)
               : nullptr;
}
inline const void *debug_method_get_document(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_document) && a->debug_method_get_document
               ? a->debug_method_get_document(info)
               : nullptr;
}
inline const std::int32_t *debug_method_get_offset_table(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_offset_table) && a->debug_method_get_offset_table
               ? a->debug_method_get_offset_table(info)
               : nullptr;
}
inline std::size_t debug_method_get_code_size(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_code_size) && a->debug_method_get_code_size
               ? a->debug_method_get_code_size(info)
               : 0;
}
inline void debug_update_frame_il_offset(std::int32_t il_offset) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(debug_update_frame_il_offset) && a->debug_update_frame_il_offset)
        a->debug_update_frame_il_offset(il_offset);
}
inline const void **debug_method_get_locals_info(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_locals_info) && a->debug_method_get_locals_info
               ? a->debug_method_get_locals_info(info)
               : nullptr;
}
inline const void *debug_local_get_type(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_local_get_type) && a->debug_local_get_type
               ? a->debug_local_get_type(info)
               : nullptr;
}
inline const char *debug_local_get_name(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_local_get_name) && a->debug_local_get_name
               ? a->debug_local_get_name(info)
               : nullptr;
}
inline std::uint32_t debug_local_get_start_offset(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_local_get_start_offset) && a->debug_local_get_start_offset
               ? a->debug_local_get_start_offset(info)
               : 0;
}
inline std::uint32_t debug_local_get_end_offset(const void *info) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_local_get_end_offset) && a->debug_local_get_end_offset
               ? a->debug_local_get_end_offset(info)
               : 0;
}
inline void *debug_method_get_param_value(const void *info, std::uint32_t position) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_param_value) && a->debug_method_get_param_value
               ? a->debug_method_get_param_value(info, position)
               : nullptr;
}
inline void *debug_frame_get_local_value(const void *info, std::uint32_t position) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_frame_get_local_value) && a->debug_frame_get_local_value
               ? a->debug_frame_get_local_value(info, position)
               : nullptr;
}
inline void *debug_method_get_breakpoint_data_at(const void *info, std::int64_t uid, std::int32_t offset) {
    const auto *a = api();
    return available() && URK_IL2CPP_HAS(debug_method_get_breakpoint_data_at) && a->debug_method_get_breakpoint_data_at
               ? a->debug_method_get_breakpoint_data_at(info, uid, offset)
               : nullptr;
}
inline void debug_method_set_breakpoint_data_at(const void *info, std::uint64_t location, void *data) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(debug_method_set_breakpoint_data_at) && a->debug_method_set_breakpoint_data_at)
        a->debug_method_set_breakpoint_data_at(info, location, data);
}
inline void debug_method_clear_breakpoint_data(const void *info) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(debug_method_clear_breakpoint_data) && a->debug_method_clear_breakpoint_data)
        a->debug_method_clear_breakpoint_data(info);
}
inline void debug_method_clear_breakpoint_data_at(const void *info, std::uint64_t location) {
    const auto *a = api();
    if (available() && URK_IL2CPP_HAS(debug_method_clear_breakpoint_data_at) &&
        a->debug_method_clear_breakpoint_data_at)
        a->debug_method_clear_breakpoint_data_at(info, location);
}
#undef URK_IL2CPP_HAS
} // namespace URK::il2cpp
)URKIL2CPP";
}

std::string Il2CppHelpersModule() {
    return R"URKHELPERS(#pragma once

#include "../hook_api.h"
#include "il2cpp_runtime.h"

#include <cstddef>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
namespace URK::il2cpp::helpers {
using DiagnosticSink = void (*)(const char *);
struct ManagedHookSpec {
    const char *image = nullptr;
    const char *namespc = nullptr;
    const char *klass = nullptr;
    const char *method = nullptr;
    const char *const *parameter_types = nullptr;
    int parameter_count = 0;
};
inline void emit(DiagnosticSink sink, const char *message) {
    if (sink && message)
        sink(message);
}
inline void emit_last_error(DiagnosticSink sink) {
    if (const char *e = URK::il2cpp::last_error(); e && e[0])
        emit(sink, e);
}
inline std::string to_utf8(URK::il2cpp::String *value, std::string_view fallback) {
    if (!value)
        return std::string(fallback);
    const std::int32_t length = URK::il2cpp::string_length(value);
    constexpr std::int32_t kMaxManagedStringUnits = 1024 * 1024;
    if (length < 0 || length > kMaxManagedStringUnits)
        return std::string(fallback);
    const std::size_t capacity = static_cast<std::size_t>(length) * 4u + 1u;
    std::vector<char> buffer(capacity ? capacity : 1u, '\0');
    if (!URK::il2cpp::string_to_utf8(value, buffer.data(), buffer.size()))
        return std::string(fallback);
    return std::string(buffer.data());
}
inline std::string to_utf8(URK::il2cpp::String *value) {
    return to_utf8(value, std::string_view{});
}
inline ManagedHookSpec managed_hook(const char *image, const char *namespc, const char *klass, const char *method) {
    return ManagedHookSpec{image, namespc, klass, method, nullptr, 0};
}
template <std::size_t N>
inline ManagedHookSpec managed_hook(const char *image, const char *namespc, const char *klass, const char *method,
                                    const char *const (&parameter_types)[N]) {
    return ManagedHookSpec{image, namespc, klass, method, parameter_types, static_cast<int>(N)};
}
inline bool ensure_thread_attached(DiagnosticSink sink = nullptr) {
    if (!URK::il2cpp::available()) {
        emit(sink, "[URK IL2CPP runtime] IL2CPP API unavailable; operation "
                   "is not requested");
        return false;
    }
    if (URK::il2cpp::thread_current())
        return true;
    if (URK::il2cpp::thread_attach())
        return true;
    emit(sink, "[URK IL2CPP runtime] thread_attach failed; operation is not "
               "requested");
    emit_last_error(sink);
    return false;
}
inline bool is_executable_protection(unsigned long protect) {
    protect &= 0xffu;
    return protect == PAGE_EXECUTE || protect == PAGE_EXECUTE_READ || protect == PAGE_EXECUTE_READWRITE ||
           protect == PAGE_EXECUTE_WRITECOPY;
}
inline bool address_in_module(void *address, const char *module_name) {
    HMODULE module = ::GetModuleHandleA(module_name);
    if (!module || !address)
        return false;
    const auto *base = reinterpret_cast<const unsigned char *>(module);
    const auto *dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE)
        return false;
    const auto *nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE)
        return false;
    const auto *ptr = reinterpret_cast<const unsigned char *>(address);
    return ptr >= base && ptr < (base + nt->OptionalHeader.SizeOfImage);
}
inline bool is_valid_icall_target(void *target) {
    if (!target)
        return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (::VirtualQuery(target, &mbi, sizeof(mbi)) != sizeof(mbi))
        return false;
    if (mbi.State != MEM_COMMIT || (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) ||
        !is_executable_protection(mbi.Protect))
        return false;
    return address_in_module(target, "GameAssembly.dll") || address_in_module(target, "UnityPlayer.dll");
}
inline std::unordered_map<std::string, void *> &icall_cache() {
    static std::unordered_map<std::string, void *> cache;
    return cache;
}
inline std::mutex &icall_cache_mutex() {
    static std::mutex m;
    return m;
}
inline void *try_resolve_icall(const char *name, DiagnosticSink sink = nullptr) {
    if (!name || !name[0]) {
        emit(sink, "[URK IL2CPP runtime] icall name is null/empty; resolve_icall "
                   "not requested");
        return nullptr;
    }
    std::lock_guard<std::mutex> lock(icall_cache_mutex());
    auto &cache = icall_cache();
    const auto found = cache.find(name);
    if (found != cache.end())
        return found->second;
    void *target = URK::il2cpp::resolve_icall(name);
    if (!target) {
        char msg[768]{};
        std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] resolve_icall failed for: %s", name);
        emit(sink, msg);
        emit_last_error(sink);
        return nullptr;
    }
    if (!is_valid_icall_target(target)) {
        char msg[896]{};
        std::snprintf(msg, sizeof(msg),
                      "[URK IL2CPP runtime] resolve_icall returned an "
                      "invalid/non-executable/non-Unity target for: %s",
                      name);
        emit(sink, msg);
        return nullptr;
    }
    cache.emplace(name, target);
    return target;
}
inline bool try_hook_icall(const char *name, void **original, void *detour, DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr) {
    if (!original || !detour) {
        emit(sink, "[URK IL2CPP runtime] icall hook original/detour is null; hook "
                   "is not installed");
        return false;
    }
    void *target = try_resolve_icall(name, sink);
    if (!target)
        return false;
    *original = target;
    if (!URK::hooks::attach_ex(original, detour, options)) {
        emit(sink, "[URK IL2CPP runtime] hook backend unavailable or attach "
                   "failed; icall hook is not installed");
        return false;
    }
    return true;
}
inline void emit_method_pointer_unavailable(DiagnosticSink sink, const char *image = nullptr,
                                            const char *namespc = nullptr, const char *klass = nullptr,
                                            const char *method = nullptr) {
    char msg[768]{};
    std::snprintf(msg, sizeof(msg),
                  "[URK IL2CPP runtime] method_pointer unavailable or validation "
                  "failed for %s:%s.%s.%s; native method hook is not installed",
                  image ? image : "", namespc ? namespc : "", klass ? klass : "", method ? method : "");
    emit(sink, msg);
    emit_last_error(sink);
}
inline void *try_method_pointer(const URK::il2cpp::Method *method, DiagnosticSink sink = nullptr,
                                const char *image = nullptr, const char *namespc = nullptr, const char *klass = nullptr,
                                const char *method_name = nullptr) {
    if (!method) {
        emit(sink, "[URK IL2CPP runtime] method handle is null; method_pointer not "
                   "requested");
        emit_last_error(sink);
        return nullptr;
    }
    void *target = URK::il2cpp::method_pointer(method);
    if (!target)
        emit_method_pointer_unavailable(sink, image, namespc, klass, method_name);
    return target;
}
inline bool try_method_pointer(const URK::il2cpp::Method *method, void *&out, DiagnosticSink sink = nullptr,
                               const char *image = nullptr, const char *namespc = nullptr, const char *klass = nullptr,
                               const char *method_name = nullptr) {
    out = try_method_pointer(method, sink, image, namespc, klass, method_name);
    return out != nullptr;
}
inline void *require_method_pointer(const URK::il2cpp::Method *method, DiagnosticSink sink = nullptr,
                                    const char *image = nullptr, const char *namespc = nullptr,
                                    const char *klass = nullptr, const char *method_name = nullptr) {
    return try_method_pointer(method, sink, image, namespc, klass, method_name);
}
// try_hook_method_pointer seeds *original with the resolved method_pointer
// target before attaching. On success, the hook backend may replace *original
// with a trampoline. On failure, no hook is installed, but *original may still
// contain the unhooked target pointer.
inline bool try_hook_method_pointer(const URK::il2cpp::Method *method, void **original, void *detour,
                                    DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                                    const char *image = nullptr, const char *namespc = nullptr,
                                    const char *klass = nullptr, const char *method_name = nullptr) {
    void *target = try_method_pointer(method, sink, image, namespc, klass, method_name);
    if (!target)
        return false;
    if (!original || !detour) {
        emit(sink, "[URK IL2CPP runtime] hook original/detour is null; native "
                   "method hook is not installed");
        return false;
    }
    *original = target;
    if (!URK::hooks::attach_ex(original, detour, options)) {
        emit(sink, "[URK IL2CPP runtime] hook backend unavailable or attach "
                   "failed; native method hook is not installed");
        return false;
    }
    return true;
}
inline bool try_hook_managed_method(const URK_Il2CppManagedMethodDesc &desc, void **original, void *detour,
                                    DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                                    URK_Il2CppManagedHookResult *result = nullptr) {
    URK_Il2CppManagedHookResult local = URK::il2cpp::managed_hook_result();
    URK_Il2CppManagedHookResult *out = result ? result : &local;
    out->size = sizeof(URK_Il2CppManagedHookResult);
    if (!original || !detour) {
        emit(sink, "[URK IL2CPP runtime] managed-method hook original/detour is "
                   "null; hook is not installed");
        return false;
    }
    if (URK::il2cpp::attach_managed_method_hook(&desc, original, detour, options, out) != 0)
        return true;
    emit(sink, out->diagnostic && out->diagnostic[0] ? out->diagnostic
                                                     : "[URK IL2CPP runtime] managed-method hook was refused or "
                                                       "methodPointer validation failed; hook is not installed");
    emit_last_error(sink);
    return false;
}
inline bool try_hook_managed_method(const char *image, const char *namespc, const char *klass, const char *method,
                                    const char *const *parameter_types, int parameter_count, void **original,
                                    void *detour, DiagnosticSink sink = nullptr,
                                    const URK_HookOptions *options = nullptr,
                                    URK_Il2CppManagedHookResult *result = nullptr) {
    const URK_Il2CppManagedMethodDesc desc =
        URK::il2cpp::managed_method_desc(image, namespc, klass, method, parameter_types, parameter_count);
    return try_hook_managed_method(desc, original, detour, sink, options, result);
}
inline bool attach_managed(const ManagedHookSpec &spec, void **original, void *detour, DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr, URK_Il2CppManagedHookResult *result = nullptr) {
    if (!ensure_thread_attached(sink))
        return false;
    return try_hook_managed_method(spec.image, spec.namespc, spec.klass, spec.method, spec.parameter_types,
                                   spec.parameter_count, original, detour, sink, options, result);
}
template <class Fn>
inline bool attach_managed(const ManagedHookSpec &spec, Fn *original, Fn detour, DiagnosticSink sink = nullptr,
                           const URK_HookOptions *options = nullptr, URK_Il2CppManagedHookResult *result = nullptr) {
    return attach_managed(spec, reinterpret_cast<void **>(original), reinterpret_cast<void *>(detour), sink, options,
                          result);
}
inline bool attach_managed(const char *image, const char *namespc, const char *klass, const char *method,
                           std::initializer_list<const char *> parameter_types, void **original, void *detour,
                           DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                           URK_Il2CppManagedHookResult *result = nullptr) {
    std::vector<const char *> params(parameter_types);
    const ManagedHookSpec spec{image, namespc, klass, method, params.data(), static_cast<int>(params.size())};
    return attach_managed(spec, original, detour, sink, options, result);
}
template <class Fn>
inline bool attach_managed(const char *image, const char *namespc, const char *klass, const char *method,
                           std::initializer_list<const char *> parameter_types, Fn *original, Fn detour,
                           DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                           URK_Il2CppManagedHookResult *result = nullptr) {
    return attach_managed(image, namespc, klass, method, parameter_types, reinterpret_cast<void **>(original),
                          reinterpret_cast<void *>(detour), sink, options, result);
}
inline bool require_class(const char *image, const char *namespc, const char *name, const URK::il2cpp::Class *&out,
                          DiagnosticSink sink = nullptr) {
    out = URK::il2cpp::find_class(image, namespc, name);
    if (out)
        return true;
    char msg[640]{};
    std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing class: %s:%s.%s", image ? image : "",
                  namespc ? namespc : "", name ? name : "");
    emit(sink, msg);
    emit_last_error(sink);
    return false;
}
inline bool require_method_exact(const char *image, const char *namespc, const char *klass, const char *name,
                                 const char *const *parameter_types, int parameter_count, const char *return_type,
                                 const URK::il2cpp::Method *&out, DiagnosticSink sink = nullptr) {
    out = URK::il2cpp::resolve_method_exact(image, namespc, klass, name, parameter_types, parameter_count);
    if (out)
        return true;
    char params[384]{};
    std::size_t used = 0;
    for (int i = 0; i < parameter_count && used < sizeof(params); ++i) {
        int w = std::snprintf(params + used, sizeof(params) - used, "%s%s", i ? ", " : "",
                              parameter_types && parameter_types[i] ? parameter_types[i] : "<unknown>");
        if (w < 0)
            break;
        used += static_cast<std::size_t>(w);
    }
    char msg[1024]{};
    std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing/changed method: %s:%s.%s.%s(%s) -> %s",
                  image ? image : "", namespc ? namespc : "", klass ? klass : "", name ? name : "", params,
                  return_type ? return_type : "<unknown>");
    emit(sink, msg);
    emit_last_error(sink);
    return false;
}
inline bool require_field(const char *image, const char *namespc, const char *klass, const char *name,
                          const URK::il2cpp::Field *&out, DiagnosticSink sink = nullptr) {
    out = URK::il2cpp::resolve_field(image, namespc, klass, name);
    if (out)
        return true;
    char msg[640]{};
    std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing/changed field: %s:%s.%s.%s", image ? image : "",
                  namespc ? namespc : "", klass ? klass : "", name ? name : "");
    emit(sink, msg);
    emit_last_error(sink);
    return false;
}
inline bool require_method(const URK::il2cpp::Class *klass, const char *name, int argc, const URK::il2cpp::Method *&out,
                           DiagnosticSink sink = nullptr) {
    out = URK::il2cpp::resolve_method(klass, name, argc);
    if (out)
        return true;
    char msg[512]{};
    std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing method: %s/%d", name ? name : "", argc);
    emit(sink, msg);
    emit_last_error(sink);
    return false;
}
inline bool require_property(const URK::il2cpp::Class *klass, const char *name, const URK::il2cpp::Property *&out,
                             DiagnosticSink sink = nullptr) {
    void *it = nullptr;
    while ((out = URK::il2cpp::class_get_properties(klass, &it)) != nullptr) {
        const char *n = URK::il2cpp::property_get_name(out);
        if (n && name && std::strcmp(n, name) == 0)
            return true;
    }
    char msg[512]{};
    std::snprintf(msg, sizeof(msg), "[URK IL2CPP runtime] missing property: %s", name ? name : "");
    emit(sink, msg);
    emit_last_error(sink);
    return false;
}
} // namespace URK::il2cpp::helpers

namespace Il2CppHook {
using DiagnosticSink = URK::il2cpp::helpers::DiagnosticSink;
using ManagedSpec = URK::il2cpp::helpers::ManagedHookSpec;

inline ManagedSpec managed(const char *image, const char *namespc, const char *klass, const char *method) {
    return URK::il2cpp::helpers::managed_hook(image, namespc, klass, method);
}

template <std::size_t N>
inline ManagedSpec managed(const char *image, const char *namespc, const char *klass, const char *method,
                           const char *const (&parameter_types)[N]) {
    return URK::il2cpp::helpers::managed_hook(image, namespc, klass, method, parameter_types);
}

inline bool attach(const ManagedSpec &spec, void **original, void *detour, DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr, URK_Il2CppManagedHookResult *result = nullptr) {
    return URK::il2cpp::helpers::attach_managed(spec, original, detour, sink, options, result);
}

template <class Fn>
inline bool attach(const ManagedSpec &spec, Fn *original, Fn detour, DiagnosticSink sink = nullptr,
                   const URK_HookOptions *options = nullptr, URK_Il2CppManagedHookResult *result = nullptr) {
    return URK::il2cpp::helpers::attach_managed(spec, original, detour, sink, options, result);
}

inline bool attach(const char *image, const char *namespc, const char *klass, const char *method,
                   std::initializer_list<const char *> parameter_types, void **original, void *detour,
                   DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
    return URK::il2cpp::helpers::attach_managed(image, namespc, klass, method, parameter_types, original, detour, sink,
                                                options, result);
}

template <class Fn>
inline bool attach(const char *image, const char *namespc, const char *klass, const char *method,
                   std::initializer_list<const char *> parameter_types, Fn *original, Fn detour,
                   DiagnosticSink sink = nullptr, const URK_HookOptions *options = nullptr,
                   URK_Il2CppManagedHookResult *result = nullptr) {
    return URK::il2cpp::helpers::attach_managed(image, namespc, klass, method, parameter_types, original, detour, sink,
                                                options, result);
}
} // namespace Il2CppHook
)URKHELPERS";
}

std::string Readme(const std::string &details) {
    std::ostringstream out;
    out << "# URKit - IL2CPP Runtime Helpers\n\nGenerated IL2CPP "
           "runtime-helper headers. Bindings use `ctx->il2cpp`, `URK_Il2CppApi`, "
           "runtime capability checks, runtime image/class/member lookup, method "
           "signatures, and `last_error` diagnostics. They intentionally do not "
           "hardcode addresses, metadata registration data, vtables, or "
           "Unity-version-specific offsets. Managed hook targets use the loader's "
           "validated first-field `MethodInfo::methodPointer` path.\n\nRuntime-only "
           "IL2CPP generation does not emit per-game "
           "SDK descriptors.\n\n"
        << details;
    return out.str();
}
} // namespace

namespace Il2CppSdkGenerator {

std::string SanitizeProjectName(const std::string &projectName, const std::string &fallback) {
    return mpg::Identifier(projectName, fallback.c_str());
}
bool HasUsableOutput(const std::string &directory) {
    std::error_code ec;
    const fs::path root(directory);
    for (const fs::path &f : {root / "il2cpp_runtime.h", root / "il2cpp_helpers.h", root / "README.md"})
        if (!fs::is_regular_file(f, ec) || fs::file_size(f, ec) == 0 || ec)
            return false;
    return true;
}
bool Generate(const std::string &directory, const std::string &reportDetails, std::string *error) {
    sdk::OutputPlan plan;
    plan.root = fs::path(directory);
    plan.files = {
        {"il2cpp_runtime.h", {}, Il2CppRuntimeModule(), mpg::OutputFilePolicy::GeneratedOverwrite, true, true},
        {"il2cpp_helpers.h", {}, Il2CppHelpersModule(), mpg::OutputFilePolicy::GeneratedOverwrite, true, true},
        {"README.md", {}, Readme(reportDetails), mpg::OutputFilePolicy::GeneratedOverwrite, true, false}};
    sdk::OutputResult out;
    return sdk::PublishOutputPlanAtomically(plan, &out, error);
}
bool GenerateModProject(const std::string &projectRoot, const std::string &sdkRoot,
                        const std::string &commonIncludeRoot, const std::string &rawProjectName,
                        const std::string &gameDirectory, const std::string &modsDirectory, bool enableLocalization,
                        std::string *error) {
    const std::string project = SanitizeProjectName(rawProjectName);
    const auto profile = mpg::Il2CppBackendProfile();

    fs::path sdkHeaderPath;
    if (!commonIncludeRoot.empty())
        sdkHeaderPath = fs::path(commonIncludeRoot) / "mod_sdk.h";

    if (!HasUsableOutput(sdkRoot)) {
        if (error)
            *error = "IL2CPP helper output is missing or incomplete; generate IL2CPP "
                     "runtime helpers before project";
        return false;
    }

    sdk::OutputPlan backend;
    backend.root = fs::path(projectRoot);
    backend.files = {
        {profile.sdkSubdirectory / "il2cpp_runtime.h",
         fs::path(sdkRoot) / "il2cpp_runtime.h",
         {},
         mpg::OutputFilePolicy::GeneratedOverwrite,
         true,
         true},
        {profile.sdkSubdirectory / "il2cpp_helpers.h",
         fs::path(sdkRoot) / "il2cpp_helpers.h",
         {},
         mpg::OutputFilePolicy::GeneratedOverwrite,
         true,
         true},
    };
    if (!sdk::WriteOutputPlan(backend, nullptr, error))
        return false;

    auto options = mpg::MakeModuleProjectOptions(profile, fs::path(projectRoot), project, sdkHeaderPath);
    options.deployDirectory = (fs::path(gameDirectory) / modsDirectory).string();
    options.enableLocalization = enableLocalization;
    return mpg::WriteModuleProject(options, error);
}
} // namespace Il2CppSdkGenerator
