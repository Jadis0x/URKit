#include "self_update.h"

#include "generator_service.h"
#include "project_ledger.h"
#include "project_version.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <limits>
#include <string_view>
#include <system_error>
#include <vector>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <winhttp.h>

namespace UrkApp {
namespace {

namespace fs = std::filesystem;

constexpr std::wstring_view kGitHubApiHost = L"api.github.com";
constexpr std::wstring_view kLatestReleasePath = L"/repos/Jadis0x/URKit/releases/latest";
constexpr std::string_view kExpectedDownloadPrefix = "https://github.com/Jadis0x/URKit/releases/download/";
constexpr size_t kMaxReleaseMetadataBytes = 1024 * 1024;
constexpr size_t kMaxDownloadBytes = 128 * 1024 * 1024;
constexpr std::wstring_view kSelfUpdateHelperName = L"urk.update-helper.exe";

struct SemanticVersion {
    std::array<uint32_t, 3> parts{};
};

bool ParseSemanticVersion(std::string_view text, SemanticVersion *version) {
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

bool IsHexDigest(std::string_view value) {
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](unsigned char character) {
               return std::isxdigit(character) != 0;
           });
}

std::string WindowsError(DWORD code) {
    return std::system_category().message(static_cast<int>(code));
}

std::wstring Widen(std::string_view value) {
    if (value.empty())
        return {};
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()),
                                           nullptr, 0);
    if (length <= 0)
        return {};
    std::wstring output(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), static_cast<int>(value.size()), output.data(),
                        length);
    return output;
}

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

struct HttpsRequest {
    InternetHandle session;
    InternetHandle connection;
    InternetHandle request;
};

bool ParseHttpsUrl(std::string_view value, std::wstring *host, std::wstring *path) {
    constexpr std::string_view prefix = "https://";
    if (!value.starts_with(prefix))
        return false;
    value.remove_prefix(prefix.size());
    const size_t pathStart = value.find('/');
    const std::string_view hostText = pathStart == std::string_view::npos ? value : value.substr(0, pathStart);
    const std::string_view pathText = pathStart == std::string_view::npos ? std::string_view("/") : value.substr(pathStart);
    if (hostText.empty() || hostText.find_first_of("?#@:") != std::string_view::npos)
        return false;
    *host = Widen(hostText);
    *path = Widen(pathText);
    return !host->empty() && !path->empty();
}

bool OpenGet(const std::wstring &host, const std::wstring &path, HttpsRequest *http, std::string *error) {
    const std::wstring userAgent = L"URKit/" + Widen(UrkVersion::kReleaseVersion);
    http->session = InternetHandle(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
                                               WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!http->session)
        http->session = InternetHandle(WinHttpOpen(userAgent.c_str(), WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                                   WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!http->session) {
        *error = "cannot start an HTTPS session: " + WindowsError(GetLastError());
        return false;
    }
    WinHttpSetTimeouts(http->session.get(), 5000, 10000, 15000, 60000);
    http->connection = InternetHandle(WinHttpConnect(http->session.get(), host.c_str(), INTERNET_DEFAULT_HTTPS_PORT, 0));
    if (!http->connection) {
        *error = "cannot connect to the release server: " + WindowsError(GetLastError());
        return false;
    }
    http->request = InternetHandle(WinHttpOpenRequest(http->connection.get(), L"GET", path.c_str(), nullptr,
                                                      WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES,
                                                      WINHTTP_FLAG_SECURE));
    if (!http->request) {
        *error = "cannot create the HTTPS request: " + WindowsError(GetLastError());
        return false;
    }
    DWORD redirectPolicy = WINHTTP_OPTION_REDIRECT_POLICY_DISALLOW_HTTPS_TO_HTTP;
    WinHttpSetOption(http->request.get(), WINHTTP_OPTION_REDIRECT_POLICY, &redirectPolicy, sizeof(redirectPolicy));
    constexpr wchar_t headers[] = L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n";
    if (!WinHttpSendRequest(http->request.get(), headers, static_cast<DWORD>(std::size(headers) - 1),
                            WINHTTP_NO_REQUEST_DATA, 0, 0, 0) ||
        !WinHttpReceiveResponse(http->request.get(), nullptr)) {
        *error = "HTTPS request failed: " + WindowsError(GetLastError());
        return false;
    }
    DWORD status = 0;
    DWORD statusSize = sizeof(status);
    if (!WinHttpQueryHeaders(http->request.get(), WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                             WINHTTP_HEADER_NAME_BY_INDEX, &status, &statusSize, WINHTTP_NO_HEADER_INDEX) ||
        status != 200) {
        *error = status == 403 ? "GitHub refused the request (HTTP 403, likely the API rate limit); try again later"
                               : "server returned HTTP " + std::to_string(status);
        return false;
    }
    return true;
}

template <typename Write>
bool ReadBody(HttpsRequest &http, size_t maximumBytes, Write &&write, std::string *error) {
    size_t total = 0;
    std::vector<char> buffer;
    for (;;) {
        DWORD available = 0;
        if (!WinHttpQueryDataAvailable(http.request.get(), &available)) {
            *error = "cannot read the response: " + WindowsError(GetLastError());
            return false;
        }
        if (available == 0)
            return true;
        if (available > maximumBytes - total) {
            *error = "the response is larger than expected";
            return false;
        }
        buffer.resize(available);
        DWORD read = 0;
        if (!WinHttpReadData(http.request.get(), buffer.data(), available, &read)) {
            *error = "cannot read the response: " + WindowsError(GetLastError());
            return false;
        }
        if (!write(buffer.data(), read)) {
            *error = "cannot store the download";
            return false;
        }
        total += read;
    }
}

fs::path ModulePath() {
    std::wstring buffer(32768, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    buffer.resize(length);
    return fs::path(buffer);
}

std::wstring Quote(const fs::path &path) {
    return L"\"" + path.wstring() + L"\"";
}

std::string LowerHex(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
                   [](unsigned char character) { return static_cast<char>(std::tolower(character)); });
    return value;
}

} // namespace

bool CheckForUpdate(UpdateCheckResult *result, AvailableUpdate *available, std::string *error) {
    std::string localError;
    if (!error)
        error = &localError;
    error->clear();
    result->currentVersion = std::string(UrkVersion::kReleaseVersion);
    if (available)
        *available = {};

    HttpsRequest http;
    if (!OpenGet(std::wstring(kGitHubApiHost), std::wstring(kLatestReleasePath), &http, error))
        return false;
    std::string document;
    if (!ReadBody(
            http, kMaxReleaseMetadataBytes,
            [&](const char *data, DWORD size) {
                document.append(data, size);
                return true;
            },
            error))
        return false;

    const nlohmann::json release = nlohmann::json::parse(document, nullptr, false);
    if (release.is_discarded() || !release.is_object() || !release.contains("tag_name") ||
        !release["tag_name"].is_string()) {
        *error = "the release service returned invalid metadata";
        return false;
    }
    const std::string tag = release["tag_name"].get<std::string>();
    SemanticVersion parsed{};
    if (!ParseSemanticVersion(tag, &parsed)) {
        *error = "the latest release tag is not a version: " + tag;
        return false;
    }
    result->latestVersion = tag;
    result->releaseUrl = release.value("html_url", std::string());
    result->updateAvailable = IsNewer(tag, UrkVersion::kReleaseVersion);
    if (!result->updateAvailable)
        return true;

    std::string downloadUrl;
    std::string digest;
    if (release.contains("assets") && release["assets"].is_array()) {
        for (const nlohmann::json &asset : release["assets"]) {
            if (!asset.is_object() || asset.value("name", std::string()) != kReleaseAssetName)
                continue;
            downloadUrl = asset.value("browser_download_url", std::string());
            digest = asset.value("digest", std::string());
            break;
        }
    }
    if (downloadUrl.empty()) {
        *error = "release " + tag + " does not publish " + kReleaseAssetName;
        return false;
    }
    constexpr std::string_view digestPrefix = "sha256:";
    if (!downloadUrl.starts_with(kExpectedDownloadPrefix) || !digest.starts_with(digestPrefix) ||
        !IsHexDigest(std::string_view(digest).substr(digestPrefix.size()))) {
        *error = "release " + tag + " has no trusted download URL or SHA-256 digest for " + kReleaseAssetName;
        return false;
    }
    if (available) {
        available->currentVersion = std::string(UrkVersion::kReleaseVersion);
        available->availableVersion = tag;
        available->releaseUrl = result->releaseUrl;
        available->downloadUrl = downloadUrl;
        available->sha256 = LowerHex(digest.substr(digestPrefix.size()));
    }
    return true;
}

bool DownloadAndRestart(const AvailableUpdate &update, const LogSink &log, bool restart, std::string *error) {
    std::string localError;
    if (!error)
        error = &localError;
    error->clear();
    if (update.downloadUrl.empty() || !IsHexDigest(update.sha256)) {
        *error = "update metadata is incomplete";
        return false;
    }
    std::wstring host;
    std::wstring path;
    if (!ParseHttpsUrl(update.downloadUrl, &host, &path)) {
        *error = "the download URL is not HTTPS";
        return false;
    }
    const fs::path target = ModulePath();
    std::error_code ec;
    const fs::path download = fs::temp_directory_path(ec) /
                              ("urk-" + update.availableVersion + "-" + std::to_string(GetCurrentProcessId()) + ".exe");
    if (ec) {
        *error = "cannot resolve the temporary directory: " + ec.message();
        return false;
    }
    fs::remove(download, ec);

    log(LogLevel::Info, "Downloading " + update.downloadUrl);
    {
        HttpsRequest http;
        if (!OpenGet(host, path, &http, error))
            return false;
        std::ofstream output(download, std::ios::binary | std::ios::trunc);
        if (!output) {
            *error = "cannot create " + download.string();
            return false;
        }
        size_t received = 0;
        const bool ok = ReadBody(
            http, kMaxDownloadBytes,
            [&](const char *data, DWORD size) {
                output.write(data, size);
                received += size;
                return static_cast<bool>(output);
            },
            error);
        output.close();
        if (!ok || !output) {
            fs::remove(download, ec);
            if (error->empty())
                *error = "cannot write " + download.string();
            return false;
        }
        log(LogLevel::Info, "Received " + std::to_string(received / 1024) + " KB");
    }

    std::string actual;
    if (!UrkProject::HashFileSha256(download, &actual, error) || actual != update.sha256) {
        fs::remove(download, ec);
        if (error->empty())
            *error = "the download does not match the published SHA-256";
        return false;
    }
    log(LogLevel::Info, "SHA-256 matches the release digest " + update.sha256.substr(0, 16) + "...");
    if (ExecutableMachine(download) != "x64") {
        fs::remove(download, ec);
        *error = "the download is not an x64 executable";
        return false;
    }

    // The helper is a copy of this executable, so the running image can be replaced.
    const fs::path helper = target.parent_path() / kSelfUpdateHelperName;
    if (!CopyFileW(target.c_str(), helper.c_str(), FALSE)) {
        fs::remove(download, ec);
        *error = "cannot prepare the update helper: " + WindowsError(GetLastError());
        return false;
    }
    std::wstring command = Quote(helper) + L" --apply-self-update --source " + Quote(download) + L" --target " +
                           Quote(target) + L" --sha256 " + Widen(update.sha256) + L" --wait-pid " +
                           std::to_wstring(GetCurrentProcessId()) + (restart ? L"" : L" --no-restart");
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(nullptr, mutableCommand.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr,
                        target.parent_path().c_str(), &startup, &process)) {
        fs::remove(download, ec);
        *error = "cannot start the update helper: " + WindowsError(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    log(LogLevel::Success, "Update to " + update.availableVersion + " is staged; it is installed when this urk.exe exits");
    return true;
}

void RemoveStaleSelfUpdateHelper() noexcept {
    const fs::path self = ModulePath();
    const fs::path helper = self.parent_path() / kSelfUpdateHelperName;
    if (self.filename() == helper.filename())
        return;
    std::error_code ec;
    if (!fs::exists(helper, ec) || ec)
        return;
    // The helper may still be closing; a later launch retries.
    for (int attempt = 0; attempt < 10; ++attempt) {
        if (fs::remove(helper, ec) && !ec)
            return;
        Sleep(200);
    }
}

bool ApplyDownloadedUpdate(const fs::path &source, const fs::path &target, const std::string &expectedSha256,
                           uint32_t waitForProcessId, bool restart, std::string *error) {
    std::string localError;
    if (!error)
        error = &localError;
    if (source.empty() || target.empty() || !IsHexDigest(expectedSha256)) {
        *error = "the update helper received invalid arguments";
        return false;
    }
    if (waitForProcessId != 0) {
        HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, waitForProcessId);
        if (process) {
            const DWORD wait = WaitForSingleObject(process, 120000);
            CloseHandle(process);
            if (wait != WAIT_OBJECT_0) {
                *error = "urk.exe did not exit within two minutes";
                return false;
            }
        }
    }
    std::string actual;
    if (!UrkProject::HashFileSha256(source, &actual, error))
        return false;
    if (actual != LowerHex(expectedSha256)) {
        *error = "the downloaded file changed before it was installed";
        return false;
    }
    // Keep the previous build until the new one is in place.
    fs::path previous = target;
    previous += L".previous";
    std::error_code ec;
    fs::remove(previous, ec);
    if (!MoveFileExW(target.c_str(), previous.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        *error = "cannot move the current urk.exe aside: " + WindowsError(GetLastError());
        return false;
    }
    if (!MoveFileExW(source.c_str(), target.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        MoveFileExW(previous.c_str(), target.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        *error = "cannot install the new urk.exe: " + WindowsError(code);
        return false;
    }
    fs::remove(previous, ec);
    if (!restart)
        return true;

    std::wstring command = Quote(target);
    std::vector<wchar_t> mutableCommand(command.begin(), command.end());
    mutableCommand.push_back(L'\0');
    STARTUPINFOW startup{};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process{};
    if (!CreateProcessW(target.c_str(), mutableCommand.data(), nullptr, nullptr, FALSE, 0, nullptr,
                        target.parent_path().c_str(), &startup, &process)) {
        *error = "the new urk.exe is installed but did not start: " + WindowsError(GetLastError());
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
}

} // namespace UrkApp
