#include "mod_api_internal.h"
#include "mono_api.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <string>

namespace {
using namespace URK::ModApiInternal;

::MonoApi *g_mono = nullptr;
thread_local std::string g_lastError;
void MonoSetError(const char *m) {
    g_lastError = m ? m : "Mono API: unknown error";
}
void MonoSetNativeExceptionError(const char *operation, uint32_t exception_code) {
    char message[192]{};
    std::snprintf(message, sizeof(message), "Mono API: %s raised native exception 0x%08X",
                  operation ? operation : "runtime operation", exception_code);
    MonoSetError(message);
}
void MonoClearError() {
    g_lastError.clear();
}
const char *MonoMissing(const char *entry) {
    static thread_local std::string message;
    message = "Mono API: ";
    message += entry ? entry : "requested entry";
    message += " unavailable";
    return message.c_str();
}
const char *MonoInvalid(const char *entry, const char *detail) {
    static thread_local std::string message;
    message = "Mono API: ";
    message += entry ? entry : "call";
    message += " invalid input";
    if (detail && *detail) {
        message += ": ";
        message += detail;
    }
    return message.c_str();
}
const char *Mono_last_error() {
    return g_lastError.c_str();
}

bool AttachCurrentMonoThreadForModApi(const char *purpose) {
    MonoClearError();
    if (!g_mono) {
        MonoSetError("Mono API: runtime is not initialized");
        return false;
    }
    MonoDomain *domain = Mono_PublishedDomain();
    if (!domain) {
        MonoSetError("Mono API: Mono domain is not published");
        return false;
    }
    if (!Mono_AttachCurrentThread(*g_mono, domain, purpose)) {
        MonoSetError("Mono API: failed to attach current thread to Mono domain");
        return false;
    }
    return true;
}

int CopyMonoAllocatedString(char *raw, char *output, size_t output_size) {
    if (!output || output_size == 0)
        return 0;

    output[0] = '\0';
    if (!raw)
        return 0;

    const size_t count = (std::min)(std::strlen(raw), output_size - 1);
    std::memcpy(output, raw, count);
    output[count] = '\0';

    if (g_mono && g_mono->free_)
        g_mono->free_(raw);

    return 1;
}

int Mono_attach_current_thread() {
    return AttachCurrentMonoThreadForModApi("mod API mono.attach_current_thread") ? 1 : 0;
}

const void *Mono_find_image(const char *image) {
    return AttachCurrentMonoThreadForModApi("mod API mono.find_image") ? g_mono->FindImage(image) : nullptr;
}

const void *Mono_find_class(const char *image, const char *namespc, const char *name) {
    return AttachCurrentMonoThreadForModApi("mod API mono.find_class") ? g_mono->FindClass(image, namespc, name)
                                                                       : nullptr;
}

const void *Mono_find_method(const char *image, const char *namespc, const char *klass, const char *name, int argc) {
    return AttachCurrentMonoThreadForModApi("mod API mono.find_method")
               ? g_mono->FindMethod(image, namespc, klass, name, argc)
               : nullptr;
}

const void *Mono_find_method_exact(const char *image, const char *namespc, const char *klass, const char *name,
                                   const char *const *parameter_types, int parameter_count) {
    return AttachCurrentMonoThreadForModApi("mod API mono.find_method_exact")
               ? g_mono->FindMethodExact(image, namespc, klass, name, parameter_types, parameter_count)
               : nullptr;
}

const void *Mono_find_field(const char *image, const char *namespc, const char *klass, const char *name) {
    return AttachCurrentMonoThreadForModApi("mod API mono.find_field") ? g_mono->FindField(image, namespc, klass, name)
                                                                       : nullptr;
}

int Mono_runtime_invoke(const void *method, void *object, void **params, void **result, void **exception,
                        uint32_t *native_exception) {
    if (result)
        *result = nullptr;
    if (exception)
        *exception = nullptr;

    if (native_exception)
        *native_exception = 0;
    if (!AttachCurrentMonoThreadForModApi("mod API mono.runtime_invoke"))
        return 0;
    if (!g_mono->runtime_invoke) {
        MonoSetError(MonoMissing("mono_runtime_invoke"));
        return 0;
    }
    if (!method) {
        MonoSetError("Mono API: runtime_invoke method is required");
        return 0;
    }

    MonoObject *managed_exception = nullptr;
    uint32_t local_native_exception = 0;

    MonoObject *value = g_mono->RuntimeInvokeSafe(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)),
                                                  object, params, &managed_exception, &local_native_exception);

    if (result)
        *result = value;
    if (exception)
        *exception = managed_exception;
    if (native_exception)
        *native_exception = local_native_exception;

    if (local_native_exception) {
        MonoSetNativeExceptionError("runtime_invoke", local_native_exception);
        return -1;
    }
    if (managed_exception) {
        MonoSetError("Mono API: runtime_invoke returned a managed exception");
        return 0;
    }
    return 1;
}

void *Mono_new_string(const char *utf8) {
    MonoClearError();
    if (!utf8) {
        MonoSetError("Mono API: new_string utf8 input is required");
        return nullptr;
    }
    return AttachCurrentMonoThreadForModApi("mod API mono.new_string") ? g_mono->NewString(utf8) : nullptr;
}

size_t Mono_array_length(void *array) {
    MonoClearError();
    if (!array) {
        MonoSetError("Mono API: array_length array is required");
        return 0;
    }
    return AttachCurrentMonoThreadForModApi("mod API mono.array_length")
               ? g_mono->ArrayLength(static_cast<MonoArray *>(array))
               : 0;
}

void *Mono_array_address(void *array, int element_size, size_t index) {
    MonoClearError();
    if (!array) {
        MonoSetError("Mono API: array_address array is required");
        return nullptr;
    }
    if (element_size <= 0) {
        MonoSetError("Mono API: array_address element size must be positive");
        return nullptr;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.array_address"))
        return nullptr;
    if (!g_mono->array_addr_with_size) {
        MonoSetError("Mono API: mono_array_addr_with_size unavailable");
        return nullptr;
    }
    const size_t length = g_mono->ArrayLength(static_cast<MonoArray *>(array));
    if (index >= length) {
        MonoSetError("Mono API: array_address index out of range");
        return nullptr;
    }
    return g_mono->array_addr_with_size(static_cast<MonoArray *>(array), element_size, index);
}

void *Mono_array_ref_at(void *array, size_t index) {
    MonoClearError();
    if (!array) {
        MonoSetError("Mono API: Unity array element access failed: array is null");
        return nullptr;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.array_ref_at"))
        return nullptr;
    if (!g_mono->array_addr_with_size) {
        MonoSetError("Mono API: Unity array element access unavailable: "
                     "mono_array_addr_with_size unavailable");
        return nullptr;
    }
    const size_t length = g_mono->ArrayLength(static_cast<MonoArray *>(array));
    if (index >= length) {
        MonoSetError("Mono API: Unity array element access failed: index out of "
                     "range");
        return nullptr;
    }
    void **slot = reinterpret_cast<void **>(
        g_mono->array_addr_with_size(static_cast<MonoArray *>(array), static_cast<int>(sizeof(void *)), index));
    if (!slot) {
        MonoSetError("Mono API: Unity array element access failed: array address "
                     "helper returned null");
        return nullptr;
    }
    return *slot;
}

int Mono_array_set_ref(void *array, size_t index, void *value) {
    MonoClearError();
    if (!array) {
        MonoSetError("Mono API: Unity array reference write failed: array is null");
        return 0;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.array_set_ref"))
        return 0;
    if (!g_mono->array_addr_with_size) {
        MonoSetError("Mono API: Unity array reference write unavailable: "
                     "mono_array_addr_with_size unavailable");
        return 0;
    }
    if (!g_mono->gc_wbarrier_set_arrayref) {
        MonoSetError("Mono API: Unity array reference write unavailable: "
                     "mono_gc_wbarrier_set_arrayref unavailable");
        return 0;
    }
    const size_t length = g_mono->ArrayLength(static_cast<MonoArray *>(array));
    if (index >= length) {
        MonoSetError("Mono API: Unity array reference write failed: index out of range");
        return 0;
    }
    void **slot = reinterpret_cast<void **>(
        g_mono->array_addr_with_size(static_cast<MonoArray *>(array), static_cast<int>(sizeof(void *)), index));
    if (!slot) {
        MonoSetError("Mono API: Unity array reference write failed: array address "
                     "helper returned null");
        return 0;
    }
    __try {
        g_mono->gc_wbarrier_set_arrayref(static_cast<MonoArray *>(array), slot, static_cast<MonoObject *>(value));
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MonoSetError("Mono API: mono_gc_wbarrier_set_arrayref raised a native exception");
        return -1;
    }
}

int Mono_object_class_name(void *object, char *output, size_t output_size) {
    return AttachCurrentMonoThreadForModApi("mod API mono.object_class_name")
               ? CopyName(g_mono->ObjectClassName(static_cast<MonoObject *>(object)), output, output_size)
               : 0;
}

const void *Mono_object_get_class(void *object) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.object_get_class"))
        return nullptr;
    if (!g_mono->object_get_class) {
        MonoSetError(MonoMissing("mono_object_get_class"));
        return nullptr;
    }
    if (!object) {
        MonoSetError(MonoInvalid("object_get_class", "object is required"));
        return nullptr;
    }
    return g_mono->object_get_class(static_cast<MonoObject *>(object));
}

void *Mono_object_unbox(void *object) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.object_unbox"))
        return nullptr;
    if (!g_mono->object_unbox) {
        MonoSetError(MonoMissing("mono_object_unbox"));
        return nullptr;
    }
    if (!object) {
        MonoSetError(MonoInvalid("object_unbox", "object is required"));
        return nullptr;
    }
    return g_mono->object_unbox(static_cast<MonoObject *>(object));
}

size_t Mono_string_length(void *string) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.string_length"))
        return 0;
    if (!g_mono->string_length) {
        MonoSetError(MonoMissing("mono_string_length"));
        return 0;
    }
    if (!string) {
        MonoSetError(MonoInvalid("string_length", "string is required"));
        return 0;
    }
    return static_cast<size_t>(g_mono->string_length(static_cast<MonoString *>(string)));
}

int Mono_string_to_utf8(void *string, char *output, size_t output_size) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.string_to_utf8"))
        return CopyMonoAllocatedString(nullptr, output, output_size);
    if (!g_mono->string_to_utf8) {
        MonoSetError(MonoMissing("mono_string_to_utf8"));
        return CopyMonoAllocatedString(nullptr, output, output_size);
    }
    if (!string) {
        MonoSetError(MonoInvalid("string_to_utf8", "string is required"));
        return CopyMonoAllocatedString(nullptr, output, output_size);
    }
    if (!g_mono->free_) {
        MonoSetError("Mono API: mono_string_to_utf8 requires mono_free to avoid "
                     "leaking runtime-allocated memory");
        return CopyMonoAllocatedString(nullptr, output, output_size);
    }

    return CopyMonoAllocatedString(g_mono->string_to_utf8(static_cast<MonoString *>(string)), output, output_size);
}

const char *Mono_class_get_name(const void *klass) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_name") && g_mono->class_get_name && klass
               ? g_mono->class_get_name(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : nullptr;
}

const char *Mono_class_get_namespace(const void *klass) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_namespace") && g_mono->class_get_namespace && klass
               ? g_mono->class_get_namespace(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : nullptr;
}

const void *Mono_class_get_parent(const void *klass) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_parent") && g_mono->class_get_parent && klass
               ? g_mono->class_get_parent(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : nullptr;
}

uint32_t Mono_class_get_flags(const void *klass) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_flags") && g_mono->class_get_flags && klass
               ? g_mono->class_get_flags(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : 0;
}

const void *Mono_class_get_fields(const void *klass, void **iterator) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_fields") && g_mono->class_get_fields && klass &&
                   iterator
               ? g_mono->class_get_fields(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)), iterator)
               : nullptr;
}

const void *Mono_class_get_methods(const void *klass, void **iterator) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_methods") && g_mono->class_get_methods && klass &&
                   iterator
               ? g_mono->class_get_methods(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)), iterator)
               : nullptr;
}

const void *Mono_class_get_properties(const void *klass, void **iterator) {
    return AttachCurrentMonoThreadForModApi("mod API mono.class_get_properties") && g_mono->class_get_properties &&
                   klass && iterator
               ? g_mono->class_get_properties(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)), iterator)
               : nullptr;
}

const char *Mono_field_get_name(const void *field) {
    return AttachCurrentMonoThreadForModApi("mod API mono.field_get_name") && g_mono->field_get_name && field
               ? g_mono->field_get_name(const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)))
               : nullptr;
}

const void *Mono_field_get_type(const void *field) {
    return AttachCurrentMonoThreadForModApi("mod API mono.field_get_type") && g_mono->field_get_type && field
               ? g_mono->field_get_type(const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)))
               : nullptr;
}

uint32_t Mono_field_get_offset(const void *field) {
    return AttachCurrentMonoThreadForModApi("mod API mono.field_get_offset") && g_mono->field_get_offset && field
               ? g_mono->field_get_offset(const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)))
               : 0;
}

uint32_t Mono_field_get_flags(const void *field) {
    return AttachCurrentMonoThreadForModApi("mod API mono.field_get_flags") && g_mono->field_get_flags && field
               ? g_mono->field_get_flags(const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)))
               : 0;
}

int Mono_field_get_value(void *object, const void *field, void *output) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.field_get_value"))
        return 0;
    if (!g_mono->field_get_value) {
        MonoSetError(MonoMissing("mono_field_get_value"));
        return 0;
    }
    if (!object || !field || !output) {
        MonoSetError(MonoInvalid("field_get_value", "object, field, and output are required"));
        return 0;
    }

    __try {
        g_mono->field_get_value(static_cast<MonoObject *>(object),
                                const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)), output);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MonoSetError("Mono API: mono_field_get_value raised a native exception");
        return -1;
    }
}

int Mono_field_set_value(void *object, const void *field, void *value) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.field_set_value"))
        return 0;
    if (!g_mono->field_set_value) {
        MonoSetError(MonoMissing("mono_field_set_value"));
        return 0;
    }
    if (!object || !field || !value) {
        MonoSetError(MonoInvalid("field_set_value", "object, field, and value are required"));
        return 0;
    }

    __try {
        g_mono->field_set_value(static_cast<MonoObject *>(object),
                                const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)), value);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        MonoSetError("Mono API: mono_field_set_value raised a native exception");
        return -1;
    }
}

const char *Mono_method_get_name(const void *method) {
    return AttachCurrentMonoThreadForModApi("mod API mono.method_get_name") && g_mono->method_get_name && method
               ? g_mono->method_get_name(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)))
               : nullptr;
}

uint32_t Mono_method_get_flags(const void *method, uint32_t *iflags) {
    if (iflags)
        *iflags = 0;

    return AttachCurrentMonoThreadForModApi("mod API mono.method_get_flags") && g_mono->method_get_flags && method
               ? g_mono->method_get_flags(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)), iflags)
               : 0;
}

const void *Mono_method_signature(const void *method) {
    return AttachCurrentMonoThreadForModApi("mod API mono.method_signature") && g_mono->method_signature && method
               ? g_mono->method_signature(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)))
               : nullptr;
}

uint32_t Mono_signature_get_param_count(const void *signature) {
    return AttachCurrentMonoThreadForModApi("mod API mono.signature_get_param_count") &&
                   g_mono->signature_get_param_count && signature
               ? g_mono->signature_get_param_count(
                     const_cast<MonoMethodSignature *>(static_cast<const MonoMethodSignature *>(signature)))
               : 0;
}

const void *Mono_signature_get_return_type(const void *signature) {
    return AttachCurrentMonoThreadForModApi("mod API mono.signature_get_return_type") &&
                   g_mono->signature_get_return_type && signature
               ? g_mono->signature_get_return_type(
                     const_cast<MonoMethodSignature *>(static_cast<const MonoMethodSignature *>(signature)))
               : nullptr;
}

const void *Mono_signature_get_param(const void *signature, void **iterator) {
    return AttachCurrentMonoThreadForModApi("mod API mono.signature_get_param") && g_mono->signature_get_params &&
                   signature && iterator
               ? g_mono->signature_get_params(
                     const_cast<MonoMethodSignature *>(static_cast<const MonoMethodSignature *>(signature)), iterator)
               : nullptr;
}

int Mono_type_get_name(const void *type, char *output, size_t output_size) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.type_get_name") || !g_mono->type_get_name || !type) {
        return CopyMonoAllocatedString(nullptr, output, output_size);
    }
    if (!g_mono->free_) {
        MonoSetError("Mono API: mono_type_get_name requires mono_free to avoid "
                     "leaking runtime-allocated memory");
        return CopyMonoAllocatedString(nullptr, output, output_size);
    }

    return CopyMonoAllocatedString(g_mono->type_get_name(const_cast<MonoType *>(static_cast<const MonoType *>(type))),
                                   output, output_size);
}

const char *Mono_property_get_name(const void *property) {
    return AttachCurrentMonoThreadForModApi("mod API mono.property_get_name") && g_mono->property_get_name && property
               ? g_mono->property_get_name(const_cast<MonoProperty *>(static_cast<const MonoProperty *>(property)))
               : nullptr;
}

const void *Mono_property_get_get_method(const void *property) {
    return AttachCurrentMonoThreadForModApi("mod API mono.property_get_get_method") &&
                   g_mono->property_get_get_method && property
               ? g_mono->property_get_get_method(
                     const_cast<MonoProperty *>(static_cast<const MonoProperty *>(property)))
               : nullptr;
}

const void *Mono_property_get_set_method(const void *property) {
    return AttachCurrentMonoThreadForModApi("mod API mono.property_get_set_method") &&
                   g_mono->property_get_set_method && property
               ? g_mono->property_get_set_method(
                     const_cast<MonoProperty *>(static_cast<const MonoProperty *>(property)))
               : nullptr;
}

void *Mono_compile_method(const void *method) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.compile_method"))
        return nullptr;
    if (!method) {
        MonoSetError("Mono API: compile_method method is required");
        return nullptr;
    }
    if (!g_mono->compile_method) {
        MonoSetError("Mono API: mono_compile_method unavailable");
        return nullptr;
    }

    uint32_t native_exception = 0;
    void *target =
        g_mono->CompileMethodSafe(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)), &native_exception);
    if (target)
        return target;
    if (native_exception) {
        MonoSetNativeExceptionError("compile_method", native_exception);
    } else {
        MonoSetError("Mono API: compile_method did not produce a validated executable native target; see loader log");
    }
    return nullptr;
}

int Mono_method_is_generic(const void *method) {
    return AttachCurrentMonoThreadForModApi("mod API mono.method_is_generic") && g_mono->method_is_generic && method
               ? g_mono->method_is_generic(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)))
               : 0;
}

const void *Mono_domain_get() {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.domain_get"))
        return nullptr;
    return g_mono->domain_get ? g_mono->domain_get() : (MonoSetError("Mono API: mono_domain_get unavailable"), nullptr);
}
const void *Mono_root_domain_get() {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.root_domain_get"))
        return nullptr;
    return g_mono->get_root_domain ? g_mono->get_root_domain()
                                   : (MonoSetError("Mono API: mono_get_root_domain unavailable"), nullptr);
}
const void *Mono_assembly_get_image_public(const void *assembly) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.assembly_get_image"))
        return nullptr;
    return g_mono->assembly_get_image && assembly
               ? g_mono->assembly_get_image(const_cast<MonoAssembly *>(static_cast<const MonoAssembly *>(assembly)))
               : (MonoSetError("Mono API: mono_assembly_get_image unavailable or "
                               "invalid input"),
                  nullptr);
}
const char *Mono_image_get_name_public(const void *image) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.image_get_name"))
        return nullptr;
    return g_mono->image_get_name && image
               ? g_mono->image_get_name(const_cast<MonoImage *>(static_cast<const MonoImage *>(image)))
               : nullptr;
}
const char *Mono_image_get_filename_public(const void *image) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.image_get_filename"))
        return nullptr;
    return g_mono->image_get_filename && image
               ? g_mono->image_get_filename(const_cast<MonoImage *>(static_cast<const MonoImage *>(image)))
               : nullptr;
}
int Mono_image_get_table_rows_public(const void *image, int table_id) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.image_get_table_rows"))
        return 0;
    return g_mono->image_get_table_rows && image
               ? g_mono->image_get_table_rows(const_cast<MonoImage *>(static_cast<const MonoImage *>(image)), table_id)
               : 0;
}
const void *Mono_image_get_class_public(const void *image, uint32_t token) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.image_get_class"))
        return nullptr;
    return g_mono->class_get && image
               ? g_mono->class_get(const_cast<MonoImage *>(static_cast<const MonoImage *>(image)), token)
               : nullptr;
}
const void *Mono_class_get_type_public(const void *klass) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.class_get_type"))
        return nullptr;
    return g_mono->class_get_type && klass
               ? g_mono->class_get_type(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : nullptr;
}
int Mono_class_is_valuetype_public(const void *klass) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.class_is_valuetype"))
        return 0;
    return g_mono->class_is_valuetype && klass
               ? g_mono->class_is_valuetype(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : 0;
}
int Mono_class_is_enum_public(const void *klass) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.class_is_enum"))
        return 0;
    return g_mono->class_is_enum && klass
               ? g_mono->class_is_enum(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : 0;
}
const void *Mono_class_get_nested_types_public(const void *klass, void **iterator) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.class_get_nested_types"))
        return nullptr;
    return g_mono->class_get_nested_types && klass && iterator
               ? g_mono->class_get_nested_types(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)),
                                                iterator)
               : nullptr;
}
const void *Mono_class_get_interfaces_public(const void *klass, void **iterator) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.class_get_interfaces"))
        return nullptr;
    return g_mono->class_get_interfaces && klass && iterator
               ? g_mono->class_get_interfaces(const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)), iterator)
               : nullptr;
}
uint32_t Mono_property_get_flags_public(const void *property) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.property_get_flags"))
        return 0;
    return g_mono->property_get_flags && property
               ? g_mono->property_get_flags(const_cast<MonoProperty *>(static_cast<const MonoProperty *>(property)))
               : 0;
}
const void *Mono_method_get_return_type_public(const void *method) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.method_get_return_type"))
        return nullptr;
    if (!g_mono || !g_mono->method_signature || !g_mono->signature_get_return_type || !method)
        return nullptr;
    auto *sig = g_mono->method_signature(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)));
    return sig ? g_mono->signature_get_return_type(sig) : nullptr;
}
const void *Mono_method_get_param_type_public(const void *method, uint32_t index) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.method_get_param_type"))
        return nullptr;
    if (!g_mono || !g_mono->method_signature || !g_mono->signature_get_params || !method)
        return nullptr;
    auto *sig = g_mono->method_signature(const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)));
    void *it = nullptr;
    for (uint32_t i = 0;; ++i) {
        auto *t = g_mono->signature_get_params(sig, &it);
        if (!t)
            return nullptr;
        if (i == index)
            return t;
    }
}
int32_t Mono_type_get_type_public(const void *type) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.type_get_type"))
        return -1;
    return g_mono->type_get_type && type
               ? g_mono->type_get_type(const_cast<MonoType *>(static_cast<const MonoType *>(type)))
               : -1;
}
uint32_t Mono_type_get_attrs_public(const void *type) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.type_get_attrs"))
        return 0;
    return g_mono->type_get_attrs && type
               ? g_mono->type_get_attrs(const_cast<MonoType *>(static_cast<const MonoType *>(type)))
               : 0;
}
const void *Mono_type_get_class_public(const void *type) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.type_get_class"))
        return nullptr;
    return g_mono->type_get_class && type
               ? g_mono->type_get_class(const_cast<MonoType *>(static_cast<const MonoType *>(type)))
               : nullptr;
}
int Mono_field_static_get_value_public(const void *klass, const void *field, void *output) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.field_static_get_value"))
        return 0;
    if (!g_mono || !g_mono->class_vtable || !g_mono->field_static_get_value || !klass || !field || !output)
        return 0;
    auto *vt =
        g_mono->class_vtable(Mono_PublishedDomain(), const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)));
    if (!vt)
        return 0;
    __try {
        g_mono->field_static_get_value(vt, const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)),
                                       output);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
int Mono_field_static_set_value_public(const void *klass, const void *field, void *value) {
    if (!AttachCurrentMonoThreadForModApi("mod API mono.field_static_set_value"))
        return 0;
    if (!g_mono || !g_mono->class_vtable || !g_mono->field_static_set_value || !klass || !field || !value)
        return 0;
    auto *vt =
        g_mono->class_vtable(Mono_PublishedDomain(), const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)));
    if (!vt)
        return 0;
    __try {
        g_mono->field_static_set_value(vt, const_cast<MonoClassField *>(static_cast<const MonoClassField *>(field)),
                                       value);
        return 1;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return -1;
    }
}
const void *Mono_thread_current_public() {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.thread_current"))
        return nullptr;
    return g_mono && g_mono->thread_current ? g_mono->thread_current() : nullptr;
}
void Mono_thread_detach_public(const void *thread) {
    MonoClearError();
    if (g_mono && g_mono->thread_detach && thread) {
        Mono_DetachThread(*g_mono, const_cast<MonoThread *>(static_cast<const MonoThread *>(thread)));
    }
}
void *Mono_object_new_public(const void *klass) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.object_new"))
        return nullptr;
    return g_mono->object_new && klass
               ? g_mono->object_new(const_cast<MonoDomain *>(static_cast<const MonoDomain *>(Mono_PublishedDomain())),
                                    const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)))
               : (MonoSetError("Mono API: mono_object_new unavailable or invalid input"), nullptr);
}
void *Mono_type_get_object_public(const void *type) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.type_get_object"))
        return nullptr;
    return g_mono->type_get_object && type
               ? g_mono->type_get_object(
                     const_cast<MonoDomain *>(static_cast<const MonoDomain *>(Mono_PublishedDomain())),
                     const_cast<MonoType *>(static_cast<const MonoType *>(type)))
               : (MonoSetError("Mono API: mono_type_get_object unavailable or "
                               "invalid input"),
                  nullptr);
}
void Mono_runtime_object_init_public(void *object) {
    MonoClearError();
    if (!AttachCurrentMonoThreadForModApi("mod API mono.runtime_object_init"))
        return;
    if (g_mono && g_mono->runtime_object_init && object)
        g_mono->runtime_object_init(static_cast<MonoObject *>(object));
}

void *Mono_method_get_object(const void *method) {
    MonoClearError();
    if (!method || !g_mono || !g_mono->method_get_object || !g_mono->domain_get) {
        MonoSetError("Mono API: method_get_object requires a method, domain_get, and mono_method_get_object");
        return nullptr;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.method_get_object"))
        return nullptr;
    MonoDomain *domain = g_mono->domain_get();
    if (!domain) {
        MonoSetError("Mono API: mono_domain_get returned null for method_get_object");
        return nullptr;
    }
    return g_mono->method_get_object(domain, const_cast<MonoMethod *>(static_cast<const MonoMethod *>(method)));
}

void *Mono_value_box(const void *klass, void *data) {
    MonoClearError();
    if (!klass || !data || !g_mono || !g_mono->value_box || !g_mono->domain_get) {
        MonoSetError("Mono API: value_box requires a class, data, domain_get, and mono_value_box");
        return nullptr;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.value_box"))
        return nullptr;
    MonoDomain *domain = g_mono->domain_get();
    if (!domain) {
        MonoSetError("Mono API: mono_domain_get returned null for value_box");
        return nullptr;
    }
    return g_mono->value_box(domain, const_cast<MonoClass *>(static_cast<const MonoClass *>(klass)), data);
}

uint32_t Mono_gchandle_new(void *object, int pinned) {
    MonoClearError();
    if (!object) {
        MonoSetError("Mono API: gchandle_new object is required");
        return 0;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.gchandle_new"))
        return 0;
    if (!g_mono->gchandle_new) {
        MonoSetError("Mono API: mono_gchandle_new unavailable");
        return 0;
    }
    return g_mono->gchandle_new(static_cast<MonoObject *>(object), pinned != 0);
}

uint32_t Mono_gchandle_new_weakref(void *object, int track_resurrection) {
    MonoClearError();
    if (!object) {
        MonoSetError("Mono API: gchandle_new_weakref object is required");
        return 0;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.gchandle_new_weakref"))
        return 0;
    if (!g_mono->gchandle_new_weakref) {
        MonoSetError("Mono API: mono_gchandle_new_weakref unavailable");
        return 0;
    }
    return g_mono->gchandle_new_weakref(static_cast<MonoObject *>(object), track_resurrection != 0);
}

void *Mono_gchandle_get_target(uint32_t gchandle) {
    MonoClearError();
    if (!gchandle) {
        MonoSetError("Mono API: gchandle_get_target handle is required");
        return nullptr;
    }
    if (!AttachCurrentMonoThreadForModApi("mod API mono.gchandle_get_target"))
        return nullptr;
    if (!g_mono->gchandle_get_target) {
        MonoSetError("Mono API: mono_gchandle_get_target unavailable");
        return nullptr;
    }
    return g_mono->gchandle_get_target(gchandle);
}

void Mono_gchandle_free(uint32_t gchandle) {
    MonoClearError();
    if (!gchandle)
        return;
    if (!AttachCurrentMonoThreadForModApi("mod API mono.gchandle_free"))
        return;
    if (!g_mono->gchandle_free) {
        MonoSetError("Mono API: mono_gchandle_free unavailable");
        return;
    }
    g_mono->gchandle_free(gchandle);
}

const URK_MonoApi g_monoApi = [] {
    URK_MonoApi api{};

    api.version = URK_MONO_API_VERSION;

    api.attach_current_thread = &Mono_attach_current_thread;
    api.find_class = &Mono_find_class;
    api.find_method = &Mono_find_method;
    api.find_field = &Mono_find_field;
    api.runtime_invoke = &Mono_runtime_invoke;
    api.new_string = &Mono_new_string;
    api.array_length = &Mono_array_length;
    api.array_address = &Mono_array_address;
    api.array_ref_at = &Mono_array_ref_at;
    api.object_class_name = &Mono_object_class_name;

    api.class_get_name = &Mono_class_get_name;
    api.class_get_namespace = &Mono_class_get_namespace;
    api.class_get_parent = &Mono_class_get_parent;
    api.class_get_flags = &Mono_class_get_flags;
    api.class_get_fields = &Mono_class_get_fields;
    api.class_get_methods = &Mono_class_get_methods;
    api.class_get_properties = &Mono_class_get_properties;

    api.field_get_name = &Mono_field_get_name;
    api.field_get_type = &Mono_field_get_type;
    api.field_get_offset = &Mono_field_get_offset;
    api.field_get_flags = &Mono_field_get_flags;

    api.method_get_name = &Mono_method_get_name;
    api.method_is_generic = &Mono_method_is_generic;
    api.method_get_flags = &Mono_method_get_flags;
    api.method_signature = &Mono_method_signature;

    api.signature_get_param_count = &Mono_signature_get_param_count;
    api.signature_get_return_type = &Mono_signature_get_return_type;
    api.signature_get_param = &Mono_signature_get_param;

    api.type_get_name = &Mono_type_get_name;

    api.property_get_name = &Mono_property_get_name;
    api.property_get_get_method = &Mono_property_get_get_method;
    api.property_get_set_method = &Mono_property_get_set_method;

    api.compile_method = &Mono_compile_method;
    api.method_get_object = &Mono_method_get_object;
    api.value_box = &Mono_value_box;

    api.find_image = &Mono_find_image;
    api.find_method_exact = &Mono_find_method_exact;
    api.object_get_class = &Mono_object_get_class;
    api.object_unbox = &Mono_object_unbox;
    api.string_to_utf8 = &Mono_string_to_utf8;
    api.string_length = &Mono_string_length;
    api.field_get_value = &Mono_field_get_value;
    api.field_set_value = &Mono_field_set_value;

    api.last_error = &Mono_last_error;
    api.domain_get = &Mono_domain_get;
    api.root_domain_get = &Mono_root_domain_get;
    api.assembly_get_image = &Mono_assembly_get_image_public;
    api.image_get_name = &Mono_image_get_name_public;
    api.image_get_filename = &Mono_image_get_filename_public;
    api.image_get_table_rows = &Mono_image_get_table_rows_public;
    api.image_get_class = &Mono_image_get_class_public;
    api.class_get_type = &Mono_class_get_type_public;
    api.class_is_valuetype = &Mono_class_is_valuetype_public;
    api.class_is_enum = &Mono_class_is_enum_public;
    api.class_get_nested_types = &Mono_class_get_nested_types_public;
    api.class_get_interfaces = &Mono_class_get_interfaces_public;
    api.property_get_flags = &Mono_property_get_flags_public;
    api.method_get_return_type = &Mono_method_get_return_type_public;
    api.method_get_param_type = &Mono_method_get_param_type_public;
    api.type_get_type = &Mono_type_get_type_public;
    api.type_get_attrs = &Mono_type_get_attrs_public;
    api.type_get_class = &Mono_type_get_class_public;
    api.field_static_get_value = &Mono_field_static_get_value_public;
    api.field_static_set_value = &Mono_field_static_set_value_public;
    api.thread_current = &Mono_thread_current_public;
    api.thread_detach = &Mono_thread_detach_public;
    api.object_new = &Mono_object_new_public;
    api.type_get_object = &Mono_type_get_object_public;
    api.runtime_object_init = &Mono_runtime_object_init_public;
    api.array_set_ref = &Mono_array_set_ref;
    api.gchandle_new = &Mono_gchandle_new;
    api.gchandle_new_weakref = &Mono_gchandle_new_weakref;
    api.gchandle_get_target = &Mono_gchandle_get_target;
    api.gchandle_free = &Mono_gchandle_free;

    return api;
}();

} // namespace

namespace URK::ModApiInternal {

const URK_MonoApi *BuildMonoApiTable(::MonoApi *api) {
    g_mono = api && api->valid() ? api : nullptr;
    if (!g_mono)
        return nullptr;

    static URK_MonoApi table{};
    table = g_monoApi;
    table.version = URK_MONO_API_VERSION;
    table.size = static_cast<uint32_t>(sizeof(URK_MonoApi));
    return &table;
}

} // namespace URK::ModApiInternal
