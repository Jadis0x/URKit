#pragma once

// Assembly-image and managed-type name matching shared by the Mono and IL2CPP
// bindings. Both runtimes report names in shapes that vary across Unity versions
// (with or without ".dll", with or without a directory, C# aliases versus CLR
// names), so the two backends must agree on what counts as the same name. Keeping
// one implementation here is what stops them drifting apart per backend.

#include <algorithm>
#include <cctype>
#include <string>
#include <string_view>
#include <vector>

namespace URK::UnityNames {

inline std::string Lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

inline std::string BaseName(std::string value) {
    const size_t separator = value.find_last_of("/\\");
    if (separator != std::string::npos)
        value.erase(0, separator + 1);
    return value;
}

inline std::string StripDll(std::string value) {
    if (value.size() > 4 && Lower(value.substr(value.size() - 4)) == ".dll")
        value.resize(value.size() - 4);
    return value;
}

// Every spelling one image name may legitimately take, lowercased.
inline std::vector<std::string> ImageNameVariants(const char *value) {
    std::vector<std::string> variants;
    if (!value || !value[0])
        return variants;
    const std::string original = Lower(value);
    const std::string base = BaseName(original);
    for (const std::string &variant : {original, StripDll(original), base, StripDll(base)}) {
        if (!variant.empty() && std::find(variants.begin(), variants.end(), variant) == variants.end())
            variants.push_back(variant);
    }
    return variants;
}

inline bool ImageNameMatches(const std::vector<std::string> &requestedVariants, const char *actual) {
    if (requestedVariants.empty())
        return false;
    const std::vector<std::string> actualVariants = ImageNameVariants(actual);
    for (const std::string &requested : requestedVariants)
        for (const std::string &candidate : actualVariants)
            if (requested == candidate)
                return true;
    return false;
}

inline bool ImageNameMatches(const char *requested, const char *actual) {
    return ImageNameMatches(ImageNameVariants(requested), actual);
}

// Folds C# aliases onto their CLR names and drops decorations the runtimes add,
// so "int", "Int32" and "System.Int32" compare equal. Comparison is
// case-insensitive: the result is only ever tested against another normalized
// name, and requiring exact case would reject spellings both runtimes accept.
inline std::string NormalizeTypeName(std::string type) {
    std::string suffix;
    while (!type.empty() && (type.back() == '&' || type.back() == '*')) {
        suffix.insert(suffix.begin(), type.back());
        type.pop_back();
    }
    if (type.rfind("class ", 0) == 0)
        type.erase(0, 6);
    if (type.rfind("struct ", 0) == 0)
        type.erase(0, 7);
    type = Lower(std::move(type));
    if (type == "bool" || type == "boolean" || type == "system.boolean")
        type = "system.boolean";
    else if (type == "byte" || type == "system.byte")
        type = "system.byte";
    else if (type == "sbyte" || type == "system.sbyte")
        type = "system.sbyte";
    else if (type == "char" || type == "system.char")
        type = "system.char";
    else if (type == "short" || type == "int16" || type == "system.int16")
        type = "system.int16";
    else if (type == "ushort" || type == "uint16" || type == "system.uint16")
        type = "system.uint16";
    else if (type == "int" || type == "int32" || type == "system.int32")
        type = "system.int32";
    else if (type == "uint" || type == "uint32" || type == "system.uint32")
        type = "system.uint32";
    else if (type == "long" || type == "int64" || type == "system.int64")
        type = "system.int64";
    else if (type == "ulong" || type == "uint64" || type == "system.uint64")
        type = "system.uint64";
    else if (type == "float" || type == "single" || type == "system.single")
        type = "system.single";
    else if (type == "double" || type == "system.double")
        type = "system.double";
    else if (type == "string" || type == "system.string")
        type = "system.string";
    else if (type == "object" || type == "system.object")
        type = "system.object";
    else if (type == "type" || type == "system.type")
        type = "system.type";
    else if (type == "void" || type == "system.void")
        type = "system.void";
    return type + suffix;
}

inline bool TypeNameMatches(const char *actual, const char *requested) {
    if (!actual || !requested)
        return false;
    return NormalizeTypeName(actual) == NormalizeTypeName(requested);
}

inline bool TypeNameMatches(const std::string &actual, const char *requested) {
    return TypeNameMatches(actual.c_str(), requested);
}

} // namespace URK::UnityNames
