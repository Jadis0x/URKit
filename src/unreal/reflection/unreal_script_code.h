#pragma once

// Blueprint bytecode (UStruct::Script) as portable text for the type dump; urk-sdk renders it.

#include "unreal/detect/unreal_engine_detect.h"
#include "unreal/reflection/unreal_functions.h"

#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace URK::Unreal {

// Loaded bytecode holds raw pointers and FScriptNames; these name them.
struct ScriptSymbols {
    // "/Script/Engine.KismetSystemLibrary:PrintString" and the object's class name.
    std::function<std::pair<std::string, std::string>(Address)> object;
    std::function<std::string(Address)> field;
    std::function<std::string(std::uint32_t comparison, std::uint32_t number)> name;
};

// What differs between engine versions in the byte stream.
struct ScriptEncoding {
    // UE5: vector, rotator and transform constants are doubles.
    bool largeWorld = true;
    // Later UE5: EBlueprintTextLiteralType gained LocalizedTextWithNotes at 2.
    bool textWithNotes = false;
};

struct DecodedScript {
    // Top-level statements: byte offset, expression text.
    std::vector<std::pair<std::int32_t, std::string>> statements;
    // First offset not understood, or -1 when the script ended at its last byte.
    std::int32_t failedAt = -1;
    std::string failure;

    bool Complete() const { return failedAt < 0; }
};

DecodedScript DecodeScript(std::span<const std::uint8_t> code, const ScriptSymbols &symbols,
                           const ScriptEncoding &encoding);

// Tries encoding first, then the others; encoding becomes the one that read the whole script.
DecodedScript DecodeScriptAnyEncoding(std::span<const std::uint8_t> code, const ScriptSymbols &symbols,
                                      ScriptEncoding &encoding);

// UStruct::Script: the TArray whose last byte is EX_EndOfScript in script functions and empty in native ones.
std::int32_t FindScriptOffset(const ObjectFinder &finder, const StructOffsets &structs,
                              const FunctionOffsets &functions, std::string *why = nullptr);

// Dump lines for one function's bytecode ("X"/"XF"); empty when it has none. Game thread.
class ScriptDescriber {
  public:
    ScriptDescriber(const ObjectFinder &finder, const StructOffsets &structs, const PropertyChain &chain,
                    const FunctionOffsets &functions, const EngineVersion &version);

    struct Result {
        std::string lines;
        bool present = false;
        bool complete = false;
    };
    Result Describe(Address function);
    std::int32_t Offset() const { return offset_; }
    // Why the Script offset was not found; empty until measured or when found.
    const std::string &Failure() const { return failure_; }
    bool Measured() const { return measured_; }

  private:
    std::pair<std::string, std::string> ObjectOf(Address object) const;

    const ObjectFinder &finder_;
    const StructOffsets &structs_;
    const PropertyChain &chain_;
    const FunctionOffsets &functions_;
    ScriptEncoding encoding_;
    ScriptSymbols symbols_;
    std::int32_t offset_ = kOffsetNotFound;
    bool measured_ = false;
    std::string failure_;
};

} // namespace URK::Unreal
