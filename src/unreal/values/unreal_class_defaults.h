#pragma once

// What a class's defaults set: default-object values that differ from the parent's, and the
// Blueprint's component templates where they differ from the parent's template. Text for the type dump.

#include "unreal/values/unreal_places.h"

#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace URK::Unreal {

class ClassDefaults {
  public:
    ClassDefaults(UnrealEngine &engine, Places &places) : engine_(&engine), places_(&places) {}

    // "D<TAB>path<TAB>value" and "K<TAB>component<TAB>class<TAB>path<TAB>value" lines. Game thread.
    std::string Describe(Address classObject);

  private:
    struct Scope {
        // What the line starts with: "D\t", or "K\t<component>\t<class>\t".
        std::string record;
        // Objects whose subobjects print as their name, not a path.
        Address owner = kNullAddress;
        Address baseOwner = kNullAddress;
    };
    struct Field {
        PropertyInfo info;
        std::string name;
    };
    // A struct's or class's own fields, kept while it keeps its name and outer.
    struct Fields {
        std::uint64_t name = 0;
        Address outer = kNullAddress;
        std::vector<Field> list;
    };

    const std::vector<Field> &FieldsOf(Address structObject);
    // obj's members (its class and supers) against base's; members below baseClass against zero.
    void CompareObject(Address object, Address base, Address baseClass, const std::string &prefix, int depth,
                       const Scope &scope, std::string &out);
    void CompareFields(const std::uint8_t *value, const std::uint8_t *base, Address owner, Address baseOwner,
                       const std::vector<Field> &fields, const std::string &prefix, int depth, const Scope &scope,
                       std::string &out);
    void CompareValue(const PlaceTarget &value, const PlaceTarget *base, const std::string &path, int depth,
                      const Scope &scope, std::string &out);
    std::optional<std::string> Render(const PlaceTarget &value, int depth, const Scope &scope);
    std::string ObjectText(Address object, const Scope &scope) const;
    std::optional<PlaceTarget> MemberOf(Address object, const char *name) const;
    std::optional<PlaceTarget> Step(const PlaceTarget &from, std::int32_t kind, std::int32_t index,
                                    const char *name = nullptr) const;
    void Emit(const Scope &scope, const std::string &path, const std::string &text, std::string &out);
    // A class's own component templates by variable name: construction script nodes, then overrides.
    std::vector<std::pair<std::string, Address>> TemplatesOf(Address classObject);
    void Component(Address classObject, Address object, const std::string &name, std::string &out);

    UnrealEngine *engine_;
    Places *places_;
    std::unordered_map<Address, Fields> fields_;
    std::set<Address> visited_;
    std::size_t lines_ = 0;
};

} // namespace URK::Unreal
