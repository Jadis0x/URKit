#pragma once

#include "mod_sdk.h"

#include <windows.h>

#include <chrono>
#include <cstddef>
#include <cstdint>

using Il2CppDomain = void;
using Il2CppAssembly = void;
using Il2CppImage = void;
using Il2CppClass = void;
using Il2CppMethod = void;
using Il2CppType = void;
using Il2CppClassField = void;
using Il2CppProperty = void;
using Il2CppObject = void;
using Il2CppString = void;
using Il2CppArray = void;
using Il2CppThread = void;

using Il2CppEvent = void;
using Il2CppReflectionType = void;
using Il2CppReflectionMethod = void;
using Il2CppException = void;
using Il2CppProfiler = void;
using Il2CppManagedMemorySnapshot = void;
using Il2CppAsyncResult = void;
using Il2CppDelegate = void;
using Il2CppDebugTypeInfo = void;
using Il2CppDebugDocument = void;
using Il2CppDebugMethodInfo = void;
using Il2CppDebugLocalsInfo = void;

struct Il2CppStackFrameInfo;
struct Il2CppMemoryCallbacks;

using Il2CppMethodPointer = void (*)();
using Il2CppProfileFunc = void (*)();
using Il2CppProfileMethodFunc = void (*)();
using Il2CppProfileAllocFunc = void (*)();
using Il2CppProfileGCFunc = void (*)();
using Il2CppProfileGCResizeFunc = void (*)();
using Il2CppFrameWalkFunc = void (*)(const Il2CppStackFrameInfo *, void *);
using Il2CppSetFindPlugInCallback = void *(*)();
using Il2CppRegisterObjectCallback = void (*)(Il2CppObject *, void *);
using Il2CppWorldChangedCallback = void (*)();

using Il2CppStat = int;
using Il2CppProfileFlags = int;
using Il2CppRuntimeUnhandledExceptionPolicy = int;
using il2cpp_array_size_t = uintptr_t;

#define IL2CPP_FN(ret, name, args) using name##_t = ret(*) args

// lifecycle / config
IL2CPP_FN(int, il2cpp_init, (const char *domain_name));
IL2CPP_FN(void, il2cpp_shutdown, ());
IL2CPP_FN(void, il2cpp_set_config_dir, (const char *config_path));
IL2CPP_FN(void, il2cpp_set_data_dir, (const char *data_path));
IL2CPP_FN(void, il2cpp_set_commandline_arguments, (int argc, const char *argv[], const char *basedir));
IL2CPP_FN(void, il2cpp_set_memory_callbacks, (Il2CppMemoryCallbacks * callbacks));
IL2CPP_FN(void, il2cpp_set_find_plugin_callback, (Il2CppSetFindPlugInCallback method));

// allocation / internal calls
IL2CPP_FN(void *, il2cpp_alloc, (size_t size));
IL2CPP_FN(void, il2cpp_free, (void *ptr));
IL2CPP_FN(void, il2cpp_add_internal_call, (const char *name, Il2CppMethodPointer method));
IL2CPP_FN(void *, il2cpp_resolve_icall, (const char *name));

// domain / assembly / image
IL2CPP_FN(Il2CppDomain *, il2cpp_domain_get, ());
IL2CPP_FN(const Il2CppAssembly **, il2cpp_domain_get_assemblies, (const Il2CppDomain *, size_t *));
IL2CPP_FN(const Il2CppAssembly *, il2cpp_domain_assembly_open, (Il2CppDomain * domain, const char *name));

IL2CPP_FN(const Il2CppImage *, il2cpp_assembly_get_image, (const Il2CppAssembly *));

IL2CPP_FN(Il2CppImage *, il2cpp_get_corlib, ());
IL2CPP_FN(const Il2CppAssembly *, il2cpp_image_get_assembly, (const Il2CppImage *image));
IL2CPP_FN(const char *, il2cpp_image_get_name, (const Il2CppImage *));
IL2CPP_FN(const char *, il2cpp_image_get_filename, (const Il2CppImage *));
IL2CPP_FN(size_t, il2cpp_image_get_class_count, (const Il2CppImage *));
IL2CPP_FN(Il2CppClass *, il2cpp_image_get_class, (const Il2CppImage *, size_t));
IL2CPP_FN(const Il2CppMethod *, il2cpp_image_get_entry_point, (const Il2CppImage *image));

// class
IL2CPP_FN(Il2CppClass *, il2cpp_class_from_name, (const Il2CppImage *, const char *, const char *));
IL2CPP_FN(Il2CppClass *, il2cpp_class_from_type, (const Il2CppType *));
IL2CPP_FN(Il2CppClass *, il2cpp_class_from_il2cpp_type, (const Il2CppType *type));
IL2CPP_FN(Il2CppClass *, il2cpp_class_from_system_type, (Il2CppReflectionType * type));

IL2CPP_FN(const char *, il2cpp_class_get_name, (Il2CppClass *));
IL2CPP_FN(const char *, il2cpp_class_get_namespace, (Il2CppClass *));
IL2CPP_FN(const char *, il2cpp_class_get_assemblyname, (Il2CppClass *));
IL2CPP_FN(const Il2CppImage *, il2cpp_class_get_image, (Il2CppClass * klass));

IL2CPP_FN(Il2CppClass *, il2cpp_class_get_parent, (Il2CppClass *));
IL2CPP_FN(bool, il2cpp_class_has_parent, (Il2CppClass * klass, Il2CppClass *klassc));
IL2CPP_FN(Il2CppClass *, il2cpp_class_get_declaring_type, (Il2CppClass * klass));
IL2CPP_FN(Il2CppClass *, il2cpp_class_get_element_class, (Il2CppClass *));
IL2CPP_FN(const Il2CppType *, il2cpp_class_get_type, (Il2CppClass *));
IL2CPP_FN(uint32_t, il2cpp_class_get_type_token, (Il2CppClass *));

IL2CPP_FN(uint32_t, il2cpp_class_get_flags, (Il2CppClass *));
IL2CPP_FN(int, il2cpp_class_get_rank, (const Il2CppClass *));
IL2CPP_FN(int32_t, il2cpp_class_instance_size, (Il2CppClass * klass));
IL2CPP_FN(int32_t, il2cpp_class_value_size, (Il2CppClass * klass, uint32_t *align));
IL2CPP_FN(size_t, il2cpp_class_num_fields, (const Il2CppClass *enumKlass));
IL2CPP_FN(int, il2cpp_class_array_element_size, (const Il2CppClass *klass));

IL2CPP_FN(bool, il2cpp_class_is_valuetype, (const Il2CppClass *));
IL2CPP_FN(bool, il2cpp_class_is_enum, (const Il2CppClass *));
IL2CPP_FN(bool, il2cpp_class_is_generic, (const Il2CppClass *klass));
IL2CPP_FN(bool, il2cpp_class_is_inflated, (const Il2CppClass *klass));
IL2CPP_FN(bool, il2cpp_class_is_abstract, (const Il2CppClass *klass));
IL2CPP_FN(bool, il2cpp_class_is_interface, (const Il2CppClass *klass));
IL2CPP_FN(bool, il2cpp_class_is_subclass_of, (Il2CppClass *, Il2CppClass *, bool));
IL2CPP_FN(bool, il2cpp_class_is_assignable_from, (Il2CppClass *, Il2CppClass *));
IL2CPP_FN(bool, il2cpp_class_has_attribute, (Il2CppClass * klass, Il2CppClass *attr_class));
IL2CPP_FN(bool, il2cpp_class_has_references, (Il2CppClass * klass));

IL2CPP_FN(const Il2CppType *, il2cpp_class_enum_basetype, (Il2CppClass * klass));
IL2CPP_FN(Il2CppClassField *, il2cpp_class_get_field_from_name, (Il2CppClass *, const char *));
IL2CPP_FN(const Il2CppMethod *, il2cpp_class_get_method_from_name, (Il2CppClass *, const char *, int));
IL2CPP_FN(const Il2CppProperty *, il2cpp_class_get_property_from_name, (Il2CppClass * klass, const char *name));

IL2CPP_FN(Il2CppClassField *, il2cpp_class_get_fields, (Il2CppClass *, void **));
IL2CPP_FN(const Il2CppMethod *, il2cpp_class_get_methods, (Il2CppClass *, void **));
IL2CPP_FN(Il2CppProperty *, il2cpp_class_get_properties, (Il2CppClass *, void **));
IL2CPP_FN(Il2CppClass *, il2cpp_class_get_nested_types, (Il2CppClass *, void **));
IL2CPP_FN(Il2CppClass *, il2cpp_class_get_interfaces, (Il2CppClass *, void **));
IL2CPP_FN(const Il2CppEvent *, il2cpp_class_get_events, (Il2CppClass * klass, void **iter));

// class bitmap / testing
IL2CPP_FN(size_t, il2cpp_class_get_bitmap_size, (const Il2CppClass *klass));
IL2CPP_FN(void, il2cpp_class_get_bitmap, (Il2CppClass * klass, size_t *bitmap));

// method
IL2CPP_FN(const char *, il2cpp_method_get_name, (const Il2CppMethod *));
IL2CPP_FN(Il2CppClass *, il2cpp_method_get_class, (const Il2CppMethod *));
IL2CPP_FN(Il2CppClass *, il2cpp_method_get_declaring_type, (const Il2CppMethod *method));
IL2CPP_FN(const Il2CppType *, il2cpp_method_get_return_type, (const Il2CppMethod *));
IL2CPP_FN(uint32_t, il2cpp_method_get_param_count, (const Il2CppMethod *));
IL2CPP_FN(const Il2CppType *, il2cpp_method_get_param, (const Il2CppMethod *, uint32_t));
IL2CPP_FN(const char *, il2cpp_method_get_param_name, (const Il2CppMethod *method, uint32_t index));
IL2CPP_FN(uint32_t, il2cpp_method_get_flags, (const Il2CppMethod *, uint32_t *));
IL2CPP_FN(uint32_t, il2cpp_method_get_token, (const Il2CppMethod *));

IL2CPP_FN(Il2CppReflectionMethod *, il2cpp_method_get_object, (const Il2CppMethod *method, Il2CppClass *refclass));
IL2CPP_FN(bool, il2cpp_method_is_generic, (const Il2CppMethod *method));
IL2CPP_FN(bool, il2cpp_method_is_inflated, (const Il2CppMethod *method));
IL2CPP_FN(bool, il2cpp_method_is_instance, (const Il2CppMethod *method));
IL2CPP_FN(bool, il2cpp_method_has_attribute, (const Il2CppMethod *method, Il2CppClass *attr_class));

// field
IL2CPP_FN(const char *, il2cpp_field_get_name, (Il2CppClassField *));
IL2CPP_FN(Il2CppClass *, il2cpp_field_get_parent, (Il2CppClassField * field));
IL2CPP_FN(const Il2CppType *, il2cpp_field_get_type, (Il2CppClassField *));
IL2CPP_FN(uint32_t, il2cpp_field_get_flags, (Il2CppClassField *));
IL2CPP_FN(size_t, il2cpp_field_get_offset, (Il2CppClassField *));

IL2CPP_FN(void, il2cpp_field_get_value, (Il2CppObject *, Il2CppClassField *, void *));
IL2CPP_FN(void, il2cpp_field_set_value, (Il2CppObject *, Il2CppClassField *, void *));
IL2CPP_FN(void, il2cpp_field_static_get_value, (Il2CppClassField *, void *));
IL2CPP_FN(void, il2cpp_field_static_set_value, (Il2CppClassField *, void *));
IL2CPP_FN(Il2CppObject *, il2cpp_field_get_value_object, (Il2CppClassField * field, Il2CppObject *obj));
IL2CPP_FN(bool, il2cpp_field_has_attribute, (Il2CppClassField * field, Il2CppClass *attr_class));

// property
IL2CPP_FN(const char *, il2cpp_property_get_name, (Il2CppProperty *));
IL2CPP_FN(Il2CppClass *, il2cpp_property_get_parent, (Il2CppProperty * prop));
IL2CPP_FN(const Il2CppMethod *, il2cpp_property_get_get_method, (Il2CppProperty *));
IL2CPP_FN(const Il2CppMethod *, il2cpp_property_get_set_method, (Il2CppProperty *));
IL2CPP_FN(uint32_t, il2cpp_property_get_flags, (Il2CppProperty *));

// type
IL2CPP_FN(char *, il2cpp_type_get_name, (const Il2CppType *));
IL2CPP_FN(int, il2cpp_type_get_type, (const Il2CppType *));
IL2CPP_FN(uint32_t, il2cpp_type_get_attrs, (const Il2CppType *));
IL2CPP_FN(Il2CppObject *, il2cpp_type_get_object, (const Il2CppType *));
IL2CPP_FN(Il2CppClass *, il2cpp_type_get_class_or_element_class, (const Il2CppType *type));

// object
IL2CPP_FN(Il2CppClass *, il2cpp_object_get_class, (Il2CppObject *));
IL2CPP_FN(uint32_t, il2cpp_object_get_size, (Il2CppObject * obj));
IL2CPP_FN(const Il2CppMethod *, il2cpp_object_get_virtual_method, (Il2CppObject * obj, const Il2CppMethod *method));
IL2CPP_FN(Il2CppObject *, il2cpp_object_new, (const Il2CppClass *));
IL2CPP_FN(void *, il2cpp_object_unbox, (Il2CppObject *));
IL2CPP_FN(Il2CppObject *, il2cpp_object_is_inst, (Il2CppObject *, Il2CppClass *));
IL2CPP_FN(Il2CppObject *, il2cpp_value_box, (Il2CppClass *, void *));

// string
IL2CPP_FN(Il2CppString *, il2cpp_string_new, (const char *));
IL2CPP_FN(Il2CppString *, il2cpp_string_new_len, (const char *str, uint32_t length));
IL2CPP_FN(Il2CppString *, il2cpp_string_new_utf16, (const uint16_t *, int32_t));
IL2CPP_FN(Il2CppString *, il2cpp_string_new_wrapper, (const char *str));
IL2CPP_FN(int32_t, il2cpp_string_length, (Il2CppString *));
IL2CPP_FN(const uint16_t *, il2cpp_string_chars, (Il2CppString *));
IL2CPP_FN(Il2CppString *, il2cpp_string_intern, (Il2CppString * str));
IL2CPP_FN(Il2CppString *, il2cpp_string_is_interned, (Il2CppString * str));

// array
IL2CPP_FN(Il2CppClass *, il2cpp_array_class_get, (Il2CppClass *, uint32_t));
IL2CPP_FN(Il2CppClass *, il2cpp_bounded_array_class_get, (Il2CppClass * element_class, uint32_t rank, bool bounded));
// Unity's public IL2CPP ABI uses il2cpp_array_size_t here. Keeping this
// pointer-sized prevents truncation on x64 and correctly reflects exported
// implementations shared with other size_t-returning APIs after ICF.
IL2CPP_FN(il2cpp_array_size_t, il2cpp_array_length, (Il2CppArray *));
IL2CPP_FN(uint32_t, il2cpp_array_get_byte_length, (Il2CppArray *));
IL2CPP_FN(int, il2cpp_array_element_size, (const Il2CppClass *array_class));
IL2CPP_FN(char *, il2cpp_array_addr_with_size, (Il2CppArray *, int, uintptr_t));
IL2CPP_FN(Il2CppArray *, il2cpp_array_new, (Il2CppClass *, uintptr_t));
IL2CPP_FN(Il2CppArray *, il2cpp_array_new_specific, (Il2CppClass *, uintptr_t));
IL2CPP_FN(Il2CppArray *, il2cpp_array_new_full,
          (Il2CppClass * array_class, il2cpp_array_size_t *lengths, il2cpp_array_size_t *lower_bounds));

// runtime
IL2CPP_FN(Il2CppObject *, il2cpp_runtime_invoke, (const Il2CppMethod *, void *, void **, Il2CppObject **));
IL2CPP_FN(Il2CppObject *, il2cpp_runtime_invoke_convert_args,
          (const Il2CppMethod *method, void *obj, Il2CppObject **params, int paramCount, Il2CppObject **exc));
IL2CPP_FN(void, il2cpp_runtime_class_init, (Il2CppClass * klass));
IL2CPP_FN(void, il2cpp_runtime_object_init, (Il2CppObject * obj));
IL2CPP_FN(void, il2cpp_runtime_object_init_exception, (Il2CppObject * obj, Il2CppObject **exc));
IL2CPP_FN(void, il2cpp_runtime_unhandled_exception_policy_set, (Il2CppRuntimeUnhandledExceptionPolicy value));

// exception
IL2CPP_FN(void, il2cpp_raise_exception, (Il2CppException * ex));
IL2CPP_FN(Il2CppException *, il2cpp_exception_from_name_msg,
          (Il2CppImage * image, const char *name_space, const char *name, const char *msg));
IL2CPP_FN(Il2CppException *, il2cpp_get_exception_argument_null, (const char *arg));
IL2CPP_FN(void, il2cpp_format_exception, (const Il2CppException *ex, char *message, int message_size));
IL2CPP_FN(void, il2cpp_format_stack_trace, (const Il2CppException *ex, char *output, int output_size));
IL2CPP_FN(void, il2cpp_unhandled_exception, (Il2CppException * ex));

// gc / gchandle
IL2CPP_FN(void, il2cpp_gc_collect, (int maxGenerations));
IL2CPP_FN(int64_t, il2cpp_gc_get_used_size, ());
IL2CPP_FN(int64_t, il2cpp_gc_get_heap_size, ());

IL2CPP_FN(uint32_t, il2cpp_gchandle_new, (Il2CppObject * obj, bool pinned));
IL2CPP_FN(uint32_t, il2cpp_gchandle_new_weakref, (Il2CppObject * obj, bool track_resurrection));
IL2CPP_FN(Il2CppObject *, il2cpp_gchandle_get_target, (uint32_t gchandle));
IL2CPP_FN(void, il2cpp_gchandle_free, (uint32_t gchandle));
IL2CPP_FN(void, il2cpp_gc_wbarrier_set_field, (Il2CppObject * obj, void **target_address, Il2CppObject *value));

// object/array layout query exports
IL2CPP_FN(uint32_t, il2cpp_object_header_size, ());
IL2CPP_FN(uint32_t, il2cpp_array_object_header_size, ());
IL2CPP_FN(uint32_t, il2cpp_offset_of_array_length_in_array_object_header, ());
IL2CPP_FN(uint32_t, il2cpp_offset_of_array_bounds_in_array_object_header, ());
IL2CPP_FN(uint32_t, il2cpp_allocation_granularity, ());

// thread
IL2CPP_FN(Il2CppThread *, il2cpp_thread_current, ());
IL2CPP_FN(Il2CppThread *, il2cpp_thread_attach, (Il2CppDomain *));
IL2CPP_FN(void, il2cpp_thread_detach, (Il2CppThread *));
IL2CPP_FN(char *, il2cpp_thread_get_name, (Il2CppThread * thread, uint32_t *len));
IL2CPP_FN(Il2CppThread **, il2cpp_thread_get_all_attached_threads, (size_t *size));
IL2CPP_FN(bool, il2cpp_is_vm_thread, (Il2CppThread * thread));

// stacktrace
IL2CPP_FN(void, il2cpp_current_thread_walk_frame_stack, (Il2CppFrameWalkFunc func, void *user_data));
IL2CPP_FN(void, il2cpp_thread_walk_frame_stack, (Il2CppThread * thread, Il2CppFrameWalkFunc func, void *user_data));
IL2CPP_FN(bool, il2cpp_current_thread_get_top_frame, (Il2CppStackFrameInfo & frame));
IL2CPP_FN(bool, il2cpp_thread_get_top_frame, (Il2CppThread * thread, Il2CppStackFrameInfo &frame));
IL2CPP_FN(bool, il2cpp_current_thread_get_frame_at, (int32_t offset, Il2CppStackFrameInfo &frame));
IL2CPP_FN(bool, il2cpp_thread_get_frame_at, (Il2CppThread * thread, int32_t offset, Il2CppStackFrameInfo &frame));
IL2CPP_FN(int32_t, il2cpp_current_thread_get_stack_depth, ());
IL2CPP_FN(int32_t, il2cpp_thread_get_stack_depth, (Il2CppThread * thread));

// monitor
IL2CPP_FN(void, il2cpp_monitor_enter, (Il2CppObject * obj));
IL2CPP_FN(bool, il2cpp_monitor_try_enter, (Il2CppObject * obj, uint32_t timeout));
IL2CPP_FN(void, il2cpp_monitor_exit, (Il2CppObject * obj));
IL2CPP_FN(void, il2cpp_monitor_pulse, (Il2CppObject * obj));
IL2CPP_FN(void, il2cpp_monitor_pulse_all, (Il2CppObject * obj));
IL2CPP_FN(void, il2cpp_monitor_wait, (Il2CppObject * obj));
IL2CPP_FN(bool, il2cpp_monitor_try_wait, (Il2CppObject * obj, uint32_t timeout));

// delegate
IL2CPP_FN(Il2CppAsyncResult *, il2cpp_delegate_begin_invoke,
          (Il2CppDelegate * delegate, void **params, Il2CppDelegate *asyncCallback, Il2CppObject *state));
IL2CPP_FN(Il2CppObject *, il2cpp_delegate_end_invoke, (Il2CppAsyncResult * asyncResult, void **out_args));

// profiler
IL2CPP_FN(void, il2cpp_profiler_install, (Il2CppProfiler * prof, Il2CppProfileFunc shutdown_callback));
IL2CPP_FN(void, il2cpp_profiler_set_events, (Il2CppProfileFlags events));
IL2CPP_FN(void, il2cpp_profiler_install_enter_leave, (Il2CppProfileMethodFunc enter, Il2CppProfileMethodFunc fleave));
IL2CPP_FN(void, il2cpp_profiler_install_allocation, (Il2CppProfileAllocFunc callback));
IL2CPP_FN(void, il2cpp_profiler_install_gc,
          (Il2CppProfileGCFunc callback, Il2CppProfileGCResizeFunc heap_resize_callback));

// liveness
IL2CPP_FN(void *, il2cpp_unity_liveness_calculation_begin,
          (Il2CppClass * filter, int max_object_count, Il2CppRegisterObjectCallback callback, void *userdata,
           Il2CppWorldChangedCallback onWorldStarted, Il2CppWorldChangedCallback onWorldStopped));
IL2CPP_FN(void, il2cpp_unity_liveness_calculation_end, (void *state));
IL2CPP_FN(void, il2cpp_unity_liveness_calculation_from_root, (Il2CppObject * root, void *state));
IL2CPP_FN(void, il2cpp_unity_liveness_calculation_from_statics, (void *state));

// stats
IL2CPP_FN(bool, il2cpp_stats_dump_to_file, (const char *path));
IL2CPP_FN(uint64_t, il2cpp_stats_get_value, (Il2CppStat stat));

// memory snapshot
IL2CPP_FN(Il2CppManagedMemorySnapshot *, il2cpp_capture_memory_snapshot, ());
IL2CPP_FN(void, il2cpp_free_captured_memory_snapshot, (Il2CppManagedMemorySnapshot * snapshot));

// debug
IL2CPP_FN(const Il2CppDebugTypeInfo *, il2cpp_debug_get_class_info, (const Il2CppClass *klass));
IL2CPP_FN(const Il2CppDebugDocument *, il2cpp_debug_class_get_document, (const Il2CppDebugTypeInfo *info));
IL2CPP_FN(const char *, il2cpp_debug_document_get_filename, (const Il2CppDebugDocument *document));
IL2CPP_FN(const char *, il2cpp_debug_document_get_directory, (const Il2CppDebugDocument *document));
IL2CPP_FN(const Il2CppDebugMethodInfo *, il2cpp_debug_get_method_info, (const Il2CppMethod *method));
IL2CPP_FN(const Il2CppDebugDocument *, il2cpp_debug_method_get_document, (const Il2CppDebugMethodInfo *info));
IL2CPP_FN(const int32_t *, il2cpp_debug_method_get_offset_table, (const Il2CppDebugMethodInfo *info));
IL2CPP_FN(size_t, il2cpp_debug_method_get_code_size, (const Il2CppDebugMethodInfo *info));
IL2CPP_FN(void, il2cpp_debug_update_frame_il_offset, (int32_t il_offset));
IL2CPP_FN(const Il2CppDebugLocalsInfo **, il2cpp_debug_method_get_locals_info, (const Il2CppDebugMethodInfo *info));
IL2CPP_FN(const Il2CppClass *, il2cpp_debug_local_get_type, (const Il2CppDebugLocalsInfo *info));
IL2CPP_FN(const char *, il2cpp_debug_local_get_name, (const Il2CppDebugLocalsInfo *info));
IL2CPP_FN(uint32_t, il2cpp_debug_local_get_start_offset, (const Il2CppDebugLocalsInfo *info));
IL2CPP_FN(uint32_t, il2cpp_debug_local_get_end_offset, (const Il2CppDebugLocalsInfo *info));
IL2CPP_FN(Il2CppObject *, il2cpp_debug_method_get_param_value, (const Il2CppStackFrameInfo *info, uint32_t position));
IL2CPP_FN(Il2CppObject *, il2cpp_debug_frame_get_local_value, (const Il2CppStackFrameInfo *info, uint32_t position));
IL2CPP_FN(void *, il2cpp_debug_method_get_breakpoint_data_at,
          (const Il2CppDebugMethodInfo *info, int64_t uid, int32_t offset));
IL2CPP_FN(void, il2cpp_debug_method_set_breakpoint_data_at,
          (const Il2CppDebugMethodInfo *info, uint64_t location, void *data));
IL2CPP_FN(void, il2cpp_debug_method_clear_breakpoint_data, (const Il2CppDebugMethodInfo *info));
IL2CPP_FN(void, il2cpp_debug_method_clear_breakpoint_data_at, (const Il2CppDebugMethodInfo *info, uint64_t location));

#undef IL2CPP_FN

struct Il2CppApi {
    HMODULE gameAssembly = nullptr;
    HMODULE unityPlayer = nullptr;
    uintptr_t gameAssemblyBase = 0;
    uintptr_t unityPlayerBase = 0;
    // Set after all required core exports have been resolved by exact name and
    // their GameAssembly PE targets have passed validation. Optional capability
    // exports may remain null on Unity versions that do not provide them.
    bool exportsValidated = false;
    bool metadataReady = false;
    Il2CppDomain *cachedDomain = nullptr;

#define M(name) name##_t name = nullptr
    M(il2cpp_init);
    M(il2cpp_shutdown);
    M(il2cpp_set_config_dir);
    M(il2cpp_set_data_dir);
    M(il2cpp_set_commandline_arguments);
    M(il2cpp_set_memory_callbacks);
    M(il2cpp_set_find_plugin_callback);
    M(il2cpp_alloc);
    M(il2cpp_free);
    M(il2cpp_add_internal_call);
    M(il2cpp_resolve_icall);
    M(il2cpp_domain_get);
    M(il2cpp_domain_get_assemblies);
    M(il2cpp_domain_assembly_open);
    M(il2cpp_assembly_get_image);
    M(il2cpp_get_corlib);
    M(il2cpp_image_get_assembly);
    M(il2cpp_image_get_name);
    M(il2cpp_image_get_filename);
    M(il2cpp_image_get_class_count);
    M(il2cpp_image_get_class);
    M(il2cpp_image_get_entry_point);
    M(il2cpp_class_from_name);
    M(il2cpp_class_from_type);
    M(il2cpp_class_from_il2cpp_type);
    M(il2cpp_class_from_system_type);
    M(il2cpp_class_get_name);
    M(il2cpp_class_get_namespace);
    M(il2cpp_class_get_assemblyname);
    M(il2cpp_class_get_image);
    M(il2cpp_class_get_parent);
    M(il2cpp_class_has_parent);
    M(il2cpp_class_get_declaring_type);
    M(il2cpp_class_get_element_class);
    M(il2cpp_class_get_type);
    M(il2cpp_class_get_type_token);
    M(il2cpp_class_get_flags);
    M(il2cpp_class_get_rank);
    M(il2cpp_class_instance_size);
    M(il2cpp_class_value_size);
    M(il2cpp_class_num_fields);
    M(il2cpp_class_array_element_size);
    M(il2cpp_class_is_valuetype);
    M(il2cpp_class_is_enum);
    M(il2cpp_class_is_generic);
    M(il2cpp_class_is_inflated);
    M(il2cpp_class_is_abstract);
    M(il2cpp_class_is_interface);
    M(il2cpp_class_is_subclass_of);
    M(il2cpp_class_is_assignable_from);
    M(il2cpp_class_has_attribute);
    M(il2cpp_class_has_references);
    M(il2cpp_class_enum_basetype);
    M(il2cpp_class_get_field_from_name);
    M(il2cpp_class_get_method_from_name);
    M(il2cpp_class_get_property_from_name);
    M(il2cpp_class_get_fields);
    M(il2cpp_class_get_methods);
    M(il2cpp_class_get_properties);
    M(il2cpp_class_get_nested_types);
    M(il2cpp_class_get_interfaces);
    M(il2cpp_class_get_events);
    M(il2cpp_class_get_bitmap_size);
    M(il2cpp_class_get_bitmap);
    M(il2cpp_method_get_name);
    M(il2cpp_method_get_class);
    M(il2cpp_method_get_declaring_type);
    M(il2cpp_method_get_return_type);
    M(il2cpp_method_get_param_count);
    M(il2cpp_method_get_param);
    M(il2cpp_method_get_param_name);
    M(il2cpp_method_get_flags);
    M(il2cpp_method_get_token);
    M(il2cpp_method_get_object);
    M(il2cpp_method_is_generic);
    M(il2cpp_method_is_inflated);
    M(il2cpp_method_is_instance);
    M(il2cpp_method_has_attribute);
    M(il2cpp_field_get_name);
    M(il2cpp_field_get_parent);
    M(il2cpp_field_get_type);
    M(il2cpp_field_get_flags);
    M(il2cpp_field_get_offset);
    M(il2cpp_field_get_value);
    M(il2cpp_field_set_value);
    M(il2cpp_field_static_get_value);
    M(il2cpp_field_static_set_value);
    M(il2cpp_field_get_value_object);
    M(il2cpp_field_has_attribute);
    M(il2cpp_property_get_name);
    M(il2cpp_property_get_parent);
    M(il2cpp_property_get_get_method);
    M(il2cpp_property_get_set_method);
    M(il2cpp_property_get_flags);
    M(il2cpp_type_get_name);
    M(il2cpp_type_get_type);
    M(il2cpp_type_get_attrs);
    M(il2cpp_type_get_object);
    M(il2cpp_type_get_class_or_element_class);
    M(il2cpp_object_get_class);
    M(il2cpp_object_get_size);
    M(il2cpp_object_get_virtual_method);
    M(il2cpp_object_new);
    M(il2cpp_object_unbox);
    M(il2cpp_object_is_inst);
    M(il2cpp_value_box);
    M(il2cpp_string_new);
    M(il2cpp_string_new_len);
    M(il2cpp_string_new_utf16);
    M(il2cpp_string_new_wrapper);
    M(il2cpp_string_length);
    M(il2cpp_string_chars);
    M(il2cpp_string_intern);
    M(il2cpp_string_is_interned);
    M(il2cpp_array_class_get);
    M(il2cpp_bounded_array_class_get);
    M(il2cpp_array_length);
    M(il2cpp_array_get_byte_length);
    M(il2cpp_array_element_size);
    M(il2cpp_array_addr_with_size);
    M(il2cpp_array_new);
    M(il2cpp_array_new_specific);
    M(il2cpp_array_new_full);
    M(il2cpp_runtime_invoke);
    M(il2cpp_runtime_invoke_convert_args);
    M(il2cpp_runtime_class_init);
    M(il2cpp_runtime_object_init);
    M(il2cpp_runtime_object_init_exception);
    M(il2cpp_runtime_unhandled_exception_policy_set);
    M(il2cpp_raise_exception);
    M(il2cpp_exception_from_name_msg);
    M(il2cpp_get_exception_argument_null);
    M(il2cpp_format_exception);
    M(il2cpp_format_stack_trace);
    M(il2cpp_unhandled_exception);
    M(il2cpp_gc_collect);
    M(il2cpp_gc_get_used_size);
    M(il2cpp_gc_get_heap_size);
    M(il2cpp_gchandle_new);
    M(il2cpp_gchandle_new_weakref);
    M(il2cpp_gchandle_get_target);
    M(il2cpp_gchandle_free);
    M(il2cpp_gc_wbarrier_set_field);
    M(il2cpp_object_header_size);
    M(il2cpp_array_object_header_size);
    M(il2cpp_offset_of_array_length_in_array_object_header);
    M(il2cpp_offset_of_array_bounds_in_array_object_header);
    M(il2cpp_allocation_granularity);
    M(il2cpp_thread_current);
    M(il2cpp_thread_attach);
    M(il2cpp_thread_detach);
    M(il2cpp_thread_get_name);
    M(il2cpp_thread_get_all_attached_threads);
    M(il2cpp_is_vm_thread);
    M(il2cpp_current_thread_walk_frame_stack);
    M(il2cpp_thread_walk_frame_stack);
    M(il2cpp_current_thread_get_top_frame);
    M(il2cpp_thread_get_top_frame);
    M(il2cpp_current_thread_get_frame_at);
    M(il2cpp_thread_get_frame_at);
    M(il2cpp_current_thread_get_stack_depth);
    M(il2cpp_thread_get_stack_depth);
    M(il2cpp_monitor_enter);
    M(il2cpp_monitor_try_enter);
    M(il2cpp_monitor_exit);
    M(il2cpp_monitor_pulse);
    M(il2cpp_monitor_pulse_all);
    M(il2cpp_monitor_wait);
    M(il2cpp_monitor_try_wait);
    M(il2cpp_delegate_begin_invoke);
    M(il2cpp_delegate_end_invoke);
    M(il2cpp_profiler_install);
    M(il2cpp_profiler_set_events);
    M(il2cpp_profiler_install_enter_leave);
    M(il2cpp_profiler_install_allocation);
    M(il2cpp_profiler_install_gc);
    M(il2cpp_unity_liveness_calculation_begin);
    M(il2cpp_unity_liveness_calculation_end);
    M(il2cpp_unity_liveness_calculation_from_root);
    M(il2cpp_unity_liveness_calculation_from_statics);
    M(il2cpp_stats_dump_to_file);
    M(il2cpp_stats_get_value);
    M(il2cpp_capture_memory_snapshot);
    M(il2cpp_free_captured_memory_snapshot);
    M(il2cpp_debug_get_class_info);
    M(il2cpp_debug_class_get_document);
    M(il2cpp_debug_document_get_filename);
    M(il2cpp_debug_document_get_directory);
    M(il2cpp_debug_get_method_info);
    M(il2cpp_debug_method_get_document);
    M(il2cpp_debug_method_get_offset_table);
    M(il2cpp_debug_method_get_code_size);
    M(il2cpp_debug_update_frame_il_offset);
    M(il2cpp_debug_method_get_locals_info);
    M(il2cpp_debug_local_get_type);
    M(il2cpp_debug_local_get_name);
    M(il2cpp_debug_local_get_start_offset);
    M(il2cpp_debug_local_get_end_offset);
    M(il2cpp_debug_method_get_param_value);
    M(il2cpp_debug_frame_get_local_value);
    M(il2cpp_debug_method_get_breakpoint_data_at);
    M(il2cpp_debug_method_set_breakpoint_data_at);
    M(il2cpp_debug_method_clear_breakpoint_data);
    M(il2cpp_debug_method_clear_breakpoint_data_at);
#undef M

    bool valid() const;
    bool thread_attach_available() const;
    Il2CppDomain *Domain() const;
    bool WaitForMetadataAccess(std::chrono::milliseconds timeout,
                               std::chrono::milliseconds preDomainDelay = std::chrono::milliseconds(1500));
    bool MetadataAccessReady() const;
    bool TryAssemblyCount(size_t &count) const;
    const Il2CppImage *FindImage(const char *imageName) const;
    Il2CppClass *FindClass(const char *imageName, const char *namespc, const char *name) const;
    const Il2CppMethod *FindMethod(Il2CppClass *klass, const char *name, int argc) const;
    const Il2CppMethod *FindMethodExact(Il2CppClass *klass, const char *name, const char *const *parameterTypes,
                                        int parameterCount) const;
    void *MethodPointer(const Il2CppMethod *method) const;
    Il2CppClassField *FindField(Il2CppClass *klass, const char *name) const;
};

bool Il2Cpp_BindExports(Il2CppApi &api, int timeoutMs = 30000);
bool Il2Cpp_WaitForMetadataReady(Il2CppApi &api, int timeoutMs = 30000, int preDomainDelayMs = 1500);
const URK_Il2CppApi *ModApi_Il2Cpp(Il2CppApi *api);
