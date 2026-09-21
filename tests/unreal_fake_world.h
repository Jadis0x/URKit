#pragma once

// A synthetic Unreal process for the calibration tests: a name pool, an object
// array holding the classes and structs the calibration anchors on, and the
// FField chains hanging off the engine structs.
//
// Everything is laid out at the offsets named here, so a test asserts that the
// calibration recovered a layout it was never told. The two configuration flags
// cover the engine's own layout changes: properties stopped being objects in
// UE4.25, and FFieldVariant lost its trailing bool in UE5.1.1.

#include "src/unreal/unreal_property_offsets.h"
#include "tests/unreal_fake_image.h"

#include <cstring>

namespace UnrealTest {

using URK::Unreal::Address;

inline constexpr Address kWorldImageBase = 0x30000000;

// Image regions. The name pool's block table is followed by a long null run, so
// everything else starts past it.
inline constexpr Address kPoolAddress = 0x0000;
// Past the block table's null run, or the pool scan would count it as a block.
inline constexpr Address kVTableAddress = 0x3000;
inline constexpr Address kBlockZero = 0x4000;
inline constexpr Address kBlockOne = 0x5000;
inline constexpr Address kArrayAddress = 0x6000;
inline constexpr Address kItemsAddress = 0x6100;
inline constexpr Address kObjectsAddress = 0x8000;
inline constexpr Address kFieldsAddress = 0x14000;
inline constexpr Address kFieldClassesAddress = 0x1C000;
inline constexpr std::size_t kWorldImageSize = 0x1D000;

// The UObject header the world is built to.
inline constexpr std::int32_t kFlagsOffset = 0x08;
inline constexpr std::int32_t kIndexOffset = 0x0C;
inline constexpr std::int32_t kClassOffset = 0x10;
inline constexpr std::int32_t kNameOffset = 0x18;
inline constexpr std::int32_t kOuterOffset = 0x20;

// The UField and UStruct layout the calibration must recover.
inline constexpr std::int32_t kNextOffset = 0x28;
inline constexpr std::int32_t kSuperOffset = 0x30;
inline constexpr std::int32_t kChildrenOffset = 0x38;
inline constexpr std::int32_t kChildPropertiesOffset = 0x40;
inline constexpr std::int32_t kPropertiesSizeOffset = 0x48;
inline constexpr std::int32_t kMinAlignmentOffset = 0x4C;
inline constexpr std::int32_t kCastFlagsOffset = 0x100;
inline constexpr std::int32_t kObjectStride = 0x180;

// FField, up to Owner. What follows depends on Owner's width.
inline constexpr std::int32_t kFieldClassOffset = 0x08;
inline constexpr std::int32_t kFieldOwnerOffset = 0x10;
inline constexpr std::int32_t kFieldStride = 0x80;
inline constexpr std::int32_t kFieldClassCastFlagsOffset = 0x10;
inline constexpr std::int32_t kFieldClassStride = 0x40;

struct WorldConfig {
    // Before UE4.25 properties were UObjects and led the Children chain.
    bool legacyProperties = false;
    // Before UE5.1.1 FFieldVariant carried a trailing bool, pushing Next out.
    bool wideFieldOwner = false;
};

// Where each FField member lands once Owner's width is known.
struct FieldLayout {
    std::int32_t next = 0;
    std::int32_t name = 0;
    std::int32_t flags = 0;
    std::int32_t arrayDim = 0;
    std::int32_t elementSize = 0;
    std::int32_t propertyFlags = 0;
    std::int32_t offsetInternal = 0;

    static FieldLayout For(bool wideFieldOwner) {
        FieldLayout layout;
        layout.next = kFieldOwnerOffset + (wideFieldOwner ? 0x10 : 0x08);
        layout.name = layout.next + 0x08;
        layout.flags = layout.name + 0x08;
        layout.arrayDim = layout.flags + 0x08;
        layout.elementSize = layout.arrayDim + 0x04;
        layout.propertyFlags = layout.arrayDim + 0x08;
        layout.offsetInternal = layout.arrayDim + 0x10;
        return layout;
    }
};

using URK::Unreal::kCastFlagActor;
using URK::Unreal::kCastFlagByteProperty;
using URK::Unreal::kCastFlagClass;
using URK::Unreal::kCastFlagField;
using URK::Unreal::kCastFlagFunction;
using URK::Unreal::kCastFlagIntProperty;
using URK::Unreal::kCastFlagNumericProperty;
using URK::Unreal::kCastFlagProperty;
using URK::Unreal::kCastFlagStruct;

inline constexpr std::uint64_t kCastFlagScriptStruct = 0x10;
inline constexpr std::uint64_t kCastFlagFloatProperty = 0x100;

inline constexpr std::uint64_t kPodMemberFlags =
    URK::Unreal::kPropertyFlagEdit | URK::Unreal::kPropertyFlagZeroConstructor | URK::Unreal::kPropertyFlagSaveGame |
    URK::Unreal::kPropertyFlagIsPlainOldData | URK::Unreal::kPropertyFlagNoDestructor |
    URK::Unreal::kPropertyFlagHasGetValueTypeHash;

class World {
  public:
    explicit World(WorldConfig config)
        : config_(config), fieldLayout_(FieldLayout::For(config.wideFieldOwner)),
          image_(kWorldImageBase, kWorldImageSize) {
        BuildNamePool();
        Populate();
        FinishObjectArray();
    }

    const FakeImage &Image() const { return image_; }
    Address PoolAddress() const { return image_.At(kPoolAddress); }
    Address ArrayAddress() const { return image_.At(kArrayAddress); }
    const FieldLayout &Fields() const { return fieldLayout_; }
    const WorldConfig &Config() const { return config_; }

    static URK::Unreal::ObjectArrayLayout ArrayLayout() {
        URK::Unreal::ObjectArrayLayout layout;
        layout.chunked = false;
        layout.fixed = {.objectsOffset = 0x00, .maxObjectsOffset = 0x08, .numObjectsOffset = 0x0C};
        layout.item = {.pointerOffset = 0x00, .stride = 0x08};
        return layout;
    }

    static URK::Unreal::ObjectOffsets HeaderOffsets() {
        URK::Unreal::ObjectOffsets offsets;
        offsets.flags = kFlagsOffset;
        offsets.index = kIndexOffset;
        offsets.classPointer = kClassOffset;
        offsets.outer = kOuterOffset;
        offsets.name = kNameOffset;
        return offsets;
    }

  private:
    // Entries pack at the pool's two-byte stride, so a comparison index is the
    // byte offset halved.
    std::uint32_t AddName(const char *text) {
        const auto length = static_cast<std::uint16_t>(std::strlen(text));
        const Address entry = kBlockZero + static_cast<Address>(nameCursor_);
        image_.Put<std::uint16_t>(entry, static_cast<std::uint16_t>(length << 1));
        image_.PutBytes(entry + 2, text, length);
        const auto index = static_cast<std::uint32_t>(nameCursor_ / 2);
        nameCursor_ = (nameCursor_ + 2 + length + 1) & ~1;
        return index;
    }

    void BuildNamePool() {
        image_.Put<std::int32_t>(kPoolAddress + 0x00, 1);
        image_.Put<std::int32_t>(kPoolAddress + 0x04, 0x400);
        image_.Put<Address>(kPoolAddress + 0x10, image_.At(kBlockZero));
        image_.Put<Address>(kPoolAddress + 0x18, image_.At(kBlockOne));
        image_.Put<Address>(kVTableAddress, image_.Base());

        // The markers the pool calibration keys on must come first.
        AddName("None");
        AddName("ByteProperty");
        AddName("/Script/CoreUObject");
        image_.Put<std::uint16_t>(kBlockOne, static_cast<std::uint16_t>(4 << 1));
        image_.PutBytes(kBlockOne + 2, "Spar", 4);
    }

    Address ObjectAt(std::int32_t index) const {
        return image_.At(kObjectsAddress + static_cast<Address>(index) * kObjectStride);
    }
    Address FieldAt(std::int32_t index) const {
        return image_.At(kFieldsAddress + static_cast<Address>(index) * kFieldStride);
    }
    Address FieldClassAt(std::int32_t index) const {
        return image_.At(kFieldClassesAddress + static_cast<Address>(index) * kFieldClassStride);
    }

    std::int32_t AddObject(const char *name, std::uint64_t castFlags) {
        const std::int32_t index = objectCount_++;
        image_.Put<Address>(kItemsAddress + static_cast<Address>(index) * 8, ObjectAt(index));

        const Address local = kObjectsAddress + static_cast<Address>(index) * kObjectStride;
        image_.Put<Address>(local, image_.At(kVTableAddress));
        image_.Put<std::uint32_t>(local + kFlagsOffset, 0x43);
        image_.Put<std::int32_t>(local + kIndexOffset, index);
        image_.Put<std::uint32_t>(local + kNameOffset, AddName(name));
        image_.Put<std::uint64_t>(local + kCastFlagsOffset, castFlags);
        return index;
    }

    std::int32_t AddFieldClass(std::uint64_t castFlags) {
        const std::int32_t index = fieldClassCount_++;
        const Address local = kFieldClassesAddress + static_cast<Address>(index) * kFieldClassStride;
        image_.Put<std::uint64_t>(local + kFieldClassCastFlagsOffset, castFlags);
        return index;
    }

    // Appends one property to a struct's FField chain.
    std::int32_t AddField(std::int32_t owner, std::int32_t fieldClass, const char *name, std::int32_t memberOffset,
                          std::int32_t elementSize, std::uint64_t propertyFlags) {
        const std::int32_t index = fieldCount_++;
        const Address local = kFieldsAddress + static_cast<Address>(index) * kFieldStride;

        image_.Put<Address>(local, image_.At(kVTableAddress));
        image_.Put<Address>(local + kFieldClassOffset, FieldClassAt(fieldClass));
        image_.Put<Address>(local + kFieldOwnerOffset, ObjectAt(owner));
        if (config_.wideFieldOwner)
            image_.Put<std::uint8_t>(local + kFieldOwnerOffset + 0x08, 1);

        image_.Put<std::uint32_t>(local + fieldLayout_.name, AddName(name));
        image_.Put<std::int32_t>(local + fieldLayout_.arrayDim, 1);
        image_.Put<std::int32_t>(local + fieldLayout_.elementSize, elementSize);
        image_.Put<std::uint64_t>(local + fieldLayout_.propertyFlags, propertyFlags);
        image_.Put<std::int32_t>(local + fieldLayout_.offsetInternal, memberOffset);
        return index;
    }

    void LinkObject(std::int32_t object, std::int32_t fieldOffset, std::int32_t target) {
        const Address local = kObjectsAddress + static_cast<Address>(object) * kObjectStride;
        image_.Put<Address>(local + fieldOffset, target < 0 ? 0 : ObjectAt(target));
    }

    void LinkFieldChain(std::int32_t owner, const std::vector<std::int32_t> &fields) {
        if (fields.empty())
            return;
        const Address ownerLocal = kObjectsAddress + static_cast<Address>(owner) * kObjectStride;
        image_.Put<Address>(ownerLocal + kChildPropertiesOffset, FieldAt(fields.front()));
        for (std::size_t i = 0; i + 1 < fields.size(); ++i) {
            const Address local = kFieldsAddress + static_cast<Address>(fields[i]) * kFieldStride;
            image_.Put<Address>(local + fieldLayout_.next, FieldAt(fields[i + 1]));
        }
    }

    void SetSize(std::int32_t object, std::int32_t size, std::int32_t alignment) {
        const Address local = kObjectsAddress + static_cast<Address>(object) * kObjectStride;
        image_.Put<std::int32_t>(local + kPropertiesSizeOffset, size);
        image_.Put<std::int32_t>(local + kMinAlignmentOffset, alignment);
    }

    void Populate() {
        constexpr std::uint64_t kClassCast = kCastFlagField | kCastFlagStruct | kCastFlagClass;
        constexpr std::uint64_t kStructCast = kCastFlagField | kCastFlagStruct | kCastFlagScriptStruct;
        constexpr std::uint64_t kFunctionCast = kCastFlagField | kCastFlagStruct | kCastFlagFunction;

        AddObject("/Script/Engine", 0);
        const std::int32_t field = AddObject("Field", kCastFlagField);
        const std::int32_t structure = AddObject("Struct", kCastFlagField | kCastFlagStruct);
        const std::int32_t classObject = AddObject("Class", kClassCast);
        AddObject("Object", kClassCast);
        AddObject("Actor", kCastFlagActor);

        const std::int32_t color = AddObject("Color", kStructCast);
        const std::int32_t guid = AddObject("Guid", kStructCast);
        const std::int32_t transform = AddObject("Transform", kStructCast);
        const std::int32_t vector = AddObject("Vector", kStructCast);
        AddObject("Vector4", kStructCast);

        const std::int32_t controller = AddObject("Controller", kClassCast);
        const std::int32_t playerController = AddObject("PlayerController", kClassCast);
        const std::int32_t unPossess = AddObject("UnPossess", kFunctionCast);
        const std::int32_t wasReleased = AddObject("WasInputKeyJustReleased", kFunctionCast);
        const std::int32_t possess = AddObject("Possess", kFunctionCast);
        const std::int32_t isKeyDown = AddObject("IsInputKeyDown", kFunctionCast);

        const std::int32_t systemLibrary = AddObject("KismetSystemLibrary", kClassCast);
        const std::int32_t stringLibrary = AddObject("KismetStringLibrary", kClassCast);
        const std::int32_t printString = AddObject("PrintString", kFunctionCast);
        const std::int32_t concat = AddObject("Concat_StrStr", kFunctionCast);
        const std::int32_t delay = AddObject("Delay", kFunctionCast);
        const std::int32_t length = AddObject("Len", kFunctionCast);

        LinkObject(structure, kSuperOffset, field);
        LinkObject(classObject, kSuperOffset, structure);
        LinkObject(playerController, kSuperOffset, controller);

        SetSize(color, 0x04, 0x04);
        SetSize(guid, 0x10, 0x04);
        SetSize(transform, 0x30, 0x10);
        SetSize(controller, 0x400, 0x08);
        SetSize(playerController, 0x520, 0x08);
        SetSize(vector, 0x0C, 0x04);

        // Functions stay in the Children chain in both systems.
        LinkObject(controller, kChildrenOffset, unPossess);
        LinkObject(unPossess, kNextOffset, possess);
        LinkObject(unPossess, kOuterOffset, controller);
        LinkObject(possess, kOuterOffset, controller);

        LinkObject(playerController, kChildrenOffset, wasReleased);
        LinkObject(wasReleased, kNextOffset, isKeyDown);
        LinkObject(wasReleased, kOuterOffset, playerController);
        LinkObject(isKeyDown, kOuterOffset, playerController);

        LinkObject(systemLibrary, kChildrenOffset, printString);
        LinkObject(printString, kNextOffset, delay);
        LinkObject(printString, kOuterOffset, systemLibrary);
        LinkObject(delay, kOuterOffset, systemLibrary);

        LinkObject(stringLibrary, kChildrenOffset, concat);
        LinkObject(concat, kNextOffset, length);
        LinkObject(concat, kOuterOffset, stringLibrary);
        LinkObject(length, kOuterOffset, stringLibrary);

        if (config_.legacyProperties) {
            const std::int32_t x = AddObject("X", kCastFlagField);
            const std::int32_t a = AddObject("A", kCastFlagField);
            const std::int32_t r = AddObject("R", kCastFlagField);
            LinkObject(x, kOuterOffset, vector);
            LinkObject(a, kOuterOffset, guid);
            LinkObject(r, kOuterOffset, color);
            LinkObject(vector, kChildrenOffset, x);
            LinkObject(guid, kChildrenOffset, a);
            LinkObject(color, kChildrenOffset, r);
            return;
        }

        // FGuid is declared as four int32s, FColor as four bytes in B, G, R, A
        // order, FVector as three floats. That declaration order is what every
        // property anchor relies on.
        const std::int32_t intClass =
            AddFieldClass(kCastFlagField | kCastFlagProperty | kCastFlagNumericProperty | kCastFlagIntProperty);
        const std::int32_t byteClass =
            AddFieldClass(kCastFlagField | kCastFlagProperty | kCastFlagNumericProperty | kCastFlagByteProperty);
        const std::int32_t floatClass =
            AddFieldClass(kCastFlagField | kCastFlagProperty | kCastFlagNumericProperty | kCastFlagFloatProperty);

        constexpr std::uint64_t kVisibleFlags = kPodMemberFlags | URK::Unreal::kPropertyFlagBlueprintVisible;

        LinkFieldChain(guid, {AddField(guid, intClass, "A", 0x00, 4, kPodMemberFlags),
                              AddField(guid, intClass, "B", 0x04, 4, kPodMemberFlags),
                              AddField(guid, intClass, "C", 0x08, 4, kPodMemberFlags),
                              AddField(guid, intClass, "D", 0x0C, 4, kPodMemberFlags)});

        LinkFieldChain(color, {AddField(color, byteClass, "B", 0x00, 1, kVisibleFlags),
                               AddField(color, byteClass, "G", 0x01, 1, kVisibleFlags),
                               AddField(color, byteClass, "R", 0x02, 1, kVisibleFlags),
                               AddField(color, byteClass, "A", 0x03, 1, kVisibleFlags)});

        LinkFieldChain(vector, {AddField(vector, floatClass, "X", 0x00, 4, kVisibleFlags),
                                AddField(vector, floatClass, "Y", 0x04, 4, kVisibleFlags),
                                AddField(vector, floatClass, "Z", 0x08, 4, kVisibleFlags)});
    }

    void FinishObjectArray() {
        image_.Put<Address>(kArrayAddress + 0x00, image_.At(kItemsAddress));
        image_.Put<std::int32_t>(kArrayAddress + 0x08, 0x40);
        image_.Put<std::int32_t>(kArrayAddress + 0x0C, objectCount_);
    }

    WorldConfig config_;
    FieldLayout fieldLayout_;
    FakeImage image_;
    std::int32_t nameCursor_ = 0;
    std::int32_t objectCount_ = 0;
    std::int32_t fieldCount_ = 0;
    std::int32_t fieldClassCount_ = 0;
};

} // namespace UnrealTest
