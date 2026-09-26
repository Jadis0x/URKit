#pragma once

// Reflected types for the SDK generator: names, kinds, signatures. No offsets.

#include "unreal/reflection/unreal_enums.h"
#include "unreal/reflection/unreal_functions.h"
#include "unreal/reflection/unreal_type_queries.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <atomic>
#include <memory>
#include <map>
#include <string>
#include <unordered_map>
#include <unordered_set>

namespace URK::Unreal {

// Format the generator reads. Bump on any change to the line layout.
inline constexpr int kTypeDumpVersion = 3;
inline constexpr const char *kTypeDumpMagic = "URKIT-UNREAL-TYPES";

// One block per class ("package<TAB>name") so dumps from different maps merge.
using TypeDumpBlocks = std::map<std::string, std::string>;

// Identifies the game build and the loader; a dump of another is replaced, not merged.
struct TypeDumpImage {
    std::uint32_t timeDateStamp = 0;
    std::uint32_t sizeOfImage = 0;
    std::string engine;
    // The loader's own PE stamp: a new loader may describe types differently.
    std::uint32_t loaderStamp = 0;
};

// Monotonic milliseconds (QPC); the loader's frame budgets use the same clock.
double NowMilliseconds();

struct TypeDumpSources {
    const ObjectFinder &finder;
    const StructOffsets &structs;
    const PropertyChain &chain;
    const PropertyValues &values;
    const FunctionOffsets &functions;
    const TypeQueries &types;
    const EnumNames *enums = nullptr;
};

// Non-blocking dump: Scan queues, Step describes within a budget, writing is off-thread.
class TypeDumper {
  public:
    using Report = std::function<void(const std::string &)>;

    // Reads the existing dump; call before the game loop steps it.
    TypeDumper(const TypeDumpSources &sources, std::string path, const TypeDumpImage &image, Report report);
    ~TypeDumper();

    // Game thread, on BeginPlay. Returns the number of types queued.
    std::size_t Scan(const std::string &label, const EnumNames *enums);
    // Game thread, every frame; more: types are still coming (DescribeNow), so hold the write.
    void Step(double budgetMs, bool more = false);
    // Describes one type at once, before a map change can free it (a class just loaded). Game thread.
    bool DescribeNow(Address object);
    bool Idle() const { return queue_.empty() && ready_.empty(); }

  private:
    struct Queued {
        Address object = kNullAddress;
        std::uint64_t name = 0;
        std::uint8_t kind = 0;
        std::string key;
    };
    struct Seen {
        std::uint64_t name = 0;
        Address outer = kNullAddress;
        std::uint8_t kind = 0;
        std::string key;
    };

    std::uint64_t NameValue(Address object) const;
    Address OuterValue(Address object) const;
    void Describe(const Queued &item);
    std::uint8_t KindOf(Address object);
    void Flush();

    TypeDumpSources sources_;
    std::string path_;
    TypeDumpImage image_;
    Report report_;
    // The file's blocks; a detached writer owns it while busy_ is set.
    std::shared_ptr<TypeDumpBlocks> file_ = std::make_shared<TypeDumpBlocks>();
    std::shared_ptr<std::atomic<bool>> busy_ = std::make_shared<std::atomic<bool>>(false);
    std::unordered_set<std::string> known_;
    std::unordered_map<Address, Seen> seen_;
    std::unordered_map<Address, std::uint8_t> metaclasses_;
    std::deque<Queued> queue_;
    TypeDumpBlocks ready_;
    std::string label_;
    unsigned long long started_ = 0;
    double spentMs_ = 0;
    int frames_ = 0;
};

} // namespace URK::Unreal
