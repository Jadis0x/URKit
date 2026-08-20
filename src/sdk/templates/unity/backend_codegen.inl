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
    const auto replaceRequired = [&text](std::string_view needle, std::string_view replacement,
                                         const char *description) {
        const auto position = text.find(needle);
        if (position == std::string::npos)
            throw std::runtime_error(std::string("Unity SDK generation could not replace ") + description);
        if (text.find(needle, position + needle.size()) != std::string::npos)
            throw std::runtime_error(std::string("Unity SDK generation found duplicate replacement target for ") +
                                     description);
        text.replace(position, needle.size(), replacement);
    };
    replaceRequired(genericInspect, replacementInspect, "the Inspect implementation");
    replaceRequired(genericFindMethod, replacementFindMethod, "method lookup");
    replaceRequired(genericFindExact, replacementFindExact, "exact method lookup");
    replaceRequired(genericFieldStaticGet, replacementFieldStaticGet, "static field reads");
    replaceRequired(genericFieldStaticSet, replacementFieldStaticSet, "static field writes");
    replaceRequired(genericFindField, replacementFindField, "field lookup");
    return text;
}


