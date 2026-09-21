#include "unreal_names.h"

namespace URK::Unreal {
namespace {

// "None" and "CoreUObj", the two strings guaranteed to be in the first block.
// The first fixes the entry header size, the second confirms the block really
// is a name block and not an unrelated allocation.
constexpr std::uint32_t kNoneBytes = 0x656E6F4E;
constexpr std::uint64_t kCoreUObjectBytes = 0x6A624F5565726F43;

// "Byte", the start of the entry that follows "None". Its known length is what
// calibrates the shift that separates length from the flag bits below it.
constexpr std::uint32_t kByteBytes = 0x65747942;
constexpr std::uint16_t kBytePropertyLength = 0xC;
constexpr std::int32_t kNoneLength = 4;

// The wide flag is the lowest header bit in every version; only the width of
// the length field above it moves.
constexpr std::uint16_t kWideMask = 0x1;

constexpr std::int32_t kBlockScanLimit = 0x1000;
constexpr std::int32_t kMaxBlocks = 0x10000;
constexpr std::int32_t kMaxNameLength = 0x400;
constexpr int kMaxNumberedDepth = 4;

// Chunk size of TNameEntryArray, fixed in every version that used it.
constexpr std::uint32_t kEntryArrayChunkSize = 0x4000;

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

// "ByteProperty" follows "None". Shifting its header down until it equals the
// known length gives the width of the flag bits underneath.
bool ResolveLengthShift(const MemoryReader &reader, Address firstBlock, const NamePoolLayout &layout,
                        std::int32_t &lengthShift) {
    Address entry = firstBlock + static_cast<Address>(layout.entryStringOffset) + kNoneLength;
    bool located = false;
    for (int padding = 0; padding < 4; ++padding) {
        const std::optional<std::uint32_t> word = reader.ReadUInt32(entry + layout.entryStringOffset);
        if (word && *word == kByteBytes) {
            located = true;
            break;
        }
        entry += 1;
    }
    if (!located)
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

    lengthShift = shift;
    return true;
}

std::optional<NamePoolLayout> ResolvePool(const MemoryReader &reader, Address pool) {
    NamePoolLayout layout;
    if (!ResolvePoolHeader(reader, pool, layout))
        return std::nullopt;

    const std::optional<Address> firstBlock = reader.ReadPointer(pool + layout.blocksOffset);
    if (!firstBlock || *firstBlock == kNullAddress)
        return std::nullopt;

    if (!ResolveEntryStringOffset(reader, *firstBlock, layout.entryStringOffset))
        return std::nullopt;

    // A six-byte header carries an extra field ahead of the length word.
    layout.entryHeaderOffset = layout.entryStringOffset == 6 ? 4 : 0;
    layout.entryStride = layout.entryStringOffset == 2 ? 2 : 4;

    if (!ResolveLengthShift(reader, *firstBlock, layout, layout.lengthShift))
        return std::nullopt;

    return layout;
}

// The chunk table runs until a gap; the counts sit directly after it, and the
// chunk count matching the pointers already seen identifies them.
std::optional<NameEntryArrayLayout> ResolveEntryArrayHeader(const MemoryReader &reader, Address base) {
    std::int32_t validPointers = 0;
    std::int32_t nulls = 0;

    for (std::int32_t offset = 0; offset < 0x800; offset += sizeof(Address)) {
        const std::optional<Address> slot = reader.ReadPointer(base + offset);
        if (!slot)
            return std::nullopt;

        if (*slot == kNullAddress) {
            ++nulls;
            continue;
        }
        if (nulls == 0) {
            ++validPointers;
            continue;
        }

        const std::optional<std::int32_t> numElements = reader.ReadInt32(base + offset);
        const std::optional<std::int32_t> numChunks = reader.ReadInt32(base + offset + 4);
        if (numElements && numChunks && *numChunks == validPointers) {
            NameEntryArrayLayout layout;
            layout.numElementsOffset = offset;
            layout.blockCountOffset = offset + 4;
            return layout;
        }
    }
    return std::nullopt;
}

// Entry zero is "None", which fixes the string offset. Entries three and eight
// store their own index above the wide flag, which fixes the index offset.
bool ResolveEntryArrayFields(const MemoryReader &reader, Address entryZero, Address entryThree, Address entryEight,
                             NameEntryArrayLayout &layout) {
    for (std::int32_t offset = 0; offset < 0x20; ++offset) {
        const std::optional<std::uint32_t> word = reader.ReadUInt32(entryZero + offset);
        if (word && *word == kNoneBytes) {
            layout.entryStringOffset = offset;
            break;
        }
    }
    if (layout.entryStringOffset == kOffsetNotFound)
        return false;

    for (std::int32_t offset = 0; offset < 0x20; ++offset) {
        const std::optional<std::uint32_t> three = reader.ReadUInt32(entryThree + offset);
        const std::optional<std::uint32_t> eight = reader.ReadUInt32(entryEight + offset);
        if (three && eight && (*three >> 1) == 0x3 && (*eight >> 1) == 0x8) {
            layout.entryIndexOffset = offset;
            return true;
        }
    }
    return false;
}

} // namespace

std::optional<NameTable> NameTable::Resolve(const MemoryReader &reader, Address address) {
    if (const std::optional<NamePoolLayout> pool = ResolvePool(reader, address)) {
        NameLayout layout;
        layout.storage = NameStorage::Pool;
        layout.pool = *pool;
        return NameTable(reader, address, layout);
    }

    const std::optional<NameEntryArrayLayout> header = ResolveEntryArrayHeader(reader, address);
    if (!header)
        return std::nullopt;

    NameLayout layout;
    layout.storage = NameStorage::EntryArray;
    layout.entries = *header;

    NameTable table(reader, address, layout);
    const auto entryAddress = [&](std::uint32_t index) -> Address {
        const std::optional<Address> chunk =
            reader.ReadPointer(address + static_cast<Address>(index / kEntryArrayChunkSize) * sizeof(Address));
        if (!chunk || *chunk == kNullAddress)
            return kNullAddress;
        const std::optional<Address> entry =
            reader.ReadPointer(*chunk + static_cast<Address>(index % kEntryArrayChunkSize) * sizeof(Address));
        return entry ? *entry : kNullAddress;
    };

    const Address zero = entryAddress(0);
    const Address three = entryAddress(3);
    const Address eight = entryAddress(8);
    if (zero == kNullAddress || three == kNullAddress || eight == kNullAddress)
        return std::nullopt;
    if (!ResolveEntryArrayFields(reader, zero, three, eight, table.layout_.entries))
        return std::nullopt;

    return table;
}

void NameTable::CalibrateBlockOffsetBits(const ObjectArray &objects, std::int32_t nameOffset) {
    if (layout_.storage != NameStorage::Pool)
        return;

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

std::optional<std::string> NameTable::ReadFromEntryArray(std::uint32_t comparisonIndex) const {
    const NameEntryArrayLayout &entries = layout_.entries;

    const std::optional<std::int32_t> numElements = reader_->ReadInt32(address_ + entries.numElementsOffset);
    if (!numElements || comparisonIndex > static_cast<std::uint32_t>(*numElements))
        return std::nullopt;

    const std::optional<Address> chunk = reader_->ReadPointer(
        address_ + static_cast<Address>(comparisonIndex / kEntryArrayChunkSize) * sizeof(Address));
    if (!chunk || *chunk == kNullAddress)
        return std::nullopt;

    const std::optional<Address> entry = reader_->ReadPointer(
        *chunk + static_cast<Address>(comparisonIndex % kEntryArrayChunkSize) * sizeof(Address));
    if (!entry || *entry == kNullAddress)
        return std::nullopt;

    const std::optional<std::uint32_t> indexField = reader_->ReadUInt32(*entry + entries.entryIndexOffset);
    if (!indexField)
        return std::nullopt;

    // Entry-array names are null terminated rather than length prefixed.
    const Address text = *entry + entries.entryStringOffset;
    std::string name;
    if (*indexField & kWideMask) {
        for (std::int32_t i = 0; i < kMaxNameLength; ++i) {
            const std::optional<std::uint16_t> unit = reader_->ReadAs<std::uint16_t>(text + static_cast<Address>(i) * 2);
            if (!unit || *unit == 0)
                break;
            name.push_back(*unit < 0x80 ? static_cast<char>(*unit) : '?');
        }
        return name;
    }
    for (std::int32_t i = 0; i < kMaxNameLength; ++i) {
        const std::optional<std::uint8_t> byte = reader_->ReadAs<std::uint8_t>(text + static_cast<Address>(i));
        if (!byte || *byte == 0)
            break;
        name.push_back(static_cast<char>(*byte));
    }
    return name;
}

std::optional<std::string> NameTable::Read(std::uint32_t comparisonIndex) const {
    if (layout_.storage == NameStorage::Pool)
        return ReadFromPool(comparisonIndex, 0);
    return ReadFromEntryArray(comparisonIndex);
}

std::optional<std::string> NameTable::ReadFName(Address fname) const {
    const std::optional<std::uint32_t> comparisonIndex = reader_->ReadUInt32(fname);
    if (!comparisonIndex)
        return std::nullopt;

    std::optional<std::string> name = Read(*comparisonIndex);
    if (!name)
        return std::nullopt;

    const std::optional<std::int32_t> number = reader_->ReadInt32(fname + sizeof(std::int32_t));
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
