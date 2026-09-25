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
// Elements of one container checked against the engine's hash per validation.
constexpr std::int32_t kCheckedPerSet = 256;

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
        if (num > 0)
            std::memcpy(block->data, data, static_cast<std::size_t>(num) * width);
        Store<std::uint8_t *>(array, block->data);
        Store<std::int32_t>(array + 12, static_cast<std::int32_t>(block->bytes / width));
        engine_->ReleaseOrLeak(data, "an array's old buffer");
        data = block->data;
    }
    // Build new elements past Num, then move them into place.
    std::uint8_t *fresh = data + static_cast<std::size_t>(num) * width;
    std::memset(fresh, 0, static_cast<std::size_t>(count) * width);
    for (std::int32_t i = 0; i < count; ++i) {
        if (owned_->Initialize(inner, fresh + static_cast<std::size_t>(i) * width))
            continue;
        const std::string why = owned_->Failure();
        for (std::int32_t made = 0; made <= i; ++made)
            owned_->Destroy(inner, fresh + static_cast<std::size_t>(made) * width);
        std::memset(fresh, 0, static_cast<std::size_t>(count) * width);
        return Fail(why);
    }
    if (index < num)
        std::rotate(data + static_cast<std::size_t>(index) * width, fresh, fresh + static_cast<std::size_t>(count) * width);
    Store<std::int32_t>(array + 8, num + count);
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
    std::uint8_t *first = data + static_cast<std::size_t>(index) * width;
    for (std::int32_t i = 0; i < count; ++i) {
        if (!owned_->Releasable(inner, first + static_cast<std::size_t>(i) * width))
            return Fail(owned_->Failure());
    }
    for (std::int32_t i = 0; i < count; ++i)
        owned_->Destroy(inner, first + static_cast<std::size_t>(i) * width);
    if (num > index + count)
        std::memmove(first, first + static_cast<std::size_t>(count) * width,
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
        if (const auto found = layouts_.find(container.field);
            found != layouts_.end() && found->second.inner == container.inner &&
            found->second.valueInner == container.valueInner && found->second.elementSize == container.elementSize) {
            if (!found->second.layout)
                failure_ = found->second.failure;
            return found->second.layout;
        }
    }
    CachedLayout cached;
    cached.inner = container.inner;
    cached.valueInner = container.valueInner;
    cached.elementSize = container.elementSize;
    const auto remember = [&](std::optional<SetLayout> layout, std::string why) {
        cached.layout = std::move(layout);
        cached.failure = std::move(why);
        if (!cached.layout)
            failure_ = cached.failure;
        std::lock_guard lock(layoutMutex_);
        return (layouts_[container.field] = std::move(cached)).layout;
    };
    if (container.elementSize != SetFields::kSize)
        return remember(std::nullopt, "a set or map here is " + std::to_string(container.elementSize) +
                                          " bytes, not the " + std::to_string(SetFields::kSize) +
                                          " of the sparse layout");

    SetLayout layout;
    layout.isMap = container.kind == PropertyKind::Map;
    const std::optional<PropertyInfo> key = values_->Describe(container.inner);
    const std::optional<PropertyInfo> value =
        layout.isMap ? values_->Describe(container.valueInner) : std::optional<PropertyInfo>(PropertyInfo{});
    if (!key || !value || key->elementSize <= 0 || (layout.isMap && value->elementSize <= 0))
        return remember(std::nullopt, "the element properties did not resolve");
    layout.key = *key;
    layout.value = *value;
    const std::int32_t keyAlign = values_->AlignmentOf(*key);
    const std::int32_t valueAlign = layout.isMap ? values_->AlignmentOf(*value) : 1;
    if (keyAlign <= 0 || valueAlign <= 0)
        return remember(std::nullopt, "an element's alignment is unknown");

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

    // Must match the engine's layout word for word after the element properties.
    const PropertyTailOffsets &tail = values_->Tail();
    const std::int32_t after = layout.isMap ? tail.mapValue : tail.setElement;
    if (after == kOffsetNotFound)
        return remember(std::nullopt, "the element property offsets were not measured");
    const std::int32_t expected[] = {layout.hashNextIdOffset, layout.hashIndexOffset, setElementSize, layout.alignment,
                                     std::max(setElementSize, 8)};
    bool found = false;
    constexpr std::int32_t kSearch = 0x40;
    for (std::int32_t at = after + 8; at + 20 <= after + 8 + kSearch && !found; at += 4) {
        bool same = true;
        for (int i = 0; i < 5 && same; ++i)
            same = finder_->Reader().ReadInt32(container.field + at + i * 4).value_or(-1) == expected[i];
        if (same && layout.isMap) {
            // ValueOffset precedes the set layout (after zero Key/ElementOffset in older builds).
            same = finder_->Reader().ReadInt32(container.field + at - 4).value_or(-1) == layout.valueOffset ||
                   finder_->Reader().ReadInt32(container.field + at - 8).value_or(-1) == layout.valueOffset;
        }
        found = same;
    }
    if (!found)
        return remember(std::nullopt, "the engine's stored set layout differs from the computed one");
    if (layout.isMap && value->offset != layout.valueOffset)
        return remember(std::nullopt, "the map value property is not at the computed value offset");
    return remember(layout, {});
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

    // Each occupied slot must be in its bucket's chain exactly once.
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
    Check(layout, set);
    return true;
}

// --- sets and maps: hashing -------------------------------------------------------------

// Per key type: a struct's hash is its own, so the struct is part of it.
std::string Containers::KeySignature(const PropertyInfo &key) const {
    return std::to_string(static_cast<int>(key.kind)) + ":" + std::to_string(key.elementSize) + ":" +
           std::to_string(key.kind == PropertyKind::Struct ? key.inner : 0);
}

// A stored bucket our hash disagrees with drops that hash for the key type.
void Containers::Check(const SetLayout &layout, std::uint8_t *set) {
    const std::int32_t hashSize = Load<std::int32_t>(set + SetFields::kHashSize);
    if (hashSize <= 1 || !virtuals_.HashReady())
        return;
    const std::string signature = KeySignature(layout.key);
    {
        std::lock_guard lock(hashMutex_);
        if (keys_[signature].contradicted)
            return;
    }
    std::int32_t checked = 0;
    for (const std::int32_t slot : Slots(set)) {
        if (checked++ >= kCheckedPerSet)
            break;
        const std::uint8_t *element = Element(layout, set, slot);
        const std::optional<std::uint32_t> hash = virtuals_.Hash(layout.key, element);
        if (!hash)
            return;
        if (static_cast<std::int32_t>(*hash & static_cast<std::uint32_t>(hashSize - 1)) ==
            Load<std::int32_t>(element + layout.hashIndexOffset))
            continue;
        {
            std::lock_guard lock(hashMutex_);
            keys_[signature].contradicted = true;
        }
        NoteMemory(std::string(PropertyKindName(layout.key.kind)) +
                   " keys: a live container stored a bucket the engine's own hash does not give; additions go "
                   "into one bucket until the engine rehashes");
        return;
    }
}

std::optional<std::uint32_t> Containers::KeyHash(const SetLayout &layout, const std::uint8_t *key) {
    {
        std::lock_guard lock(hashMutex_);
        if (keys_[KeySignature(layout.key)].contradicted)
            return std::nullopt;
    }
    return virtuals_.Hash(layout.key, key);
}

bool Containers::KeysEqual(const SetLayout &layout, const std::uint8_t *a, const std::uint8_t *b) {
    if (const std::optional<bool> same = virtuals_.Identical(layout.key, a, b))
        return *same;
    return owned_->Equal(layout.key, a, b);
}

// --- sets and maps: changes --------------------------------------------------------------

// TSparseArray::AddUninitialized; a failure leaves the set unchanged.
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
                Store<std::uint8_t *>(set + SetFields::kBitsSecondary, block->data);
                Store<std::int32_t>(set + SetFields::kMaxBits, static_cast<std::int32_t>(block->bytes / 4) * 32);
                engine_->ReleaseOrLeak(bitsSecondary, "a set's old allocation flags");
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
            Store<std::uint8_t *>(set + SetFields::kData, block->data);
            Store<std::int32_t>(set + SetFields::kMax, static_cast<std::int32_t>(block->bytes / stride));
            engine_->ReleaseOrLeak(data, "a set's old elements");
            data = block->data;
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

// TScriptSparseSet::LinkElement.
void Containers::LinkInto(const SetLayout &layout, std::uint8_t *set, std::int32_t slot, std::int32_t bucket) {
    std::uint8_t *element = Element(layout, set, slot);
    std::int32_t *buckets = Buckets(set);
    Store<std::int32_t>(element + layout.hashIndexOffset, bucket);
    Store<std::int32_t>(element + layout.hashNextIdOffset, buckets[bucket]);
    buckets[bucket] = slot;
}

// Every element into bucket 0: valid for any key; the engine rehashes later.
void Containers::Collapse(const SetLayout &layout, std::uint8_t *set) {
    auto *old = Load<std::uint8_t *>(set + SetFields::kHashSecondary);
    Store<std::uint8_t *>(set + SetFields::kHashSecondary, nullptr);
    Store<std::int32_t>(set + SetFields::kHashSize, 1);
    Store<std::int32_t>(set + SetFields::kHashInline, kIndexNone);
    for (const std::int32_t slot : Slots(set))
        LinkInto(layout, set, slot, 0);
    engine_->ReleaseOrLeak(old, "a set's old hash");
}

// TScriptSparseSet::Rehash into a preallocated table; cannot fail.
void Containers::Relink(const SetLayout &layout, std::uint8_t *set, std::uint8_t *table, std::int32_t buckets,
                        const std::vector<std::pair<std::int32_t, std::uint32_t>> &hashes) {
    auto *old = Load<std::uint8_t *>(set + SetFields::kHashSecondary);
    std::fill_n(reinterpret_cast<std::int32_t *>(table), buckets, kIndexNone);
    Store<std::uint8_t *>(set + SetFields::kHashSecondary, table);
    Store<std::int32_t>(set + SetFields::kHashSize, buckets);
    for (const auto &[slot, hash] : hashes)
        LinkInto(layout, set, slot, static_cast<std::int32_t>(hash & static_cast<std::uint32_t>(buckets - 1)));
    engine_->ReleaseOrLeak(old, "a set's old hash");
}

std::int32_t Containers::Find(const SetLayout &layout, std::uint8_t *set, const std::uint8_t *key) {
    if (!Validate(layout, set))
        return kIndexNone;
    for (const std::int32_t slot : Slots(set)) {
        if (KeysEqual(layout, Element(layout, set, slot), key))
            return slot;
    }
    return kIndexNone;
}

std::int32_t Containers::Add(const SetLayout &layout, std::uint8_t *set, std::uint8_t *key, bool *consumed) {
    *consumed = false;
    if (!Validate(layout, set))
        return kIndexNone;
    for (const std::int32_t slot : Slots(set)) {
        if (KeysEqual(layout, Element(layout, set, slot), key))
            return slot;
    }

    // TScriptSparseSet::AddNewElement's ConditionalRehash.
    const std::int32_t hashSize = Load<std::int32_t>(set + SetFields::kHashSize);
    const std::optional<std::uint32_t> keyHash = KeyHash(layout, key);
    std::int32_t buckets = hashSize;
    std::vector<std::pair<std::int32_t, std::uint32_t>> hashes;
    bool collapse = !keyHash && hashSize > 1;
    if (keyHash) {
        // Engine's bucket count; any power of two is still correct.
        const std::int32_t desired = HashBucketsFor(Count(set) + 1);
        if (hashSize < desired) {
            buckets = desired;
            for (const std::int32_t slot : Slots(set)) {
                const std::optional<std::uint32_t> hash = KeyHash(layout, Element(layout, set, slot));
                if (!hash) {
                    collapse = true;
                    break;
                }
                hashes.emplace_back(slot, *hash);
            }
        }
    }

    // Everything that can fail, before the set changes.
    std::uint8_t *table = nullptr;
    if (!collapse && buckets > 1 && buckets != hashSize) {
        const std::optional<EngineCalls::Block> block = engine_->Allocate(static_cast<std::size_t>(buckets) * 4, 4);
        if (!block) {
            Fail(engine_->Failure());
            return kIndexNone;
        }
        table = block->data;
    }
    std::vector<std::uint8_t> value;
    if (layout.isMap) {
        value.assign(static_cast<std::size_t>(layout.value.elementSize), 0);
        if (!owned_->Initialize(layout.value, value.data())) {
            const std::string why = owned_->Failure();
            owned_->Destroy(layout.value, value.data());
            engine_->ReleaseOrLeak(table, "an unused hash table");
            Fail(why);
            return kIndexNone;
        }
    }
    const std::int32_t slot = AllocateSlot(layout, set);
    if (slot == kIndexNone) {
        const std::string why = failure_;
        if (layout.isMap)
            owned_->Destroy(layout.value, value.data());
        engine_->ReleaseOrLeak(table, "an unused hash table");
        Fail(why);
        return kIndexNone;
    }

    // Committed: nothing below fails.
    std::uint8_t *element = Element(layout, set, slot);
    std::memcpy(element, key, static_cast<std::size_t>(layout.key.elementSize));
    if (layout.isMap)
        std::memcpy(element + layout.valueOffset, value.data(), value.size());
    *consumed = true;
    if (collapse) {
        Collapse(layout, set);
        bool first = false;
        {
            std::lock_guard lock(hashMutex_);
            KeyRecord &record = keys_[KeySignature(layout.key)];
            first = !record.collapsedNoted;
            record.collapsedNoted = true;
        }
        if (first) {
            const std::string why = virtuals_.HashReady()
                                        ? "this key type has none, or a live container contradicted it"
                                        : virtuals_.Failure();
            NoteMemory(std::string(PropertyKindName(layout.key.kind)) + " keys: no engine hash (" + why +
                       "), so an addition relinks the set into one bucket; the engine rehashes on its next add");
        }
        return slot;
    }
    if (table) {
        hashes.emplace_back(slot, *keyHash);
        Relink(layout, set, table, buckets, hashes);
        return slot;
    }
    if (hashSize == 0) {
        Store<std::int32_t>(set + SetFields::kHashSize, 1);
        Store<std::int32_t>(set + SetFields::kHashInline, kIndexNone);
    }
    const std::uint32_t mask = static_cast<std::uint32_t>(std::max(hashSize, 1) - 1);
    LinkInto(layout, set, slot, keyHash ? static_cast<std::int32_t>(*keyHash & mask) : 0);
    return slot;
}

bool Containers::Remove(const SetLayout &layout, std::uint8_t *set, std::int32_t slot) {
    if (!Validate(layout, set))
        return false;
    std::uint8_t *element = Element(layout, set, slot);
    if (!element)
        return Fail("the slot is not occupied");
    if (!owned_->Releasable(layout.key, element) ||
        (layout.isMap && !owned_->Releasable(layout.value, element + layout.valueOffset)))
        return Fail(owned_->Failure());
    // TScriptSparseSet::RemoveAt: out of its bucket's chain first.
    std::int32_t *link = Buckets(set) + Load<std::int32_t>(element + layout.hashIndexOffset);
    while (*link != kIndexNone) {
        if (*link == slot) {
            *link = Load<std::int32_t>(element + layout.hashNextIdOffset);
            break;
        }
        link = reinterpret_cast<std::int32_t *>(Element(layout, set, *link) + layout.hashNextIdOffset);
    }
    owned_->Destroy(layout.key, element);
    if (layout.isMap)
        owned_->Destroy(layout.value, element + layout.valueOffset);
    FreeSlot(layout, set, slot);
    return true;
}

bool Containers::HoldsStorage(const std::uint8_t *set) {
    for (const std::int32_t at : {SetFields::kData, SetFields::kBitsSecondary, SetFields::kHashSecondary}) {
        if (Load<void *>(set + at))
            return true;
    }
    return false;
}

void Containers::FreeStorage(std::uint8_t *set) {
    void *buffers[] = {Load<void *>(set + SetFields::kData), Load<void *>(set + SetFields::kBitsSecondary),
                       Load<void *>(set + SetFields::kHashSecondary)};
    std::memset(set, 0, SetFields::kSize);
    Store<std::int32_t>(set + SetFields::kFirstFree, kIndexNone);
    for (void *buffer : buffers)
        engine_->ReleaseOrLeak(buffer, "a set's storage");
}

} // namespace URK::Unreal
