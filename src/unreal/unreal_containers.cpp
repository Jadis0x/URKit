#include "unreal_containers.h"
#include "unreal_owned_values.h"

#include <algorithm>
#include <cstring>

namespace URK::Unreal {
namespace {

template <typename T> T Load(const std::uint8_t *at) {
    T value{};
    std::memcpy(&value, at, sizeof(T));
    return value;
}

template <typename T> void Store(std::uint8_t *at, T value) { std::memcpy(at, &value, sizeof(T)); }

constexpr std::int32_t kIndexNone = -1;
// Evidence, in bits, before a hash rule is trusted: each element whose stored
// bucket a rule predicts is worth log2(bucket count).
constexpr int kTrustedBits = 24;

std::int32_t Align(std::int32_t value, std::int32_t alignment) {
    return alignment <= 1 ? value : (value + alignment - 1) / alignment * alignment;
}

// FStructBuilder, as the engine lays out a TPair or a TSetElement.
struct StructBuilder {
    std::int32_t end = 0;
    std::int32_t alignment = 1;
    std::int32_t Add(std::int32_t size, std::int32_t align) {
        const std::int32_t at = Align(end, align);
        end = at + size;
        alignment = std::max(alignment, align);
        return at;
    }
    std::int32_t Size() const { return Align(end, alignment); }
};

std::uint32_t RoundUpToPowerOfTwo(std::uint32_t value) {
    std::uint32_t result = 1;
    while (result < value)
        result <<= 1;
    return result;
}

// TSparseSetAllocator::GetNumberOfHashBuckets with the engine defaults.
std::int32_t HashBucketsFor(std::int32_t elements) {
    if (elements >= 4)
        return static_cast<std::int32_t>(RoundUpToPowerOfTwo(static_cast<std::uint32_t>(elements) / 2 + 8));
    return 1;
}

bool BitAt(const std::uint8_t *words, std::int32_t index) {
    return (Load<std::uint32_t>(words + (index / 32) * 4) >> (index % 32)) & 1u;
}

void SetBit(std::uint8_t *words, std::int32_t index, bool value) {
    std::uint8_t *word = words + (index / 32) * 4;
    std::uint32_t bits = Load<std::uint32_t>(word);
    const std::uint32_t mask = 1u << (index % 32);
    Store<std::uint32_t>(word, value ? (bits | mask) : (bits & ~mask));
}

// --- hash rules: the formulas UE's GetTypeHash has used from 4.25 to 5.8 ---------

enum Rule {
    kZeroExtended,
    kSignExtended,
    kInt64,
    kFloatBits,
    kDoubleBits,
    kNameIndexPlusNumberCombined, // HashCombineFast(Index + Number, Index)
    kNameIndexCombined,           // HashCombineFast(Index, Index)
    kNameIndexPlusNumber,         // Index + Number
    kPointerMurmur,               // GetTypeHash64(MurmurFinalize64(p))
    kPointerShifted,              // GetTypeHash64(p >> 4)
    kPointerShiftedCombined,      // HashCombine(GetTypeHash64(p >> 4), 0)
    kStringCaseless,              // FCrc::Strihash_DEPRECATED
    kRuleCount,
};

std::uint32_t TypeHash64(std::uint64_t value) {
    return static_cast<std::uint32_t>(value) + static_cast<std::uint32_t>(value >> 32) * 23u;
}

std::uint64_t MurmurFinalize64(std::uint64_t hash) {
    hash ^= hash >> 33;
    hash *= 0xff51afd7ed558ccdull;
    hash ^= hash >> 33;
    hash *= 0xc4ceb9fe1a85ec53ull;
    hash ^= hash >> 33;
    return hash;
}

std::uint32_t HashCombineFast(std::uint32_t a, std::uint32_t b) { return a ^ (b + 0x9e3779b9u + (a << 6) + (a >> 2)); }

std::uint32_t HashCombine(std::uint32_t a, std::uint32_t c) {
    std::uint32_t b = 0x9e3779b9u;
    a += b;
    a -= b; a -= c; a ^= (c >> 13);
    b -= c; b -= a; b ^= (a << 8);
    c -= a; c -= b; c ^= (b >> 13);
    a -= b; a -= c; a ^= (c >> 12);
    b -= c; b -= a; b ^= (a << 16);
    c -= a; c -= b; c ^= (b >> 5);
    a -= b; a -= c; a ^= (c >> 3);
    b -= c; b -= a; b ^= (a << 10);
    c -= a; c -= b; c ^= (b >> 15);
    return c;
}

const std::uint32_t *CrcTable() {
    static std::uint32_t table[256];
    static const bool built = [] {
        for (std::uint32_t i = 0; i < 256; ++i) {
            std::uint32_t crc = i;
            for (int k = 0; k < 8; ++k)
                crc = (crc & 1) ? (crc >> 1) ^ 0xEDB88320u : crc >> 1;
            table[i] = crc;
        }
        return true;
    }();
    (void)built;
    return table;
}

std::vector<Rule> RulesFor(const PropertyInfo &key) {
    switch (key.kind) {
    case PropertyKind::Bool:
    case PropertyKind::Byte:
    case PropertyKind::UInt16:
    case PropertyKind::UInt32:
        return {kZeroExtended};
    case PropertyKind::Int8:
    case PropertyKind::Int16:
    case PropertyKind::Int32:
        return {kSignExtended};
    case PropertyKind::Enum:
        return key.elementSize == 8 ? std::vector<Rule>{kInt64} : std::vector<Rule>{kZeroExtended, kSignExtended};
    case PropertyKind::Int64:
    case PropertyKind::UInt64:
        return {kInt64};
    case PropertyKind::Float:
        return {kFloatBits};
    case PropertyKind::Double:
        return {kDoubleBits};
    case PropertyKind::Name:
        return {kNameIndexPlusNumberCombined, kNameIndexCombined, kNameIndexPlusNumber};
    case PropertyKind::Object:
    case PropertyKind::Class:
        return {kPointerMurmur, kPointerShifted, kPointerShiftedCombined};
    case PropertyKind::String:
        return {kStringCaseless};
    default:
        return {};
    }
}

} // namespace

bool Containers::Fail(std::string why) {
    failure_ = std::move(why);
    return false;
}

// --- arrays ------------------------------------------------------------------------

bool Containers::ArrayInsert(const PropertyInfo &inner, std::uint8_t *array, std::int32_t index, std::int32_t count) {
    auto *data = Load<std::uint8_t *>(array);
    const std::int32_t num = Load<std::int32_t>(array + 8);
    const std::int32_t max = Load<std::int32_t>(array + 12);
    const std::int32_t size = inner.elementSize;
    const std::int32_t alignment = values_->AlignmentOf(inner);
    if (num < 0 || max < num || (num > 0 && !data) || size <= 0 || alignment <= 0)
        return Fail("the array is not in a state it can be changed from");
    if (index < 0 || index > num || count < 1 || num + count > kMaxContainerElements)
        return Fail("the index or count is out of range");

    const std::size_t width = static_cast<std::size_t>(size);
    if (num + count > max) {
        // TArray's growth leaves slack; any capacity the allocation holds is valid.
        const std::int32_t wanted = std::max(num + count, num + num / 2 + 4);
        const std::optional<EngineCalls::Block> block = engine_->Allocate(static_cast<std::size_t>(wanted) * width,
                                                                          static_cast<std::size_t>(alignment));
        if (!block)
            return Fail(engine_->Failure());
        // Elements are trivially relocatable: TArray moves them bytewise too.
        if (index > 0)
            std::memcpy(block->data, data, static_cast<std::size_t>(index) * width);
        if (num > index)
            std::memcpy(block->data + static_cast<std::size_t>(index + count) * width,
                        data + static_cast<std::size_t>(index) * width, static_cast<std::size_t>(num - index) * width);
        if (data && !engine_->Free(data)) {
            engine_->Free(block->data);
            return Fail(engine_->Failure());
        }
        data = block->data;
        Store<std::uint8_t *>(array, data);
        Store<std::int32_t>(array + 12, static_cast<std::int32_t>(block->bytes / width));
    } else {
        if (num > index)
            std::memmove(data + static_cast<std::size_t>(index + count) * width,
                         data + static_cast<std::size_t>(index) * width, static_cast<std::size_t>(num - index) * width);
        std::memset(data + static_cast<std::size_t>(index) * width, 0, static_cast<std::size_t>(count) * width);
    }
    Store<std::int32_t>(array + 8, num + count);
    for (std::int32_t i = 0; i < count; ++i) {
        if (!owned_->Initialize(inner, data + static_cast<std::size_t>(index + i) * width))
            return Fail(owned_->Failure());
    }
    return true;
}

bool Containers::ArrayRemove(const PropertyInfo &inner, std::uint8_t *array, std::int32_t index, std::int32_t count) {
    auto *data = Load<std::uint8_t *>(array);
    const std::int32_t num = Load<std::int32_t>(array + 8);
    const std::int32_t max = Load<std::int32_t>(array + 12);
    const std::int32_t size = inner.elementSize;
    if (num < 0 || max < num || (num > 0 && !data) || size <= 0)
        return Fail("the array is not in a state it can be changed from");
    if (index < 0 || count < 1 || index + count > num)
        return Fail("the index or count is out of range");
    const std::size_t width = static_cast<std::size_t>(size);
    for (std::int32_t i = 0; i < count; ++i) {
        if (!owned_->Destroy(inner, data + static_cast<std::size_t>(index + i) * width))
            return Fail(owned_->Failure());
    }
    if (num > index + count)
        std::memmove(data + static_cast<std::size_t>(index) * width,
                     data + static_cast<std::size_t>(index + count) * width,
                     static_cast<std::size_t>(num - index - count) * width);
    Store<std::int32_t>(array + 8, num - count);
    return true;
}

// --- sets and maps: layout ----------------------------------------------------------

std::optional<SetLayout> Containers::LayoutOf(const PropertyInfo &container) {
    if (container.kind != PropertyKind::Set && container.kind != PropertyKind::Map)
        return std::nullopt;
    {
        std::lock_guard lock(layoutMutex_);
        if (const auto found = layouts_.find(container.field); found != layouts_.end())
            return found->second;
    }
    std::optional<SetLayout> result;
    const auto remember = [&](std::string why) {
        std::lock_guard lock(layoutMutex_);
        layouts_[container.field] = result;
        if (!result)
            failure_ = std::move(why);
        return result;
    };
    if (container.elementSize != SetFields::kSize)
        return remember("a set or map here is " + std::to_string(container.elementSize) + " bytes, not the " +
                        std::to_string(SetFields::kSize) + " of the sparse layout");

    SetLayout layout;
    layout.isMap = container.kind == PropertyKind::Map;
    const std::optional<PropertyInfo> key = values_->Describe(container.inner);
    const std::optional<PropertyInfo> value =
        layout.isMap ? values_->Describe(container.valueInner) : std::optional<PropertyInfo>(PropertyInfo{});
    if (!key || !value || key->elementSize <= 0 || (layout.isMap && value->elementSize <= 0))
        return remember("the element properties did not resolve");
    layout.key = *key;
    layout.value = *value;
    const std::int32_t keyAlign = values_->AlignmentOf(*key);
    const std::int32_t valueAlign = layout.isMap ? values_->AlignmentOf(*value) : 1;
    if (keyAlign <= 0 || valueAlign <= 0)
        return remember("an element's alignment is unknown");

    // TPair<Key, Value>, then TSparseSetElement, then the sparse array slot.
    StructBuilder pair;
    pair.Add(key->elementSize, keyAlign);
    if (layout.isMap)
        layout.valueOffset = pair.Add(value->elementSize, valueAlign);
    const std::int32_t elementSize = layout.isMap ? pair.Size() : key->elementSize;
    const std::int32_t elementAlign = layout.isMap ? pair.alignment : keyAlign;
    StructBuilder element;
    element.Add(elementSize, elementAlign);
    layout.hashNextIdOffset = element.Add(4, 4);
    layout.hashIndexOffset = element.Add(4, 4);
    const std::int32_t setElementSize = element.Size();
    layout.alignment = std::max(element.alignment, 4);
    layout.stride = Align(std::max(setElementSize, 8), layout.alignment);

    // The engine computed the same layout when it linked the property; it must be
    // there, word for word, after the element properties.
    const PropertyTailOffsets &tail = values_->Tail();
    const std::int32_t after = layout.isMap ? tail.mapValue : tail.setElement;
    if (after == kOffsetNotFound)
        return remember("the element property offsets were not measured");
    const std::int32_t expected[] = {layout.hashNextIdOffset, layout.hashIndexOffset, setElementSize, layout.alignment,
                                     std::max(setElementSize, 8)};
    bool found = false;
    constexpr std::int32_t kSearch = 0x40;
    for (std::int32_t at = after + 8; at + 20 <= after + 8 + kSearch && !found; at += 4) {
        bool same = true;
        for (int i = 0; i < 5 && same; ++i)
            same = finder_->Reader().ReadInt32(container.field + at + i * 4).value_or(-1) == expected[i];
        if (same && layout.isMap) {
            // FScriptMapLayout keeps ValueOffset just before the set layout (after
            // a zero KeyOffset/ElementOffset in older builds).
            same = finder_->Reader().ReadInt32(container.field + at - 4).value_or(-1) == layout.valueOffset ||
                   finder_->Reader().ReadInt32(container.field + at - 8).value_or(-1) == layout.valueOffset;
        }
        found = same;
    }
    if (!found)
        return remember("the engine's stored set layout differs from the computed one");
    if (layout.isMap && value->offset != layout.valueOffset)
        return remember("the map value property is not at the computed value offset");
    result = layout;
    return remember({});
}

// --- sets and maps: structure ---------------------------------------------------------

std::uint8_t *Containers::Bits(std::uint8_t *set) {
    auto *secondary = Load<std::uint8_t *>(set + SetFields::kBitsSecondary);
    return secondary ? secondary : set + SetFields::kBitsInline;
}

std::int32_t *Containers::Buckets(std::uint8_t *set) {
    auto *secondary = Load<std::int32_t *>(set + SetFields::kHashSecondary);
    return secondary ? secondary : reinterpret_cast<std::int32_t *>(set + SetFields::kHashInline);
}

std::int32_t Containers::Count(const std::uint8_t *set) {
    return Load<std::int32_t>(set + SetFields::kNum) - Load<std::int32_t>(set + SetFields::kNumFree);
}

std::vector<std::int32_t> Containers::Slots(const std::uint8_t *set) {
    std::vector<std::int32_t> slots;
    const std::int32_t num = Load<std::int32_t>(set + SetFields::kNum);
    const auto *secondary = Load<const std::uint8_t *>(set + SetFields::kBitsSecondary);
    const std::uint8_t *bits = secondary ? secondary : set + SetFields::kBitsInline;
    for (std::int32_t i = 0; i < num; ++i) {
        if (BitAt(bits, i))
            slots.push_back(i);
    }
    return slots;
}

std::uint8_t *Containers::Element(const SetLayout &layout, std::uint8_t *set, std::int32_t slot) {
    const std::int32_t num = Load<std::int32_t>(set + SetFields::kNum);
    if (slot < 0 || slot >= num)
        return nullptr;
    auto *secondary = Load<std::uint8_t *>(set + SetFields::kBitsSecondary);
    if (!BitAt(secondary ? secondary : set + SetFields::kBitsInline, slot))
        return nullptr;
    return Load<std::uint8_t *>(set + SetFields::kData) + static_cast<std::size_t>(slot) * layout.stride;
}

bool Containers::Validate(const SetLayout &layout, const std::uint8_t *constSet) {
    auto *set = const_cast<std::uint8_t *>(constSet);
    const MemoryReader &reader = finder_->Reader();
    auto *data = Load<std::uint8_t *>(set + SetFields::kData);
    const std::int32_t num = Load<std::int32_t>(set + SetFields::kNum);
    const std::int32_t max = Load<std::int32_t>(set + SetFields::kMax);
    const auto *bitsSecondary = Load<std::uint8_t *>(set + SetFields::kBitsSecondary);
    const std::int32_t numBits = Load<std::int32_t>(set + SetFields::kNumBits);
    const std::int32_t maxBits = Load<std::int32_t>(set + SetFields::kMaxBits);
    const std::int32_t firstFree = Load<std::int32_t>(set + SetFields::kFirstFree);
    const std::int32_t numFree = Load<std::int32_t>(set + SetFields::kNumFree);
    const auto *hashSecondary = Load<std::uint8_t *>(set + SetFields::kHashSecondary);
    const std::int32_t hashSize = Load<std::int32_t>(set + SetFields::kHashSize);

    if (num < 0 || max < num || num > kMaxContainerElements || (num > 0 && !data))
        return Fail("the set's element array is inconsistent");
    if (num > 0 && !reader.Readable(reinterpret_cast<Address>(data), static_cast<std::size_t>(num) * layout.stride))
        return Fail("the set's element array is not readable");
    if (numBits != num || maxBits < numBits || (!bitsSecondary && numBits > SetFields::kBitsInlineWords * 32))
        return Fail("the set's allocation flags disagree with its elements");
    if (bitsSecondary && !reader.Readable(reinterpret_cast<Address>(bitsSecondary),
                                          static_cast<std::size_t>((numBits + 31) / 32) * 4))
        return Fail("the set's allocation flags are not readable");
    if (numFree < 0 || numFree > num)
        return Fail("the set's free list count is out of range");
    if (hashSize < 0 || (hashSize & (hashSize - 1)) != 0 || (hashSize > 1) != (hashSecondary != nullptr))
        return Fail("the set's hash is inconsistent");
    if (hashSecondary && !reader.Readable(reinterpret_cast<Address>(hashSecondary),
                                          static_cast<std::size_t>(hashSize) * 4))
        return Fail("the set's hash is not readable");

    const std::uint8_t *bits = Bits(set);
    std::int32_t allocated = 0;
    for (std::int32_t i = 0; i < num; ++i)
        allocated += BitAt(bits, i) ? 1 : 0;
    if (allocated != num - numFree)
        return Fail("the set's allocation flags disagree with its free list");
    if (allocated > 0 && hashSize == 0)
        return Fail("the set holds elements but no hash");

    // The free list: numFree links, each a free slot, linked both ways.
    std::int32_t at = firstFree;
    std::int32_t previous = kIndexNone;
    for (std::int32_t step = 0; step < numFree; ++step) {
        if (at < 0 || at >= num || BitAt(bits, at))
            return Fail("the set's free list reaches an occupied or invalid slot");
        const std::uint8_t *link = data + static_cast<std::size_t>(at) * layout.stride;
        if (Load<std::int32_t>(link) != previous)
            return Fail("the set's free list is not linked both ways");
        previous = at;
        at = Load<std::int32_t>(link + 4);
    }

    // Every bucket chain: occupied slots stored with that bucket, and every
    // occupied slot reached exactly once.
    const std::int32_t *buckets = Buckets(set);
    std::vector<bool> seen(static_cast<std::size_t>(num), false);
    std::int32_t chained = 0;
    for (std::int32_t bucket = 0; bucket < hashSize; ++bucket) {
        for (std::int32_t id = buckets[bucket]; id != kIndexNone;) {
            if (id < 0 || id >= num || !BitAt(bits, id) || seen[static_cast<std::size_t>(id)] || chained >= allocated)
                return Fail("the set's hash chains are inconsistent");
            const std::uint8_t *element = data + static_cast<std::size_t>(id) * layout.stride;
            if (Load<std::int32_t>(element + layout.hashIndexOffset) != bucket)
                return Fail("an element's stored bucket disagrees with its chain");
            seen[static_cast<std::size_t>(id)] = true;
            ++chained;
            id = Load<std::int32_t>(element + layout.hashNextIdOffset);
        }
    }
    if (chained != allocated)
        return Fail("not every element is in the set's hash");
    Learn(layout, set);
    return true;
}

// --- sets and maps: hashing -------------------------------------------------------------

std::string Containers::KeySignature(const PropertyInfo &key) const {
    return std::to_string(static_cast<int>(key.kind)) + ":" + std::to_string(key.elementSize);
}

std::optional<std::uint32_t> Containers::Hash(int rule, const PropertyInfo &key, const std::uint8_t *value) const {
    const auto integer = [&]() -> std::int64_t {
        switch (key.elementSize) {
        case 1:
            return key.kind == PropertyKind::Int8 ? Load<std::int8_t>(value) : Load<std::uint8_t>(value);
        case 2:
            return key.kind == PropertyKind::Int16 ? Load<std::int16_t>(value) : Load<std::uint16_t>(value);
        case 4:
            return Load<std::int32_t>(value);
        default:
            return Load<std::int64_t>(value);
        }
    };
    switch (rule) {
    case kZeroExtended: {
        if (key.kind == PropertyKind::Bool)
            return (value[key.boolLayout.byteOffset] & key.boolLayout.fieldMask) ? 1u : 0u;
        std::uint32_t bits = 0;
        std::memcpy(&bits, value, static_cast<std::size_t>(std::min(key.elementSize, 4)));
        return bits;
    }
    case kSignExtended:
        return static_cast<std::uint32_t>(static_cast<std::int32_t>(integer()));
    case kInt64:
        return TypeHash64(Load<std::uint64_t>(value));
    case kFloatBits:
        return Load<std::uint32_t>(value);
    case kDoubleBits:
        return TypeHash64(Load<std::uint64_t>(value));
    case kNameIndexPlusNumberCombined:
        return HashCombineFast(Load<std::uint32_t>(value) + Load<std::uint32_t>(value + 4), Load<std::uint32_t>(value));
    case kNameIndexCombined:
        return HashCombineFast(Load<std::uint32_t>(value), Load<std::uint32_t>(value));
    case kNameIndexPlusNumber:
        return Load<std::uint32_t>(value) + Load<std::uint32_t>(value + 4);
    case kPointerMurmur:
        return TypeHash64(MurmurFinalize64(Load<std::uint64_t>(value)));
    case kPointerShifted:
        return TypeHash64(Load<std::uint64_t>(value) >> 4);
    case kPointerShiftedCombined:
        return HashCombine(TypeHash64(Load<std::uint64_t>(value) >> 4), 0);
    case kStringCaseless: {
        const auto *chars = Load<const char16_t *>(value);
        const std::int32_t num = Load<std::int32_t>(value + 8);
        const std::uint32_t *table = CrcTable();
        std::uint32_t hash = 0;
        for (std::int32_t i = 0; chars && i + 1 < num && chars[i]; ++i) {
            char16_t ch = chars[i];
            if (ch >= u'a' && ch <= u'z')
                ch = static_cast<char16_t>(ch - 32);
            std::uint32_t b = ch & 0xFF;
            hash = ((hash >> 8) & 0x00FFFFFF) ^ table[(hash ^ b) & 0xFF];
            b = (ch >> 8) & 0xFF;
            hash = ((hash >> 8) & 0x00FFFFFF) ^ table[(hash ^ b) & 0xFF];
        }
        return hash;
    }
    default:
        return std::nullopt;
    }
}

static const char *RuleName(int rule) {
    switch (rule) {
    case kZeroExtended:
        return "the value";
    case kSignExtended:
        return "the sign-extended value";
    case kInt64:
        return "the 64-bit fold";
    case kFloatBits:
        return "the float's bits";
    case kDoubleBits:
        return "the double's bits folded";
    case kNameIndexPlusNumberCombined:
        return "HashCombineFast(index + number, index)";
    case kNameIndexCombined:
        return "HashCombineFast(index, index)";
    case kNameIndexPlusNumber:
        return "index + number";
    case kPointerMurmur:
        return "the murmur-finalized pointer";
    case kPointerShifted:
        return "the pointer shifted by 4";
    case kPointerShiftedCombined:
        return "HashCombine(pointer shifted by 4, 0)";
    case kStringCaseless:
        return "the caseless CRC";
    default:
        return "?";
    }
}

// Every element's stored bucket in a container the engine hashed is a fact. A
// rule is trusted once one container alone gives enough evidence, and dropped
// for good by a single miss anywhere.
void Containers::Learn(const SetLayout &layout, std::uint8_t *set) {
    const std::vector<Rule> rules = RulesFor(layout.key);
    const std::int32_t hashSize = Load<std::int32_t>(set + SetFields::kHashSize);
    if (rules.empty() || hashSize <= 1)
        return;
    std::string trusted;
    {
        std::lock_guard lock(hashMutex_);
        HashCandidates &candidates = hashes_[KeySignature(layout.key)];
        if (candidates.evidence.empty())
            candidates.evidence.assign(kRuleCount, 0);
        if (candidates.decided)
            return;
        int bits = 0;
        while ((1 << bits) < hashSize)
            ++bits;
        std::vector<int> gathered(kRuleCount, 0);
        for (const std::int32_t slot : Slots(set)) {
            const std::uint8_t *element = Element(layout, set, slot);
            const std::int32_t stored = Load<std::int32_t>(element + layout.hashIndexOffset);
            for (const Rule rule : rules) {
                if (candidates.evidence[rule] < 0)
                    continue;
                const std::optional<std::uint32_t> hash = Hash(rule, layout.key, element);
                if (!hash || static_cast<std::int32_t>(*hash & static_cast<std::uint32_t>(hashSize - 1)) != stored)
                    candidates.evidence[rule] = -1;
                else
                    gathered[rule] += bits;
            }
        }
        bool open = false;
        for (const Rule rule : rules) {
            if (candidates.evidence[rule] == 0 && gathered[rule] >= kTrustedBits) {
                candidates.evidence[rule] = 1;
                candidates.decided = true;
                trusted = RuleName(rule);
            }
            open = open || candidates.evidence[rule] == 0;
        }
        if (!open && trusted.empty()) {
            candidates.decided = true;
            if (note_)
                note_(std::string(PropertyKindName(layout.key.kind)) +
                      " keys match no known hash rule; additions go into one bucket until the engine rehashes");
            return;
        }
    }
    if (!trusted.empty() && note_)
        note_(std::string(PropertyKindName(layout.key.kind)) + " keys hash as " + trusted +
              ": proven on every element of a live container, and used to link additions");
}

int Containers::HashRule(const SetLayout &layout) {
    const std::vector<Rule> rules = RulesFor(layout.key);
    std::lock_guard lock(hashMutex_);
    const auto found = hashes_.find(KeySignature(layout.key));
    if (found == hashes_.end() || found->second.evidence.empty())
        return -1;
    for (const Rule rule : rules) {
        if (found->second.evidence[rule] == 1)
            return rule;
    }
    return -1;
}

// --- sets and maps: changes --------------------------------------------------------------

// TSparseArray::AddUninitialized: a free slot if there is one, else a new one.
std::int32_t Containers::AllocateSlot(const SetLayout &layout, std::uint8_t *set) {
    auto *data = Load<std::uint8_t *>(set + SetFields::kData);
    const std::int32_t num = Load<std::int32_t>(set + SetFields::kNum);
    const std::int32_t max = Load<std::int32_t>(set + SetFields::kMax);
    const std::int32_t numFree = Load<std::int32_t>(set + SetFields::kNumFree);
    const std::size_t stride = static_cast<std::size_t>(layout.stride);

    std::int32_t index = kIndexNone;
    if (numFree > 0) {
        index = Load<std::int32_t>(set + SetFields::kFirstFree);
        const std::int32_t next = Load<std::int32_t>(data + static_cast<std::size_t>(index) * stride + 4);
        Store<std::int32_t>(set + SetFields::kFirstFree, next);
        Store<std::int32_t>(set + SetFields::kNumFree, numFree - 1);
        if (numFree - 1 > 0)
            Store<std::int32_t>(data + static_cast<std::size_t>(next) * stride, kIndexNone);
    } else {
        if (num + 1 > kMaxContainerElements) {
            Fail("the set is at its size limit");
            return kIndexNone;
        }
        // The allocation flags grow with the elements: inline words first.
        const std::int32_t maxBits = Load<std::int32_t>(set + SetFields::kMaxBits);
        auto *bitsSecondary = Load<std::uint8_t *>(set + SetFields::kBitsSecondary);
        const std::int32_t inlineBits = SetFields::kBitsInlineWords * 32;
        if (num + 1 > maxBits) {
            if (!bitsSecondary && num + 1 <= inlineBits) {
                Store<std::int32_t>(set + SetFields::kMaxBits, inlineBits);
            } else {
                const std::int32_t words = std::max((num + 1 + 31) / 32, std::max(maxBits / 32, 1) * 2);
                const std::optional<EngineCalls::Block> block =
                    engine_->Allocate(static_cast<std::size_t>(words) * 4, 4);
                if (!block) {
                    Fail(engine_->Failure());
                    return kIndexNone;
                }
                std::memcpy(block->data, Bits(set), static_cast<std::size_t>((num + 31) / 32) * 4);
                if (bitsSecondary && !engine_->Free(bitsSecondary)) {
                    engine_->Free(block->data);
                    Fail(engine_->Failure());
                    return kIndexNone;
                }
                Store<std::uint8_t *>(set + SetFields::kBitsSecondary, block->data);
                Store<std::int32_t>(set + SetFields::kMaxBits, static_cast<std::int32_t>(block->bytes / 4) * 32);
            }
        }
        if (num + 1 > max) {
            const std::int32_t wanted = std::max(num + 1, num + num / 2 + 4);
            const std::optional<EngineCalls::Block> block =
                engine_->Allocate(static_cast<std::size_t>(wanted) * stride, static_cast<std::size_t>(layout.alignment));
            if (!block) {
                Fail(engine_->Failure());
                return kIndexNone;
            }
            if (num > 0)
                std::memcpy(block->data, data, static_cast<std::size_t>(num) * stride);
            if (data && !engine_->Free(data)) {
                engine_->Free(block->data);
                Fail(engine_->Failure());
                return kIndexNone;
            }
            data = block->data;
            Store<std::uint8_t *>(set + SetFields::kData, data);
            Store<std::int32_t>(set + SetFields::kMax, static_cast<std::int32_t>(block->bytes / stride));
        }
        index = num;
        Store<std::int32_t>(set + SetFields::kNum, num + 1);
        Store<std::int32_t>(set + SetFields::kNumBits, num + 1);
        SetBit(Bits(set), index, false);
    }
    SetBit(Bits(set), index, true);
    std::memset(data + static_cast<std::size_t>(index) * stride, 0, stride);
    return index;
}

// TSparseArray::RemoveAtUninitialized.
void Containers::FreeSlot(const SetLayout &layout, std::uint8_t *set, std::int32_t slot) {
    auto *data = Load<std::uint8_t *>(set + SetFields::kData);
    const std::int32_t numFree = Load<std::int32_t>(set + SetFields::kNumFree);
    const std::int32_t firstFree = Load<std::int32_t>(set + SetFields::kFirstFree);
    const std::size_t stride = static_cast<std::size_t>(layout.stride);
    if (numFree > 0)
        Store<std::int32_t>(data + static_cast<std::size_t>(firstFree) * stride, slot);
    std::uint8_t *link = data + static_cast<std::size_t>(slot) * stride;
    Store<std::int32_t>(link, kIndexNone);
    Store<std::int32_t>(link + 4, numFree > 0 ? firstFree : kIndexNone);
    Store<std::int32_t>(set + SetFields::kFirstFree, slot);
    Store<std::int32_t>(set + SetFields::kNumFree, numFree + 1);
    SetBit(Bits(set), slot, false);
}

// Every element into one bucket. Correct for any key: a lookup hashes to bucket
// 0 and walks the chain. The engine's next add sees fewer buckets than it wants
// and rehashes with the real hashes.
bool Containers::Collapse(const SetLayout &layout, std::uint8_t *set) {
    auto *secondary = Load<std::uint8_t *>(set + SetFields::kHashSecondary);
    if (secondary) {
        if (!engine_->Free(secondary))
            return Fail(engine_->Failure());
        Store<std::uint8_t *>(set + SetFields::kHashSecondary, nullptr);
    }
    Store<std::int32_t>(set + SetFields::kHashSize, 1);
    std::int32_t head = kIndexNone;
    for (const std::int32_t slot : Slots(set)) {
        std::uint8_t *element = Element(layout, set, slot);
        Store<std::int32_t>(element + layout.hashIndexOffset, 0);
        Store<std::int32_t>(element + layout.hashNextIdOffset, head);
        head = slot;
    }
    Store<std::int32_t>(set + SetFields::kHashInline, head);
    return true;
}

// TScriptSparseSet::Rehash with a proven hash: same buckets, same order.
bool Containers::Rehash(const SetLayout &layout, std::uint8_t *set, int rule, std::int32_t buckets) {
    if (buckets <= 1)
        return Collapse(layout, set);
    const std::optional<EngineCalls::Block> block = engine_->Allocate(static_cast<std::size_t>(buckets) * 4, 4);
    if (!block)
        return Fail(engine_->Failure());
    auto *table = reinterpret_cast<std::int32_t *>(block->data);
    std::fill(table, table + buckets, kIndexNone);
    for (const std::int32_t slot : Slots(set)) {
        std::uint8_t *element = Element(layout, set, slot);
        const std::optional<std::uint32_t> hash = Hash(rule, layout.key, element);
        if (!hash) {
            engine_->Free(block->data);
            return Fail("a key could not be hashed");
        }
        const std::int32_t bucket = static_cast<std::int32_t>(*hash & static_cast<std::uint32_t>(buckets - 1));
        Store<std::int32_t>(element + layout.hashIndexOffset, bucket);
        Store<std::int32_t>(element + layout.hashNextIdOffset, table[bucket]);
        table[bucket] = slot;
    }
    auto *old = Load<std::uint8_t *>(set + SetFields::kHashSecondary);
    if (old && !engine_->Free(old)) {
        engine_->Free(block->data);
        return Fail(engine_->Failure());
    }
    Store<std::uint8_t *>(set + SetFields::kHashSecondary, block->data);
    Store<std::int32_t>(set + SetFields::kHashSize, buckets);
    return true;
}

std::int32_t Containers::Find(const SetLayout &layout, std::uint8_t *set, const std::uint8_t *key) {
    if (!Validate(layout, set))
        return kIndexNone;
    for (const std::int32_t slot : Slots(set)) {
        if (owned_->Equal(layout.key, Element(layout, set, slot), key))
            return slot;
    }
    return kIndexNone;
}

std::int32_t Containers::Add(const SetLayout &layout, std::uint8_t *set, std::uint8_t *key, bool *consumed) {
    *consumed = false;
    if (!Validate(layout, set))
        return kIndexNone;
    for (const std::int32_t slot : Slots(set)) {
        if (owned_->Equal(layout.key, Element(layout, set, slot), key))
            return slot;
    }
    const int rule = HashRule(layout);
    const std::int32_t slot = AllocateSlot(layout, set);
    if (slot == kIndexNone)
        return kIndexNone;
    std::uint8_t *element = Element(layout, set, slot);
    if (layout.isMap && !owned_->Initialize(layout.value, element + layout.valueOffset)) {
        FreeSlot(layout, set, slot);
        Fail(owned_->Failure());
        return kIndexNone;
    }
    std::memcpy(element, key, static_cast<std::size_t>(layout.key.elementSize));
    *consumed = true;

    const std::int32_t count = Count(set);
    const std::int32_t hashSize = Load<std::int32_t>(set + SetFields::kHashSize);
    if (rule >= 0) {
        // TScriptSparseSet::AddNewElement.
        const std::int32_t desired = HashBucketsFor(count);
        if (hashSize == 0 || hashSize < desired) {
            if (!Rehash(layout, set, rule, desired))
                return kIndexNone;
            return slot;
        }
        const std::optional<std::uint32_t> hash = Hash(rule, layout.key, element);
        if (!hash)
            return Collapse(layout, set) ? slot : kIndexNone;
        const std::int32_t bucket = static_cast<std::int32_t>(*hash & static_cast<std::uint32_t>(hashSize - 1));
        std::int32_t *buckets = Buckets(set);
        Store<std::int32_t>(element + layout.hashIndexOffset, bucket);
        Store<std::int32_t>(element + layout.hashNextIdOffset, buckets[bucket]);
        buckets[bucket] = slot;
        return slot;
    }
    if (hashSize > 1) {
        {
            std::lock_guard lock(hashMutex_);
            HashCandidates &candidates = hashes_[KeySignature(layout.key)];
            if (!candidates.collapsedNoted && note_) {
                candidates.collapsedNoted = true;
                note_(std::string(PropertyKindName(layout.key.kind)) +
                      " keys: no hash rule proven yet, so an addition relinks the set into one bucket; the "
                      "engine rehashes on its next add");
            }
        }
        return Collapse(layout, set) ? slot : kIndexNone;
    }
    if (hashSize == 0) {
        Store<std::int32_t>(set + SetFields::kHashSize, 1);
        Store<std::int32_t>(set + SetFields::kHashInline, kIndexNone);
    }
    std::int32_t *buckets = Buckets(set);
    Store<std::int32_t>(element + layout.hashIndexOffset, 0);
    Store<std::int32_t>(element + layout.hashNextIdOffset, buckets[0]);
    buckets[0] = slot;
    return slot;
}

bool Containers::Remove(const SetLayout &layout, std::uint8_t *set, std::int32_t slot) {
    if (!Validate(layout, set))
        return false;
    std::uint8_t *element = Element(layout, set, slot);
    if (!element)
        return Fail("the slot is not occupied");
    // TScriptSparseSet::RemoveAt: out of its bucket's chain first.
    std::int32_t *link = Buckets(set) + Load<std::int32_t>(element + layout.hashIndexOffset);
    while (*link != kIndexNone) {
        if (*link == slot) {
            *link = Load<std::int32_t>(element + layout.hashNextIdOffset);
            break;
        }
        link = reinterpret_cast<std::int32_t *>(Element(layout, set, *link) + layout.hashNextIdOffset);
    }
    if (!owned_->Destroy(layout.key, element))
        return Fail(owned_->Failure());
    if (layout.isMap && !owned_->Destroy(layout.value, element + layout.valueOffset))
        return Fail(owned_->Failure());
    FreeSlot(layout, set, slot);
    return true;
}

bool Containers::FreeStorage(std::uint8_t *set) {
    for (const std::int32_t at : {SetFields::kData, SetFields::kBitsSecondary, SetFields::kHashSecondary}) {
        void *buffer = Load<void *>(set + at);
        if (buffer && !engine_->Free(buffer))
            return Fail(engine_->Failure());
        Store<void *>(set + at, nullptr);
    }
    std::memset(set, 0, SetFields::kSize);
    Store<std::int32_t>(set + SetFields::kFirstFree, kIndexNone);
    return true;
}

} // namespace URK::Unreal
