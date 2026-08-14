#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <mutex>
#include <string>

namespace URK::DevMcp {

class ProcessCancellation;

struct ServiceResult {
    bool ok = false;
    nlohmann::json value = nlohmann::json::object();
    std::string code;
    std::string message;
};

class ProjectService {
  public:
    explicit ProjectService(std::filesystem::path projectRoot);

    ServiceResult ProjectInfo() const;
    ServiceResult Build(std::string preset, ProcessCancellation *cancellation = nullptr);
    ServiceResult Deploy(std::string preset);
    ServiceResult ReadLogs(std::size_t maximumLines) const;
    const std::filesystem::path &ProjectRoot() const;

  private:
    ServiceResult LoadProject(nlohmann::json *presets = nullptr) const;
    bool IsPresetAllowed(const std::string &preset, nlohmann::json *presets, std::string *error) const;

    std::filesystem::path projectRoot_;
    mutable std::mutex mutationMutex_;
};

ServiceResult ServiceFailure(std::string code, std::string message);
ServiceResult ServiceSuccess(nlohmann::json value);

}
