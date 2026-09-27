#include "unreal/reflection/unreal_script_code.h"
#include "unreal/memory/unreal_text.h"

#include <charconv>
#include <cstdio>
#include <cstring>

namespace URK::Unreal {
namespace {

// EExprToken, the same values 4.25-5.8 (later versions only add).
enum : std::uint8_t {
    kLocalVariable = 0x00,
    kInstanceVariable = 0x01,
    kDefaultVariable = 0x02,
    kReturn = 0x04,
    kJump = 0x06,
    kJumpIfNot = 0x07,
    kAssert = 0x09,
    kNothing = 0x0B,
    kNothingInt32 = 0x0C,
    kLet = 0x0F,
    kBitFieldConst = 0x11,
    kClassContext = 0x12,
    kMetaCast = 0x13,
    kLetBool = 0x14,
    kEndParmValue = 0x15,
    kEndFunctionParms = 0x16,
    kSelf = 0x17,
    kSkip = 0x18,
    kContext = 0x19,
    kContextFailSilent = 0x1A,
    kVirtualFunction = 0x1B,
    kFinalFunction = 0x1C,
    kIntConst = 0x1D,
    kFloatConst = 0x1E,
    kStringConst = 0x1F,
    kObjectConst = 0x20,
    kNameConst = 0x21,
    kRotationConst = 0x22,
    kVectorConst = 0x23,
    kByteConst = 0x24,
    kIntZero = 0x25,
    kIntOne = 0x26,
    kTrue = 0x27,
    kFalse = 0x28,
    kTextConst = 0x29,
    kNoObject = 0x2A,
    kTransformConst = 0x2B,
    kIntConstByte = 0x2C,
    kNoInterface = 0x2D,
    kDynamicCast = 0x2E,
    kStructConst = 0x2F,
    kEndStructConst = 0x30,
    kSetArray = 0x31,
    kEndArray = 0x32,
    kPropertyConst = 0x33,
    kUnicodeStringConst = 0x34,
    kInt64Const = 0x35,
    kUInt64Const = 0x36,
    kDoubleConst = 0x37,
    kCast = 0x38,
    kSetSet = 0x39,
    kEndSet = 0x3A,
    kSetMap = 0x3B,
    kEndMap = 0x3C,
    kSetConst = 0x3D,
    kEndSetConst = 0x3E,
    kMapConst = 0x3F,
    kEndMapConst = 0x40,
    kVector3fConst = 0x41,
    kStructMemberContext = 0x42,
    kLetMulticastDelegate = 0x43,
    kLetDelegate = 0x44,
    kLocalVirtualFunction = 0x45,
    kLocalFinalFunction = 0x46,
    kLocalOutVariable = 0x48,
    kDeprecatedOp4A = 0x4A,
    kInstanceDelegate = 0x4B,
    kPushExecutionFlow = 0x4C,
    kPopExecutionFlow = 0x4D,
    kComputedJump = 0x4E,
    kPopExecutionFlowIfNot = 0x4F,
    kBreakpoint = 0x50,
    kInterfaceContext = 0x51,
    kObjToInterfaceCast = 0x52,
    kEndOfScript = 0x53,
    kCrossInterfaceCast = 0x54,
    kInterfaceToObjCast = 0x55,
    kWireTracepoint = 0x5A,
    kSkipOffsetConst = 0x5B,
    kAddMulticastDelegate = 0x5C,
    kClearMulticastDelegate = 0x5D,
    kTracepoint = 0x5E,
    kLetObj = 0x5F,
    kLetWeakObjPtr = 0x60,
    kBindDelegate = 0x61,
    kRemoveMulticastDelegate = 0x62,
    kCallMulticastDelegate = 0x63,
    kLetValueOnPersistentFrame = 0x64,
    kArrayConst = 0x65,
    kEndArrayConst = 0x66,
    kSoftObjectConst = 0x67,
    kCallMath = 0x68,
    kSwitchValue = 0x69,
    kInstrumentationEvent = 0x6A,
    kArrayGetByRef = 0x6B,
    kClassSparseDataVariable = 0x6C,
    kFieldPathConst = 0x6D,
    kAutoRtfmTransact = 0x70,
    kAutoRtfmStopTransact = 0x71,
    kAutoRtfmAbortIfNot = 0x72,
    kAutoRtfmAbort = 0x73,
};

// EScriptInstrumentation::InlineEvent carries a name.
constexpr std::uint8_t kInlineEvent = 4;
constexpr int kMaxDepth = 256;
constexpr std::int32_t kMaxScriptBytes = 16 << 20;
// EX_EndOfScript closes every compiled script.
constexpr std::uint8_t kLastByte = kEndOfScript;
// UStruct::Script within this many bytes after PropertiesSize.
constexpr std::int32_t kScriptSearch = 0x48;
constexpr std::size_t kNativeSamples = 64;
// Small games have few bodies (NachoNo: 15); every other script function must still hold a valid array.
constexpr std::size_t kMinScriptSamples = 4;

std::string Quote(std::string_view text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\') {
            out += '\\';
            out += ch;
        } else if (ch == '\n') {
            out += "\\n";
        } else if (ch == '\t') {
            out += "\\t";
        } else if (ch == '\r') {
            out += "\\r";
        } else if (byte < 0x20 || byte == 0x7F) {
            char escaped[8];
            std::snprintf(escaped, sizeof(escaped), "\\x%02X", byte);
            out += escaped;
        } else {
            out += ch;
        }
    }
    return out + '"';
}

std::string Number(double value, bool single) {
    // Shortest text that reads back the same value: 0.35, not 0.34999999999999998.
    char text[40];
    const std::to_chars_result end = single ? std::to_chars(text, text + sizeof(text), static_cast<float>(value))
                                            : std::to_chars(text, text + sizeof(text), value);
    return std::string(text, end.ptr);
}

class Decoder {
  public:
    Decoder(std::span<const std::uint8_t> code, const ScriptSymbols &symbols, const ScriptEncoding &encoding)
        : code_(code), symbols_(symbols), encoding_(encoding) {}

    DecodedScript Run() {
        DecodedScript result;
        while (!failed_ && pos_ < code_.size()) {
            const std::int32_t offset = static_cast<std::int32_t>(pos_);
            std::string text;
            std::uint8_t token = 0;
            if (!Expr(text, 0, &token))
                break;
            if (text.empty()) {
                pos_ = static_cast<std::size_t>(offset);
                Fail("list end outside a list");
                break;
            }
            result.statements.emplace_back(offset, std::move(text));
            if (token == kEndOfScript) {
                if (pos_ != code_.size())
                    Fail("EX_EndOfScript before the last byte");
                else
                    return result;
            }
        }
        if (!failed_)
            Fail("no EX_EndOfScript");
        result.failedAt = static_cast<std::int32_t>(failAt_);
        result.failure = failure_;
        return result;
    }

  private:
    bool Fail(const std::string &why) {
        if (!failed_) {
            failed_ = true;
            failAt_ = pos_;
            failure_ = why;
        }
        return false;
    }

    template <typename T> bool Take(T &value) {
        if (pos_ + sizeof(T) > code_.size())
            return Fail("runs past the end");
        std::memcpy(&value, code_.data() + pos_, sizeof(T));
        pos_ += sizeof(T);
        return true;
    }

    template <typename T> bool Integer(const char *tag, std::string &out) {
        T value{};
        if (!Take(value))
            return false;
        out += tag;
        out += '(';
        out += std::to_string(value);
        out += ')';
        return true;
    }

    bool Pointer(Address &value) { return Take(value); }

    bool Field(const char *tag, std::string &out) {
        Address field = kNullAddress;
        if (!Pointer(field))
            return false;
        out += tag;
        out += '(';
        out += Quote(field == kNullAddress ? std::string() : symbols_.field(field));
        out += ')';
        return true;
    }

    std::string ObjectText(Address object) {
        if (object == kNullAddress)
            return "\"\",\"\"";
        const auto [path, type] = symbols_.object(object);
        return Quote(path) + ',' + Quote(type);
    }

    std::string PathText(Address object) {
        return Quote(object == kNullAddress ? std::string() : symbols_.object(object).first);
    }

    bool ScriptName(std::string &text) {
        std::uint32_t comparison = 0, display = 0, number = 0;
        if (!Take(comparison) || !Take(display) || !Take(number))
            return false;
        text = Quote(symbols_.name(comparison, number));
        return true;
    }

    // Floats in UE4, doubles in UE5 (LWC).
    bool Reals(const char *tag, int count, bool wide, std::string &out) {
        out += tag;
        out += '(';
        for (int i = 0; i < count; ++i) {
            double value = 0;
            if (wide) {
                if (!Take(value))
                    return false;
            } else {
                float single = 0;
                if (!Take(single))
                    return false;
                value = single;
            }
            out += (i ? "," : "");
            out += Number(value, !wide);
        }
        out += ')';
        return true;
    }

    bool Ansi(std::string &out) {
        std::string text;
        for (;;) {
            std::uint8_t ch = 0;
            if (!Take(ch))
                return false;
            if (ch == 0)
                break;
            // ANSICHAR is Latin-1.
            if (ch < 0x80) {
                text += static_cast<char>(ch);
            } else {
                text += static_cast<char>(0xC0 | (ch >> 6));
                text += static_cast<char>(0x80 | (ch & 0x3F));
            }
        }
        out += "Str(" + Quote(text) + ')';
        return true;
    }

    bool Unicode(std::string &out) {
        std::u16string text;
        for (;;) {
            std::uint16_t ch = 0;
            if (!Take(ch))
                return false;
            if (ch == 0)
                break;
            text += static_cast<char16_t>(ch);
        }
        out += "Str(" + Quote(Utf16ToUtf8(text)) + ')';
        return true;
    }

    // Appends ",arg" for each expression up to (and eating) the end token.
    bool Until(std::uint8_t end, std::string &out, int depth) {
        for (;;) {
            std::string arg;
            std::uint8_t token = 0;
            if (!Expr(arg, depth + 1, &token))
                return false;
            if (token == end)
                return true;
            out += ',';
            out += arg;
        }
    }

    bool Sub(std::string &out, int depth) { return Expr(out, depth + 1, nullptr); }

    // tag(a,b): each a sub-expression.
    bool Tagged(const char *tag, int count, std::string &out, int depth) {
        out += tag;
        out += '(';
        for (int i = 0; i < count; ++i) {
            if (i)
                out += ',';
            if (!Sub(out, depth))
                return false;
        }
        out += ')';
        return true;
    }

    // A text literal's parts are string constants; anything else means the wrong numbering.
    bool TextString(std::string &out, int depth) {
        if (pos_ >= code_.size() || (code_[pos_] != kStringConst && code_[pos_] != kUnicodeStringConst))
            return Fail("text literal without its string");
        out += ',';
        return Sub(out, depth);
    }

    bool Text(std::string &out, int depth) {
        std::uint8_t kind = 0;
        if (!Take(kind))
            return false;
        // Older numbering: Empty, Localized, Invariant, Literal, Table.
        enum { Empty, Localized, LocalizedNotes, Invariant, Literal, Table, Unknown };
        static constexpr int kOld[] = {Empty, Localized, Invariant, Literal, Table};
        static constexpr int kNew[] = {Empty, Localized, LocalizedNotes, Invariant, Literal, Table};
        int type = Unknown;
        if (encoding_.textWithNotes && kind < 6)
            type = kNew[kind];
        else if (!encoding_.textWithNotes && kind < 5)
            type = kOld[kind];
        out += "Text(";
        switch (type) {
        case Empty:
            out += "\"empty\"";
            break;
        case Localized:
        case LocalizedNotes:
            out += "\"loc\"";
            for (int i = 0; i < (type == LocalizedNotes ? 4 : 3); ++i) {
                if (!TextString(out, depth))
                    return false;
            }
            break;
        case Invariant:
        case Literal:
            out += type == Invariant ? "\"inv\"" : "\"lit\"";
            if (!TextString(out, depth))
                return false;
            break;
        case Table: {
            Address table = kNullAddress;
            if (!Pointer(table))
                return false;
            out += "\"table\"";
            if (!TextString(out, depth) || !TextString(out, depth))
                return false;
            break;
        }
        default:
            return Fail("unknown text literal type " + std::to_string(kind));
        }
        out += ')';
        return true;
    }

    // Final, local final and math calls: a UFunction pointer, then the arguments.
    bool Call(const char *tag, std::string &out, int depth) {
        Address function = kNullAddress;
        if (!Pointer(function))
            return false;
        out += tag;
        out += '(';
        out += PathText(function);
        if (!Until(kEndFunctionParms, out, depth))
            return false;
        out += ')';
        return true;
    }

    bool Context(const char *tag, std::string &out, int depth) {
        out += tag;
        out += '(';
        if (!Sub(out, depth))
            return false;
        std::uint32_t skip = 0;
        Address rvalue = kNullAddress;
        if (!Take(skip) || !Pointer(rvalue))
            return false;
        out += ',';
        if (!Sub(out, depth))
            return false;
        out += ')';
        return true;
    }

    // A cast to a class: its path, then the operand.
    bool ClassCast(const char *tag, std::string &out, int depth) {
        Address type = kNullAddress;
        if (!Pointer(type))
            return false;
        out += tag;
        out += '(';
        out += PathText(type);
        out += ',';
        if (!Sub(out, depth))
            return false;
        out += ')';
        return true;
    }

    // Container literals: inner properties (unused), a count, then elements up to the end token.
    bool Literal(const char *tag, int properties, std::uint8_t end, std::string &out, int depth) {
        for (int i = 0; i < properties; ++i) {
            Address property = kNullAddress;
            if (!Pointer(property))
                return false;
        }
        std::int32_t count = 0;
        if (!Take(count))
            return false;
        out += tag;
        out += "(";
        out += std::to_string(count);
        if (!Until(end, out, depth))
            return false;
        out += ')';
        return true;
    }

    bool Expr(std::string &out, int depth, std::uint8_t *seen) {
        if (depth > kMaxDepth)
            return Fail("nested too deep");
        const std::size_t start = pos_;
        std::uint8_t token = 0;
        if (!Take(token))
            return false;
        if (seen)
            *seen = token;
        switch (token) {
        case kLocalVariable:
            return Field("Local", out);
        case kInstanceVariable:
            return Field("Inst", out);
        case kDefaultVariable:
            return Field("Default", out);
        case kLocalOutVariable:
            return Field("Out", out);
        case kClassSparseDataVariable:
            return Field("Sparse", out);
        case kPropertyConst:
            return Field("PropConst", out);
        case kReturn:
            return Tagged("Return", 1, out, depth);
        case kJump:
            return Integer<std::uint32_t>("Jump", out);
        case kPushExecutionFlow:
            return Integer<std::uint32_t>("Push", out);
        case kSkipOffsetConst:
            return Integer<std::uint32_t>("SkipOffset", out);
        case kJumpIfNot: {
            std::uint32_t target = 0;
            if (!Take(target))
                return false;
            out += "JumpIfNot(" + std::to_string(target) + ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kAssert: {
            std::uint16_t line = 0;
            std::uint8_t debug = 0;
            if (!Take(line) || !Take(debug))
                return false;
            out += "Assert(" + std::to_string(line) + ',' + std::to_string(debug) + ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kNothing:
        case kDeprecatedOp4A:
            out += "Nothing";
            return true;
        case kNothingInt32:
            return Integer<std::int32_t>("NothingInt32", out);
        case kLet: {
            Address property = kNullAddress;
            if (!Pointer(property))
                return false;
            return Tagged("Let", 2, out, depth);
        }
        case kLetBool:
            return Tagged("LetBool", 2, out, depth);
        case kLetObj:
            return Tagged("LetObj", 2, out, depth);
        case kLetWeakObjPtr:
            return Tagged("LetWeak", 2, out, depth);
        case kLetDelegate:
            return Tagged("LetDelegate", 2, out, depth);
        case kLetMulticastDelegate:
            return Tagged("LetMulticast", 2, out, depth);
        case kLetValueOnPersistentFrame:
        case kStructMemberContext: {
            Address property = kNullAddress;
            if (!Pointer(property))
                return false;
            out += token == kStructMemberContext ? "Member(" : "LetFrame(";
            out += Quote(property == kNullAddress ? std::string() : symbols_.field(property));
            out += ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kBitFieldConst: {
            Address property = kNullAddress;
            std::uint8_t value = 0;
            if (!Pointer(property) || !Take(value))
                return false;
            out += "BitField(" + Quote(symbols_.field(property)) + ',' + std::to_string(value) + ')';
            return true;
        }
        case kClassContext:
            return Context("ClassContext", out, depth);
        case kContext:
            return Context("Context", out, depth);
        case kContextFailSilent:
            return Context("ContextFS", out, depth);
        case kInterfaceContext:
            return Tagged("IfaceContext", 1, out, depth);
        case kMetaCast:
            return ClassCast("MetaCast", out, depth);
        case kDynamicCast:
            return ClassCast("DynCast", out, depth);
        case kObjToInterfaceCast:
            return ClassCast("ToIface", out, depth);
        case kCrossInterfaceCast:
            return ClassCast("CrossIface", out, depth);
        case kInterfaceToObjCast:
            return ClassCast("IfaceToObj", out, depth);
        case kCast: {
            std::uint8_t kind = 0;
            if (!Take(kind))
                return false;
            out += "Cast(" + std::to_string(kind) + ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kEndParmValue:
            out += "EndParmValue";
            return true;
        case kEndFunctionParms:
        case kEndStructConst:
        case kEndArray:
        case kEndSet:
        case kEndMap:
        case kEndSetConst:
        case kEndMapConst:
        case kEndArrayConst:
            // Only a list's closing token; the list eats it.
            return seen ? true : Fail("list end outside a list");
        case kSelf:
            out += "Self";
            return true;
        case kSkip: {
            std::uint32_t skip = 0;
            if (!Take(skip))
                return false;
            return Tagged("Skip", 1, out, depth);
        }
        case kVirtualFunction:
        case kLocalVirtualFunction: {
            std::string name;
            if (!ScriptName(name))
                return false;
            out += "VCall(" + name;
            if (!Until(kEndFunctionParms, out, depth))
                return false;
            out += ')';
            return true;
        }
        case kFinalFunction:
        case kLocalFinalFunction:
        case kCallMath:
            return Call("Call", out, depth);
        case kCallMulticastDelegate:
            return Call("Broadcast", out, depth);
        case kIntConst:
            return Integer<std::int32_t>("Int", out);
        case kInt64Const:
            return Integer<std::int64_t>("Int64", out);
        case kUInt64Const:
            return Integer<std::uint64_t>("UInt64", out);
        case kByteConst:
            return Integer<std::uint8_t>("Byte", out);
        case kIntConstByte:
            return Integer<std::uint8_t>("Int", out);
        case kIntZero:
            out += "Int(0)";
            return true;
        case kIntOne:
            out += "Int(1)";
            return true;
        case kTrue:
            out += "True";
            return true;
        case kFalse:
            out += "False";
            return true;
        case kFloatConst: {
            float value = 0;
            if (!Take(value))
                return false;
            out += "Float(" + Number(value, true) + ')';
            return true;
        }
        case kDoubleConst: {
            double value = 0;
            if (!Take(value))
                return false;
            out += "Double(" + Number(value, false) + ')';
            return true;
        }
        case kStringConst:
            return Ansi(out);
        case kUnicodeStringConst:
            return Unicode(out);
        case kTextConst:
            return Text(out, depth);
        case kObjectConst: {
            Address object = kNullAddress;
            if (!Pointer(object))
                return false;
            out += "Obj(" + ObjectText(object) + ')';
            return true;
        }
        case kNameConst: {
            std::string name;
            if (!ScriptName(name))
                return false;
            out += "Name(" + name + ')';
            return true;
        }
        case kRotationConst:
            return Reals("Rotator", 3, encoding_.largeWorld, out);
        case kVectorConst:
            return Reals("Vector", 3, encoding_.largeWorld, out);
        case kVector3fConst:
            return Reals("Vector3f", 3, false, out);
        case kTransformConst:
            return Reals("Transform", 10, encoding_.largeWorld, out);
        case kNoObject:
            out += "NoObj";
            return true;
        case kNoInterface:
            out += "NoIface";
            return true;
        case kStructConst: {
            Address type = kNullAddress;
            std::int32_t size = 0;
            if (!Pointer(type) || !Take(size))
                return false;
            out += "Struct(" + PathText(type);
            if (!Until(kEndStructConst, out, depth))
                return false;
            out += ')';
            return true;
        }
        case kSetArray:
            out += "SetArray(";
            if (!Sub(out, depth) || !Until(kEndArray, out, depth))
                return false;
            out += ')';
            return true;
        case kSetSet:
        case kSetMap: {
            out += token == kSetSet ? "SetSet(" : "SetMap(";
            std::int32_t count = 0;
            if (!Sub(out, depth) || !Take(count) || !Until(token == kSetSet ? kEndSet : kEndMap, out, depth))
                return false;
            out += ')';
            return true;
        }
        case kArrayConst:
            return Literal("ArrayConst", 1, kEndArrayConst, out, depth);
        case kSetConst:
            return Literal("SetConst", 1, kEndSetConst, out, depth);
        case kMapConst:
            return Literal("MapConst", 2, kEndMapConst, out, depth);
        case kInstanceDelegate: {
            std::string name;
            if (!ScriptName(name))
                return false;
            out += "InstDelegate(" + name + ')';
            return true;
        }
        case kBindDelegate: {
            std::string name;
            if (!ScriptName(name))
                return false;
            out += "BindDelegate(" + name + ',';
            if (!Sub(out, depth))
                return false;
            out += ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kAddMulticastDelegate:
            return Tagged("AddMulticast", 2, out, depth);
        case kRemoveMulticastDelegate:
            return Tagged("RemoveMulticast", 2, out, depth);
        case kClearMulticastDelegate:
            return Tagged("ClearMulticast", 1, out, depth);
        case kPopExecutionFlow:
            out += "Pop";
            return true;
        case kPopExecutionFlowIfNot:
            return Tagged("PopIfNot", 1, out, depth);
        case kComputedJump:
            return Tagged("ComputedJump", 1, out, depth);
        case kBreakpoint:
        case kTracepoint:
        case kWireTracepoint:
            out += "Tracepoint";
            return true;
        case kEndOfScript:
            out += "EndOfScript";
            return true;
        case kSoftObjectConst:
            return Tagged("Soft", 1, out, depth);
        case kFieldPathConst:
            return Tagged("FieldPath", 1, out, depth);
        case kArrayGetByRef:
            return Tagged("GetByRef", 2, out, depth);
        case kSwitchValue: {
            std::uint16_t cases = 0;
            std::uint32_t end = 0;
            if (!Take(cases) || !Take(end))
                return false;
            out += "Switch(";
            if (!Sub(out, depth))
                return false;
            for (std::uint16_t i = 0; i < cases; ++i) {
                std::uint32_t next = 0;
                out += ',';
                if (!Sub(out, depth) || !Take(next))
                    return false;
                out += ',';
                if (!Sub(out, depth))
                    return false;
            }
            out += ',';
            if (!Sub(out, depth))
                return false;
            out += ')';
            return true;
        }
        case kInstrumentationEvent: {
            if (pos_ >= code_.size())
                return Fail("runs past the end");
            const std::uint8_t kind = code_[pos_];
            pos_ += 1 + (kind == kInlineEvent ? 12 : 0);
            if (pos_ > code_.size())
                return Fail("runs past the end");
            out += "Instrumentation(" + std::to_string(kind) + ')';
            return true;
        }
        case kAutoRtfmTransact: {
            std::int32_t id = 0;
            std::uint32_t skip = 0;
            if (!Take(id) || !Take(skip))
                return false;
            out += "RtfmTransact(" + std::to_string(id);
            if (!Until(kAutoRtfmStopTransact, out, depth))
                return false;
            out += ')';
            return true;
        }
        case kAutoRtfmStopTransact: {
            std::int32_t id = 0;
            std::int8_t mode = 0;
            if (!Take(id) || !Take(mode))
                return false;
            out += "RtfmStop(" + std::to_string(id) + ',' + std::to_string(mode) + ')';
            return true;
        }
        case kAutoRtfmAbortIfNot:
            return Tagged("RtfmAbortIfNot", 1, out, depth);
        case kAutoRtfmAbort:
            out += "RtfmAbort";
            return true;
        default: {
            pos_ = start;
            char why[48];
            std::snprintf(why, sizeof(why), "unknown token 0x%02X", token);
            return Fail(why);
        }
        }
    }

    std::span<const std::uint8_t> code_;
    const ScriptSymbols &symbols_;
    const ScriptEncoding &encoding_;
    std::size_t pos_ = 0;
    bool failed_ = false;
    std::size_t failAt_ = 0;
    std::string failure_;
};

// Tabs and line breaks split dump records.
std::string Clean(std::string text) {
    for (char &ch : text) {
        if (ch == '\t' || ch == '\n' || ch == '\r')
            ch = ' ';
    }
    return text;
}

} // namespace

DecodedScript DecodeScript(std::span<const std::uint8_t> code, const ScriptSymbols &symbols,
                           const ScriptEncoding &encoding) {
    return Decoder(code, symbols, encoding).Run();
}

DecodedScript DecodeScriptAnyEncoding(std::span<const std::uint8_t> code, const ScriptSymbols &symbols,
                                      ScriptEncoding &encoding) {
    DecodedScript first = DecodeScript(code, symbols, encoding);
    if (first.Complete())
        return first;
    const ScriptEncoding tries[] = {{encoding.largeWorld, !encoding.textWithNotes},
                                    {!encoding.largeWorld, encoding.textWithNotes},
                                    {!encoding.largeWorld, !encoding.textWithNotes}};
    for (const ScriptEncoding &other : tries) {
        DecodedScript decoded = DecodeScript(code, symbols, other);
        if (decoded.Complete()) {
            encoding = other;
            return decoded;
        }
    }
    return first;
}

namespace {
std::string HexOffset(std::int32_t value) {
    char text[16];
    std::snprintf(text, sizeof(text), "0x%X", static_cast<unsigned>(value));
    return text;
}
} // namespace

std::int32_t FindScriptOffset(const ObjectFinder &finder, const StructOffsets &structs,
                              const FunctionOffsets &functions, std::string *why) {
    const auto fail = [why](std::string reason) {
        if (why)
            *why = std::move(reason);
        return kOffsetNotFound;
    };
    if (structs.propertiesSize == kOffsetNotFound || functions.functionFlags == kOffsetNotFound)
        return fail("UStruct.PropertiesSize or UFunction.FunctionFlags is unknown");
    const MemoryReader &reader = finder.Reader();
    // Every script function: engine classes come first in the array, and their script functions have no body.
    std::vector<Address> scripts;
    std::vector<Address> natives;
    finder.Objects().ForEach([&](std::int32_t, Address object) {
        if (object == kNullAddress || !ObjectIs(finder, structs, object, kCastFlagFunction))
            return true;
        const std::optional<std::uint32_t> flags = reader.ReadUInt32(object + functions.functionFlags);
        if (!flags)
            return true;
        if (!(*flags & kFunctionFlagNative))
            scripts.push_back(object);
        else if (natives.size() < kNativeSamples)
            natives.push_back(object);
        return true;
    });
    if (scripts.empty() || natives.empty())
        return fail(std::to_string(scripts.size()) + " script and " + std::to_string(natives.size()) +
                    " native functions");

    std::string closest;
    const std::int32_t from = (structs.propertiesSize + 4 + 7) & ~7;
    for (std::int32_t offset = from; offset <= structs.propertiesSize + kScriptSearch; offset += 8) {
        const auto refuse = [&](const std::string &reason) {
            if (closest.empty())
                closest = "at " + HexOffset(offset) + ": " + reason;
        };
        bool ok = true;
        for (const Address native : natives) {
            const std::optional<std::int32_t> num = reader.ReadInt32(native + offset + 8);
            if (!num || *num != 0) {
                ok = false;
                break;
            }
        }
        if (!ok)
            continue;
        std::size_t ended = 0;
        for (std::size_t i = 0; ok && i < scripts.size(); ++i) {
            const Address at = scripts[i] + offset;
            const std::optional<Address> data = reader.ReadPointer(at);
            const std::optional<std::int32_t> num = reader.ReadInt32(at + 8);
            const std::optional<std::int32_t> max = reader.ReadInt32(at + 12);
            if (!data || !num || !max || *num < 0 || *num > *max || *max > kMaxScriptBytes) {
                refuse("not an array in function " + std::to_string(i));
                ok = false;
                break;
            }
            // A script function with no body (an event declaration, a signature) says nothing.
            if (*num == 0)
                continue;
            const std::optional<std::uint8_t> last = reader.ReadAs<std::uint8_t>(*data + *num - 1);
            if (!last || *last != kLastByte) {
                refuse("last byte " + (last ? std::to_string(*last) : std::string("unreadable")) + " after " +
                       std::to_string(ended) + " that ended right");
                ok = false;
                break;
            }
            ++ended;
        }
        if (ok && ended >= kMinScriptSamples)
            return offset;
        if (ok)
            refuse(std::to_string(ended) + " of " + std::to_string(scripts.size()) + " script functions have a body");
    }
    return fail(closest.empty() ? "every offset held bytes in a native function" : closest);
}

ScriptDescriber::ScriptDescriber(const ObjectFinder &finder, const StructOffsets &structs,
                                 const PropertyChain &chain, const FunctionOffsets &functions,
                                 const EngineVersion &version)
    : finder_(finder), structs_(structs), chain_(chain), functions_(functions) {
    encoding_.largeWorld = version.major >= 5;
    encoding_.textWithNotes = version.major > 5 || (version.major == 5 && version.minor >= 5);
    symbols_.object = [this](Address object) { return ObjectOf(object); };
    symbols_.field = [this](Address field) { return chain_.NameOf(field).value_or("?"); };
    symbols_.name = [this](std::uint32_t comparison, std::uint32_t number) {
        std::string name = finder_.Names().Read(comparison).value_or("?");
        if (number > 0)
            name += '_' + std::to_string(number - 1);
        return name;
    };
}

// UE's path form: Package.Object:Subobject.Deeper.
std::pair<std::string, std::string> ScriptDescriber::ObjectOf(Address object) const {
    if (!IsLiveObject(finder_, object))
        return {"?", ""};
    std::vector<std::string> names;
    for (Address at = object; at != kNullAddress && names.size() < 32; at = finder_.OuterOf(at))
        names.push_back(finder_.NameOf(at).value_or("?"));
    std::string path;
    for (std::size_t i = names.size(); i-- > 0;) {
        const std::size_t depth = names.size() - 1 - i;
        if (depth > 0)
            path += depth == 2 ? ':' : '.';
        path += names[i];
    }
    return {path, finder_.NameOf(finder_.ClassOf(object)).value_or("")};
}

ScriptDescriber::Result ScriptDescriber::Describe(Address function) {
    if (!measured_) {
        offset_ = FindScriptOffset(finder_, structs_, functions_, &failure_);
        measured_ = true;
    }
    Result result;
    if (offset_ == kOffsetNotFound)
        return result;
    const MemoryReader &reader = finder_.Reader();
    const std::optional<Address> data = reader.ReadPointer(function + offset_);
    const std::optional<std::int32_t> num = reader.ReadInt32(function + offset_ + 8);
    if (!data || !num || *num <= 0)
        return result;
    result.present = true;
    if (*num > kMaxScriptBytes)
        return {"XF\t0\tscript of " + std::to_string(*num) + " bytes\n", true, false};
    std::vector<std::uint8_t> code(static_cast<std::size_t>(*num));
    if (!reader.Read(*data, code.data(), code.size()))
        return {"XF\t0\tunreadable\n", true, false};
    const DecodedScript decoded = DecodeScriptAnyEncoding(code, symbols_, encoding_);
    for (const auto &[offset, text] : decoded.statements)
        result.lines += "X\t" + std::to_string(offset) + '\t' + text + '\n';
    if (!decoded.Complete())
        result.lines += "XF\t" + std::to_string(decoded.failedAt) + '\t' + Clean(decoded.failure) + '\n';
    result.complete = decoded.Complete();
    return result;
}

} // namespace URK::Unreal
