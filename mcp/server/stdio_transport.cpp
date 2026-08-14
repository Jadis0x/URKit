#include "stdio_transport.h"

#include <istream>
#include <ostream>

namespace URK::DevMcp {

StdioTransport::StdioTransport(std::istream &input, std::ostream &output, std::size_t inputLimit,
                               std::size_t outputLimit)
    : input_(input), output_(output), inputLimit_(inputLimit), outputLimit_(outputLimit) {
}

StdioTransport::ReadResult StdioTransport::Read(std::string *line) {
    if (!line)
        return ReadResult::Error;
    line->clear();
    bool tooLarge = false;
    for (;;) {
        const int value = input_.get();
        if (value == std::char_traits<char>::eof()) {
            if (input_.bad())
                return ReadResult::Error;
            if (line->empty() && !tooLarge)
                return ReadResult::End;
            return tooLarge ? ReadResult::TooLarge : ReadResult::Message;
        }
        if (value == '\n')
            return tooLarge ? ReadResult::TooLarge : ReadResult::Message;
        if (value == '\r')
            continue;
        if (line->size() < inputLimit_)
            line->push_back(static_cast<char>(value));
        else
            tooLarge = true;
    }
}

bool StdioTransport::Emit(const nlohmann::json &message) {
    std::string serialized = message.dump();
    if (serialized.size() > outputLimit_) {
        const nlohmann::json id = message.is_object() && message.contains("id") ? message["id"] : nullptr;
        serialized = nlohmann::json{{"jsonrpc", "2.0"},
                                    {"id", id},
                                    {"error", {{"code", -32603}, {"message", "MCP response exceeded the limit"}}}}
                         .dump();
    }
    std::lock_guard lock(outputMutex_);
    output_ << serialized << '\n' << std::flush;
    return output_.good();
}

}
