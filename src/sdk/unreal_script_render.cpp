// Blueprint bytecode from the type dump as C++-like pseudo-code: one file per class, for reading.

#include "unreal_type_model.h"

#include <algorithm>
#include <charconv>
#include <cstdio>
#include <functional>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>

namespace UnrealTypeCodegen {
namespace {

constexpr std::uint64_t kConstParm = 0x2;
constexpr std::uint64_t kOutParm = 0x100;
constexpr std::uint64_t kReturnParm = 0x400;
constexpr std::uint32_t kFunctionStatic = 0x2000;
// Flow-stack states tried per entry before pops are left unresolved.
constexpr std::size_t kMaxStates = 20000;
constexpr std::size_t kMaxFlowDepth = 64;
constexpr int kMaxNesting = 48;

// --- expressions as the dump writes them: Tag(arg,...), "strings", numbers --------------------

struct Node {
    std::string tag;
    std::string text;
    bool atom = false;
    bool quoted = false;
    std::vector<Node> args;

    bool Is(std::string_view name) const { return !atom && tag == name; }
    const Node &Arg(std::size_t i) const {
        static const Node empty;
        return i < args.size() ? args[i] : empty;
    }
};

class Parser {
  public:
    explicit Parser(std::string_view text) : text_(text) {}

    std::optional<Node> Parse() {
        Node node;
        if (!Value(node, 0) || pos_ != text_.size())
            return std::nullopt;
        return node;
    }

  private:
    bool Value(Node &node, int depth) {
        if (pos_ >= text_.size() || depth > 512)
            return false;
        if (text_[pos_] == '"') {
            node.atom = node.quoted = true;
            return String(node.text);
        }
        const std::size_t start = pos_;
        while (pos_ < text_.size() && text_[pos_] != '(' && text_[pos_] != ')' && text_[pos_] != ',')
            ++pos_;
        const std::string word(text_.substr(start, pos_ - start));
        if (word.empty())
            return false;
        if (pos_ < text_.size() && text_[pos_] == '(') {
            node.tag = word;
            ++pos_;
            if (pos_ < text_.size() && text_[pos_] == ')') {
                ++pos_;
                return true;
            }
            for (;;) {
                Node arg;
                if (!Value(arg, depth + 1))
                    return false;
                node.args.push_back(std::move(arg));
                if (pos_ >= text_.size())
                    return false;
                const char next = text_[pos_++];
                if (next == ')')
                    return true;
                if (next != ',')
                    return false;
            }
        }
        const char first = word[0];
        if ((first >= '0' && first <= '9') || first == '-' || first == '+' || word == "nan" || word == "inf") {
            node.atom = true;
            node.text = word;
        } else {
            node.tag = word;
        }
        return true;
    }

    bool String(std::string &out) {
        ++pos_;
        while (pos_ < text_.size()) {
            const char ch = text_[pos_++];
            if (ch == '"')
                return true;
            if (ch != '\\') {
                out += ch;
                continue;
            }
            if (pos_ >= text_.size())
                return false;
            const char escaped = text_[pos_++];
            if (escaped == 'n')
                out += '\n';
            else if (escaped == 't')
                out += '\t';
            else if (escaped == 'r')
                out += '\r';
            else if (escaped == 'x' && pos_ + 2 <= text_.size()) {
                out += static_cast<char>(std::stoi(std::string(text_.substr(pos_, 2)), nullptr, 16));
                pos_ += 2;
            } else
                out += escaped;
        }
        return false;
    }

    std::string_view text_;
    std::size_t pos_ = 0;
};

// --- names ------------------------------------------------------------------------------------

std::string Ident(const std::string &name) {
    std::string out;
    for (const char ch : name) {
        const bool keep = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9');
        const char next = keep ? ch : '_';
        if (next == '_' && !out.empty() && out.back() == '_')
            continue;
        out += next;
    }
    if (out.empty())
        return "Unnamed";
    if (out.front() >= '0' && out.front() <= '9')
        out = "N" + out;
    return out;
}

std::string Literal(const std::string &text) {
    std::string out = "\"";
    for (const char ch : text) {
        const unsigned char byte = static_cast<unsigned char>(ch);
        if (ch == '"' || ch == '\\')
            out += std::string("\\") + ch;
        else if (ch == '\n')
            out += "\\n";
        else if (ch == '\t')
            out += "\\t";
        else if (ch == '\r')
            out += "\\r";
        else if (byte < 0x20) {
            char octal[8];
            std::snprintf(octal, sizeof(octal), "\\%03o", byte);
            out += octal;
        } else
            out += ch;
    }
    return out + '"';
}

std::string Hex4(int value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%04X", static_cast<unsigned>(value));
    return text;
}

// A real number that reads as one (2 -> 2.0), in the shortest form that reads back the same value.
std::string Real(const std::string &text, bool single) {
    std::string out = text;
    double value = 0;
    const char *end = text.data() + text.size();
    if (std::from_chars(text.data(), end, value).ptr == end) {
        char buffer[40];
        const std::to_chars_result written =
            single ? std::to_chars(buffer, buffer + sizeof(buffer), static_cast<float>(value))
                   : std::to_chars(buffer, buffer + sizeof(buffer), value);
        out.assign(buffer, written.ptr);
    }
    if (out.find_first_of(".eEn") != std::string::npos)
        return out;
    return out + ".0";
}

// "/Script/Engine.KismetSystemLibrary:PrintString" -> package, class, function.
struct FunctionPath {
    std::string package;
    std::string owner;
    std::string name;
};

FunctionPath SplitFunction(const std::string &path) {
    FunctionPath out;
    const std::size_t colon = path.rfind(':');
    const std::string object = colon == std::string::npos ? path : path.substr(0, colon);
    out.name = colon == std::string::npos ? path : path.substr(colon + 1);
    const std::size_t dot = object.rfind('.');
    out.package = dot == std::string::npos ? std::string() : object.substr(0, dot);
    out.owner = dot == std::string::npos ? object : object.substr(dot + 1);
    return out;
}

// The last name of an object path.
std::string LastName(const std::string &path) {
    const std::size_t cut = path.find_last_of(".:");
    return cut == std::string::npos ? path : path.substr(cut + 1);
}

std::string PackageOf(const std::string &path) {
    const std::size_t dot = path.find('.');
    return dot == std::string::npos ? path : path.substr(0, dot);
}

// --- rendered text with a precedence: operands of operators get parentheses ----------------

struct Text {
    std::string s;
    bool compound = false;
};

std::string Operand(const Text &text) { return text.compound ? "(" + text.s + ")" : text.s; }

std::string Negated(const Text &text) {
    if (!text.compound && text.s.size() > 1 && text.s[0] == '!')
        return text.s.substr(1);
    return "!" + Operand(text);
}

const std::map<std::string, std::string> &BinaryOperators() {
    static const std::map<std::string, std::string> operators = {
        {"Add", "+"},           {"Subtract", "-"},        {"Multiply", "*"},  {"Divide", "/"},
        {"Percent", "%"},       {"Less", "<"},            {"Greater", ">"},   {"LessEqual", "<="},
        {"GreaterEqual", ">="}, {"EqualEqual", "=="},     {"NotEqual", "!="}, {"And", "&"},
        {"Or", "|"},            {"Xor", "^"},             {"BooleanAND", "&&"}, {"BooleanOR", "||"},
        {"BooleanXOR", "!="},   {"BooleanNAND", "nand"}};
    return operators;
}

// Conv_IntToFloat -> float.
const std::map<std::string, std::string> &NumericTypes() {
    static const std::map<std::string, std::string> types = {
        {"Float", "float"}, {"Double", "double"}, {"Int", "int32"}, {"Int64", "int64"}, {"Byte", "uint8"},
        {"Bool", "bool"}};
    return types;
}

// --- the class writer -------------------------------------------------------------------------

enum class Flow { Plain, Goto, Branch, Exit };

struct Statement {
    int offset = 0;
    Node node;
    bool parsed = true;
};

struct Line {
    int indent = 0;
    std::string text;
    // Position this line starts, for labels; -1 for closing braces and the like.
    int position = -1;
};

class Writer {
  public:
    Writer(const Type &entry, const TypeMap &types) : entry_(entry), types_(types) {}

    std::string Render() {
        const Function *ubergraph = nullptr;
        for (const Function &function : entry_.functions) {
            if (function.name.rfind("ExecuteUbergraph", 0) == 0 && !function.script.empty())
                ubergraph = &function;
        }
        if (ubergraph)
            ubergraph_ = Load(*ubergraph);

        std::ostringstream events, functions, resumes;
        std::set<int> covered;
        for (const Function &function : entry_.functions) {
            if (function.script.empty() || &function == ubergraph)
                continue;
            Loaded loaded = Load(function);
            if (const std::optional<int> entry = EventEntry(loaded, ubergraph != nullptr)) {
                events << EventText(function, loaded, *entry, covered);
                continue;
            }
            functions << FunctionText(function, loaded);
        }
        if (ubergraph) {
            for (const int resume : Resumes()) {
                if (!ubergraph_.index.count(resume))
                    continue;
                resumes << "\n// Resumes here after a latent call (Delay, async load...) reaches its end.\n"
                        << "void " << ClassName() << "::resume_" << Hex4(resume) << "()\n"
                        << Body(ubergraph_, ubergraph_.index.at(resume), {}, true, &covered, nullptr);
            }
        }
        std::ostringstream out;
        out << "// " << entry_.name << ": pseudo-code decompiled by urk-sdk from Blueprint bytecode. For reading;\n"
            << "// it is not compiled. Names are the engine's; UFoo::Bar(...) is a static call, resume_XXXX a latent\n"
            << "// continuation, L_XXXX a bytecode offset.\n";
        const std::string all = events.str() + functions.str() + resumes.str();
        out << all;
        if (ubergraph)
            out << Leftover(covered);
        return out.str();
    }

  private:
    struct Loaded {
        const Function *function = nullptr;
        std::vector<Statement> statements;
        std::map<int, int> index;
        // Occurrences of each Local() name in the whole function.
        std::map<std::string, int> uses;
    };

    // --- loading ---

    static void CountLocals(const Node &node, std::map<std::string, int> &uses) {
        if (node.Is("Local") && !node.args.empty())
            ++uses[node.args[0].text];
        for (const Node &arg : node.args)
            CountLocals(arg, uses);
    }

    static Loaded Load(const Function &function) {
        Loaded loaded;
        loaded.function = &function;
        for (const auto &[offset, text] : function.script) {
            Statement statement;
            statement.offset = offset;
            if (std::optional<Node> node = Parser(text).Parse()) {
                statement.node = std::move(*node);
            } else {
                statement.parsed = false;
                statement.node.tag = "Unparsed";
            }
            loaded.index[offset] = static_cast<int>(loaded.statements.size());
            CountLocals(statement.node, loaded.uses);
            loaded.statements.push_back(std::move(statement));
        }
        return loaded;
    }

    // An event stub: copies its parameters into the ubergraph frame and calls it with an entry offset.
    std::optional<int> EventEntry(const Loaded &loaded, bool hasUbergraph) const {
        if (!hasUbergraph)
            return std::nullopt;
        for (const Statement &statement : loaded.statements) {
            const Node &node = statement.node;
            if (node.Is("LetFrame") || node.Is("Tracepoint") || node.Is("Instrumentation") || node.Is("Nothing"))
                continue;
            if (node.Is("Call") && node.args.size() == 2 && node.args[1].Is("Int") &&
                SplitFunction(node.args[0].text).name.rfind("ExecuteUbergraph", 0) == 0)
                return std::stoi(node.args[1].Arg(0).text);
            return std::nullopt;
        }
        return std::nullopt;
    }

    std::set<int> Resumes() const {
        std::set<int> resumes;
        std::function<void(const Node &)> walk = [&](const Node &node) {
            if (node.Is("SkipOffset") && !node.args.empty())
                resumes.insert(std::stoi(node.args[0].text));
            for (const Node &arg : node.args)
                walk(arg);
        };
        for (const Statement &statement : ubergraph_.statements)
            walk(statement.node);
        return resumes;
    }

    // --- types ---

    const Type *Find(const std::string &package, const std::string &name) const {
        const auto found = types_.find(Key(package, name));
        return found == types_.end() ? nullptr : &found->second;
    }

    bool IsActor(const Type *type) const {
        for (int depth = 0; type && depth < 64; ++depth) {
            if (type->name == "Actor" && type->package == "/Script/Engine")
                return true;
            type = Find(type->superPackage, type->superName);
        }
        return false;
    }

    // UE's C++ spelling: AActor, UObject, FVector.
    std::string CppName(const std::string &name, const std::string &package) const {
        const Type *type = Find(package, name);
        if (type && type->isEnum)
            return Ident(name);
        if (type && type->isStruct)
            return "F" + Ident(name);
        return (IsActor(type) ? "A" : "U") + Ident(name);
    }

    std::string ClassName() const { return CppName(entry_.name, entry_.package); }

    std::string TypeText(const Shape &shape) const {
        static const std::map<std::string, std::string> plain = {
            {"bool", "bool"},     {"int8", "int8"},     {"int16", "int16"},   {"int32", "int32"},
            {"int64", "int64"},   {"uint16", "uint16"}, {"uint32", "uint32"}, {"uint64", "uint64"},
            {"float", "float"},   {"double", "double"}, {"name", "FName"},    {"string", "FString"},
            {"text", "FText"},    {"utf8 string", "FUtf8String"},             {"ansi string", "FAnsiString"},
            {"field path", "TFieldPath<FProperty>"}, {"multicast delegate", "FMulticastScriptDelegate"},
            {"sparse delegate", "FSparseDelegate"},   {"delegate", "FScriptDelegate"}};
        if (const auto found = plain.find(shape.kind); found != plain.end())
            return found->second;
        const auto element = [&](std::size_t i) {
            return i < shape.elements.size() ? TypeText(shape.elements[i]) : std::string("?");
        };
        const std::string inner = shape.inner.empty() ? "UObject" : CppName(shape.inner, shape.innerPackage);
        if (shape.kind == "byte")
            return shape.inner.empty() ? "uint8" : Ident(shape.inner);
        if (shape.kind == "enum")
            return Ident(shape.inner);
        if (shape.kind == "object")
            return inner + "*";
        if (shape.kind == "class")
            return "UClass*";
        if (shape.kind == "weak object")
            return "TWeakObjectPtr<" + inner + ">";
        if (shape.kind == "soft object")
            return "TSoftObjectPtr<" + inner + ">";
        if (shape.kind == "lazy object")
            return "TLazyObjectPtr<" + inner + ">";
        if (shape.kind == "interface")
            return "TScriptInterface<" + inner + ">";
        if (shape.kind == "struct")
            return shape.inner.empty() ? "FStruct" : "F" + Ident(shape.inner);
        if (shape.kind == "array")
            return "TArray<" + element(0) + ">";
        if (shape.kind == "set")
            return "TSet<" + element(0) + ">";
        if (shape.kind == "map")
            return "TMap<" + element(0) + ", " + element(1) + ">";
        if (shape.kind == "optional")
            return "TOptional<" + element(0) + ">";
        return "/*" + shape.kind + "*/ auto";
    }

    std::string Signature(const Function &function, const std::string &name) const {
        std::string result = "void";
        std::string params;
        for (const Member &parameter : function.parameters) {
            if (parameter.shape.flags & kReturnParm) {
                result = TypeText(parameter.shape);
                continue;
            }
            const bool out = (parameter.shape.flags & kOutParm) && !(parameter.shape.flags & kConstParm);
            params += (params.empty() ? "" : ", ") + TypeText(parameter.shape) + (out ? "& " : " ") +
                      Ident(parameter.name);
        }
        return result + ' ' + ClassName() + "::" + name + '(' + params + ')';
    }

    // The callee's parameters in call order (the return value is never passed).
    const Function *Callee(const FunctionPath &path) const {
        const Type *type = Find(path.package, path.owner);
        for (int depth = 0; type && depth < 64; ++depth) {
            for (const Function &function : type->functions) {
                if (function.name == path.name)
                    return &function;
            }
            type = Find(type->superPackage, type->superName);
        }
        return nullptr;
    }

    static const Member *Parameter(const Function *callee, std::size_t index) {
        if (!callee)
            return nullptr;
        std::size_t at = 0;
        for (const Member &parameter : callee->parameters) {
            if (parameter.shape.flags & kReturnParm)
                continue;
            if (at++ == index)
                return &parameter;
        }
        return nullptr;
    }

    static bool Written(const Member *parameter) {
        return parameter && (parameter->shape.flags & kOutParm) && !(parameter->shape.flags & kConstParm);
    }

    // --- expressions ---

    struct Scope {
        // Event parameters: the ubergraph frame copies named by the event's own parameters.
        std::map<std::string, std::string> renames;
        std::set<std::string> *locals = nullptr;
    };

    std::string Variable(const std::string &name, const Scope &scope) const {
        if (const auto found = scope.renames.find(name); found != scope.renames.end())
            return found->second;
        if (scope.locals)
            scope.locals->insert(name);
        return Ident(name);
    }

    std::vector<Text> Args(const Node &node, std::size_t from, const Scope &scope, const Function *callee,
                           int depth) const {
        std::vector<Text> args;
        for (std::size_t i = from; i < node.args.size(); ++i) {
            Text arg = Expr(node.args[i], scope, depth + 1);
            if (Written(Parameter(callee, i - from)))
                arg.s = "&" + Operand(arg);
            args.push_back(arg);
        }
        return args;
    }

    static std::string Joined(const std::vector<Text> &args) {
        std::string out;
        for (const Text &arg : args)
            out += (out.empty() ? "" : ", ") + arg.s;
        return out;
    }

    // Library calls with a C++ spelling: operators, numeric conversions, array methods.
    std::optional<Text> Idiom(const FunctionPath &path, const std::vector<Text> &args) const {
        const std::string &name = path.name;
        if (path.owner == "KismetMathLibrary") {
            const std::size_t underscore = name.find('_');
            const std::string stem = name.substr(0, underscore);
            if (args.size() == 2) {
                const auto found = BinaryOperators().find(stem);
                if (found != BinaryOperators().end() && found->second != "nand" &&
                    (underscore != std::string::npos || stem.rfind("Boolean", 0) == 0))
                    return Text{Operand(args[0]) + ' ' + found->second + ' ' + Operand(args[1]), true};
            }
            if (args.size() == 1 && name == "Not_PreBool")
                return Text{Negated(args[0]), false};
            if (args.size() == 1 && name.rfind("Conv_", 0) == 0) {
                const std::size_t to = name.find("To", 5);
                if (to != std::string::npos) {
                    const auto type = NumericTypes().find(name.substr(to + 2));
                    const auto from = NumericTypes().find(name.substr(5, to - 5));
                    if (type != NumericTypes().end() && from != NumericTypes().end())
                        return Text{"(" + type->second + ")" + Operand(args[0]), false};
                }
            }
        }
        if (path.owner == "KismetArrayLibrary" && !args.empty()) {
            const std::string array = Operand(args[0]);
            if (name == "Array_Length" && args.size() == 1)
                return Text{array + ".Num()", false};
            if (name == "Array_LastIndex" && args.size() == 1)
                return Text{array + ".Num() - 1", true};
            if (name == "Array_Clear" && args.size() == 1)
                return Text{array + ".Empty()", false};
            if (name == "Array_Get" && args.size() == 3)
                return Text{args[2].s.substr(args[2].s[0] == '&' ? 1 : 0) + " = " + array + '[' + args[1].s + ']',
                            true};
            if (name == "Array_Add" && args.size() == 2)
                return Text{array + ".Add(" + args[1].s + ')', false};
            if (name == "Array_Contains" && args.size() == 2)
                return Text{array + ".Contains(" + args[1].s + ')', false};
            if (name == "Array_Find" && args.size() == 2)
                return Text{array + ".Find(" + args[1].s + ')', false};
            if (name == "Array_Remove" && args.size() == 2)
                return Text{array + ".RemoveAt(" + args[1].s + ')', false};
            if (name == "Array_RemoveItem" && args.size() == 2)
                return Text{array + ".Remove(" + args[1].s + ')', false};
            if (name == "Array_IsValidIndex" && args.size() == 2)
                return Text{array + ".IsValidIndex(" + args[1].s + ')', false};
        }
        return std::nullopt;
    }

    // An object reached with no context: static functions by class, others on self.
    Text CallText(const Node &node, const Scope &scope, int depth, const std::string *on) const {
        const FunctionPath path = SplitFunction(node.Arg(0).text);
        const Function *callee = Callee(path);
        const std::vector<Text> args = Args(node, 1, scope, callee, depth);
        if (std::optional<Text> idiom = Idiom(path, args))
            return *idiom;
        const bool isStatic = callee ? (callee->flags & kFunctionStatic) != 0 : (on && on->empty());
        if (isStatic || (on && on->empty()))
            return {CppName(path.owner, path.package) + "::" + path.name + '(' + Joined(args) + ')', false};
        return {(on ? *on : std::string()) + path.name + '(' + Joined(args) + ')', false};
    }

    Text ObjectText(const Node &node) const {
        const std::string path = node.Arg(0).text;
        const std::string type = node.Arg(1).text;
        const std::string name = LastName(path);
        if (path.empty())
            return {"nullptr", false};
        if (name.rfind("Default__", 0) == 0)
            return {"GetDefault<" + CppName(name.substr(9), PackageOf(path)) + ">()", false};
        if (type == "Class" || (type.size() > 5 && type.compare(type.size() - 5, 5, "Class") == 0))
            return {CppName(name, PackageOf(path)) + "::StaticClass()", false};
        return {"FindObject<" + CppName(type, "/Script/CoreUObject") + ">(" + Literal(path) + ')', false};
    }

    Text StructText(const Node &node, const Scope &scope, int depth) const {
        const std::string path = node.Arg(0).text;
        const std::string name = LastName(path);
        if (name == "LatentActionInfo" && node.Arg(1).Is("SkipOffset"))
            return {"LatentInfo(resume_" + Hex4(std::stoi(node.Arg(1).Arg(0).text)) + ')', false};
        // Members in field order: the struct's own, then its super's (TFieldIterator).
        std::vector<std::string> names;
        for (const Type *type = Find(PackageOf(path), name); type && names.size() < 256;
             type = Find(type->superPackage, type->superName)) {
            for (const Member &member : type->members)
                names.push_back(member.name);
        }
        const bool named = names.size() == node.args.size() - 1;
        std::string out = "F" + Ident(name) + '{';
        for (std::size_t i = 1; i < node.args.size(); ++i) {
            out += i > 1 ? ", " : "";
            if (named)
                out += '.' + Ident(names[i - 1]) + " = ";
            out += Expr(node.args[i], scope, depth + 1).s;
        }
        return {out + '}', false};
    }

    Text TextConst(const Node &node) const {
        const std::string kind = node.Arg(0).text;
        const auto string = [&](std::size_t i) { return Literal(node.Arg(i).Arg(0).text); };
        if (kind == "loc")
            return {"NSLOCTEXT(" + string(3) + ", " + string(2) + ", " + string(1) + ')', false};
        if (kind == "inv")
            return {"INVTEXT(" + string(1) + ')', false};
        if (kind == "lit")
            return {"FText::FromString(" + string(1) + ')', false};
        if (kind == "table")
            return {"LOCTABLE(" + string(1) + ", " + string(2) + ')', false};
        return {"FText::GetEmpty()", false};
    }

    Text Expr(const Node &node, const Scope &scope, int depth) const {
        if (depth > kMaxNesting)
            return {"/*...*/", false};
        if (node.atom)
            return {node.quoted ? Literal(node.text) : node.text, false};
        const std::string &tag = node.tag;
        const auto arg = [&](std::size_t i) { return Expr(node.Arg(i), scope, depth + 1); };
        const auto list = [&](std::size_t from) {
            std::string out;
            for (std::size_t i = from; i < node.args.size(); ++i)
                out += (i > from ? ", " : "") + Expr(node.args[i], scope, depth + 1).s;
            return out;
        };
        if (tag == "Local" || tag == "Out" || tag == "Default" || tag == "Sparse")
            return {Variable(node.Arg(0).text, scope), false};
        if (tag == "Inst")
            return {Ident(node.Arg(0).text), false};
        if (tag == "PropConst")
            return {"FindFProperty(" + Literal(node.Arg(0).text) + ')', false};
        if (tag == "Self")
            return {"this", false};
        if (tag == "Int" || tag == "Int64" || tag == "UInt64" || tag == "Byte")
            return {node.Arg(0).text, false};
        if (tag == "Float" || tag == "Double")
            return {Real(node.Arg(0).text, tag == "Float"), false};
        if (tag == "True" || tag == "False")
            return {tag == "True" ? "true" : "false", false};
        if (tag == "Str")
            return {Literal(node.Arg(0).text), false};
        if (tag == "Name")
            return {"FName(" + Literal(node.Arg(0).text) + ')', false};
        if (tag == "Text")
            return TextConst(node);
        if (tag == "Obj")
            return ObjectText(node);
        if (tag == "NoObj" || tag == "NoIface")
            return {"nullptr", false};
        if (tag == "Vector" || tag == "Vector3f" || tag == "Rotator")
            return {"F" + tag + '(' + list(0) + ')', false};
        if (tag == "Transform" && node.args.size() == 10) {
            std::string q, t, s;
            for (std::size_t i = 0; i < 10; ++i)
                (i < 4 ? q : i < 7 ? t : s) += (i == 0 || i == 4 || i == 7 ? "" : ", ") + node.args[i].text;
            return {"FTransform(FQuat(" + q + "), FVector(" + t + "), FVector(" + s + "))", false};
        }
        if (tag == "Struct")
            return StructText(node, scope, depth);
        if (tag == "SkipOffset")
            return {"resume_" + Hex4(std::stoi(node.Arg(0).text)), false};
        if (tag == "Call")
            return CallText(node, scope, depth, nullptr);
        if (tag == "VCall")
            return {node.Arg(0).text + '(' + Joined(Args(node, 1, scope, nullptr, depth)) + ')', false};
        if (tag == "Broadcast") {
            std::string delegate = arg(1).s;
            std::string rest;
            for (std::size_t i = 2; i < node.args.size(); ++i)
                rest += (i > 2 ? ", " : "") + arg(i).s;
            return {delegate + ".Broadcast(" + rest + ')', false};
        }
        if (tag == "Context" || tag == "ContextFS" || tag == "ClassContext") {
            const Node &object = node.Arg(0);
            const Node &inner = node.Arg(1);
            // A static call through its class default object.
            if (inner.Is("Call") && object.Is("Obj") && LastName(object.Arg(0).text).rfind("Default__", 0) == 0) {
                const std::string none;
                return CallText(inner, scope, depth + 1, &none);
            }
            const Text target = Expr(object, scope, depth + 1);
            std::string prefix = target.s == "this" ? std::string() : Operand(target) + "->";
            if (tag == "ClassContext")
                prefix = Operand(target) + "->GetDefaultObject()->";
            if (inner.Is("Call"))
                return CallText(inner, scope, depth + 1, &prefix);
            const Text member = Expr(inner, scope, depth + 1);
            return {prefix + member.s, member.compound};
        }
        if (tag == "IfaceContext" || tag == "IfaceToObj" || tag == "FieldPath")
            return tag == "IfaceToObj" ? arg(1) : arg(0);
        if (tag == "Member")
            return {Operand(arg(1)) + '.' + Ident(node.Arg(0).text), false};
        if (tag == "GetByRef")
            return {Operand(arg(0)) + '[' + arg(1).s + ']', false};
        if (tag == "DynCast" || tag == "CrossIface" || tag == "ToIface")
            return {"Cast<" + CppName(LastName(node.Arg(0).text), PackageOf(node.Arg(0).text)) + ">(" + arg(1).s + ')',
                    false};
        if (tag == "MetaCast")
            return {"ClassCast<" + CppName(LastName(node.Arg(0).text), PackageOf(node.Arg(0).text)) + ">(" +
                        arg(1).s + ')',
                    false};
        if (tag == "Cast")
            return CastText(node, scope, depth);
        if (tag == "Soft")
            return {"FSoftObjectPath(" + arg(0).s + ')', false};
        if (tag == "BitField")
            return {node.Arg(1).text == "0" ? "false" : "true", false};
        if (tag == "ArrayConst" || tag == "SetConst")
            return {'{' + list(1) + '}', false};
        if (tag == "MapConst") {
            std::string out;
            for (std::size_t i = 1; i + 1 < node.args.size(); i += 2)
                out += (i > 1 ? ", " : "") + ('{' + arg(i).s + ", " + arg(i + 1).s + '}');
            return {'{' + out + '}', false};
        }
        if (tag == "InstDelegate")
            return {"FScriptDelegate(this, " + Literal(node.Arg(0).text) + ')', false};
        if (tag == "Switch" && node.args.size() >= 2) {
            const Text index = arg(0);
            std::string out;
            for (std::size_t i = 1; i + 2 < node.args.size(); i += 2)
                out += Operand(index) + " == " + Operand(arg(i)) + " ? " + Operand(arg(i + 1)) + " : ";
            return {out + Operand(arg(node.args.size() - 1)), !out.empty()};
        }
        if (node.args.empty())
            return {"/*" + tag + "*/", false};
        return {"/*" + tag + "*/(" + list(0) + ')', false};
    }

    // ECastToken: UE4 0x46.., UE5 0...; 4..0x12 are float/double width changes (numbering differs by UE5 version).
    Text CastText(const Node &node, const Scope &scope, int depth) const {
        const int kind = std::stoi(node.Arg(0).text);
        const Text value = Expr(node.Arg(1), scope, depth + 1);
        switch (kind) {
        case 0x46:
        case 0:
            return value;
        case 0x47:
        case 0x49:
        case 1:
        case 2:
            return {Operand(value) + " != nullptr", true};
        case 3:
            return {"(float)" + Operand(value), false};
        default:
            if (kind >= 4 && kind <= 0x12)
                return value;
            return {"/*cast " + std::to_string(kind) + "*/ " + value.s, value.compound};
        }
    }

    // --- statements ---

    std::string StatementText(const Node &node, const Scope &scope) const {
        const std::string &tag = node.tag;
        const auto arg = [&](std::size_t i) { return Expr(node.Arg(i), scope, 1); };
        if (tag == "Let" || tag == "LetBool" || tag == "LetObj" || tag == "LetWeak" || tag == "LetDelegate" ||
            tag == "LetMulticast")
            return arg(0).s + " = " + arg(1).s + ';';
        if (tag == "LetFrame")
            return Variable(node.Arg(0).text, scope) + " = " + arg(1).s + ';';
        if (tag == "Assert")
            return "check(" + arg(2).s + ");";
        if (tag == "SetArray" || tag == "SetSet") {
            std::string out;
            for (std::size_t i = tag == "SetSet" ? 2 : 1; i < node.args.size(); ++i)
                out += (out.empty() ? "" : ", ") + arg(i).s;
            return arg(0).s + " = {" + out + "};";
        }
        if (tag == "SetMap") {
            std::string out;
            for (std::size_t i = 2; i + 1 < node.args.size(); i += 2)
                out += (out.empty() ? "" : ", ") + ('{' + arg(i).s + ", " + arg(i + 1).s + '}');
            return arg(0).s + " = {" + out + "};";
        }
        if (tag == "BindDelegate")
            return arg(1).s + ".BindUFunction(" + arg(2).s + ", " + Literal(node.Arg(0).text) + ");";
        if (tag == "AddMulticast")
            return arg(0).s + ".Add(" + arg(1).s + ");";
        if (tag == "RemoveMulticast")
            return arg(0).s + ".Remove(" + arg(1).s + ");";
        if (tag == "ClearMulticast")
            return arg(0).s + ".Clear();";
        if (tag == "Unparsed")
            return "/* statement the renderer could not parse */";
        return Expr(node, scope, 0).s + ';';
    }

    static bool Silent(const Node &node) {
        return node.Is("Push") || node.Is("Nothing") || node.Is("NothingInt32") || node.Is("Tracepoint") ||
               node.Is("Instrumentation") || node.Is("EndParmValue");
    }

    // --- control flow ---

    // Positions are the layout: statements in offset order, then copies, each Plain/Branch falling into p + 1.
    struct Region {
        // Statement index per position.
        std::vector<int> order;
        // Statement index -> its first (original) position.
        std::map<int, int> position;
        // 0, or the copy number of a statement repeated because its flow stack differs by path.
        std::vector<int> copy;
        // Per Pop/PopIfNot: target offset, -1 end of thread, -2 differs by path (too many states to copy).
        std::vector<int> popTo;
        std::vector<Flow> flow;
        std::vector<int> target;
        std::vector<std::vector<int>> preds;
        std::vector<bool> exitTarget;
        std::vector<Node> nodes;
        std::vector<bool> removed;
    };

    static int Target(const Node &node) { return std::stoi(node.Arg(0).text); }

    // Walks every (statement, flow stack) state from start. A statement whose stack differs by path and that
    // leads to a Pop with differing targets (a Sequence pin reached from two places) is copied per stack.
    Region Analyze(const Loaded &loaded, int start) const {
        Region region;
        const int total = static_cast<int>(loaded.statements.size());
        const auto next = [&](int index) { return index + 1 < total ? index + 1 : -1; };
        const auto indexOf = [&](int offset) {
            const auto found = loaded.index.find(offset);
            return found == loaded.index.end() ? -1 : found->second;
        };

        using State = std::pair<int, std::vector<int>>;
        std::map<State, int> ids;
        std::vector<State> states;
        std::vector<int> fall, jump, popTo, work;
        bool overflow = false;
        const auto intern = [&](int index, std::vector<int> stack) {
            if (index < 0)
                return -1;
            State key{index, std::move(stack)};
            const auto found = ids.find(key);
            if (found != ids.end())
                return found->second;
            if (states.size() >= kMaxStates) {
                overflow = true;
                return -1;
            }
            const int id = static_cast<int>(states.size());
            ids.emplace(key, id);
            states.push_back(std::move(key));
            fall.push_back(-1);
            jump.push_back(-1);
            popTo.push_back(-1);
            work.push_back(id);
            return id;
        };
        intern(start, {});
        while (!work.empty()) {
            const int id = work.back();
            work.pop_back();
            const int index = states[static_cast<std::size_t>(id)].first;
            std::vector<int> stack = states[static_cast<std::size_t>(id)].second;
            const Node &node = loaded.statements[static_cast<std::size_t>(index)].node;
            int toFall = -1;
            int toJump = -1;
            if (node.Is("Jump")) {
                toJump = intern(indexOf(Target(node)), stack);
            } else if (node.Is("JumpIfNot")) {
                toFall = intern(next(index), stack);
                toJump = intern(indexOf(Target(node)), stack);
            } else if (node.Is("Push")) {
                std::vector<int> pushed = stack;
                if (pushed.size() < kMaxFlowDepth)
                    pushed.push_back(Target(node));
                toFall = intern(next(index), std::move(pushed));
            } else if (node.Is("Pop") || node.Is("PopIfNot")) {
                if (node.Is("PopIfNot"))
                    toFall = intern(next(index), stack);
                if (!stack.empty()) {
                    const int to = stack.back();
                    stack.pop_back();
                    popTo[static_cast<std::size_t>(id)] = to;
                    toJump = intern(indexOf(to), std::move(stack));
                }
            } else if (!(node.Is("Return") || node.Is("EndOfScript") || node.Is("ComputedJump"))) {
                toFall = intern(next(index), stack);
            }
            fall[static_cast<std::size_t>(id)] = toFall;
            jump[static_cast<std::size_t>(id)] = toJump;
        }
        const int stateCount = static_cast<int>(states.size());

        // Pops whose target differs by path, and the states that lead to one.
        std::map<int, std::set<int>> popTargets;
        std::map<int, int> firstState, stateCounts;
        for (int id = 0; id < stateCount; ++id) {
            const int index = states[static_cast<std::size_t>(id)].first;
            firstState.emplace(index, id);
            ++stateCounts[index];
            const Node &node = loaded.statements[static_cast<std::size_t>(index)].node;
            if (node.Is("Pop") || node.Is("PopIfNot"))
                popTargets[index].insert(popTo[static_cast<std::size_t>(id)]);
        }
        const auto differs = [&](int index) {
            const auto found = popTargets.find(index);
            return found != popTargets.end() && found->second.size() > 1;
        };
        std::set<int> split;
        if (!overflow) {
            std::vector<std::vector<int>> into(static_cast<std::size_t>(stateCount));
            for (int id = 0; id < stateCount; ++id) {
                for (const int to : {fall[static_cast<std::size_t>(id)], jump[static_cast<std::size_t>(id)]}) {
                    if (to >= 0)
                        into[static_cast<std::size_t>(to)].push_back(id);
                }
            }
            std::vector<bool> leads(static_cast<std::size_t>(stateCount), false);
            std::vector<int> back;
            for (int id = 0; id < stateCount; ++id) {
                if (differs(states[static_cast<std::size_t>(id)].first)) {
                    leads[static_cast<std::size_t>(id)] = true;
                    back.push_back(id);
                }
            }
            while (!back.empty()) {
                const int id = back.back();
                back.pop_back();
                for (const int from : into[static_cast<std::size_t>(id)]) {
                    if (!leads[static_cast<std::size_t>(from)]) {
                        leads[static_cast<std::size_t>(from)] = true;
                        back.push_back(from);
                    }
                }
            }
            for (int id = 0; id < stateCount; ++id) {
                const int index = states[static_cast<std::size_t>(id)].first;
                if (leads[static_cast<std::size_t>(id)] && stateCounts[index] > 1)
                    split.insert(index);
            }
            // Copies beyond twice the code read worse than the marker they replace.
            std::size_t copied = 0;
            for (const int index : split)
                copied += static_cast<std::size_t>(stateCounts[index] - 1);
            if (copied > 2 * firstState.size() + 32)
                split.clear();
        }
        // A split statement's original is the state the one before it falls into, so the text reads on.
        std::map<int, int> original;
        for (const auto &[index, id] : firstState) {
            int chosen = id;
            const auto before = original.find(index - 1);
            // The entry keeps its own (empty-stack) state: Body starts at the original.
            if (split.count(index) && index != start && before != original.end()) {
                const int into = fall[static_cast<std::size_t>(before->second)];
                if (into >= 0 && states[static_cast<std::size_t>(into)].first == index)
                    chosen = into;
            }
            original.emplace(index, chosen);
        }
        // One node per statement, or per state where split.
        const auto nodeOf = [&](int id) {
            if (id < 0)
                return -1;
            const int index = states[static_cast<std::size_t>(id)].first;
            return split.count(index) ? id : original.at(index);
        };

        // Layout: originals by offset, then copies grouped by stack; a jump where fall-through would land wrong.
        std::vector<int> layout;
        for (const auto &[index, id] : original)
            layout.push_back(id);
        std::vector<int> copies;
        for (int id = 0; id < stateCount; ++id) {
            if (split.count(states[static_cast<std::size_t>(id)].first) &&
                id != original.at(states[static_cast<std::size_t>(id)].first))
                copies.push_back(id);
        }
        std::sort(copies.begin(), copies.end(), [&](int a, int b) {
            const State &x = states[static_cast<std::size_t>(a)];
            const State &y = states[static_cast<std::size_t>(b)];
            return x.second != y.second ? x.second < y.second : x.first < y.first;
        });
        layout.insert(layout.end(), copies.begin(), copies.end());

        // Entries: a node id, or -(target node + 2) for an inserted jump.
        std::vector<int> entries;
        for (std::size_t i = 0; i < layout.size(); ++i) {
            const int id = layout[i];
            entries.push_back(id);
            const int to = nodeOf(fall[static_cast<std::size_t>(id)]);
            if (to >= 0 && (i + 1 >= layout.size() || layout[i + 1] != to))
                entries.push_back(-(to + 2));
        }
        const int count = static_cast<int>(entries.size());
        std::map<int, int> positionOfNode;
        for (int p = 0; p < count; ++p) {
            if (entries[static_cast<std::size_t>(p)] >= 0)
                positionOfNode[entries[static_cast<std::size_t>(p)]] = p;
        }
        const auto positionOf = [&](int node) {
            const auto found = positionOfNode.find(node);
            return found == positionOfNode.end() ? -1 : found->second;
        };

        region.order.assign(count, -1);
        region.copy.assign(count, 0);
        region.popTo.assign(count, -1);
        region.flow.assign(count, Flow::Plain);
        region.target.assign(count, -1);
        region.preds.assign(count, {});
        region.exitTarget.assign(count, false);
        region.removed.assign(count, false);
        std::map<int, int> copiesOf;
        std::vector<bool> falls(static_cast<std::size_t>(count), false);
        for (int p = 0; p < count; ++p) {
            const int entry = entries[static_cast<std::size_t>(p)];
            if (entry < 0) {
                // The inserted jump: to the node the previous one falls into.
                const int to = -entry - 2;
                const int index = states[static_cast<std::size_t>(to)].first;
                Node goTo;
                goTo.tag = "Jump";
                goTo.args.push_back(Node{"", std::to_string(loaded.statements[static_cast<std::size_t>(index)].offset), true});
                region.nodes.push_back(std::move(goTo));
                region.order[p] = region.order[p - 1];
                region.flow[p] = Flow::Goto;
                region.target[p] = positionOf(to);
                continue;
            }
            const int index = states[static_cast<std::size_t>(entry)].first;
            const Node &node = loaded.statements[static_cast<std::size_t>(index)].node;
            region.nodes.push_back(node);
            region.order[p] = index;
            const auto [seen, added] = copiesOf.emplace(index, 0);
            if (added)
                region.position[index] = p;
            else
                region.copy[p] = ++seen->second;
            falls[static_cast<std::size_t>(p)] = fall[static_cast<std::size_t>(entry)] >= 0;
            if (node.Is("Jump")) {
                region.flow[p] = Flow::Goto;
                region.target[p] = positionOf(nodeOf(jump[static_cast<std::size_t>(entry)]));
            } else if (node.Is("JumpIfNot")) {
                region.flow[p] = Flow::Branch;
                region.target[p] = positionOf(nodeOf(jump[static_cast<std::size_t>(entry)]));
            } else if (node.Is("Pop") || node.Is("PopIfNot")) {
                // Unexplored states (too many) leave every pop unknown.
                const int to = overflow || (differs(index) && !split.count(index)) ? -2
                                                                                   : popTo[static_cast<std::size_t>(entry)];
                const bool branch = node.Is("PopIfNot");
                region.popTo[p] = to;
                region.flow[p] = to >= 0 ? (branch ? Flow::Branch : Flow::Goto) : (branch ? Flow::Branch : Flow::Exit);
                region.target[p] = to >= 0 ? positionOf(nodeOf(jump[static_cast<std::size_t>(entry)])) : -1;
                region.exitTarget[p] = to < 0;
            } else if (node.Is("Return") || node.Is("EndOfScript") || node.Is("ComputedJump")) {
                region.flow[p] = Flow::Exit;
            }
        }
        for (int p = 0; p < count; ++p) {
            if ((region.flow[p] == Flow::Plain || region.flow[p] == Flow::Branch) && falls[static_cast<std::size_t>(p)] &&
                p + 1 < count)
                region.preds[p + 1].push_back(p);
            if ((region.flow[p] == Flow::Goto || region.flow[p] == Flow::Branch) && region.target[p] >= 0)
                region.preds[region.target[p]].push_back(p);
        }
        return region;
    }

    // Compiler temporaries read once, right after they are set, move into the reading statement.
    void Inline(const Loaded &loaded, Region &region) const {
        const int count = static_cast<int>(region.order.size());
        for (int p = count - 1; p >= 0; --p) {
            Node &let = region.nodes[p];
            if (!(let.Is("Let") || let.Is("LetBool") || let.Is("LetObj") || let.Is("LetWeak")) ||
                !let.Arg(0).Is("Local") || region.flow[p] != Flow::Plain)
                continue;
            const std::string name = let.Arg(0).Arg(0).text;
            if ((name.rfind("CallFunc_", 0) != 0 && name.rfind("K2Node_", 0) != 0) || loaded.uses.at(name) != 2)
                continue;
            int q = p + 1;
            while (q < count && region.removed[q])
                ++q;
            if (q >= count)
                continue;
            bool linear = true;
            for (int r = p + 1; r <= q && linear; ++r)
                linear = region.preds[r].size() == 1 && region.preds[r][0] == r - 1 &&
                         region.order[r] == region.order[r - 1] + 1;
            if (!linear)
                continue;
            if (Replace(region.nodes[q], name, let.Arg(1), nullptr, 0))
                region.removed[p] = true;
        }
    }

    // Replaces the one read of Local(name); never a write (a Let's target or an out argument).
    bool Replace(Node &node, const std::string &name, const Node &value, const Function *callee, int depth) const {
        if (depth > 256)
            return false;
        const bool let = node.Is("Let") || node.Is("LetBool") || node.Is("LetObj") || node.Is("LetWeak");
        const Function *calls = node.Is("Call") ? Callee(SplitFunction(node.Arg(0).text)) : nullptr;
        for (std::size_t i = 0; i < node.args.size(); ++i) {
            Node &arg = node.args[i];
            if (arg.Is("Local") && arg.Arg(0).text == name) {
                if ((let && i == 0) || (node.Is("Call") && i > 0 && Written(Parameter(calls, i - 1))) ||
                    node.Is("Member"))
                    return false;
                arg = value;
                return true;
            }
            if (Replace(arg, name, value, calls, depth + 1))
                return true;
        }
        (void)callee;
        return false;
    }

    struct Loop {
        int head = -1;
        int exit = -1;
    };

    class Emitter {
      public:
        Emitter(const Writer &writer, Region &region, const Scope &scope, const std::string &returnName)
            : w_(writer), r_(region), scope_(scope), returnName_(returnName) {}

        std::vector<Line> lines;
        std::set<int> labels;

        void Range(int from, int to, int indent, std::vector<Loop> &loops, int depth) {
            int p = from;
            while (p < to) {
                if (depth > kMaxNesting) {
                    Flat(p, to, indent);
                    return;
                }
                const int back = LoopEnd(p, to);
                if (back >= 0 && !(loops.size() && loops.back().head == p)) {
                    EmitLoop(p, back, indent, loops, depth);
                    p = back + 1;
                    continue;
                }
                p = One(p, to, indent, loops, depth);
            }
        }

        // Ends the text so far unless it already leaves.
        void Stop(int indent) {
            for (auto line = lines.rbegin(); line != lines.rend(); ++line) {
                if (line->position >= 0)
                    continue;
                const std::string &text = line->text;
                if (text == "return;" || text.rfind("goto ", 0) == 0)
                    return;
                break;
            }
            Say(indent, "return;");
        }

        // An explicit jump back to where the text started.
        void Jump(int target, int indent) {
            const std::vector<Loop> none;
            Say(indent, Goto(target, none, -1));
        }

      private:
        void Mark(int p, int indent) { lines.push_back({indent, "", p}); }
        void Say(int indent, const std::string &text) { lines.push_back({indent, text, -1}); }

        // from: the jumping position, to tell a missing target from the end of the thread.
        std::string Goto(int target, const std::vector<Loop> &loops, int from) {
            if (target < 0 && from >= 0 && !r_.exitTarget[from] &&
                (r_.nodes[from].Is("Jump") || r_.nodes[from].Is("JumpIfNot")))
                return "goto L_" + Hex4(Target(r_.nodes[from])) + ";  // no statement starts there";
            if (target < 0)
                return "return;";
            if (!loops.empty() && target == loops.back().head)
                return "continue;";
            if (!loops.empty() && target == loops.back().exit)
                return "break;";
            if (ExitAt(target))
                return "return;";
            labels.insert(target);
            return "goto " + w_.LabelOf(r_, target) + ';';
        }

        // A plain return (no value) at a position: jumping there is returning.
        bool ExitAt(int p) const {
            return p >= 0 && p < static_cast<int>(r_.flow.size()) && r_.flow[p] == Flow::Exit &&
                   (r_.nodes[p].Is("EndOfScript") || (r_.nodes[p].Is("Return") && r_.nodes[p].Arg(0).Is("Nothing")) ||
                    (r_.nodes[p].Is("Pop") && r_.exitTarget[p]));
        }

        // Every predecessor of [from, to) comes from inside, or is one of the allowed entries into from.
        bool SingleEntry(int from, int to, int entry) const {
            for (int p = from; p < to; ++p) {
                for (const int pred : r_.preds[p]) {
                    if (pred >= from && pred < to)
                        continue;
                    if (p == from && pred == entry)
                        continue;
                    return false;
                }
            }
            return true;
        }

        // The last back edge to p within [p, to), if its body has no other way in.
        int LoopEnd(int p, int to) const {
            int last = -1;
            for (const int pred : r_.preds[p]) {
                if (pred >= p && pred < to && (r_.flow[pred] == Flow::Goto || r_.flow[pred] == Flow::Branch) &&
                    r_.target[pred] == p)
                    last = std::max(last, pred);
            }
            if (last < 0)
                return -1;
            for (int q = p + 1; q <= last; ++q) {
                for (const int pred : r_.preds[q]) {
                    if (pred < p || pred > last)
                        return -1;
                }
            }
            return last;
        }

        void EmitLoop(int head, int last, int indent, std::vector<Loop> &loops, int depth) {
            Mark(head, indent);
            loops.push_back({head, last + 1});
            // The condition's temporaries were inlined into the branch.
            int test = head;
            while (test < last && r_.removed[test])
                ++test;
            const bool guarded = r_.flow[test] == Flow::Branch && r_.target[test] == last + 1 &&
                                 r_.flow[last] == Flow::Goto && r_.target[last] == head;
            if (guarded) {
                for (int p = head + 1; p <= test; ++p)
                    Mark(p, indent + 1);
                Say(indent, "while (" + Condition(test).s + ") {");
                Range(test + 1, last, indent + 1, loops, depth + 1);
            } else {
                Say(indent, "while (true) {");
                Range(head, last + 1, indent + 1, loops, depth + 1);
            }
            // A trailing "continue;" says nothing.
            if (!lines.empty() && lines.back().text == "continue;")
                lines.pop_back();
            Say(indent, "}");
            loops.pop_back();
        }

        Text Condition(int p) const {
            const Node &node = r_.nodes[p];
            return w_.Expr(node.Is("PopIfNot") ? node.Arg(0) : node.Arg(1), scope_, 1);
        }

        // One statement or one structured if; returns the next position.
        int One(int p, int to, int indent, std::vector<Loop> &loops, int depth) {
            Mark(p, indent);
            if (r_.removed[p])
                return p + 1;
            const Node &node = r_.nodes[p];
            switch (r_.flow[p]) {
            case Flow::Plain: {
                if (Silent(node))
                    return p + 1;
                // "ReturnValue = x; return;" reads as "return x;".
                if (!returnName_.empty() && node.Is("Let") && node.Arg(0).Is("Out") &&
                    node.Arg(0).Arg(0).text == returnName_ && p + 1 < static_cast<int>(r_.flow.size()) &&
                    (ExitAt(p + 1) || (r_.flow[p + 1] == Flow::Goto && ExitAt(r_.target[p + 1]))) &&
                    r_.preds[p + 1].size() == 1) {
                    Say(indent, "return " + w_.Expr(node.Arg(1), scope_, 1).s + ';');
                    Mark(p + 1, indent);
                    return p + 2;
                }
                Say(indent, w_.StatementText(node, scope_));
                return p + 1;
            }
            case Flow::Exit:
                if (node.Is("ComputedJump"))
                    Say(indent, "goto *" + w_.Expr(node.Arg(0), scope_, 1).s + ";  // entry point dispatch");
                else if (node.Is("Return") && !node.Arg(0).Is("Nothing"))
                    Say(indent, "return " + w_.Expr(node.Arg(0), scope_, 1).s + ';');
                else if (node.Is("Pop") && r_.popTo[p] == -2)
                    Say(indent, "return;  // flow stack differs by path");
                else if (p + 1 < to || !node.Is("EndOfScript"))
                    Say(indent, "return;");
                return p + 1;
            case Flow::Goto: {
                const int target = r_.target[p];
                if (target == p + 1)
                    return p + 1;
                Say(indent, Goto(target, loops, p));
                return p + 1;
            }
            case Flow::Branch:
                return Branch(p, to, indent, loops, depth);
            }
            return p + 1;
        }

        int Branch(int p, int to, int indent, std::vector<Loop> &loops, int depth) {
            const int target = r_.target[p];
            const Text text = Condition(p);
            const std::string &condition = text.s;
            if (target > p + 1 && target <= to && SingleEntry(p + 1, target, p)) {
                const int last = target - 1;
                const int elseEnd = r_.flow[last] == Flow::Goto ? r_.target[last] : -1;
                if (last > p + 1 && elseEnd > target && elseEnd <= to && SingleEntry(target, elseEnd, p) &&
                    !(loops.size() && (elseEnd == loops.back().exit || elseEnd == loops.back().head))) {
                    Say(indent, "if (" + condition + ") {");
                    Range(p + 1, last, indent + 1, loops, depth + 1);
                    Mark(last, indent + 1);
                    Say(indent, "} else {");
                    Range(target, elseEnd, indent + 1, loops, depth + 1);
                    Say(indent, "}");
                    return elseEnd;
                }
                Say(indent, "if (" + condition + ") {");
                Range(p + 1, target, indent + 1, loops, depth + 1);
                Say(indent, "}");
                return target;
            }
            Say(indent, "if (" + Negated(text) + ") " + Goto(r_.exitTarget[p] ? -1 : target, loops, p));
            return p + 1;
        }

        // Label form: no structure, every jump a goto.
        void Flat(int from, int to, int indent) {
            const std::vector<Loop> none;
            for (int p = from; p < to; ++p) {
                Mark(p, indent);
                if (r_.removed[p] || Silent(r_.nodes[p]))
                    continue;
                if (r_.flow[p] == Flow::Goto)
                    Say(indent, Goto(r_.target[p], none, p));
                else if (r_.flow[p] == Flow::Branch)
                    Say(indent, "if (" + Negated(Condition(p)) + ") " +
                                    Goto(r_.exitTarget[p] ? -1 : r_.target[p], none, p));
                else if (r_.flow[p] == Flow::Exit)
                    Say(indent, "return;");
                else
                    Say(indent, w_.StatementText(r_.nodes[p], scope_));
            }
        }

        const Writer &w_;
        Region &r_;
        const Scope &scope_;
        std::string returnName_;
    };

    int OffsetOf(const Region &region, int p) const { return offsets_.at(region.order[p]); }
    // L_0210, or L_0210_1 for its first copy.
    std::string LabelOf(const Region &region, int p) const {
        std::string label = "L_" + Hex4(OffsetOf(region, p));
        return region.copy[p] ? label + '_' + std::to_string(region.copy[p]) : label;
    }

    // The body of one entry into loaded, braces included.
    std::string Body(const Loaded &loaded, int start, const Scope &scopeIn, bool ubergraph, std::set<int> *covered,
                     const Function *function) {
        // Ubergraph entries are often a lone Jump; start where it leads.
        for (int hops = 0; hops < 8 && start >= 0 && start < static_cast<int>(loaded.statements.size()); ++hops) {
            const Node &node = loaded.statements[static_cast<std::size_t>(start)].node;
            const auto found = node.Is("Jump") ? loaded.index.find(Target(node)) : loaded.index.end();
            if (found == loaded.index.end())
                break;
            if (covered)
                covered->insert(start);
            start = found->second;
        }
        Region region = Analyze(loaded, start);
        offsets_.clear();
        for (const Statement &statement : loaded.statements)
            offsets_.push_back(statement.offset);
        if (covered) {
            for (const int index : region.order)
                covered->insert(index);
        }
        Inline(loaded, region);
        std::set<std::string> used;
        Scope scope = scopeIn;
        scope.locals = &used;
        std::string returnName;
        if (function) {
            for (const Member &parameter : function->parameters) {
                if (parameter.shape.flags & kReturnParm)
                    returnName = parameter.name;
            }
        }
        Emitter emitter(*this, region, scope, returnName);
        std::vector<Loop> loops;
        // Execution starts at the entry; code before it (reached by jumps) follows.
        const int entry = region.position.count(start) ? region.position.at(start) : 0;
        const int count = static_cast<int>(region.order.size());
        emitter.Range(entry, count, 1, loops, 0);
        if (entry > 0) {
            emitter.Stop(1);
            emitter.Range(0, entry, 1, loops, 0);
            if (region.flow[entry - 1] == Flow::Plain || region.flow[entry - 1] == Flow::Branch)
                emitter.Jump(entry, 1);
        }

        std::ostringstream out;
        out << "{\n";
        // Locals still named (not inlined), declared as the function's frame has them.
        if (function && !ubergraph) {
            for (const Member &local : function->locals) {
                if (used.count(local.name))
                    out << "    " << TypeText(local.shape) << ' ' << Ident(local.name) << ";\n";
            }
        }
        // A return closing the body says nothing.
        std::vector<Line> &lines = emitter.lines;
        for (auto last = lines.rbegin(); last != lines.rend(); ++last) {
            if (last->position >= 0)
                continue;
            if (last->indent == 1 && last->text == "return;")
                lines.erase(std::next(last).base());
            break;
        }
        const bool partial = loaded.function && loaded.function->failedAt >= 0;
        for (const Line &line : emitter.lines) {
            if (line.position >= 0) {
                if (emitter.labels.count(line.position)) {
                    emitter.labels.erase(line.position);
                    out << std::string(static_cast<std::size_t>(std::max(0, line.indent - 1)) * 4, ' ')
                        << LabelOf(region, line.position) << ":\n";
                }
                continue;
            }
            out << std::string(static_cast<std::size_t>(line.indent) * 4, ' ') << line.text << '\n';
        }
        if (partial)
            out << "    // Decoding stopped at 0x" << Hex4(loaded.function->failedAt) << ": "
                << loaded.function->failure << ". What follows it is missing.\n";
        out << "}\n";
        return out.str();
    }

    std::string FunctionText(const Function &function, const Loaded &loaded) {
        std::ostringstream out;
        out << '\n' << Signature(function, function.name) << '\n' << Body(loaded, 0, {}, false, nullptr, &function);
        return out.str();
    }

    std::string EventText(const Function &function, const Loaded &stub, int entry, std::set<int> &covered) {
        Scope scope;
        for (const Statement &statement : stub.statements) {
            const Node &node = statement.node;
            if (node.Is("LetFrame") && node.Arg(1).Is("Local"))
                scope.renames[node.Arg(0).text] = Ident(node.Arg(1).Arg(0).text);
        }
        std::ostringstream out;
        out << '\n' << Signature(function, function.name) << '\n';
        if (!ubergraph_.index.count(entry))
            return out.str() + "{\n    // Enters ExecuteUbergraph at 0x" + Hex4(entry) + ", which was not dumped.\n}\n";
        out << Body(ubergraph_, ubergraph_.index.at(entry), scope, true, &covered, nullptr);
        return out.str();
    }

    // A statement on its own: jumps as gotos, flow-stack operations spelled out.
    std::string LabelForm(const Node &node, const Scope &scope) const {
        const auto label = [&]() { return "L_" + Hex4(Target(node)); };
        if (node.Is("Jump"))
            return "goto " + label() + ';';
        if (node.Is("JumpIfNot"))
            return "if (" + Negated(Expr(node.Arg(1), scope, 1)) + ") goto " + label() + ';';
        if (node.Is("Push"))
            return "push_flow(" + label() + ");";
        if (node.Is("Pop"))
            return "pop_flow();";
        if (node.Is("PopIfNot"))
            return "if (" + Negated(Expr(node.Arg(0), scope, 1)) + ") pop_flow();";
        if (node.Is("Return"))
            return node.Arg(0).Is("Nothing") ? "return;" : "return " + Expr(node.Arg(0), scope, 1).s + ';';
        return StatementText(node, scope);
    }

    // Ubergraph code no event or resume point reaches.
    std::string Leftover(const std::set<int> &covered) {
        std::vector<int> rest;
        for (int i = 0; i < static_cast<int>(ubergraph_.statements.size()); ++i) {
            const Node &node = ubergraph_.statements[i].node;
            if (!covered.count(i) && !Silent(node) && !node.Is("EndOfScript") && !node.Is("ComputedJump") &&
                !(node.Is("Return") && node.Arg(0).Is("Nothing")))
                rest.push_back(i);
        }
        if (rest.empty())
            return {};
        std::ostringstream out;
        out << "\n// Ubergraph code no event reaches from this class (a child class's event, or dead code):\n"
            << "void " << ClassName() << "::" << ubergraph_.function->name << "_rest()\n{\n";
        const Scope scope;
        for (const int i : rest)
            out << "    /* 0x" << Hex4(ubergraph_.statements[i].offset) << " */ "
                << LabelForm(ubergraph_.statements[i].node, scope) << '\n';
        out << "}\n";
        return out.str();
    }

    const Type &entry_;
    const TypeMap &types_;
    Loaded ubergraph_;
    std::vector<int> offsets_;
};

} // namespace

std::string RenderBlueprint(const Type &entry, const TypeMap &types) {
    if (entry.isStruct || entry.isEnum)
        return {};
    const bool any = std::any_of(entry.functions.begin(), entry.functions.end(),
                                 [](const Function &function) { return !function.script.empty(); });
    if (!any)
        return {};
    try {
        return Writer(entry, types).Render();
    } catch (const std::exception &error) {
        // A malformed record costs this class its pseudo-code, not the whole SDK.
        return "// " + entry.name + ": pseudo-code could not be written (" + error.what() + ").\n";
    }
}

} // namespace UnrealTypeCodegen
