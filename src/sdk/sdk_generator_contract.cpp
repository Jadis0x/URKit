#include "sdk_generator_contract.h"

#include <filesystem>
#include <system_error>

namespace SdkGenerator {
namespace {

namespace fs = std::filesystem;
using ModProjectGenerator::OutputFilePolicy;

bool RecreateDirectory(const fs::path &path, std::string *error) {
    std::error_code ec;
    fs::remove_all(path, ec);
    if (ec) {
        if (error)
            *error = "cannot clean " + path.string() + ": " + ec.message();
        return false;
    }
    fs::create_directories(path, ec);
    if (ec) {
        if (error)
            *error = "cannot create " + path.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool CopyFileToDestination(const fs::path &from, const fs::path &to, std::string *error) {
    if (from.empty()) {
        if (error)
            *error = "source path is empty for " + to.string();
        return false;
    }
    std::error_code ec;
    if (const auto parent = to.parent_path(); !parent.empty()) {
        fs::create_directories(parent, ec);
        if (ec) {
            if (error)
                *error = "cannot create " + parent.string() + ": " + ec.message();
            return false;
        }
    }
    if (fs::equivalent(from, to, ec) && !ec)
        return true;
    ec.clear();
    fs::copy_file(from, to, fs::copy_options::overwrite_existing, ec);
    if (ec) {
        if (error)
            *error = "cannot copy " + from.string() + " to " + to.string() + ": " + ec.message();
        return false;
    }
    return true;
}

bool MoveDirectoryIntoPlace(const fs::path &from, const fs::path &to, std::string *error) {
    const fs::path backup = to.parent_path() / (to.filename().string() + ".previous");
    std::error_code ec;
    fs::remove_all(backup, ec);
    if (ec) {
        if (error)
            *error = "cannot clean " + backup.string() + ": " + ec.message();
        return false;
    }
    if (fs::exists(to, ec)) {
        ec.clear();
        fs::rename(to, backup, ec);
        if (ec) {
            if (error)
                *error = "cannot stage previous output " + to.string() + ": " + ec.message();
            return false;
        }
    }
    ec.clear();
    fs::rename(from, to, ec);
    if (ec) {
        std::error_code restore;
        if (fs::exists(backup, restore))
            fs::rename(backup, to, restore);
        if (error)
            *error = "cannot publish output " + to.string() + ": " + ec.message();
        return false;
    }
    fs::remove_all(backup, ec);
    return true;
}

bool IsRegularNonEmpty(const fs::path &path) {
    std::error_code ec;
    return fs::is_regular_file(path, ec) && !ec && fs::file_size(path, ec) > 0 && !ec;
}

bool MaterializeFile(const OutputFile &file, const fs::path &destination, std::string *error) {
    if (!file.sourcePath.empty())
        return CopyFileToDestination(file.sourcePath, destination, error);
    return ModProjectGenerator::WriteText(destination, file.contents, error);
}

bool WriteFileByPolicy(const OutputPlan &plan, const OutputFile &file, std::string *error) {
    const fs::path destination = plan.root / file.relativePath;
    switch (file.policy) {
        case OutputFilePolicy::GeneratedOverwrite:
            return MaterializeFile(file, destination, error);
        case OutputFilePolicy::EditablePreserve:
            if (plan.preserveEditableFiles && fs::exists(destination))
                return true;
            return MaterializeFile(file, destination, error);
        case OutputFilePolicy::DocumentationPreserve:
            if (fs::exists(destination))
                return true;
            return MaterializeFile(file, destination, error);
    }
    if (error)
        *error = "unknown output policy for " + file.relativePath.generic_string();
    return false;
}

OutputPlan PlanForRoot(const OutputPlan &plan, const fs::path &root) {
    OutputPlan copy = plan;
    copy.root = root;
    return copy;
}

} // namespace

bool WriteOutputPlan(const OutputPlan &plan, OutputResult *result, std::string *error) {
    if (plan.root.empty()) {
        if (error)
            *error = "output root is empty";
        return false;
    }
    std::error_code ec;
    fs::create_directories(plan.root, ec);
    if (ec) {
        if (error)
            *error = "cannot create " + plan.root.string() + ": " + ec.message();
        return false;
    }
    for (const auto &file : plan.files) {
        if (!WriteFileByPolicy(plan, file, error))
            return false;
    }
    if (result) {
        result->moduleFiles.clear();
        result->requiredFiles.clear();
        for (const auto &file : plan.files) {
            if (file.moduleFile)
                result->moduleFiles.push_back(file.relativePath);
            if (file.required)
                result->requiredFiles.push_back(file.relativePath);
        }
    }
    return ValidateOutputPlan(plan, error);
}

bool ValidateOutputPlan(const OutputPlan &plan, std::string *error) {
    for (const auto &file : plan.files) {
        if (!file.required)
            continue;
        const fs::path fullPath = plan.root / file.relativePath;
        if (!IsRegularNonEmpty(fullPath)) {
            if (error)
                *error = "missing or empty generated output: " + fullPath.string();
            return false;
        }
    }
    return true;
}

bool PublishOutputPlanAtomically(const OutputPlan &plan, OutputResult *result, std::string *error) {
    if (plan.root.empty()) {
        if (error)
            *error = "output root is empty";
        return false;
    }
    const fs::path stageRoot = plan.root.parent_path() / (plan.root.filename().string() + ".tmp");
    if (!RecreateDirectory(stageRoot, error))
        return false;
    const OutputPlan stagePlan = PlanForRoot(plan, stageRoot);
    if (!WriteOutputPlan(stagePlan, result, error)) {
        std::error_code cleanup;
        fs::remove_all(stageRoot, cleanup);
        return false;
    }
    if (!MoveDirectoryIntoPlace(stageRoot, plan.root, error)) {
        std::error_code cleanup;
        fs::remove_all(stageRoot, cleanup);
        return false;
    }
    return ValidateOutputPlan(plan, error);
}

} // namespace SdkGenerator
