#include "network_http.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstring>
#include <mutex>
#include <string>

#ifdef URK_WITH_CURL
#include <curl/curl.h>
#endif

namespace {
constexpr uint32_t kDefaultTimeoutMs = 5000;
constexpr uint32_t kMaxTimeoutMs = 30000;
constexpr size_t kMaxRequestBodyBytes = 1024 * 1024;
constexpr size_t kDefaultMaxResponseBytes = 1024 * 1024;

const std::string &JsonAcceptHeader() {
    static const std::string value = "Accept: application/json";
    return value;
}

const std::string &JsonContentTypeHeader() {
    static const std::string value = "Content-Type: application/json";
    return value;
}

const std::string &UserAgent() {
    static const std::string value = "URKit/1";
    return value;
}

void CopyText(char *dst, size_t dstSize, const std::string &src, uint32_t *flags, uint32_t truncatedFlag) {
    if (!dst || dstSize == 0)
        return;
    const size_t copyLen = std::min(dstSize - 1, src.size());
    if (copyLen > 0)
        std::memcpy(dst, src.data(), copyLen);
    dst[copyLen] = '\0';
    if (copyLen < src.size() && flags)
        *flags |= truncatedFlag;
}

bool IsHttpsUrl(const char *url) {
    if (!url)
        return false;
    constexpr char prefix[] = "https://";
    for (size_t i = 0; i < sizeof(prefix) - 1; ++i) {
        if (std::tolower(static_cast<unsigned char>(url[i])) != prefix[i])
            return false;
    }
    return true;
}

uint32_t ClampTimeout(uint32_t timeoutMs) {
    if (timeoutMs == 0)
        return kDefaultTimeoutMs;
    return std::clamp(timeoutMs, 1000u, kMaxTimeoutMs);
}

const char *MethodText(uint32_t method) {
    switch (method) {
        case URK_NETWORK_HTTP_GET:
            return "GET";
        case URK_NETWORK_HTTP_POST:
            return "POST";
        case URK_NETWORK_HTTP_PUT:
            return "PUT";
        case URK_NETWORK_HTTP_PATCH:
            return "PATCH";
        case URK_NETWORK_HTTP_DELETE:
            return "DELETE";
        default:
            return nullptr;
    }
}

bool HeaderNameAllowed(const char *name) {
    if (!name || !name[0])
        return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(name); *p; ++p) {
        if (*p <= 32 || *p >= 127 || *p == ':')
            return false;
    }
    return true;
}

bool HeaderValueAllowed(const char *value) {
    if (!value)
        return false;
    for (const unsigned char *p = reinterpret_cast<const unsigned char *>(value); *p; ++p) {
        if (*p == '\r' || *p == '\n')
            return false;
    }
    return true;
}

#ifdef URK_WITH_CURL
std::once_flag g_curlInitOnce;
std::atomic_bool g_curlReady{false};

bool EnsureCurl() {
    std::call_once(g_curlInitOnce, [] {
        g_curlReady.store(curl_global_init(CURL_GLOBAL_DEFAULT) == CURLE_OK, std::memory_order_release);
    });
    return g_curlReady.load(std::memory_order_acquire);
}

struct WriteState {
    std::string *body = nullptr;
    size_t maxBytes = 0;
    bool truncated = false;
};

size_t WriteBody(char *ptr, size_t size, size_t nmemb, void *userdata) {
    auto *state = static_cast<WriteState *>(userdata);
    if (size != 0 && nmemb > static_cast<size_t>(-1) / size)
        return 0;
    const size_t bytes = size * nmemb;
    if (!state || !state->body)
        return 0;
    if (bytes == 0)
        return 0;
    if (state->body->size() + bytes > state->maxBytes) {
        const size_t remaining = state->maxBytes > state->body->size() ? state->maxBytes - state->body->size() : 0;
        if (remaining > 0)
            state->body->append(ptr, remaining);
        state->truncated = true;
        return 0;
    }
    state->body->append(ptr, bytes);
    return bytes;
}

void ApplyMethod(CURL *curl, uint32_t method, const std::string &body) {
    switch (method) {
        case URK_NETWORK_HTTP_GET:
            curl_easy_setopt(curl, CURLOPT_HTTPGET, 1L);
            break;
        case URK_NETWORK_HTTP_POST:
            curl_easy_setopt(curl, CURLOPT_POST, 1L);
            curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
            curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
            break;
        default:
            curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, MethodText(method));
            if (method != URK_NETWORK_HTTP_DELETE || !body.empty()) {
                curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
                curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, static_cast<long>(body.size()));
            }
            break;
    }
}

NetworkHttpResult PerformJsonRequest(const URK_NetworkRequest &request, size_t maxResponseBytes) {
    NetworkHttpResult result;
    if (!EnsureCurl()) {
        result.error = "libcurl global initialization failed";
        return result;
    }
    if (!IsHttpsUrl(request.url)) {
        result.error = "HTTPS URL is required";
        return result;
    }
    if (request.headerCount > 0 && !request.headers) {
        result.error = "header count is non-zero but headers is null";
        return result;
    }
    if (request.headerCount > 32) {
        result.error = "too many HTTP headers";
        return result;
    }
    if (!MethodText(request.method)) {
        result.error = "unsupported HTTP method";
        return result;
    }
    if (request.jsonBody && std::strlen(request.jsonBody) > kMaxRequestBodyBytes) {
        result.error = "request body exceeded maximum size";
        return result;
    }

    CURL *curl = curl_easy_init();
    if (!curl) {
        result.error = "curl_easy_init failed";
        return result;
    }

    char curlError[CURL_ERROR_SIZE]{};
    WriteState writeState{&result.body, maxResponseBytes, false};
    struct curl_slist *headers = nullptr;
    headers = curl_slist_append(headers, JsonAcceptHeader().c_str());
    headers = curl_slist_append(headers, JsonContentTypeHeader().c_str());

    for (size_t i = 0; i < request.headerCount; ++i) {
        const auto &header = request.headers[i];
        if (!HeaderNameAllowed(header.name) || !HeaderValueAllowed(header.value)) {
            result.error = "invalid HTTP header";
            curl_slist_free_all(headers);
            curl_easy_cleanup(curl);
            return result;
        }
        std::string line(header.name);
        line += ": ";
        line += header.value;
        headers = curl_slist_append(headers, line.c_str());
    }

    const std::string body = request.jsonBody ? request.jsonBody : "";
    curl_easy_setopt(curl, CURLOPT_URL, request.url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, curlError);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, &WriteBody);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &writeState);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, UserAgent().c_str());
    curl_easy_setopt(curl, CURLOPT_TIMEOUT_MS, static_cast<long>(ClampTimeout(request.timeoutMs)));
    curl_easy_setopt(curl, CURLOPT_CONNECTTIMEOUT_MS, static_cast<long>(ClampTimeout(request.timeoutMs)));
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 0L);
    curl_easy_setopt(curl, CURLOPT_PROXY, "");
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 1L);
    curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 2L);
    curl_easy_setopt(curl, CURLOPT_PROTOCOLS_STR, "https");
    curl_easy_setopt(curl, CURLOPT_REDIR_PROTOCOLS_STR, "https");
    if (request.pinnedPublicKey && request.pinnedPublicKey[0])
        curl_easy_setopt(curl, CURLOPT_PINNEDPUBLICKEY, request.pinnedPublicKey);
    ApplyMethod(curl, request.method, body);

    const CURLcode code = curl_easy_perform(curl);
    long status = 0;
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &status);
    result.statusCode = static_cast<int32_t>(status);
    result.flags = writeState.truncated ? URK_NETWORK_RESULT_BODY_TRUNCATED : URK_NETWORK_RESULT_NONE;

    if (code == CURLE_OK) {
        result.completed = true;
    } else if (writeState.truncated) {
        result.error = "response body exceeded maximum size";
    } else if (curlError[0]) {
        result.error = curlError;
    } else {
        result.error = curl_easy_strerror(code);
    }

    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    return result;
}
#endif

} // namespace

bool NetworkHttp_Available() {
#ifdef URK_WITH_CURL
    return EnsureCurl();
#else
    return false;
#endif
}

bool NetworkHttp_JsonRequest(const URK_NetworkRequest *request, URK_NetworkResponse *response) {
    if (!response || response->size < sizeof(URK_NetworkResponse))
        return false;
    response->statusCode = 0;
    response->bodyLength = 0;
    response->flags = URK_NETWORK_RESULT_NONE;
    if (response->body && response->bodyCapacity)
        response->body[0] = '\0';
    if (response->error && response->errorCapacity)
        response->error[0] = '\0';

    if (!request || request->size < sizeof(URK_NetworkRequest)) {
        std::string error = "network request struct is too small";
        CopyText(response->error, response->errorCapacity, error, &response->flags, URK_NETWORK_RESULT_ERROR_TRUNCATED);
        return false;
    }

#ifdef URK_WITH_CURL
    const NetworkHttpResult result = PerformJsonRequest(*request, kDefaultMaxResponseBytes);
    response->statusCode = result.statusCode;
    response->bodyLength = result.body.size();
    response->flags |= result.flags;
    CopyText(response->body, response->bodyCapacity, result.body, &response->flags, URK_NETWORK_RESULT_BODY_TRUNCATED);
    CopyText(response->error, response->errorCapacity, result.error, &response->flags,
             URK_NETWORK_RESULT_ERROR_TRUNCATED);
    return result.completed;
#else
    std::string error = "network API was built without libcurl";
    CopyText(response->error, response->errorCapacity, error, &response->flags, URK_NETWORK_RESULT_ERROR_TRUNCATED);
    return false;
#endif
}

NetworkHttpResult NetworkHttp_RequestJson(uint32_t method, const std::string &url, const std::string &jsonBody,
                                          uint32_t timeoutMs, const std::string &pinnedPublicKey,
                                          size_t maxResponseBytes) {
    URK_NetworkRequest request{};
    request.size = sizeof(request);
    request.method = method;
    request.url = url.c_str();
    request.jsonBody = jsonBody.c_str();
    request.timeoutMs = timeoutMs;
    request.pinnedPublicKey = pinnedPublicKey.empty() ? nullptr : pinnedPublicKey.c_str();
#ifdef URK_WITH_CURL
    return PerformJsonRequest(request, maxResponseBytes);
#else
    (void)maxResponseBytes;
    NetworkHttpResult result;
    result.error = "network API was built without libcurl";
    return result;
#endif
}

void NetworkHttp_Shutdown() {
#ifdef URK_WITH_CURL
    if (g_curlReady.exchange(false, std::memory_order_acq_rel))
        curl_global_cleanup();
#endif
}
