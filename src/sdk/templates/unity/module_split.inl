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
