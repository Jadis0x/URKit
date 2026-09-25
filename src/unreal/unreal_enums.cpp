#include "unreal_enums.h"

#include <cctype>
#include <cstdio>

namespace URK::Unreal {
namespace {

constexpr std::int32_t kMaxEntries = 4096;
constexpr std::int32_t kFirstOffset = 0x28;
constexpr std::int32_t kLastOffset = 0x100;
constexpr std::size_t kMaxSamples = 64;
// Agreement needed before a storage is trusted.
constexpr int kEnumsNeeded = 3;
constexpr int kChecksNeeded = 8;

bool SameCaseless(std::string_view a, std::string_view b) {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) != std::tolower(static_cast<unsigned char>(b[i])))
            return false;
    }
    return true;
}

} // namespace

std::string_view EnumNames::ShortName(std::string_view name) {
    const std::size_t scope = name.rfind("::");
    return scope == std::string_view::npos ? name : name.substr(scope + 2);
}

std::optional<std::vector<EnumNames::Entry>> EnumNames::Read(Address enumObject, std::int32_t offset, Form form) const {
    const MemoryReader &reader = finder_->Reader();
    const NameTable &names = finder_->Names();
    Address nameArray = kNullAddress;
    Address valueArray = kNullAddress;
    std::int32_t count = 0;
    std::size_t stride = 0;
    if (form == Form::Pairs) {
        // TArray<TPair<FName, int64>>
        nameArray = reader.ReadPointer(enumObject + offset).value_or(kNullAddress);
        count = reader.ReadInt32(enumObject + offset + 8).value_or(-1);
        const std::int32_t max = reader.ReadInt32(enumObject + offset + 12).value_or(-1);
        if (max < count)
            return std::nullopt;
        valueArray = nameArray + 8;
        stride = 16;
    } else {
        // FNameData: tagged FName* and int64* (bit 0 marks a heap array), then the count.
        nameArray = reader.ReadPointer(enumObject + offset).value_or(kNullAddress) & ~Address{1};
        valueArray = reader.ReadPointer(enumObject + offset + 8).value_or(kNullAddress) & ~Address{1};
        count = reader.ReadInt32(enumObject + offset + 16).value_or(-1);
        stride = 8;
    }
    if (count <= 0 || count > kMaxEntries || nameArray == kNullAddress || valueArray == kNullAddress ||
        !MemoryReader::PlausiblePointer(nameArray) || !MemoryReader::PlausiblePointer(valueArray))
        return std::nullopt;
    const std::size_t span = static_cast<std::size_t>(count) * stride;
    if (!reader.Readable(nameArray, span) || !reader.Readable(valueArray, span))
        return std::nullopt;

    std::vector<Entry> entries;
    entries.reserve(static_cast<std::size_t>(count));
    for (std::int32_t i = 0; i < count; ++i) {
        const Address at = static_cast<Address>(i) * stride;
        std::optional<std::string> name = names.ReadFName(nameArray + at);
        const std::optional<std::int64_t> value = reader.ReadAs<std::int64_t>(valueArray + at);
        if (!name || name->empty() || !value)
            return std::nullopt;
        entries.push_back({std::move(*name), *value});
    }
    return entries;
}

bool EnumNames::Measure(EngineCalls &engine) {
    if (const int state = state_.load(std::memory_order_acquire); state != 0)
        return state == 1;

    std::vector<Address> samples;
    const ObjectArray &objects = finder_->Objects();
    for (std::int32_t index = 0; index < objects.Num() && samples.size() < kMaxSamples; ++index) {
        const Address object = objects.ObjectAt(index);
        if (object != kNullAddress && ObjectIs(*finder_, structs_, object, kCastFlagEnum))
            samples.push_back(object);
    }
    if (samples.size() < static_cast<std::size_t>(kEnumsNeeded)) {
        failure_ = "too few enums are loaded to measure their storage";
        state_.store(2, std::memory_order_release);
        return false;
    }

    // What the closest candidate saw, for the failure message.
    std::string closest = "no candidate read plausibly";
    int closestChecks = -1;
    for (std::int32_t offset = kFirstOffset; offset < kLastOffset; offset += 8) {
        for (const Form form : {Form::Parallel, Form::Pairs}) {
            // Plausible enums only; one engine disagreement rejects the candidate.
            int enums = 0;
            int checks = 0;
            int readable = 0;
            bool agrees = true;
            std::string mismatch;
            for (std::size_t s = 0; s < samples.size() && agrees; ++s) {
                const std::optional<std::vector<Entry>> read = Read(samples[s], offset, form);
                if (!read)
                    continue;
                ++readable;
                int asked = 0;
                for (std::size_t i = 0; i < read->size() && asked < 2 && agrees; ++i) {
                    const std::int64_t value = (*read)[i].value;
                    if (value < 0 || value > 255)
                        continue;
                    // The engine answers with the first entry holding a value.
                    std::size_t first = 0;
                    while ((*read)[first].value != value)
                        ++first;
                    if (first != i)
                        continue;
                    const std::optional<std::string> engineName =
                        engine.EnumeratorName(samples[s], static_cast<std::uint8_t>(value));
                    if (!engineName) {
                        failure_ = "the engine's enumerator names are unavailable (" + engine.Failure() + ")";
                        state_.store(2, std::memory_order_release);
                        return false;
                    }
                    agrees = SameCaseless(*engineName, (*read)[i].name);
                    if (!agrees)
                        mismatch = " (engine '" + *engineName + "', read '" + (*read)[i].name + "')";
                    ++asked;
                    ++checks;
                }
                enums += asked > 0 ? 1 : 0;
            }
            if (agrees && enums >= kEnumsNeeded && checks >= kChecksNeeded) {
                offset_ = offset;
                form_ = form;
                state_.store(1, std::memory_order_release);
                return true;
            }
            if (readable > 0 && checks > closestChecks) {
                closestChecks = checks;
                char where[96];
                std::snprintf(where, sizeof(where), "offset 0x%x %s: %d of %zu read, %d enums, %d checks",
                              static_cast<unsigned>(offset), form == Form::Parallel ? "parallel" : "pairs", readable,
                              samples.size(), enums, checks);
                closest = where + mismatch;
            }
        }
    }
    failure_ = "no enum storage agreed with the engine's own enumerator names; closest " + closest;
    state_.store(2, std::memory_order_release);
    return false;
}

std::optional<std::vector<EnumNames::Entry>> EnumNames::Entries(Address enumObject) const {
    if (!Measured() || !IsLiveObject(*finder_, enumObject) || !ObjectIs(*finder_, structs_, enumObject, kCastFlagEnum))
        return std::nullopt;
    return Read(enumObject, offset_, form_);
}

std::optional<std::int64_t> EnumNames::ValueOf(Address enumObject, std::string_view name) const {
    const std::optional<std::vector<Entry>> entries = Entries(enumObject);
    if (!entries)
        return std::nullopt;
    for (const Entry &entry : *entries) {
        if (SameCaseless(entry.name, name) || SameCaseless(ShortName(entry.name), name))
            return entry.value;
    }
    return std::nullopt;
}

std::optional<std::string> EnumNames::NameOf(Address enumObject, std::int64_t value) const {
    const std::optional<std::vector<Entry>> entries = Entries(enumObject);
    if (!entries)
        return std::nullopt;
    for (const Entry &entry : *entries) {
        if (entry.value == value)
            return std::string(ShortName(entry.name));
    }
    return std::nullopt;
}

} // namespace URK::Unreal
