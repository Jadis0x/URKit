#pragma once

#include "../runtime_api.h"

#include <cstddef>
#include <cstdint>

namespace URK::mono {
using Image = void;
using Class = void;
using Method = void;
using Field = void;
using Property = void;
using Type = void;
using Signature = void;
using Object = void;
using String = void;
using Array = void;

inline const URK_MonoApi *&ApiSlot() {
  static const URK_MonoApi *api = nullptr;
  return api;
}
inline bool has_field(const URK_MonoApi *a, std::size_t end) {
  return URK::has_runtime_capability(URK::runtime_cap_mono_api) && a &&
         a->version >= URK_MONO_API_VERSION && a->size >= end;
}
#define URK_MONO_HAS(member)                                                   \
  (has_field(a, offsetof(URK_MonoApi, member) + sizeof(a->member)))
inline bool init(const URK::ModContext *ctx) {
  URK::set_context(ctx);
  ApiSlot() = nullptr;
  if (!ctx || ctx->version < URK_SDK_VERSION || ctx->size < sizeof(URK_ModContext) ||
      ctx->runtimeBackend != URK::runtime_backend_mono ||
      !URK::has_runtime_capability(URK::runtime_cap_mono_api) || !ctx->mono ||
      ctx->mono->version < URK_MONO_API_VERSION || ctx->mono->size < sizeof(URK_MonoApi))
    return false;
  ApiSlot() = ctx->mono;
  return true;
}
inline const URK_MonoApi *api() {
  if (const URK::ModContext *ctx = URK::context()) {
    if (ctx->runtimeBackend == URK::runtime_backend_mono &&
        URK::has_runtime_capability(URK::runtime_cap_mono_api) && ctx->mono &&
        ctx->mono->version >= URK_MONO_API_VERSION && ctx->mono->size >= sizeof(URK_MonoApi))
      return ctx->mono;
  }
  return ApiSlot();
}
inline bool available() {
  const auto *a = api();
  return URK::has_runtime_capability(URK::runtime_cap_mono_api) && a != nullptr &&
         a->version >= URK_MONO_API_VERSION && a->size >= sizeof(URK_MonoApi);
}
inline bool attach_current_thread() {
  const auto *a = api();
  return a && URK_MONO_HAS(attach_current_thread) && a->attach_current_thread
             ? a->attach_current_thread() != 0
             : false;
}
inline const Image *find_image(const char *image) {
  const auto *a = api();
  return a && URK_MONO_HAS(find_image) && a->find_image
             ? static_cast<const Image *>(a->find_image(image))
             : nullptr;
}
inline const Class *find_class(const char *image, const char *namespc,
                               const char *name) {
  const auto *a = api();
  return a && URK_MONO_HAS(find_class) && a->find_class
             ? static_cast<const Class *>(a->find_class(image, namespc, name))
             : nullptr;
}
inline const Method *resolve_method(const char *image, const char *namespc,
                                    const char *klass, const char *name,
                                    int argc = -1) {
  const auto *a = api();
  return a && URK_MONO_HAS(find_method) && a->find_method
             ? static_cast<const Method *>(
                   a->find_method(image, namespc, klass, name, argc))
             : nullptr;
}
inline const Method *resolve_method_exact(const char *image,
                                          const char *namespc,
                                          const char *klass, const char *name,
                                          const char *const *parameter_types,
                                          int parameter_count) {
  const auto *a = api();
  return a && URK_MONO_HAS(find_method_exact) && a->find_method_exact
             ? static_cast<const Method *>(
                   a->find_method_exact(image, namespc, klass, name,
                                        parameter_types, parameter_count))
             : nullptr;
}
inline const Field *resolve_field(const char *image, const char *namespc,
                                  const char *klass, const char *name) {
  const auto *a = api();
  return a && URK_MONO_HAS(find_field) && a->find_field
             ? static_cast<const Field *>(
                   a->find_field(image, namespc, klass, name))
             : nullptr;
}
inline void *compile_method(const Method *method) {
  const auto *a = api();
  return a && URK_MONO_HAS(compile_method) && a->compile_method
             ? a->compile_method(method)
             : nullptr;
}
inline void *method_get_object(const Method *method, const Class *) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_get_object) && a->method_get_object
             ? a->method_get_object(method)
             : nullptr;
}
inline void *value_box(const Class *klass, void *data) {
  const auto *a = api();
  return a && URK_MONO_HAS(value_box) && a->value_box
             ? a->value_box(klass, data)
             : nullptr;
}
inline int runtime_invoke(const Method *method, Object *object, void **params,
                          void **result = nullptr, void **exception = nullptr,
                          std::uint32_t *native_exception = nullptr) {
  const auto *a = api();
  return a && URK_MONO_HAS(runtime_invoke) && a->runtime_invoke
             ? a->runtime_invoke(method, object, params, result, exception,
                                 native_exception)
             : 0;
}
inline String *string_new(const char *utf8) {
  const auto *a = api();
  return a && URK_MONO_HAS(new_string) && a->new_string
             ? static_cast<String *>(a->new_string(utf8))
             : nullptr;
}
inline String *new_string(const char *utf8) {
  const auto *a = api();
  return a && URK_MONO_HAS(new_string) && a->new_string
             ? static_cast<String *>(a->new_string(utf8))
             : nullptr;
}
inline std::size_t array_length(Array *array) {
  const auto *a = api();
  return a && URK_MONO_HAS(array_length) && a->array_length
             ? a->array_length(array)
             : 0;
}
inline void *array_address(Array *array, int element_size, std::size_t index) {
  const auto *a = api();
  return a && URK_MONO_HAS(array_address) && a->array_address
             ? a->array_address(array, element_size, index)
             : nullptr;
}
inline bool has_array_length() {
  const auto *a = api();
  return a && URK_MONO_HAS(array_length) && a->array_length;
}
inline void *array_addr_with_size(Array *array, int element_size,
                                  std::size_t index) {
  return array_address(array, element_size, index);
}
inline void *array_ref_at(Array *array, std::size_t index) {
  const auto *a = api();
  if (a && URK_MONO_HAS(array_ref_at) && a->array_ref_at)
    return a->array_ref_at(array, index);
  return nullptr;
}
inline bool has_array_ref_at() {
  const auto *a = api();
  return a && URK_MONO_HAS(array_ref_at) && a->array_ref_at;
}
inline bool array_set_ref(Array *array, std::size_t index, void *value) {
  const auto *a = api();
  return a && URK_MONO_HAS(array_set_ref) && a->array_set_ref
             ? a->array_set_ref(array, index, value) != 0
             : false;
}
inline bool has_array_set_ref() {
  const auto *a = api();
  return a && URK_MONO_HAS(array_set_ref) && a->array_set_ref;
}
inline bool object_class_name(Object *object, char *output,
                              std::size_t output_size) {
  const auto *a = api();
  return a && URK_MONO_HAS(object_class_name) && a->object_class_name
             ? a->object_class_name(object, output, output_size) != 0
             : false;
}
inline const Class *object_get_class(Object *object) {
  const auto *a = api();
  return a && URK_MONO_HAS(object_get_class) && a->object_get_class
             ? static_cast<const Class *>(a->object_get_class(object))
             : nullptr;
}
inline void *object_unbox(Object *object) {
  const auto *a = api();
  return a && URK_MONO_HAS(object_unbox) && a->object_unbox
             ? a->object_unbox(object)
             : nullptr;
}
inline Object *object_new(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(object_new) && a->object_new
             ? static_cast<Object *>(a->object_new(klass))
             : nullptr;
}
inline Object *type_get_object(const Type *type) {
  const auto *a = api();
  return a && URK_MONO_HAS(type_get_object) && a->type_get_object
             ? static_cast<Object *>(a->type_get_object(type))
             : nullptr;
}
inline void runtime_object_init(Object *object) {
  const auto *a = api();
  if (a && URK_MONO_HAS(runtime_object_init) && a->runtime_object_init &&
      object)
    a->runtime_object_init(object);
}
inline bool string_to_utf8(String *string, char *output,
                           std::size_t output_size) {
  const auto *a = api();
  return a && URK_MONO_HAS(string_to_utf8) && a->string_to_utf8
             ? a->string_to_utf8(string, output, output_size) != 0
             : false;
}
inline std::size_t string_length(String *string) {
  const auto *a = api();
  return a && URK_MONO_HAS(string_length) && a->string_length
             ? a->string_length(string)
             : 0;
}
inline const char *class_get_name(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_name) && a->class_get_name
             ? a->class_get_name(klass)
             : nullptr;
}
inline const char *class_get_namespace(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_namespace) && a->class_get_namespace
             ? a->class_get_namespace(klass)
             : nullptr;
}
inline const Class *class_get_parent(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_parent) && a->class_get_parent
             ? static_cast<const Class *>(a->class_get_parent(klass))
             : nullptr;
}
inline std::uint32_t class_get_flags(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_flags) && a->class_get_flags
             ? a->class_get_flags(klass)
             : 0;
}
inline const Field *class_get_fields(const Class *klass, void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_fields) && a->class_get_fields
             ? static_cast<const Field *>(a->class_get_fields(klass, iterator))
             : nullptr;
}
inline const Method *class_get_methods(const Class *klass, void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_methods) && a->class_get_methods
             ? static_cast<const Method *>(
                   a->class_get_methods(klass, iterator))
             : nullptr;
}
inline const Property *class_get_properties(const Class *klass,
                                            void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_properties) && a->class_get_properties
             ? static_cast<const Property *>(
                   a->class_get_properties(klass, iterator))
             : nullptr;
}
inline const char *field_get_name(const Field *field) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_get_name) && a->field_get_name
             ? a->field_get_name(field)
             : nullptr;
}
inline const Type *field_get_type(const Field *field) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_get_type) && a->field_get_type
             ? static_cast<const Type *>(a->field_get_type(field))
             : nullptr;
}
inline std::uint32_t field_get_offset(const Field *field) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_get_offset) && a->field_get_offset
             ? a->field_get_offset(field)
             : 0;
}
inline std::uint32_t field_get_flags(const Field *field) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_get_flags) && a->field_get_flags
             ? a->field_get_flags(field)
             : 0;
}
inline const char *last_error() {
  const auto *a = api();
  return a && URK_MONO_HAS(last_error) && a->last_error ? a->last_error()
                                                        : nullptr;
}
inline const void *domain_get() {
  const auto *a = api();
  return a && URK_MONO_HAS(domain_get) && a->domain_get ? a->domain_get()
                                                        : nullptr;
}
inline const void *root_domain_get() {
  const auto *a = api();
  return a && URK_MONO_HAS(root_domain_get) && a->root_domain_get
             ? a->root_domain_get()
             : nullptr;
}
inline const Image *assembly_get_image(const void *assembly) {
  const auto *a = api();
  return a && URK_MONO_HAS(assembly_get_image) && a->assembly_get_image
             ? static_cast<const Image *>(a->assembly_get_image(assembly))
             : nullptr;
}
inline const char *image_get_name(const Image *image) {
  const auto *a = api();
  return a && URK_MONO_HAS(image_get_name) && a->image_get_name
             ? a->image_get_name(image)
             : nullptr;
}
inline const char *image_get_filename(const Image *image) {
  const auto *a = api();
  return a && URK_MONO_HAS(image_get_filename) && a->image_get_filename
             ? a->image_get_filename(image)
             : nullptr;
}
inline int image_get_table_rows(const Image *image, int table_id) {
  const auto *a = api();
  return a && URK_MONO_HAS(image_get_table_rows) && a->image_get_table_rows
             ? a->image_get_table_rows(image, table_id)
             : 0;
}
inline const Class *image_get_class(const Image *image, std::uint32_t token) {
  const auto *a = api();
  return a && URK_MONO_HAS(image_get_class) && a->image_get_class
             ? static_cast<const Class *>(a->image_get_class(image, token))
             : nullptr;
}
inline const Type *class_get_type(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_type) && a->class_get_type
             ? static_cast<const Type *>(a->class_get_type(klass))
             : nullptr;
}
inline bool class_is_valuetype(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_is_valuetype) && a->class_is_valuetype
             ? a->class_is_valuetype(klass) != 0
             : false;
}
inline bool class_is_enum(const Class *klass) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_is_enum) && a->class_is_enum
             ? a->class_is_enum(klass) != 0
             : false;
}
inline const Class *class_get_nested_types(const Class *klass,
                                           void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_nested_types) && a->class_get_nested_types
             ? static_cast<const Class *>(
                   a->class_get_nested_types(klass, iterator))
             : nullptr;
}
inline const Class *class_get_interfaces(const Class *klass, void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(class_get_interfaces) && a->class_get_interfaces
             ? static_cast<const Class *>(
                   a->class_get_interfaces(klass, iterator))
             : nullptr;
}
inline const char *method_get_name(const Method *method) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_get_name) && a->method_get_name
             ? a->method_get_name(method)
             : nullptr;
}
inline std::uint32_t method_get_flags(const Method *method,
                                      std::uint32_t *iflags = nullptr) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_get_flags) && a->method_get_flags
             ? a->method_get_flags(method, iflags)
             : 0;
}
inline const Signature *method_signature(const Method *method) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_signature) && a->method_signature
             ? static_cast<const Signature *>(a->method_signature(method))
             : nullptr;
}
inline std::uint32_t signature_get_param_count(const Signature *signature) {
  const auto *a = api();
  return a && URK_MONO_HAS(signature_get_param_count) &&
                 a->signature_get_param_count
             ? a->signature_get_param_count(signature)
             : 0;
}
inline const Type *signature_get_return_type(const Signature *signature) {
  const auto *a = api();
  return a && URK_MONO_HAS(signature_get_return_type) &&
                 a->signature_get_return_type
             ? static_cast<const Type *>(
                   a->signature_get_return_type(signature))
             : nullptr;
}
inline const Type *signature_get_param(const Signature *signature,
                                       void **iterator) {
  const auto *a = api();
  return a && URK_MONO_HAS(signature_get_param) && a->signature_get_param
             ? static_cast<const Type *>(
                   a->signature_get_param(signature, iterator))
             : nullptr;
}
inline const Type *method_get_return_type(const Method *method) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_get_return_type) && a->method_get_return_type
             ? static_cast<const Type *>(a->method_get_return_type(method))
             : nullptr;
}
inline const Type *method_get_param_type(const Method *method,
                                         std::uint32_t index) {
  const auto *a = api();
  return a && URK_MONO_HAS(method_get_param_type) && a->method_get_param_type
             ? static_cast<const Type *>(
                   a->method_get_param_type(method, index))
             : nullptr;
}
inline const char *property_get_name(const Property *property) {
  const auto *a = api();
  return a && URK_MONO_HAS(property_get_name) && a->property_get_name
             ? a->property_get_name(property)
             : nullptr;
}
inline const Method *property_get_get_method(const Property *property) {
  const auto *a = api();
  return a && URK_MONO_HAS(property_get_get_method) &&
                 a->property_get_get_method
             ? static_cast<const Method *>(a->property_get_get_method(property))
             : nullptr;
}
inline const Method *property_get_set_method(const Property *property) {
  const auto *a = api();
  return a && URK_MONO_HAS(property_get_set_method) &&
                 a->property_get_set_method
             ? static_cast<const Method *>(a->property_get_set_method(property))
             : nullptr;
}
inline std::uint32_t property_get_flags(const Property *property) {
  const auto *a = api();
  return a && URK_MONO_HAS(property_get_flags) && a->property_get_flags
             ? a->property_get_flags(property)
             : 0;
}
inline bool field_get_value(Object *object, const Field *field, void *output) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_get_value) && a->field_get_value
             ? a->field_get_value(object, field, output) != 0
             : false;
}
inline bool field_set_value(Object *object, const Field *field, void *value) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_set_value) && a->field_set_value
             ? a->field_set_value(object, field, value) != 0
             : false;
}
inline bool field_static_get_value(const Class *klass, const Field *field,
                                   void *output) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_static_get_value) && a->field_static_get_value
             ? a->field_static_get_value(klass, field, output) != 0
             : false;
}
inline bool field_static_set_value(const Class *klass, const Field *field,
                                   void *value) {
  const auto *a = api();
  return a && URK_MONO_HAS(field_static_set_value) && a->field_static_set_value
             ? a->field_static_set_value(klass, field, value) != 0
             : false;
}
inline bool type_get_name(const Type *type, char *output,
                          std::size_t output_size) {
  const auto *a = api();
  return a && URK_MONO_HAS(type_get_name) && a->type_get_name
             ? a->type_get_name(type, output, output_size) != 0
             : false;
}
inline std::int32_t type_get_type(const Type *type) {
  const auto *a = api();
  return a && URK_MONO_HAS(type_get_type) && a->type_get_type
             ? a->type_get_type(type)
             : 0;
}
inline std::uint32_t type_get_attrs(const Type *type) {
  const auto *a = api();
  return a && URK_MONO_HAS(type_get_attrs) && a->type_get_attrs
             ? a->type_get_attrs(type)
             : 0;
}
inline const Class *type_get_class(const Type *type) {
  const auto *a = api();
  return a && URK_MONO_HAS(type_get_class) && a->type_get_class
             ? static_cast<const Class *>(a->type_get_class(type))
             : nullptr;
}
inline const void *thread_current() {
  const auto *a = api();
  return a && URK_MONO_HAS(thread_current) && a->thread_current
             ? a->thread_current()
             : nullptr;
}
inline void thread_detach(const void *thread) {
  const auto *a = api();
  if (a && URK_MONO_HAS(thread_detach) && a->thread_detach && thread)
    a->thread_detach(thread);
}
inline std::uint32_t gchandle_new(void *object, int pinned) {
  const auto *a = api();
  return a && URK_MONO_HAS(gchandle_new) && a->gchandle_new
             ? a->gchandle_new(object, pinned)
             : 0;
}
inline std::uint32_t gchandle_new_weakref(void *object,
                                          int track_resurrection) {
  const auto *a = api();
  return a && URK_MONO_HAS(gchandle_new_weakref) && a->gchandle_new_weakref
             ? a->gchandle_new_weakref(object, track_resurrection)
             : 0;
}
inline void *gchandle_get_target(std::uint32_t gchandle) {
  const auto *a = api();
  return a && URK_MONO_HAS(gchandle_get_target) && a->gchandle_get_target
             ? a->gchandle_get_target(gchandle)
             : nullptr;
}
inline void gchandle_free(std::uint32_t gchandle) {
  const auto *a = api();
  if (a && URK_MONO_HAS(gchandle_free) && a->gchandle_free)
    a->gchandle_free(gchandle);
}
#undef URK_MONO_HAS
} // namespace URK::mono