#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <iosfwd>
#include <mutex>
#include <string>

namespace URK::DevMcp {

class StdioTransport {
  public:
    enum class ReadResult {
        Message,
        TooLarge,
        End,
        Error
    };

    StdioTransport(std::istream &input, std::ostream &output, std::size_t inputLimit, std::size_t outputLimit);
    ReadResult Read(std::string *line);
    bool Emit(const nlohmann::json &message);

  private:
    std::istream &input_;
    std::ostream &output_;
    std::size_t inputLimit_;
    std::size_t outputLimit_;
    std::mutex outputMutex_;
};

}
