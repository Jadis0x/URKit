#pragma once

#include "mono_runtime.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace URK::mono::helpers {
using DiagnosticSink = void (*)(const char *);

inline void emit(DiagnosticSink sink, const char *message) {
  if (sink && message)
    sink(message);
}
inline void emit_last_error(DiagnosticSink sink) {
  if (const char *e = URK::mono::last_error(); e && e[0])
    emit(sink, e);
}
inline std::string to_utf8(URK::mono::String *value,
                           std::string_view fallback) {
  if (!value)
    return std::string(fallback);
  const std::size_t length = URK::mono::string_length(value);
  const std::size_t capacity = length * 4u + 1u;
  std::vector<char> buffer(capacity ? capacity : 1u, '\0');
  if (!URK::mono::string_to_utf8(value, buffer.data(), buffer.size()))
    return std::string(fallback);
  return std::string(buffer.data());
}
inline std::string to_utf8(URK::mono::String *value) {
  return to_utf8(value, std::string_view{});
}

inline bool resolve_class(const char *image, const char *namespc,
                          const char *name, const URK::mono::Class *&out) {
  out = URK::mono::find_class(image, namespc, name);
  return out != nullptr;
}
inline bool resolve_method(const char *image, const char *namespc,
                           const char *klass, const char *name, int argc,
                           const URK::mono::Method *&out) {
  out = URK::mono::resolve_method(image, namespc, klass, name, argc);
  return out != nullptr;
}
inline bool resolve_method_exact(const char *image, const char *namespc,
                                 const char *klass, const char *name,
                                 const char *const *parameter_types,
                                 int parameter_count,
                                 const URK::mono::Method *&out) {
  out = URK::mono::resolve_method_exact(image, namespc, klass, name,
                                        parameter_types, parameter_count);
  return out != nullptr;
}
inline bool resolve_field(const char *image, const char *namespc,
                          const char *klass, const char *name,
                          const URK::mono::Field *&out) {
  out = URK::mono::resolve_field(image, namespc, klass, name);
  return out != nullptr;
}

inline bool require_class(const char *image, const char *namespc,
                          const char *name, const URK::mono::Class *&out,
                          DiagnosticSink sink = nullptr) {
  if (resolve_class(image, namespc, name, out))
    return true;
  char msg[512]{};
  std::snprintf(msg, sizeof(msg), "[URK Mono SDK] missing class: %s:%s.%s",
                image ? image : "", namespc ? namespc : "", name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_method_exact(const char *image, const char *namespc,
                                 const char *klass, const char *name,
                                 const char *const *parameter_types,
                                 int parameter_count, const char *return_type,
                                 const URK::mono::Method *&out,
                                 DiagnosticSink sink = nullptr) {
  if (resolve_method_exact(image, namespc, klass, name, parameter_types,
                           parameter_count, out))
    return true;
  char params[384]{};
  std::size_t used = 0;
  for (int i = 0; i < parameter_count && used < sizeof(params); ++i) {
    const int written = std::snprintf(
        params + used, sizeof(params) - used, "%s%s", i ? ", " : "",
        parameter_types && parameter_types[i] ? parameter_types[i]
                                              : "<unknown>");
    if (written < 0)
      break;
    used += static_cast<std::size_t>(written);
  }
  char msg[1024]{};
  std::snprintf(msg, sizeof(msg),
                "[URK Mono SDK] missing/changed method: %s:%s.%s.%s(%s) -> %s",
                image ? image : "", namespc ? namespc : "", klass ? klass : "",
                name ? name : "", params,
                return_type ? return_type : "<unknown>");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_method_exact(const char *image, const char *namespc,
                                 const char *klass, const char *name,
                                 const char *const *parameter_types,
                                 int parameter_count,
                                 const URK::mono::Method *&out,
                                 DiagnosticSink sink = nullptr) {
  return require_method_exact(image, namespc, klass, name, parameter_types,
                              parameter_count, "<unknown>", out, sink);
}
inline bool require_field(const char *image, const char *namespc,
                          const char *klass, const char *name,
                          const URK::mono::Field *&out,
                          DiagnosticSink sink = nullptr) {
  if (resolve_field(image, namespc, klass, name, out))
    return true;
  char msg[640]{};
  std::snprintf(msg, sizeof(msg),
                "[URK Mono SDK] missing/changed field: %s:%s.%s.%s",
                image ? image : "", namespc ? namespc : "", klass ? klass : "",
                name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_method(const char *image, const char *namespc,
                           const char *klass, const char *name, int argc,
                           const URK::mono::Method *&out,
                           DiagnosticSink sink = nullptr) {
  if (resolve_method(image, namespc, klass, name, argc, out))
    return true;
  char msg[640]{};
  std::snprintf(msg, sizeof(msg),
                "[URK Mono SDK] missing method: %s:%s.%s.%s/%d",
                image ? image : "", namespc ? namespc : "", klass ? klass : "",
                name ? name : "", argc);
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
inline bool require_property(const URK::mono::Class *klass_handle,
                             const char *name, const URK::mono::Property *&out,
                             DiagnosticSink sink = nullptr) {
  void *it = nullptr;
  while ((out = URK::mono::class_get_properties(klass_handle, &it)) !=
         nullptr) {
    const char *n = URK::mono::property_get_name(out);
    if (n && name && std::strcmp(n, name) == 0)
      return true;
  }
  char msg[512]{};
  std::snprintf(msg, sizeof(msg), "[URK Mono SDK] missing property: %s",
                name ? name : "");
  emit(sink, msg);
  emit_last_error(sink);
  return false;
}
} // namespace URK::mono::helpers