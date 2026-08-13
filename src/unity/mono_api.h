#pragma once
#include <cstddef>
#include <cstdint>
#include <string>
#include <windows.h>

typedef void MonoDomain;
typedef void MonoAssembly;
typedef void MonoImage;
typedef void MonoClass;
typedef void MonoMethod;
typedef void MonoMethodSignature;
typedef void MonoType;
typedef void MonoObject;
typedef void MonoString;
typedef void MonoArray;
typedef void MonoThread;
typedef void MonoClassField;
typedef void MonoProperty;
typedef void MonoVTable;
typedef void (*MonoAssemblyFunc)(MonoAssembly *, void *);

#define MONO_FN(ret, name, args) typedef ret(*name##_t) args
MONO_FN(MonoDomain *, mono_jit_init, (const char *));
MONO_FN(MonoDomain *, mono_get_root_domain, (void));
MONO_FN(MonoThread *, mono_thread_attach, (MonoDomain *));
MONO_FN(MonoThread *, mono_thread_current, (void));
MONO_FN(void, mono_thread_detach, (MonoThread *));
MONO_FN(MonoDomain *, mono_domain_get, (void));
MONO_FN(void, mono_assembly_foreach, (MonoAssemblyFunc, void *));
MONO_FN(MonoAssembly *, mono_domain_assembly_open, (MonoDomain *, const char *));
MONO_FN(MonoImage *, mono_assembly_get_image, (MonoAssembly *));
MONO_FN(const char *, mono_image_get_name, (MonoImage *));
MONO_FN(const char *, mono_image_get_filename, (MonoImage *));
MONO_FN(int, mono_image_get_table_rows, (MonoImage *, int));
MONO_FN(MonoClass *, mono_class_get, (MonoImage *, uint32_t));
MONO_FN(MonoImage *, mono_image_loaded, (const char *));
MONO_FN(MonoClass *, mono_class_from_name, (MonoImage *, const char *, const char *));
MONO_FN(const char *, mono_class_get_name, (MonoClass *));
MONO_FN(const char *, mono_class_get_namespace, (MonoClass *));
MONO_FN(MonoClass *, mono_class_get_parent, (MonoClass *));
MONO_FN(MonoType *, mono_class_get_type, (MonoClass *));
MONO_FN(MonoClass *, mono_class_get_nested_types, (MonoClass *, void **));
MONO_FN(MonoClass *, mono_class_get_interfaces, (MonoClass *, void **));
MONO_FN(int, mono_class_is_valuetype, (MonoClass *));
MONO_FN(int, mono_class_is_enum, (MonoClass *));
MONO_FN(uint32_t, mono_class_get_flags, (MonoClass *));
MONO_FN(MonoMethod *, mono_class_get_methods, (MonoClass *, void **));
MONO_FN(MonoProperty *, mono_class_get_properties, (MonoClass *, void **));
MONO_FN(MonoMethod *, mono_class_get_method_from_name, (MonoClass *, const char *, int));
MONO_FN(const char *, mono_method_get_name, (MonoMethod *));
MONO_FN(int, mono_method_is_generic, (MonoMethod *));
MONO_FN(MonoObject *, mono_method_get_object, (MonoDomain *, MonoMethod *));
MONO_FN(uint32_t, mono_method_get_flags, (MonoMethod *, uint32_t *));
MONO_FN(MonoMethodSignature *, mono_method_signature, (MonoMethod *));
MONO_FN(uint32_t, mono_signature_get_param_count, (MonoMethodSignature *));
MONO_FN(MonoType *, mono_signature_get_params, (MonoMethodSignature *, void **));
MONO_FN(MonoType *, mono_signature_get_return_type, (MonoMethodSignature *));
MONO_FN(char *, mono_type_get_name, (MonoType *));
MONO_FN(int, mono_type_get_type, (MonoType *));
MONO_FN(uint32_t, mono_type_get_attrs, (MonoType *));
MONO_FN(MonoClass *, mono_type_get_class, (MonoType *));
MONO_FN(MonoClassField *, mono_class_get_fields, (MonoClass *, void **));
MONO_FN(const char *, mono_field_get_name, (MonoClassField *));
MONO_FN(MonoType *, mono_field_get_type, (MonoClassField *));
MONO_FN(uint32_t, mono_field_get_offset, (MonoClassField *));
MONO_FN(uint32_t, mono_field_get_flags, (MonoClassField *));
MONO_FN(void, mono_field_get_value, (MonoObject *, MonoClassField *, void *));
MONO_FN(void, mono_field_static_get_value, (MonoVTable *, MonoClassField *, void *));
MONO_FN(void, mono_field_static_set_value, (MonoVTable *, MonoClassField *, void *));
MONO_FN(void, mono_field_set_value, (MonoObject *, MonoClassField *, void *));
MONO_FN(const char *, mono_property_get_name, (MonoProperty *));
MONO_FN(MonoMethod *, mono_property_get_get_method, (MonoProperty *));
MONO_FN(MonoMethod *, mono_property_get_set_method, (MonoProperty *));
MONO_FN(uint32_t, mono_property_get_flags, (MonoProperty *));
MONO_FN(MonoVTable *, mono_class_vtable, (MonoDomain *, MonoClass *));
MONO_FN(void *, mono_compile_method, (MonoMethod *));
MONO_FN(void *, mono_lookup_internal_call, (MonoMethod *));
MONO_FN(MonoObject *, mono_object_new, (MonoDomain *, MonoClass *));
MONO_FN(MonoObject *, mono_value_box, (MonoDomain *, MonoClass *, void *));
MONO_FN(MonoObject *, mono_type_get_object, (MonoDomain *, MonoType *));
MONO_FN(void, mono_runtime_object_init, (MonoObject *));
MONO_FN(MonoObject *, mono_runtime_invoke, (MonoMethod *, void *, void **, MonoObject **));
MONO_FN(MonoClass *, mono_object_get_class, (MonoObject *));
MONO_FN(void *, mono_object_unbox, (MonoObject *));
MONO_FN(MonoString *, mono_string_new, (MonoDomain *, const char *));
MONO_FN(char *, mono_string_to_utf8, (MonoString *));
MONO_FN(int, mono_string_length, (MonoString *));
MONO_FN(void, mono_free, (void *));
MONO_FN(uintptr_t, mono_array_length, (MonoArray *));
MONO_FN(char *, mono_array_addr_with_size, (MonoArray *, int, uintptr_t));
MONO_FN(void, mono_gc_wbarrier_set_arrayref, (MonoArray *, void **, MonoObject *));
MONO_FN(uint32_t, mono_gchandle_new, (MonoObject *, int));
MONO_FN(uint32_t, mono_gchandle_new_weakref, (MonoObject *, int));
MONO_FN(MonoObject *, mono_gchandle_get_target, (uint32_t));
MONO_FN(void, mono_gchandle_free, (uint32_t));
MONO_FN(MonoDomain *, mono_jit_init_version, (const char *, const char *));
#undef MONO_FN

struct MonoApi {
    HMODULE module = nullptr;
    uintptr_t base = 0;
#define M(type, name) type name = nullptr
    M(mono_jit_init_t, jit_init);
    M(mono_get_root_domain_t, get_root_domain);
    M(mono_thread_attach_t, thread_attach);
    M(mono_thread_current_t, thread_current);
    M(mono_thread_detach_t, thread_detach);
    M(mono_domain_get_t, domain_get);
    M(mono_assembly_foreach_t, assembly_foreach);
    M(mono_domain_assembly_open_t, domain_assembly_open);
    M(mono_assembly_get_image_t, assembly_get_image);
    M(mono_image_get_name_t, image_get_name);
    M(mono_image_get_filename_t, image_get_filename);
    M(mono_image_get_table_rows_t, image_get_table_rows);
    M(mono_class_get_t, class_get);
    M(mono_image_loaded_t, image_loaded);
    M(mono_class_from_name_t, class_from_name);
    M(mono_class_get_name_t, class_get_name);
    M(mono_class_get_namespace_t, class_get_namespace);
    M(mono_class_get_parent_t, class_get_parent);
    M(mono_class_get_type_t, class_get_type);
    M(mono_class_get_nested_types_t, class_get_nested_types);
    M(mono_class_get_interfaces_t, class_get_interfaces);
    M(mono_class_is_valuetype_t, class_is_valuetype);
    M(mono_class_is_enum_t, class_is_enum);
    M(mono_class_get_flags_t, class_get_flags);
    M(mono_class_get_methods_t, class_get_methods);
    M(mono_class_get_properties_t, class_get_properties);
    M(mono_class_get_method_from_name_t, class_get_method_from_name);
    M(mono_method_get_name_t, method_get_name);
    M(mono_method_is_generic_t, method_is_generic);
    M(mono_method_get_object_t, method_get_object);
    M(mono_method_get_flags_t, method_get_flags);
    M(mono_method_signature_t, method_signature);
    M(mono_signature_get_param_count_t, signature_get_param_count);
    M(mono_signature_get_params_t, signature_get_params);
    M(mono_signature_get_return_type_t, signature_get_return_type);
    M(mono_type_get_name_t, type_get_name);
    M(mono_type_get_type_t, type_get_type);
    M(mono_type_get_attrs_t, type_get_attrs);
    M(mono_type_get_class_t, type_get_class);
    M(mono_class_get_fields_t, class_get_fields);
    M(mono_field_get_name_t, field_get_name);
    M(mono_field_get_type_t, field_get_type);
    M(mono_field_get_offset_t, field_get_offset);
    M(mono_field_get_flags_t, field_get_flags);
    M(mono_field_get_value_t, field_get_value);
    M(mono_field_static_get_value_t, field_static_get_value);
    M(mono_field_static_set_value_t, field_static_set_value);
    M(mono_field_set_value_t, field_set_value);
    M(mono_property_get_name_t, property_get_name);
    M(mono_property_get_get_method_t, property_get_get_method);
    M(mono_property_get_set_method_t, property_get_set_method);
    M(mono_property_get_flags_t, property_get_flags);
    M(mono_class_vtable_t, class_vtable);
    M(mono_compile_method_t, compile_method);
    M(mono_lookup_internal_call_t, lookup_internal_call);
    M(mono_object_new_t, object_new);
    M(mono_value_box_t, value_box);
    M(mono_type_get_object_t, type_get_object);
    M(mono_runtime_object_init_t, runtime_object_init);
    M(mono_runtime_invoke_t, runtime_invoke);
    M(mono_object_get_class_t, object_get_class);
    M(mono_object_unbox_t, object_unbox);
    M(mono_string_new_t, string_new);
    M(mono_string_to_utf8_t, string_to_utf8);
    M(mono_string_length_t, string_length);
    M(mono_free_t, free_);
    M(mono_array_length_t, array_length);
    M(mono_array_addr_with_size_t, array_addr_with_size);
    M(mono_gc_wbarrier_set_arrayref_t, gc_wbarrier_set_arrayref);
    M(mono_gchandle_new_t, gchandle_new);
    M(mono_gchandle_new_weakref_t, gchandle_new_weakref);
    M(mono_gchandle_get_target_t, gchandle_get_target);
    M(mono_gchandle_free_t, gchandle_free);
    M(mono_jit_init_version_t, jit_init_version);
#undef M
    bool valid() const;
    MonoImage *FindImage(const char *) const;
    MonoClass *FindClass(const char *, const char *, const char *) const;
    MonoMethod *FindMethod(const char *, const char *, const char *, const char *, int) const;
    MonoMethod *FindMethodExact(const char *, const char *, const char *, const char *, const char *const *, int) const;
    MonoClassField *FindField(const char *, const char *, const char *, const char *) const;
    void *CompileMethodSafe(MonoMethod *, uint32_t *native_exception = nullptr) const;
    MonoObject *RuntimeInvokeSafe(MonoMethod *, void *, void **, MonoObject **, uint32_t *native = nullptr) const;
    MonoString *NewString(const char *) const;
    size_t ArrayLength(MonoArray *) const;
    std::string ObjectClassName(MonoObject *) const;
};
bool Mono_Resolve(MonoApi &, int timeoutMs = 30000);

void Mono_PublishDomain(MonoDomain *domain);
MonoDomain *Mono_PublishedDomain();
bool Mono_AttachCurrentThread(MonoApi &api, MonoDomain *domain, const char *purpose,
                              MonoThread **attachedThread = nullptr);
void Mono_DetachThread(MonoApi &api, MonoThread *thread);

// Native loader/mod threads are not Mono threads. Every embedding API call must
// run on a known Mono thread, either one attached here or Unity's current thread.
class MonoThreadScope {
  public:
    enum class Mode {
        AttachCurrentThread,
        BorrowExistingThread,
    };

    MonoThreadScope(const MonoApi &api, MonoDomain *domain, const char *purpose);
    MonoThreadScope(const MonoApi &api, MonoDomain *domain, const char *purpose, Mode mode);
    ~MonoThreadScope();
    bool IsAttached() const {
        return attached_;
    }
    MonoThread *Thread() const {
        return thread_;
    }
    static bool CurrentThreadAttached();

  private:
    const MonoApi *api_ = nullptr;
    MonoThread *thread_ = nullptr;
    bool attached_ = false;
    bool ownsAttach_ = false;
};
