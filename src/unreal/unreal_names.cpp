#include "unreal_names.h"

namespace URK::Unreal {
namespace {

// Both are always in the first block: "None" sizes the entry header, "CoreUObj"
// confirms it is a name block.
constexpr std::uint32_t kNoneBytes = 0x656E6F4E;
constexpr std::uint64_t kCoreUObjectBytes = 0x6A624F5565726F43;

// "Byte", the start of the entry that follows "None". Its known length is what
// calibrates the shift that separates length from the flag bits below it.
constexpr std::uint32_t kByteBytes = 0x65747942;
constexpr std::uint32_t kIntPBytes = 0x50746E49;
constexpr std::uint16_t kBytePropertyLength = 0xC;
constexpr std::int32_t kNoneLength = 4;

// The wide flag is the lowest header bit in every version; only the width of
// the length field above it moves.
constexpr std::uint16_t kWideMask = 0x1;

constexpr std::int32_t kBlockScanLimit = 0x1000;
constexpr std::int32_t kMaxBlocks = 0x10000;
constexpr std::int32_t kMaxNameLength = 0x400;
constexpr int kMaxNumberedDepth = 4;

std::string NarrowWide(const MemoryReader &reader, Address address, std::int32_t length) {
    std::string text;
    text.reserve(static_cast<std::size_t>(length));
    for (std::int32_t i = 0; i < length; ++i) {
        const std::optional<std::uint16_t> unit = reader.ReadAs<std::uint16_t>(address + static_cast<Address>(i) * 2);
        if (!unit)
            break;
        // Engine names are ASCII in practice; anything else is not worth a
        // UTF-16 decoder here.
        text.push_back(*unit < 0x80 ? static_cast<char>(*unit) : '?');
    }
    return text;
}

std::string ReadAnsi(const MemoryReader &reader, Address address, std::int32_t length) {
    std::string text;
    text.reserve(static_cast<std::size_t>(length));
    for (std::int32_t i = 0; i < length; ++i) {
        const std::optional<std::uint8_t> byte = reader.ReadAs<std::uint8_t>(address + static_cast<Address>(i));
        if (!byte)
            break;
        text.push_back(static_cast<char>(*byte));
    }
    return text;
}

// Locates the block table by the one relation that holds: the block count field
// is followed by exactly that many plus one non-null block pointers.
bool ResolvePoolHeader(const MemoryReader &reader, Address pool, NamePoolLayout &layout) {
    for (std::int32_t offset = 0; offset < 0x20; offset += 4) {
        const std::optional<std::int32_t> blockCount = reader.ReadInt32(pool + offset);
        if (!blockCount || *blockCount <= 0 || *blockCount > kMaxBlocks)
            continue;

        const std::int32_t tableStart = offset + 8 + (offset % 8);
        std::int32_t nonNull = 0;
        std::int32_t firstNonNull = kOffsetNotFound;
        std::int32_t sinceLastValid = 0;
        constexpr std::int32_t kMaxRunOfNulls = 0x500;

        for (std::int32_t slot = 0; slot < 0x10000; slot += 8) {
            const std::int32_t slotOffset = tableStart + slot;
            const std::optional<Address> block = reader.ReadPointer(pool + slotOffset);
            if (block && *block != kNullAddress) {
                ++nonNull;
                sinceLastValid = 0;
                if (firstNonNull == kOffsetNotFound)
                    firstNonNull = slotOffset;
            } else {
                if (++sinceLastValid == kMaxRunOfNulls)
                    break;
            }
        }

        if (firstNonNull != kOffsetNotFound && *blockCount == (nonNull - 1)) {
            layout.blockCountOffset = offset;
            layout.byteCursorOffset = offset + 4;
            layout.blocksOffset = firstNonNull;
            return true;
        }
    }
    return false;
}

// The header size is where "None" starts in the first block; "CoreUObj" further
// in confirms the block. Both are written before any game content.
bool ResolveEntryStringOffset(const MemoryReader &reader, Address firstBlock, std::int32_t &stringOffset) {
    std::int32_t found = kOffsetNotFound;
    for (std::int32_t i = 0; i < kBlockScanLimit; ++i) {
        if (found == kOffsetNotFound) {
            const std::optional<std::uint32_t> word = reader.ReadUInt32(firstBlock + i);
            if (word && *word == kNoneBytes) {
                found = i;
                continue;
            }
        }
        const std::optional<std::uint64_t> wide = reader.ReadAs<std::uint64_t>(firstBlock + i);
        if (wide && *wide == kCoreUObjectBytes && found != kOffsetNotFound) {
            stringOffset = found;
            return true;
        }
    }
    return false;
}

// "None", "ByteProperty", "IntProperty" open every pool: where the next two start gives
// the entry alignment (8 in the UE5.8 editor), and ByteProperty's header the length shift.
bool ResolveSecondEntry(const MemoryReader &reader, Address firstBlock, NamePoolLayout &layout) {
    const auto aligned = [](std::int32_t at, std::int32_t stride) { return (at + stride - 1) / stride * stride; };
    const auto holds = [&](std::int32_t start, std::uint32_t text) {
        return reader.ReadUInt32(firstBlock + start + layout.entryStringOffset).value_or(0) == text;
    };
    Address entry = kNullAddress;
    for (const std::int32_t stride : {2, 4, 8}) {
        const std::int32_t second = aligned(layout.entryStringOffset + kNoneLength, stride);
        const std::int32_t third = aligned(second + layout.entryStringOffset + kBytePropertyLength, stride);
        if (holds(second, kByteBytes) && holds(third, kIntPBytes)) {
            layout.entryStride = stride;
            entry = firstBlock + static_cast<Address>(second);
            break;
        }
    }
    if (entry == kNullAddress)
        return false;

    std::optional<std::uint16_t> header = reader.ReadAs<std::uint16_t>(entry + layout.entryHeaderOffset);
    if (!header)
        return false;

    constexpr std::int32_t kMaxShift = sizeof(std::uint16_t) * 8;
    std::int32_t shift = 0;
    std::uint16_t value = *header;
    while (value != kBytePropertyLength && shift < kMaxShift) {
        ++shift;
        value = static_cast<std::uint16_t>(value >> 1);
    }
    if (shift >= kMaxShift)
        return false;

    layout.lengthShift = shift;
    return true;
}

std::optional<NamePoolLayout> ResolvePool(const MemoryReader &reader, Address pool, std::string *why) {
    const auto fail = [why](const char *reason) -> std::optional<NamePoolLayout> {
        if (why)
            *why = reason;
        return std::nullopt;
    };
    NamePoolLayout layout;
    if (!ResolvePoolHeader(reader, pool, layout))
        return fail("no block count followed by that many block pointers");

    const std::optional<Address> firstBlock = reader.ReadPointer(pool + layout.blocksOffset);
    if (!firstBlock || *firstBlock == kNullAddress)
        return fail("the first block pointer is unreadable");

    if (!ResolveEntryStringOffset(reader, *firstBlock, layout.entryStringOffset))
        return fail("the first block holds no \"None\" entry followed by \"CoreUObject\"");

    // A six-byte header carries an extra field ahead of the length word.
    layout.entryHeaderOffset = layout.entryStringOffset == 6 ? 4 : 0;

    if (!ResolveSecondEntry(reader, *firstBlock, layout))
        return fail("\"ByteProperty\" and \"IntProperty\" do not follow \"None\" at a 2, 4 or 8 byte alignment");

    return layout;
}

} // namespace

std::optional<NameTable> NameTable::Resolve(const MemoryReader &reader, Address address, std::string *why) {
    const std::optional<NamePoolLayout> pool = ResolvePool(reader, address, why);
    if (!pool)
        return std::nullopt;
    return NameTable(reader, address, NameLayout{*pool});
}

void NameTable::CalibrateBlockOffsetBits(const ObjectArray &objects, std::int32_t nameOffset) {
    const std::optional<std::int32_t> blockCount = reader_->ReadInt32(address_ + layout_.pool.blockCountOffset);
    if (!blockCount)
        return;

    constexpr std::int32_t kMaxBits = 0x20;
    const std::int32_t total = objects.Num();

    for (std::int32_t index = 0; index < total; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        const std::optional<std::uint32_t> comparisonIndex = reader_->ReadUInt32(object + nameOffset);
        if (!comparisonIndex)
            continue;

        while (layout_.pool.blockOffsetBits < kMaxBits &&
               static_cast<std::int32_t>(*comparisonIndex >> layout_.pool.blockOffsetBits) > *blockCount) {
            ++layout_.pool.blockOffsetBits;
        }
    }
}

void NameTable::CalibrateNameLayout(const ObjectArray &objects, std::int32_t nameOffset, std::int32_t outerOffset) {
    constexpr std::int32_t kSamples = 4096;
    const std::int32_t room = outerOffset > nameOffset ? outerOffset - nameOffset : 8;
    std::int32_t sampled = 0;
    std::int32_t repeated = 0;
    const std::int32_t total = objects.Num();
    for (std::int32_t index = 0; index < total && sampled < kSamples; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object == kNullAddress)
            continue;
        const std::optional<std::uint32_t> comparison = reader_->ReadUInt32(object + nameOffset);
        const std::optional<std::uint32_t> second = reader_->ReadUInt32(object + nameOffset + 4);
        if (!comparison || !second || *comparison == 0)
            continue;
        ++sampled;
        repeated += *second == *comparison ? 1 : 0;
    }
    // A display index almost always equals the comparison index.
    if (sampled == 0 || repeated * 10 < sampled * 9)
        return;
    layout_.displayIndexOffset = 4;
    layout_.numberOffset = room >= 12 ? 8 : kOffsetNotFound;
    layout_.size = room >= 12 ? 12 : 8;
}

std::optional<std::string> NameTable::ReadFromPool(std::uint32_t comparisonIndex, int depth) const {
    if (depth > kMaxNumberedDepth)
        return std::nullopt;

    const NamePoolLayout &pool = layout_.pool;
    const std::optional<std::int32_t> blockCount = reader_->ReadInt32(address_ + pool.blockCountOffset);
    if (!blockCount)
        return std::nullopt;

    const std::int32_t blockIndex = static_cast<std::int32_t>(comparisonIndex >> pool.blockOffsetBits);
    if (blockIndex < 0 || blockIndex > *blockCount)
        return std::nullopt;

    const std::uint32_t mask = (1u << pool.blockOffsetBits) - 1u;
    const Address offsetInBlock = static_cast<Address>(comparisonIndex & mask) * pool.entryStride;

    const std::optional<Address> block =
        reader_->ReadPointer(address_ + pool.blocksOffset + static_cast<Address>(blockIndex) * sizeof(Address));
    if (!block || *block == kNullAddress)
        return std::nullopt;

    const Address entry = *block + offsetInBlock;
    const std::optional<std::uint16_t> header = reader_->ReadAs<std::uint16_t>(entry + pool.entryHeaderOffset);
    if (!header)
        return std::nullopt;

    const std::int32_t length = static_cast<std::int32_t>(*header >> pool.lengthShift);

    // A zero length marks a numbered entry: the payload is a reference to the
    // base name plus the number to append.
    if (length == 0) {
        const std::int32_t payload = pool.entryStringOffset + (pool.entryStringOffset == 6 ? 2 : 0);
        const std::optional<std::int32_t> baseIndex = reader_->ReadInt32(entry + payload);
        const std::optional<std::int32_t> number = reader_->ReadInt32(entry + payload + sizeof(std::int32_t));
        if (!baseIndex || !number)
            return std::nullopt;
        std::optional<std::string> base = ReadFromPool(static_cast<std::uint32_t>(*baseIndex), depth + 1);
        if (!base)
            return std::nullopt;
        if (*number > 0)
            base->append("_").append(std::to_string(*number - 1));
        return base;
    }

    if (length > kMaxNameLength)
        return std::nullopt;

    const Address text = entry + pool.entryStringOffset;
    if (*header & kWideMask)
        return NarrowWide(*reader_, text, length);
    return ReadAnsi(*reader_, text, length);
}

std::optional<std::string> NameTable::Read(std::uint32_t comparisonIndex) const {
    return ReadFromPool(comparisonIndex, 0);
}

std::optional<std::string> NameTable::ReadFName(Address fname) const {
    const std::optional<std::uint32_t> comparisonIndex = reader_->ReadUInt32(fname);
    if (!comparisonIndex)
        return std::nullopt;

    std::optional<std::string> name = Read(*comparisonIndex);
    if (!name)
        return std::nullopt;

    if (layout_.numberOffset == kOffsetNotFound)
        return name;
    const std::optional<std::int32_t> number = reader_->ReadInt32(fname + layout_.numberOffset);
    if (number && *number > 0)
        name->append("_").append(std::to_string(*number - 1));
    return name;
}

std::optional<std::string> NameTable::ObjectName(const ObjectArray &objects, std::int32_t nameOffset,
                                                 std::int32_t objectIndex) const {
    const Address object = objects.ObjectAt(objectIndex);
    if (object == kNullAddress)
        return std::nullopt;
    return ReadFName(object + nameOffset);
}

} // namespace URK::Unreal
