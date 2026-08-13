#pragma once

#include "mod_sdk.h"

#include <cstdint>
#include <string>

struct NetworkHttpResult {
    bool completed = false;
    int32_t statusCode = 0;
    std::string body;
    std::string error;
    uint32_t flags = URK_NETWORK_RESULT_NONE;
};

bool NetworkHttp_Available();
bool NetworkHttp_JsonRequest(const URK_NetworkRequest *request, URK_NetworkResponse *response);
NetworkHttpResult NetworkHttp_RequestJson(uint32_t method, const std::string &url, const std::string &jsonBody,
                                          uint32_t timeoutMs, const std::string &pinnedPublicKey,
                                          size_t maxResponseBytes);
void NetworkHttp_Shutdown();
