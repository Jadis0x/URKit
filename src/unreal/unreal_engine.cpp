// UnrealEngine: finding the engine's globals and measuring its layout, once.

#include "unreal/unreal_sdk_api.h"
#include "unreal/hooks/unreal_object_life_hook.h"
#include "unreal/hooks/unreal_script_hook.h"
#include "unreal/reflection/unreal_enums.h"
#include "unreal/values/unreal_owned_values.h"
#include "unreal/values/unreal_places.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <set>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

UnrealEngine &UnrealEngine::Instance() {
    static UnrealEngine engine;
    return engine;
}

bool UnrealEngine::EnsureBootstrapped() {
    if (available_.load(std::memory_order_acquire))
        return true;
    if (ruledOut_.load(std::memory_order_acquire))
        return false;

    const std::uint64_t now = GetTickCount64();
    const std::uint64_t last = lastAttemptMs_.load(std::memory_order_acquire);
    if (last != 0 && now - last < kRetryCooldownMs)
        return false;

    std::lock_guard<std::mutex> lock(bootstrapMutex_);
    // Re-checked under the lock: another thread may have climbed or failed.
    if (available_.load(std::memory_order_relaxed))
        return true;
    if (ruledOut_.load(std::memory_order_relaxed))
        return false;
    const std::uint64_t lastUnderLock = lastAttemptMs_.load(std::memory_order_relaxed);
    if (lastUnderLock != 0 && GetTickCount64() - lastUnderLock < kRetryCooldownMs)
        return false;
    const std::uint64_t attemptStart = GetTickCount64();
    lastAttemptMs_.store(attemptStart, std::memory_order_release);
    ++profile_.attempts;
    std::uint64_t mark = attemptStart;
    const auto lap = [&mark] {
        const std::uint64_t now = GetTickCount64();
        const std::uint64_t elapsed = now - mark;
        mark = now;
        return elapsed;
    };

    // Partial state is unused while available_ is false; failures restamp the cooldown.
    const auto failed = [this, attemptStart](const char *why) {
        failure_.store(why, std::memory_order_release);
        const std::uint64_t now = GetTickCount64();
        profile_.failedMs += now - attemptStart;
        lastAttemptMs_.store(now, std::memory_order_release);
        return false;
    };

    const UnrealPresence &presence = Presence();
    if (ruledOut_.load(std::memory_order_acquire))
        return false;
    version_ = presence.version;

    std::vector<ScanRegion> dataRegions;
    std::vector<ScanRegion> codeRegions;
    for (const Address module : presence.runtimeModules) {
        const std::vector<ScanRegion> moduleData = ModuleDataRegions(memory_, module);
        const std::vector<ScanRegion> moduleCode = ModuleCodeRegions(memory_, module);
        dataRegions.insert(dataRegions.end(), moduleData.begin(), moduleData.end());
        codeRegions.insert(codeRegions.end(), moduleCode.begin(), moduleCode.end());
    }
    if (dataRegions.empty())
        return failed("the engine image has no data sections");

    if (!anchors_) {
        anchors_.emplace();
        for (const Address module : presence.runtimeModules) {
            GlobalCandidates found = FindGlobalCandidates(memory_, module);
            anchorModules_.emplace_back(module, found);
            anchors_->objectArrays.insert(anchors_->objectArrays.end(), found.objectArrays.begin(),
                                          found.objectArrays.end());
            anchors_->namePools.insert(anchors_->namePools.end(), found.namePools.begin(), found.namePools.end());
        }
        functionTable_ = FunctionTable::Read(memory_, presence.runtimeModules);
        profile_.anchoredArrays = anchors_->objectArrays.size();
        profile_.anchoredPools = anchors_->namePools.size();
        profile_.anchorMs = lap();
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = anchored ? GetTickCount64() + kScanFallbackMs : 0;
    }

    // Ordinary early on: the object array is built long after the loader lands.
    runtime_ = BootstrapAnchored(memory_, *anchors_);
    profile_.locatedBy = "code anchors";
    if (!runtime_ && GetTickCount64() >= scanAllowedAtMs_) {
        ++profile_.scans;
        runtime_ = BootstrapRuntime(memory_, dataRegions);
        profile_.locatedBy = "data scan";
        const bool anchored = !anchors_->objectArrays.empty() && !anchors_->namePools.empty();
        scanAllowedAtMs_ = GetTickCount64() + (anchored ? kScanFallbackMs : kScanRetryMs);
    }
    profile_.locateMs = lap();
    if (!runtime_)
        return failed("GUObjectArray and FNamePool not found yet");

    finder_ = std::make_unique<ObjectFinder>(ObjectFinder::Build(runtime_->objects, runtime_->names, runtime_->header));
    profile_.indexMs = lap();
    // Holds even when the version resource was stripped.
    if (!UsesFPropertySystem(*finder_)) {
        failure_.store("properties are UObjects, so the engine predates UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
        return false;
    }
    structs_ = FindStructOffsets(*finder_);
    fields_ = FindFieldOffsets(*finder_, runtime_->names, structs_);
    if (!fields_.Resolved())
        return failed("the FField layout did not resolve");

    tail_ = FindPropertyTailOffsets(*finder_, structs_, fields_);
    classes_ = FindClassOffsets(*finder_, structs_);
    chain_ = std::make_unique<PropertyChain>(memory_, runtime_->names, structs_, fields_);
    values_ = std::make_unique<PropertyValues>(memory_, runtime_->names, structs_, fields_, tail_);
    types_ = std::make_unique<TypeQueries>(*finder_, structs_, classes_);
    profile_.offsetsMs = lap();
    functions_ = FindFunctionOffsets(*finder_, structs_, fields_, tail_, codeRegions);
    profile_.functionsMs = lap();
    processEvent_ = FindProcessEvent(*finder_, *types_, structs_, functions_, codeRegions, functionTable_,
                                     &processEventFailure_)
                       .value_or(ProcessEventLocation{});
    profile_.processEventMs = lap();
    if (!processEvent_.Resolved()) {
        const std::uint64_t checked = GetTickCount64();
        if (processEventMissingSinceMs_ == 0)
            processEventMissingSinceMs_ = checked;
        if (checked - processEventMissingSinceMs_ < kProcessEventGraceMs)
            return failed("ProcessEvent not found yet");
    }

    available_.store(true, std::memory_order_release);
    return true;
}

namespace {

std::string ModuleFileName(Address base) {
    char path[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(reinterpret_cast<HMODULE>(base), path, MAX_PATH);
    const std::string full(path, length);
    const std::size_t slash = full.find_last_of("\\/");
    return slash == std::string::npos ? full : full.substr(slash + 1);
}

} // namespace

std::vector<std::string> UnrealEngine::ExplainFailure() {
    std::lock_guard<std::mutex> lock(bootstrapMutex_);
    std::vector<std::string> lines;
    if (available_.load(std::memory_order_acquire))
        return lines;

    const UnrealPresence &presence = Presence();
    std::string modules = "engine modules searched:";
    for (const Address module : presence.runtimeModules)
        modules += " " + ModuleFileName(module);
    lines.push_back(modules + " (" + presence.reason + ")");

    if (!runtime_) {
        const std::vector<std::string> anchors = ExplainAnchorsLocked();
        lines.insert(lines.end(), anchors.begin(), anchors.end());
        std::vector<ScanRegion> dataRegions;
        for (const Address module : presence.runtimeModules) {
            const std::vector<ScanRegion> regions = ModuleDataRegions(memory_, module);
            dataRegions.insert(dataRegions.end(), regions.begin(), regions.end());
        }
        const std::vector<Address> arrays = FindObjectArrayCandidates(memory_, dataRegions);
        const std::vector<Address> tables = FindNameTableCandidates(memory_, dataRegions);
        for (const std::string &line : ExplainNoPair(memory_, arrays, tables))
            lines.push_back("data scan: " + line);
        return lines;
    }
    if (!fields_.Resolved()) {
        const std::pair<const char *, std::int32_t> fields[] = {
            {"UStruct::ChildProperties", fields_.childProperties}, {"FField::ClassPrivate", fields_.fieldClass},
            {"FField::Next", fields_.fieldNext},                   {"FField::NamePrivate", fields_.fieldName},
            {"FFieldClass::CastFlags", fields_.fieldClassCastFlags}, {"FProperty::ArrayDim", fields_.arrayDim},
            {"FProperty::ElementSize", fields_.elementSize},       {"FProperty::PropertyFlags", fields_.propertyFlags},
            {"FProperty::Offset_Internal", fields_.offsetInternal}};
        std::string missing = "FField layout: not measured:";
        for (const auto &[name, offset] : fields)
            missing += offset == kOffsetNotFound ? std::string(" ") + name : std::string();
        lines.push_back(missing);
    }
    if (!processEvent_.Resolved() && !processEventFailure_.empty())
        lines.push_back("ProcessEvent: " + processEventFailure_);
    return lines;
}

std::vector<std::string> UnrealEngine::ExplainAnchors() {
    std::lock_guard<std::mutex> lock(bootstrapMutex_);
    return ExplainAnchorsLocked();
}

std::vector<std::string> UnrealEngine::ExplainAnchorsLocked() {
    std::vector<std::string> lines;
    for (const auto &[module, found] : anchorModules_) {
        if (found.unsearched) {
            lines.push_back(ModuleFileName(module) + ": not searched for anchors, " + found.unsearched);
            continue;
        }
        lines.push_back(ModuleFileName(module) + ": " + std::to_string(found.gcKeyLiterals) +
                        " gc.MaxObjectsInGame and " + std::to_string(found.engineNameLiterals) +
                        " ByteProperty literals, " + std::to_string(found.literalReferences) + " code references, " +
                        std::to_string(found.poolConstructors) + " pool constructors, GUObjectArray export " +
                        (found.exportedArrays ? "found" : "absent") + " -> " +
                        std::to_string(found.objectArrays.size()) + " array and " +
                        std::to_string(found.namePools.size()) + " pool candidates");
    }
    if (anchors_) {
        for (const std::string &line : ExplainNoPair(memory_, anchors_->objectArrays, anchors_->namePools))
            lines.push_back("anchors: " + line);
    }
    return lines;
}

void UnrealEngine::ResolveVersionFromCode(InstructionLength length) {
    if (!Available() || version_.Known())
        return;
    std::vector<ScanRegion> code;
    for (const Address module : Presence().runtimeModules) {
        const std::vector<ScanRegion> regions = ModuleCodeRegions(memory_, module);
        code.insert(code.end(), regions.begin(), regions.end());
    }
    EngineVersion found = FindVersionInCode(memory_, code, length);
    if (!found.Known())
        return;
    found.branch = version_.branch;
    found.changelist = version_.changelist;
    version_ = std::move(found);
}

const UnrealPresence &UnrealEngine::Presence() {
    std::lock_guard<std::mutex> lock(presenceMutex_);
    if (presence_)
        return *presence_;

    char pathBuffer[MAX_PATH]{};
    const DWORD length = GetModuleFileNameA(nullptr, pathBuffer, sizeof(pathBuffer));
    std::string path(pathBuffer, length < sizeof(pathBuffer) ? length : sizeof(pathBuffer) - 1);
    std::string name = path;
    const std::size_t slash = name.find_last_of("\\/");
    if (slash != std::string::npos)
        name = name.substr(slash + 1);

    // The main image first; an editor's engine lives in its Core/CoreUObject DLLs.
    std::vector<ModuleCandidate> modules{{name, path, MainModuleBase()}};
    HMODULE loaded[1024]{};
    DWORD needed = 0;
    if (K32EnumProcessModules(GetCurrentProcess(), loaded, sizeof(loaded), &needed)) {
        const DWORD count = std::min<DWORD>(needed / sizeof(HMODULE), static_cast<DWORD>(std::size(loaded)));
        for (DWORD i = 0; i < count; ++i) {
            char modulePath[MAX_PATH]{};
            const DWORD written = GetModuleFileNameA(loaded[i], modulePath, sizeof(modulePath));
            const std::string full(modulePath, written);
            const std::size_t cut = full.find_last_of("\\/");
            const std::string file = cut == std::string::npos ? full : full.substr(cut + 1);
            if (IsEngineCoreModule(file))
                modules.push_back({file, full, reinterpret_cast<Address>(loaded[i])});
        }
    }
    presence_ = DetectUnreal(memory_, modules);
    // Neither answer changes while the process lives.
    if (!presence_->WorthScanning() || presence_->runtimeModules.empty()) {
        failure_.store("not a UBT-built process", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    } else if (presence_->version.Known() && !presence_->version.UsesFieldProperties()) {
        failure_.store("the version resource names an engine before UE4.25", std::memory_order_release);
        ruledOut_.store(true, std::memory_order_release);
    }
    return *presence_;
}

bool UnrealEngine::RuledOut() const { return ruledOut_.load(std::memory_order_acquire); }

bool UnrealEngine::Available() const { return available_.load(std::memory_order_acquire); }

} // namespace URK::Unreal
