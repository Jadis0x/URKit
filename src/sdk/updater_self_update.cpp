#include "updater_self_update.h"

#include "updater_version.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <sstream>
#include <string_view>
#include <system_error>
#include <vector>

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <bcrypt.h>
#include <winhttp.h>
#endif

namespace UrkUpdater {
namespace {

constexpr std::string_view kLatestReleasePath = "/repos/Jadis0x/URKit/releases/latest";
constexpr std::string_view kGitHubApiHost = "api.github.com";
constexpr std::string_view kExpectedDownloadPrefix = "https://github.com/Jadis0x/URKit/releases/download/";
constexpr size_t kMaxReleaseMetadataBytes = 1024 * 1024;
constexpr size_t kMaxUpdaterBytes = 64 * 1024 * 1024;

struct SemanticVersion {
    std::array<uint32_t, 3> parts{};
};

bool ParseSemanticVersion(std::string_view text, SemanticVersion *version) {
    if (!version)
        return false;
    if (!text.empty() && text.front() == 'v')
        text.remove_prefix(1);
    for (size_t partIndex = 0; partIndex < version->parts.size(); ++partIndex) {
        const size_t separator = text.find('.');
        const std::string_view part = separator == std::string_view::npos ? text : text.substr(0, separator);
        if (part.empty() || !std::all_of(part.begin(), part.end(), [](unsigned char character) {
                return std::isdigit(character) != 0;
            })) {
            return false;
        }
        uint64_t parsed = 0;
        for (const char character : part) {
            parsed = parsed * 10 + static_cast<uint64_t>(character - '0');
            if (parsed > std::numeric_limits<uint32_t>::max())
                return false;
        }
        version->parts[partIndex] = static_cast<uint32_t>(parsed);
        if (partIndex + 1 == version->parts.size())
            return separator == std::string_view::npos;
        if (separator == std::string_view::npos)
            return false;
        text.remove_prefix(separator + 1);
    }
    return false;
}

bool IsNewer(std::string_view candidate, std::string_view current) {
    SemanticVersion candidateVersion{};
    SemanticVersion currentVersion{};
    if (!ParseSemanticVersion(candidate, &candidateVersion) || !ParseSemanticVersion(current, &currentVersion))
        return false;
    return candidateVersion.parts > currentVersion.parts;
}

bool IsSha256Digest(std::string_view value) {
    constexpr std::string_view prefix = "sha256:";
    if (!value.starts_with(prefix) || value.size() != prefix.size() + 64)
        return false;
    return std::all_of(value.begin() + static_cast<std::ptrdiff_t>(prefix.size()), value.end(),
                       [](unsigned char character) { return std::isxdigit(character) != 0; });
}

std::string NormalizeDigest(std::string_view value) {
    std::string normalized(value.substr(std::string_view("sha256:").size()));
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    return normalized;
}

bool JsonStringField(std::string_view object, std::string_view name, std::string *value) {
    const std::string key = "\"" + std::string(name) + "\"";
    const size_t keyPosition = object.find(key);
    if (keyPosition == std::string_view::npos)
        return false;
    const size_t colon = object.find(':', keyPosition + key.size());
    if (colon == std::string_view::npos)
        return false;
    size_t valueStart = object.find_first_not_of(" \t\r\n", colon + 1);
    if (valueStart == std::string_view::npos || object[valueStart] != '"')
        return false;
    ++valueStart;
    std::string parsed;
    for (size_t index = valueStart; index < object.size(); ++index) {
        const char character = object[index];
        if (character == '"') {
            *value = std::move(parsed);
            return true;
        }
        if (character != '\\') {
            parsed.push_back(character);
            continue;
        }
        if (++index >= object.size())
            return false;
        const char escaped = object[index];
        switch (escaped) {
            case '"':
            case '\\':
            case '/':
                parsed.push_back(escaped);
                break;
            case 'b':
                parsed.push_back('\b');
                break;
            case 'f':
                parsed.push_back('\f');
                break;
            case 'n':
                parsed.push_back('\n');
                break;
            case 'r':
                parsed.push_back('\r');
                break;
            case 't':
                parsed.push_back('\t');
                break;
            default:
                // Release metadata fields used here are ASCII. Refuse unsupported escapes rather than misparse them.
                return false;
        }
    }
    return false;
}

size_t JsonObjectEnd(std::string_view text, size_t objectStart) {
    if (objectStart >= text.size() || text[objectStart] != '{')
        return std::string_view::npos;
    int depth = 0;
    bool inString = false;
    bool escaped = false;
    for (size_t index = objectStart; index < text.size(); ++index) {
        const char character = text[index];
        if (inString) {
            if (escaped) {
                escaped = false;
            } else if (character == '\\') {
                escaped = true;
            } else if (character == '"') {
                inString = false;
            }
            continue;
        }
        if (character == '"') {
            inString = true;
        } else if (character == '{') {
            ++depth;
        } else if (character == '}' && --depth == 0) {
            return index + 1;
        }
    }
    return std::string_view::npos;
}

bool FindUpdaterAsset(std::string_view document, std::string *downloadUrl, std::string *digest) {
    const size_t assetsKey = document.find("\"assets\"");
    if (assetsKey == std::string_view::npos)
        return false;
    const size_t arrayStart = document.find('[', assetsKey);
    if (arrayStart == std::string_view::npos)
        return false;
    for (size_t index = arrayStart + 1; index < document.size(); ++index) {
        if (document[index] == ']')
            return false;
        if (document[index] != '{')
            continue;
        const size_t objectEnd = JsonObjectEnd(document, index);
        if (objectEnd == std::string_view::npos)
            return false;
        const std::string_view asset = document.substr(index, objectEnd - index);
        std::string name;
        if (JsonStringField(asset, "name", &name) && name == "urk-updater.exe") {
            return JsonStringField(asset, "browser_download_url", downloadUrl) && JsonStringField(asset, "digest", digest);
        }
        index = objectEnd - 1;
    }
    return false;
}

#ifdef _WIN32

class InternetHandle {
  public:
    InternetHandle() = default;
    explicit InternetHandle(HINTERNET value) : value_(value) {}
    ~InternetHandle() {
        if (value_)
            WinHttpCloseHandle(value_);
    }
    InternetHandle(const InternetHandle &) = delete;
    InternetHandle &operator=(const InternetHandle &) = delete;
    InternetHandle(InternetHandle &&other) noexcept : value_(other.value_) { other.value_ = nullptr; }
    InternetHandle &operator=(InternetHandle &&other) noexcept {
        if (this != &other) {
            if (value_)
                WinHttpCloseHandle(value_);
            value_ = other.value_;
            other.value_ = nullptr;
        }
        return *this;
    }
    HINTERNET get() const { return value_; }
    explicit operator bool() const { return value_ != nullptr; }

  private:
    HINTERNET value_ = nullptr;
};

std::string WindowsError(DWORD code) {
    return std::system_category().message(static_cast<int>(code));
}

std::wstring Utf8ToWide(std::string_view value) {
    if (value.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring output(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(),
                            length) != length) {
        return {};
    }
    return output;
}

struct HttpsUrl {
    std::wstring host;
    std::wstring path;
};

bool ParseHttpsUrl(std::string_view value, HttpsUrl *url) {
    constexpr std::string_view prefix = "https://";
    if (!url || !value.starts_with(prefix))
        return false;
    value.remove_prefix(prefix.size());
    const size_t pathStart = value.find('/');
    const std::string_view host = pathStart == std::string_view::npos ? value : value.substr(0, pathStart);
    const std::string_view path = pathStart == std::string_view::npos ? std::string_view("/") : value.substr(pathStart);
    if (host.empty() || host.find_first_of("?#@:") != std::string_view::npos)
        return false;
    url->host = Utf8ToWide(host);
    url->path = Utf8ToWide(path);
    return !url->host.empty() && !url->path.empty();
}

bool OpenGetRequest(const HttpsUrl &url, InternetHandle *session, InternetHandle *connection, InternetHandle *request,
                    std::string *error) {
    session->operator=(InternetHandle(WinHttpOpen(L"URKit Updater/0.3.0", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                                  WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0)));
    if (!*session) {
        if (error)
            *error = "cannot start HTTPS update check: " + WindowsError(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(session->get(), 5000, 5000, 15000, 30000);
    connection->operator=(InternetHandle(WinHttpConnect(session->get(), url.host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0)));
    if (!*connection) {
        if (error)
            *error = "cannot connect to update service: " + WindowsError(GetLastError());
        return false;
    }
    request->operator=(InternetHandle(WinHttpOpenRequest(connection->get(), L"GET", url.path.c_str(), nullptr,
                                                          WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                          WINHTTP_FLAG_SECURE)));
    if (!*request) {
        if (error)
            *error = "cannot create HTTPS update request: " + WindowsError(GetLastError());
        return false;
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_ALWAYS;
    WinHttpSetOption(request->get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(request->get(), headers, static_cast<DWORD>(std::size(headers) - 1), WINHTTP_NO_REQUEST_DATA,
                            0, 0, 0) ||
        !WinHttpReceiveResponse(request->get(), nullptr)) {
        if (error)
            *error = "HTTPS update request failed: " + WindowsError(GetLastError());
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(request->get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER, WINHTTP_HEADER_NAME_BY_INDEX,
                             &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        if (error)
            *error = "update service returned HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

bool ReadResponse(InternetHandle &request, size_t maximumBytes, std::string *response, std::string *error) {
    response->clear();
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            if (error)
                *error = "cannot read update response: " + WindowsError(GetLastError());
            return false;
        }
        if (available == 0)
            return true;
        if (available > maximumBytes - response->size()) {
            if (error)
                *error = "update response exceeds the allowed size";
            return false;
        }
        const size_t previousSize = response->size();
        response->resize(previousSize + available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), response->data() + previousSize, available, &read)) {
            if (error)
                *error = "cannot read update response: " + WindowsError(GetLastError());
            return false;
        }
        response->resize(previousSize + read);
    }
}

bool DownloadFile(const std::string &urlText, const std::filesystem::path &destination, std::string *error) {
    HttpsUrl url;
    if (!ParseHttpsUrl(urlText, &url)) {
        if (error)
            *error = "release updater download URL is not a valid HTTPS URL";
        return false;
    }
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
    if (!OpenGetRequest(url, &session, &connection, &request, error))
        return false;

    std::error_code filesystemError;
    std::filesystem::create_directories(destination.parent_path(), filesystemError);
    if (filesystemError) {
        if (error)
            *error = "cannot create updater download directory: " + filesystemError.message();
        return false;
    }
    std::ofstream output(destination, std::ios::binary | std::ios::trunc);
    if (!output) {
        if (error)
            *error = "cannot create updater download: " + destination.string();
        return false;
    }

    size_t total = 0;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(request.get(), &available)) {
            if (error)
                *error = "cannot download updater: " + WindowsError(GetLastError());
            return false;
        }
        if (available == 0)
            break;
        if (available > kMaxUpdaterBytes - total) {
            if (error)
                *error = "updater download exceeds the allowed size";
            return false;
        }
        std::vector<char> buffer(available);
        DWORD read = 0;
        if (!WinHttpReadData(request.get(), buffer.data(), available, &read)) {
            if (error)
                *error = "cannot download updater: " + WindowsError(GetLastError());
            return false;
        }
        output.write(buffer.data(), read);
        if (!output) {
            if (error)
                *error = "cannot write updater download: " + destination.string();
            return false;
        }
        total += read;
    }
    output.close();
    if (!output) {
        if (error)
            *error = "cannot finish updater download: " + destination.string();
        return false;
    }
    return true;
}

bool HashFileSha256(const std::filesystem::path &path, std::string *digest, std::string *error) {
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    std::vector<UCHAR> hashObject;
    std::vector<UCHAR> hashValue;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status < 0) {
        if (error)
            *error = "cannot initialize SHA-256 verification";
        return false;
    }
    struct AlgorithmGuard {
        BCRYPT_ALG_HANDLE value = nullptr;
        ~AlgorithmGuard() {
            if (value)
                BCryptCloseAlgorithmProvider(value, 0);
        }
    } algorithmGuard{algorithm};

    DWORD objectLength = 0;
    DWORD resultLength = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength),
                               &resultLength, 0);
    if (status < 0) {
        if (error)
            *error = "cannot initialize SHA-256 verification";
        return false;
    }
    DWORD hashLength = 0;
    status = BCryptGetProperty(algorithm, BCRYPT_HASH_LENGTH, reinterpret_cast<PUCHAR>(&hashLength), sizeof(hashLength),
                               &resultLength, 0);
    if (status < 0 || hashLength != 32) {
        if (error)
            *error = "cannot initialize SHA-256 verification";
        return false;
    }
    hashObject.resize(objectLength);
    hashValue.resize(hashLength);
    status = BCryptCreateHash(algorithm, &hash, hashObject.data(), static_cast<ULONG>(hashObject.size()), nullptr, 0, 0);
    if (status < 0) {
        if (error)
            *error = "cannot initialize SHA-256 verification";
        return false;
    }
    struct HashGuard {
        BCRYPT_HASH_HANDLE value = nullptr;
        ~HashGuard() {
            if (value)
                BCryptDestroyHash(value);
        }
    } hashGuard{hash};

    std::ifstream input(path, std::ios::binary);
    if (!input) {
        if (error)
            *error = "cannot open downloaded updater for SHA-256 verification";
        return false;
    }
    std::array<char, 64 * 1024> buffer{};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const std::streamsize count = input.gcount();
        if (count > 0 && BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(count), 0) < 0) {
            if (error)
                *error = "cannot calculate updater SHA-256";
            return false;
        }
    }
    if (input.bad() || BCryptFinishHash(hash, hashValue.data(), static_cast<ULONG>(hashValue.size()), 0) < 0) {
        if (error)
            *error = "cannot calculate updater SHA-256";
        return false;
    }

    static constexpr char kHex[] = "0123456789abcdef";
    digest->clear();
    digest->reserve(hashValue.size() * 2);
    for (const UCHAR byte : hashValue) {
        digest->push_back(kHex[byte >> 4]);
        digest->push_back(kHex[byte & 0x0f]);
    }
    return true;
}

bool ModulePath(std::filesystem::path *path, std::string *error) {
    std::vector<wchar_t> buffer(32768);
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length >= buffer.size()) {
        if (error)
            *error = "cannot resolve the running updater path: " + WindowsError(GetLastError());
        return false;
    }
    *path = std::filesystem::path(std::wstring(buffer.data(), length));
    return true;
}

std::wstring QuoteArgument(const std::filesystem::path &path) {
    return L"\"" + path.wstring() + L"\"";
}

#endif

} // namespace

bool CheckForUpdate(UpdateCheckResult *result, AvailableUpdate *available, std::string *error) {
    if (error)
        error->clear();
    if (!result) {
        if (error)
            *error = "self-update check output is null";
        return false;
    }
    result->currentVersion = std::string(kVersion);
    if (available)
        *available = {};
#ifndef _WIN32
    if (error)
        *error = "self-update is supported only on Windows";
    return false;
#else
    HttpsUrl apiUrl;
    if (!ParseHttpsUrl("https://" + std::string(kGitHubApiHost) + std::string(kLatestReleasePath), &apiUrl)) {
        if (error)
            *error = "cannot construct update service URL";
        return false;
    }
    InternetHandle session;
    InternetHandle request;
    InternetHandle connection;
    if (!OpenGetRequest(apiUrl, &session, &connection, &request, error))
        return false;
    std::string document;
    if (!ReadResponse(request, kMaxReleaseMetadataBytes, &document, error))
        return false;

    std::string tag;
    std::string releaseUrl;
    SemanticVersion parsedTag{};
    if (!JsonStringField(document, "tag_name", &tag) || !JsonStringField(document, "html_url", &releaseUrl) ||
        !ParseSemanticVersion(tag, &parsedTag)) {
        if (error)
            *error = "update service returned invalid release metadata";
        return false;
    }
    result->latestVersion = tag;
    result->releaseUrl = releaseUrl;
    result->updateAvailable = IsNewer(tag, kVersion);
    if (!result->updateAvailable)
        return true;

    std::string downloadUrl;
    std::string digest;
    if (!FindUpdaterAsset(document, &downloadUrl, &digest)) {
        if (error)
            *error = "latest release " + tag + " does not publish urk-updater.exe yet";
        return false;
    }
    if (!downloadUrl.starts_with(kExpectedDownloadPrefix) || !IsSha256Digest(digest)) {
        if (error)
            *error = "latest release updater asset does not have a trusted download URL and SHA-256 digest";
        return false;
    }
    if (available) {
        available->currentVersion = std::string(kVersion);
        available->availableVersion = tag;
        available->releaseUrl = releaseUrl;
        available->downloadUrl = downloadUrl;
        available->sha256 = NormalizeDigest(digest);
    }
    return true;
#endif
}

bool DownloadAndRestart(const AvailableUpdate &update, std::string *error) {
    if (error)
        error->clear();
#ifndef _WIN32
    if (error)
        *error = "self-update is supported only on Windows";
    return false;
#else
    if (update.downloadUrl.empty() || update.sha256.size() != 64 || !std::all_of(update.sha256.begin(), update.sha256.end(),
                                                                                 [](unsigned char character) {
                                                                                     return std::isxdigit(character) != 0;
                                                                                 })) {
        if (error)
            *error = "self-update metadata is incomplete or invalid";
        return false;
    }
    std::filesystem::path target;
    if (!ModulePath(&target, error))
        return false;
    std::error_code filesystemError;
    const std::filesystem::path temporaryRoot = std::filesystem::temp_directory_path(filesystemError);
    if (filesystemError) {
        if (error)
            *error = "cannot resolve updater download directory: " + filesystemError.message();
        return false;
    }
    const std::filesystem::path download = temporaryRoot /
                                           ("urk-updater-" + update.availableVersion + "-" +
                                            std::to_string(GetCurrentProcessId()) + ".exe");
    const std::filesystem::path helper = target.parent_path() / "urk-updater.update-helper.exe";
    std::filesystem::remove(download, filesystemError);
    if (!DownloadFile(update.downloadUrl, download, error)) {
        std::filesystem::remove(download, filesystemError);
        return false;
    }
    std::string actualDigest;
    if (!HashFileSha256(download, &actualDigest, error)) {
        std::filesystem::remove(download, filesystemError);
        return false;
    }
    if (actualDigest != update.sha256) {
        std::filesystem::remove(download, filesystemError);
        if (error)
            *error = "downloaded updater SHA-256 does not match the published release digest";
        return false;
    }
    // The helper is a stable sibling filename. Replacing an old helper keeps
    // subsequent updater releases from failing solely because a prior helper
    // binary remains next to the updater.
    if (!CopyFileW(target.c_str(), helper.c_str(), FALSE)) {
        std::filesystem::remove(download, filesystemError);
        if (error)
            *error = "cannot prepare self-update helper: " + WindowsError(GetLastError());
        return false;
    }
    std::wstring command = QuoteArgument(helper) + L" --apply-self-update --source " + QuoteArgument(download) +
                           L" --target " + QuoteArgument(target) + L" --sha256 " + Utf8ToWide(update.sha256) +
                           L" --wait-pid " + std::to_wstring(GetCurrentProcessId());
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        target.parent_path().c_str(), &startup, &process)) {
        std::filesystem::remove(download, filesystemError);
        if (error)
            *error = "cannot start self-update helper: " + WindowsError(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#endif
}

bool ApplyDownloadedUpdate(const std::filesystem::path &source, const std::filesystem::path &target,
                           const std::string &expectedSha256, uint32_t waitForProcessId, std::string *error) {
    if (error)
        error->clear();
#ifndef _WIN32
    if (error)
        *error = "self-update is supported only on Windows";
    return false;
#else
    if (source.empty() || target.empty() || expectedSha256.size() != 64 ||
        !std::all_of(expectedSha256.begin(), expectedSha256.end(),
                     [](unsigned char character) { return std::isxdigit(character) != 0; })) {
        if (error)
            *error = "self-update helper received invalid arguments";
        return false;
    }
    if (waitForProcessId != 0) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, waitForProcessId);
        if (process) {
            const DWORD wait = WaitForSingleObject(process, 60000);
            CloseHandle(process);
            if (wait != WAIT_OBJECT_0) {
                if (error)
                    *error = "timed out waiting for the running updater to exit";
                return false;
            }
        }
    }
    std::string actualDigest;
    if (!HashFileSha256(source, &actualDigest, error))
        return false;
    std::string expected = expectedSha256;
    std::transform(expected.begin(), expected.end(), expected.begin(), [](unsigned char character) {
        return static_cast<char>(std::tolower(character));
    });
    if (actualDigest != expected) {
        if (error)
            *error = "downloaded updater SHA-256 changed before replacement";
        return false;
    }
    if (!MoveFileExW(source.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        if (error)
            *error = "cannot replace urk-updater.exe: " + WindowsError(GetLastError());
        return false;
    }
    std::wstring command = QuoteArgument(target);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(target.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        target.parent_path().c_str(), &startup, &process)) {
        if (error)
            *error = "updated urk-updater.exe was installed but could not be restarted: " + WindowsError(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#endif
}

} // namespace UrkUpdater
