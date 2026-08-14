#pragma once

#include <nlohmann/json.hpp>

#include <string>

namespace URK::DevBridge {

nlohmann::json ListRuntimeTests();
bool RunRuntimeTest(const std::string &qualifiedName, nlohmann::json *result, std::string *code, std::string *error);

}
