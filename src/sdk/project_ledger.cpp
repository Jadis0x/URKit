#include "project_ledger.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <system_error>

namespace UrkProject {
namespace {

namespace fs = std::filesystem;

constexpr std::array<std::uint32_t, 64> kSha256RoundConstants{
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u, 0x923f82a4u, 0xab1c5ed5u,
    0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u, 0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u,
    0xe49b69c1u, 0xefbe4786u, 0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u, 0x06ca6351u, 0x14292967u,
    0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u, 0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u,
    0xa2bfe8a1u, 0xa81a664bu, 0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au, 0x5b9cca4fu, 0x682e6ff3u,
    0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u, 0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u,
};

std::uint32_t RotateRight(std::uint32_t value, unsigned shift) {
    return (value >> shift) | (value << (32u - shift));
}

class Sha256 {
  public:
    void Update(const std::uint8_t *data, std::size_t size) {
        if (!data || size == 0)
            return;
        bitCount_ += static_cast<std::uint64_t>(size) * 8u;
        while (size != 0) {
            const std::size_t count = (std::min)(size, block_.size() - blockSize_);
            std::memcpy(block_.data() + blockSize_, data, count);
            blockSize_ += count;
            data += count;
            size -= count;
            if (blockSize_ == block_.size()) {
                Transform(block_.data());
                blockSize_ = 0;
            }
        }
    }

    std::array<std::uint8_t, 32> Finish() {
        block_[blockSize_++] = 0x80u;
        if (blockSize_ > 56) {
            while (blockSize_ < block_.size())
                block_[blockSize_++] = 0;
            Transform(block_.data());
            blockSize_ = 0;
        }
        while (blockSize_ < 56)
            block_[blockSize_++] = 0;
        for (int index = 7; index >= 0; --index)
            block_[blockSize_++] = static_cast<std::uint8_t>(bitCount_ >> (index * 8));
        Transform(block_.data());

        std::array<std::uint8_t, 32> digest{};
        for (std::size_t index = 0; index < state_.size(); ++index) {
            digest[index * 4] = static_cast<std::uint8_t>(state_[index] >> 24);
            digest[index * 4 + 1] = static_cast<std::uint8_t>(state_[index] >> 16);
            digest[index * 4 + 2] = static_cast<std::uint8_t>(state_[index] >> 8);
            digest[index * 4 + 3] = static_cast<std::uint8_t>(state_[index]);
        }
        return digest;
    }

  private:
    void Transform(const std::uint8_t *block) {
        std::array<std::uint32_t, 64> words{};
        for (std::size_t index = 0; index < 16; ++index) {
            const std::size_t offset = index * 4;
            words[index] = (static_cast<std::uint32_t>(block[offset]) << 24) |
                           (static_cast<std::uint32_t>(block[offset + 1]) << 16) |
                           (static_cast<std::uint32_t>(block[offset + 2]) << 8) |
                           static_cast<std::uint32_t>(block[offset + 3]);
        }
        for (std::size_t index = 16; index < words.size(); ++index) {
            const std::uint32_t lower0 = RotateRight(words[index - 15], 7) ^ RotateRight(words[index - 15], 18) ^
                                         (words[index - 15] >> 3);
            const std::uint32_t lower1 = RotateRight(words[index - 2], 17) ^ RotateRight(words[index - 2], 19) ^
                                         (words[index - 2] >> 10);
            words[index] = words[index - 16] + lower0 + words[index - 7] + lower1;
        }

        std::uint32_t a = state_[0];
        std::uint32_t b = state_[1];
        std::uint32_t c = state_[2];
        std::uint32_t d = state_[3];
        std::uint32_t e = state_[4];
        std::uint32_t f = state_[5];
        std::uint32_t g = state_[6];
        std::uint32_t h = state_[7];
        for (std::size_t index = 0; index < words.size(); ++index) {
            const std::uint32_t upper1 = RotateRight(e, 6) ^ RotateRight(e, 11) ^ RotateRight(e, 25);
            const std::uint32_t choice = (e & f) ^ ((~e) & g);
            const std::uint32_t temporary1 = h + upper1 + choice + kSha256RoundConstants[index] + words[index];
            const std::uint32_t upper0 = RotateRight(a, 2) ^ RotateRight(a, 13) ^ RotateRight(a, 22);
            const std::uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
            const std::uint32_t temporary2 = upper0 + majority;
            h = g;
            g = f;
            f = e;
            e = d + temporary1;
            d = c;
            c = b;
            b = a;
            a = temporary1 + temporary2;
        }
        state_[0] += a;
        state_[1] += b;
        state_[2] += c;
        state_[3] += d;
        state_[4] += e;
        state_[5] += f;
        state_[6] += g;
        state_[7] += h;
    }

    std::array<std::uint32_t, 8> state_{0x6a09e667u, 0xbb67ae85u, 0x3c6ef372u, 0xa54ff53au,
                                        0x510e527fu, 0x9b05688cu, 0x1f83d9abu, 0x5be0cd19u};
    std::array<std::uint8_t, 64> block_{};
    std::size_t blockSize_ = 0;
    std::uint64_t bitCount_ = 0;
};

bool ParsePositiveInt(const std::string &text, int *value) {
    if (!value || text.empty())
        return false;
    int parsed = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), parsed);
    if (error != std::errc{} || end != text.data() + text.size() || parsed <= 0)
        return false;
    *value = parsed;
    return true;
}

bool IsSafeRelativePath(const fs::path &path) {
    return !path.empty() && path.is_relative() && !path.lexically_normal().generic_string().starts_with("../") &&
           path.lexically_normal() != fs::path("..");
}

bool IsSha256(const std::string &value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
        return (character >= '0' && character <= '9') || (character >= 'a' && character <= 'f');
    });
}

} // namespace

bool HashFileSha256(const fs::path &path, std::string *hash, std::string *error) {
    if (!hash) {
        if (error)
            *error = "SHA-256 output is null";
        return false;
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot read generated file " + path.string();
        return false;
    }

    Sha256 digest;
    std::array<std::uint8_t, 64 * 1024> buffer{};
    while (input) {
        input.read(reinterpret_cast<char *>(buffer.data()), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0)
            digest.Update(buffer.data(), static_cast<std::size_t>(count));
    }
    if (input.bad()) {
        if (error)
            *error = "cannot finish reading generated file " + path.string();
        return false;
    }

    std::ostringstream text;
    text << std::hex << std::setfill('0');
    for (const std::uint8_t byte : digest.Finish())
        text << std::setw(2) << static_cast<unsigned>(byte);
    *hash = text.str();
    return true;
}

bool ReadGeneratedFileLedger(const fs::path &projectRoot, GeneratedFileLedger *ledger, std::string *error) {
    if (!ledger) {
        if (error)
            *error = "generated-file ledger output is null";
        return false;
    }
    std::ifstream input(projectRoot / kGeneratedFileLedgerRelativePath, std::ios::binary);
    if (!input) {
        if (error)
            *error = "generated-file ledger is missing";
        return false;
    }

    GeneratedFileLedger parsed;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        if (line.empty() || line.front() == '#')
            continue;
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos || equals == 0) {
            if (error)
                *error = "generated-file ledger contains an invalid line";
            return false;
        }
        const std::string key = line.substr(0, equals);
        const std::string value = line.substr(equals + 1);
        if (key == "schema_version") {
            if (!ParsePositiveInt(value, &parsed.schemaVersion) ||
                parsed.schemaVersion != kGeneratedFileLedgerSchemaVersion) {
                if (error)
                    *error = "generated-file ledger has an unsupported schema version";
                return false;
            }
        } else if (key == "sdk_version") {
            if (!ParsePositiveInt(value, &parsed.sdkVersion)) {
                if (error)
                    *error = "generated-file ledger has an invalid SDK version";
                return false;
            }
        } else if (key == "file") {
            const std::size_t separator = value.find('\t');
            if (separator == std::string::npos || !IsSha256(value.substr(0, separator))) {
                if (error)
                    *error = "generated-file ledger has an invalid file hash";
                return false;
            }
            const fs::path relative = fs::path(value.substr(separator + 1)).lexically_normal();
            if (!IsSafeRelativePath(relative) ||
                !parsed.hashes.emplace(relative.generic_string(), value.substr(0, separator)).second) {
                if (error)
                    *error = "generated-file ledger has an invalid or duplicate path";
                return false;
            }
        } else {
            if (error)
                *error = "generated-file ledger contains an unknown field";
            return false;
        }
    }
    if (!input.eof() || parsed.sdkVersion <= 0) {
        if (error)
            *error = "generated-file ledger is incomplete";
        return false;
    }
    *ledger = std::move(parsed);
    return true;
}

bool WriteGeneratedFileLedger(const fs::path &projectRoot, int sdkVersion, const std::vector<fs::path> &relativePaths,
                              std::string *error) {
    if (projectRoot.empty() || sdkVersion <= 0) {
        if (error)
            *error = "cannot write generated-file ledger without a project root and SDK version";
        return false;
    }

    std::vector<std::pair<std::string, std::string>> entries;
    entries.reserve(relativePaths.size());
    for (const fs::path &rawPath : relativePaths) {
        const fs::path relative = rawPath.lexically_normal();
        if (!IsSafeRelativePath(relative)) {
            if (error)
                *error = "generated-file ledger rejected an unsafe path";
            return false;
        }
        std::string hash;
        if (!HashFileSha256(projectRoot / relative, &hash, error))
            return false;
        entries.emplace_back(relative.generic_string(), std::move(hash));
    }
    std::sort(entries.begin(), entries.end());
    if (std::adjacent_find(entries.begin(), entries.end(), [](const auto &left, const auto &right) {
            return left.first == right.first;
        }) != entries.end()) {
        if (error)
            *error = "generated-file ledger received duplicate paths";
        return false;
    }

    const fs::path path = projectRoot / kGeneratedFileLedgerRelativePath;
    const fs::path temporary = path.parent_path() / "generated-files.ini.tmp";
    std::error_code ec;
    fs::create_directories(path.parent_path(), ec);
    if (ec) {
        if (error)
            *error = "cannot create generated-file ledger directory: " + ec.message();
        return false;
    }
    std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error)
            *error = "cannot write generated-file ledger";
        return false;
    }
    output << "# Managed by URKit. SHA-256 baselines for replaceable generated files.\n"
           << "schema_version=" << kGeneratedFileLedgerSchemaVersion << '\n'
           << "sdk_version=" << sdkVersion << '\n';
    for (const auto &[pathText, hash] : entries)
        output << "file=" << hash << '\t' << pathText << '\n';
    output.close();
    if (!output) {
        fs::remove(temporary, ec);
        if (error)
            *error = "cannot finish generated-file ledger";
        return false;
    }
    fs::rename(temporary, path, ec);
    if (ec) {
        fs::remove(path, ec);
        ec.clear();
        fs::rename(temporary, path, ec);
    }
    if (ec) {
        fs::remove(temporary, ec);
        if (error)
            *error = "cannot publish generated-file ledger: " + ec.message();
        return false;
    }
    return true;
}

} // namespace UrkProject
