#include <cstdint>
#include <unknwn.h>
#include <windows.h>
#include <winhttp.h>

namespace {
HMODULE SystemWinHttp() {
    static HMODULE module = [] {
        wchar_t path[MAX_PATH] = {};
        const UINT length = GetSystemDirectoryW(path, MAX_PATH);
        constexpr UINT suffixLength = static_cast<UINT>(sizeof(L"\\winhttp.dll") / sizeof(wchar_t));
        if (!length || length >= MAX_PATH - suffixLength)
            return static_cast<HMODULE>(nullptr);
        lstrcatW(path, L"\\winhttp.dll");
        return LoadLibraryW(path);
    }();
    return module;
}

template <class T> T Resolve(const char *name) {
    HMODULE module = SystemWinHttp();
    return module ? reinterpret_cast<T>(GetProcAddress(module, name)) : nullptr;
}

void MissingExport() {
    SetLastError(ERROR_PROC_NOT_FOUND);
}

} // namespace

#define URK_WINHTTP_FORWARD(return_type, name, params, args, fail_value)                                               \
    return_type WINAPI Proxy_##name params {                                                                           \
        using Fn = return_type(WINAPI *) params;                                                                       \
        static Fn fn = Resolve<Fn>(#name);                                                                             \
        if (!fn) {                                                                                                     \
            MissingExport();                                                                                           \
            return fail_value;                                                                                         \
        }                                                                                                              \
        return fn args;                                                                                                \
    }

#define URK_WINHTTP_FORWARD_VOID(name, params, args)                                                                   \
    void WINAPI Proxy_##name params {                                                                                  \
        using Fn = void(WINAPI *) params;                                                                              \
        static Fn fn = Resolve<Fn>(#name);                                                                             \
        if (!fn) {                                                                                                     \
            MissingExport();                                                                                           \
            return;                                                                                                    \
        }                                                                                                              \
        fn args;                                                                                                       \
    }

#define URK_WINHTTP_FORWARD_OPAQUE(name)                                                                               \
    uintptr_t WINAPI Proxy_##name(uintptr_t a1, uintptr_t a2, uintptr_t a3, uintptr_t a4, uintptr_t a5, uintptr_t a6,  \
                                  uintptr_t a7, uintptr_t a8, uintptr_t a9, uintptr_t a10, uintptr_t a11,              \
                                  uintptr_t a12) {                                                                     \
        using Fn = uintptr_t(WINAPI *)(uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t,    \
                                       uintptr_t, uintptr_t, uintptr_t, uintptr_t, uintptr_t);                         \
        static Fn fn = Resolve<Fn>(#name);                                                                             \
        if (!fn) {                                                                                                     \
            MissingExport();                                                                                           \
            return 0;                                                                                                  \
        }                                                                                                              \
        return fn(a1, a2, a3, a4, a5, a6, a7, a8, a9, a10, a11, a12);                                                  \
    }

extern "C" {

URK_WINHTTP_FORWARD(HRESULT, DllCanUnloadNow, (), (), S_FALSE)
URK_WINHTTP_FORWARD(HRESULT, DllGetClassObject, (REFCLSID clsid, REFIID iid, LPVOID *output), (clsid, iid, output),
                    CLASS_E_CLASSNOTAVAILABLE)

URK_WINHTTP_FORWARD_OPAQUE(Private1)
URK_WINHTTP_FORWARD_OPAQUE(SvchostPushServiceGlobals)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpAutoProxySvcMain)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionDeletePolicyEntries)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionDeleteProxyInfo)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionFreeNameList)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionFreeProxyInfo)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionFreeProxyList)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionGetNameList)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionGetProxyInfo)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionGetProxyList)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionOnlyConvert)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionOnlyReceive)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionOnlySend)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionSetPolicyEntries)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionSetProxyInfo)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpConnectionUpdateIfIndexTable)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpCreateProxyList)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpCreateProxyManager)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpCreateProxyResult)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpCreateUiCompatibleProxyString)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpGetProxyForUrlHvsi)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpGetTunnelSocket)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpPacJsWorkerMain)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpProbeConnectivity)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpReadProxySettingsHvsi)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpRefreshProxySettings)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpResolverGetProxyForUrl)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpSaveProxyCredentials)
URK_WINHTTP_FORWARD_OPAQUE(WinHttpSetSecureLegacyServersAppCompat)

URK_WINHTTP_FORWARD(BOOL, WinHttpAddRequestHeaders, (HINTERNET request, LPCWSTR headers, DWORD length, DWORD modifiers),
                    (request, headers, length, modifiers), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpAddRequestHeadersEx,
                    (HINTERNET request, DWORD modifiers, ULONGLONG flags, ULONGLONG extra, DWORD headerCount,
                     WINHTTP_EXTENDED_HEADER *headers),
                    (request, modifiers, flags, extra, headerCount, headers), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpCheckPlatform, (), (), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpCloseHandle, (HINTERNET handle), (handle), FALSE)
URK_WINHTTP_FORWARD(HINTERNET, WinHttpConnect, (HINTERNET session, LPCWSTR server, INTERNET_PORT port, DWORD reserved),
                    (session, server, port, reserved), nullptr)
URK_WINHTTP_FORWARD(BOOL, WinHttpCrackUrl, (LPCWSTR url, DWORD length, DWORD flags, LPURL_COMPONENTS components),
                    (url, length, flags, components), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpCreateProxyResolver, (HINTERNET session, HINTERNET *resolver), (session, resolver),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpCreateUrl, (LPURL_COMPONENTS components, DWORD flags, LPWSTR url, LPDWORD length),
                    (components, flags, url, length), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpDetectAutoProxyConfigUrl, (DWORD flags, LPWSTR *autoConfigUrl), (flags, autoConfigUrl),
                    FALSE)
URK_WINHTTP_FORWARD_VOID(WinHttpFreeProxyResult, (WINHTTP_PROXY_RESULT * proxyResult), (proxyResult))
URK_WINHTTP_FORWARD_VOID(WinHttpFreeProxyResultEx, (WINHTTP_PROXY_RESULT_EX * proxyResult), (proxyResult))
URK_WINHTTP_FORWARD_VOID(WinHttpFreeProxySettings, (WINHTTP_PROXY_SETTINGS * proxySettings), (proxySettings))
URK_WINHTTP_FORWARD(DWORD, WinHttpFreeProxySettingsEx, (WINHTTP_PROXY_SETTINGS_TYPE settingsType, PVOID proxySettings),
                    (settingsType, proxySettings), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD_VOID(WinHttpFreeQueryConnectionGroupResult, (WINHTTP_QUERY_CONNECTION_GROUP_RESULT * result),
                         (result))
URK_WINHTTP_FORWARD(BOOL, WinHttpGetDefaultProxyConfiguration, (WINHTTP_PROXY_INFO * proxyInfo), (proxyInfo), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpGetIEProxyConfigForCurrentUser, (WINHTTP_CURRENT_USER_IE_PROXY_CONFIG * proxyConfig),
                    (proxyConfig), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpGetProxyForUrl,
                    (HINTERNET session, LPCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *autoProxyOptions,
                     WINHTTP_PROXY_INFO *proxyInfo),
                    (session, url, autoProxyOptions, proxyInfo), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxyForUrlEx,
                    (HINTERNET resolver, PCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *autoProxyOptions, DWORD_PTR context),
                    (resolver, url, autoProxyOptions, context), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxyForUrlEx2,
                    (HINTERNET resolver, PCWSTR url, WINHTTP_AUTOPROXY_OPTIONS *autoProxyOptions,
                     DWORD interfaceSelectionContextLength, BYTE *interfaceSelectionContext, DWORD_PTR context),
                    (resolver, url, autoProxyOptions, interfaceSelectionContextLength, interfaceSelectionContext,
                     context),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxyResult, (HINTERNET resolver, WINHTTP_PROXY_RESULT *proxyResult),
                    (resolver, proxyResult), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxyResultEx, (HINTERNET resolver, WINHTTP_PROXY_RESULT_EX *proxyResult),
                    (resolver, proxyResult), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxySettingsEx,
                    (HINTERNET resolver, WINHTTP_PROXY_SETTINGS_TYPE settingsType,
                     PWINHTTP_PROXY_SETTINGS_PARAM settingsParam, DWORD_PTR context),
                    (resolver, settingsType, settingsParam, context), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxySettingsResultEx, (HINTERNET resolver, PVOID proxySettings),
                    (resolver, proxySettings), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpGetProxySettingsVersion, (HINTERNET session, DWORD *proxySettingsVersion),
                    (session, proxySettingsVersion), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(HINTERNET, WinHttpOpen, (LPCWSTR agent, DWORD access, LPCWSTR proxy, LPCWSTR bypass, DWORD flags),
                    (agent, access, proxy, bypass, flags), nullptr)
URK_WINHTTP_FORWARD(HINTERNET, WinHttpOpenRequest,
                    (HINTERNET connect, LPCWSTR verb, LPCWSTR object, LPCWSTR version, LPCWSTR referrer,
                     LPCWSTR const *acceptTypes, DWORD flags),
                    (connect, verb, object, version, referrer, acceptTypes, flags), nullptr)
URK_WINHTTP_FORWARD(HINTERNET, WinHttpProtocolCompleteUpgrade, (HINTERNET request, DWORD_PTR context),
                    (request, context), nullptr)
URK_WINHTTP_FORWARD(DWORD, WinHttpProtocolReceive,
                    (HINTERNET protocolHandle, ULONGLONG flags, PVOID buffer, DWORD bufferLength, DWORD *bytesRead),
                    (protocolHandle, flags, buffer, bufferLength, bytesRead), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpProtocolSend,
                    (HINTERNET protocolHandle, ULONGLONG flags, PVOID buffer, DWORD bufferLength),
                    (protocolHandle, flags, buffer, bufferLength), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpQueryAuthSchemes,
                    (HINTERNET request, LPDWORD supportedSchemes, LPDWORD firstScheme, LPDWORD authTarget),
                    (request, supportedSchemes, firstScheme, authTarget), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpQueryConnectionGroup,
                    (HINTERNET internet, const GUID *connectionGuid, ULONGLONG flags,
                     PWINHTTP_QUERY_CONNECTION_GROUP_RESULT *result),
                    (internet, connectionGuid, flags, result), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpQueryDataAvailable, (HINTERNET request, LPDWORD available), (request, available),
                    FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpQueryHeaders,
                    (HINTERNET request, DWORD infoLevel, LPCWSTR name, LPVOID buffer, LPDWORD length, LPDWORD index),
                    (request, infoLevel, name, buffer, length, index), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpQueryHeadersEx,
                    (HINTERNET request, DWORD infoLevel, ULONGLONG flags, UINT codePage, PDWORD index,
                     PWINHTTP_HEADER_NAME headerName, PVOID buffer, PDWORD bufferLength,
                     PWINHTTP_EXTENDED_HEADER *headers, PDWORD headerCount),
                    (request, infoLevel, flags, codePage, index, headerName, buffer, bufferLength, headers,
                     headerCount),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpQueryOption, (HINTERNET handle, DWORD option, LPVOID buffer, LPDWORD length),
                    (handle, option, buffer, length), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpReadData, (HINTERNET request, LPVOID buffer, DWORD bytes, LPDWORD bytesRead),
                    (request, buffer, bytes, bytesRead), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpReadDataEx,
                    (HINTERNET request, LPVOID buffer, DWORD bytes, LPDWORD bytesRead, ULONGLONG flags,
                     DWORD propertySize, PVOID property),
                    (request, buffer, bytes, bytesRead, flags, propertySize, property), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpReadProxySettings,
                    (HINTERNET session, PCWSTR connectionName, BOOL fallBackToDefaultSettings,
                     BOOL setAutoDiscoverForDefaultSettings, DWORD *settingsVersion, BOOL *defaultSettingsAreReturned,
                     WINHTTP_PROXY_SETTINGS *proxySettings),
                    (session, connectionName, fallBackToDefaultSettings, setAutoDiscoverForDefaultSettings,
                     settingsVersion, defaultSettingsAreReturned, proxySettings),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpReceiveResponse, (HINTERNET request, LPVOID reserved), (request, reserved), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpRegisterProxyChangeNotification,
                    (ULONGLONG flags, WINHTTP_PROXY_CHANGE_CALLBACK callback, PVOID context,
                     WINHTTP_PROXY_CHANGE_REGISTRATION_HANDLE *registration),
                    (flags, callback, context, registration), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpResetAutoProxy, (HINTERNET session, DWORD flags), (session, flags),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpSendRequest,
                    (HINTERNET request, LPCWSTR headers, DWORD headersLength, LPVOID optional, DWORD optionalLength,
                     DWORD totalLength, DWORD_PTR context),
                    (request, headers, headersLength, optional, optionalLength, totalLength, context), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpSetCredentials,
                    (HINTERNET request, DWORD authTargets, DWORD authScheme, LPCWSTR userName, LPCWSTR password,
                     LPVOID authParams),
                    (request, authTargets, authScheme, userName, password, authParams), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpSetDefaultProxyConfiguration, (WINHTTP_PROXY_INFO * proxyInfo), (proxyInfo), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpSetOption, (HINTERNET handle, DWORD option, LPVOID buffer, DWORD length),
                    (handle, option, buffer, length), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpSetProxySettingsPerUser, (BOOL proxySettingsPerUser), (proxySettingsPerUser),
                    ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(WINHTTP_STATUS_CALLBACK, WinHttpSetStatusCallback,
                    (HINTERNET handle, WINHTTP_STATUS_CALLBACK callback, DWORD flags, DWORD_PTR reserved),
                    (handle, callback, flags, reserved), WINHTTP_INVALID_STATUS_CALLBACK)
URK_WINHTTP_FORWARD(BOOL, WinHttpSetTimeouts, (HINTERNET handle, int resolve, int connect, int send, int receive),
                    (handle, resolve, connect, send, receive), FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpTimeFromSystemTime, (const SYSTEMTIME *systemTime, LPWSTR time), (systemTime, time),
                    FALSE)
URK_WINHTTP_FORWARD(BOOL, WinHttpTimeToSystemTime, (LPCWSTR time, SYSTEMTIME *systemTime), (time, systemTime), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpUnregisterProxyChangeNotification,
                    (WINHTTP_PROXY_CHANGE_REGISTRATION_HANDLE registration), (registration), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpWebSocketClose,
                    (HINTERNET webSocket, USHORT status, PVOID reason, DWORD reasonLength),
                    (webSocket, status, reason, reasonLength), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(HINTERNET, WinHttpWebSocketCompleteUpgrade, (HINTERNET request, DWORD_PTR context),
                    (request, context), nullptr)
URK_WINHTTP_FORWARD(DWORD, WinHttpWebSocketQueryCloseStatus,
                    (HINTERNET webSocket, USHORT *status, PVOID reason, DWORD reasonLength,
                     DWORD *reasonLengthConsumed),
                    (webSocket, status, reason, reasonLength, reasonLengthConsumed), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpWebSocketReceive,
                    (HINTERNET webSocket, PVOID buffer, DWORD bufferLength, DWORD *bytesRead,
                     WINHTTP_WEB_SOCKET_BUFFER_TYPE *bufferType),
                    (webSocket, buffer, bufferLength, bytesRead, bufferType), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpWebSocketSend,
                    (HINTERNET webSocket, WINHTTP_WEB_SOCKET_BUFFER_TYPE bufferType, PVOID buffer, DWORD bufferLength),
                    (webSocket, bufferType, buffer, bufferLength), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(DWORD, WinHttpWebSocketShutdown,
                    (HINTERNET webSocket, USHORT status, PVOID reason, DWORD reasonLength),
                    (webSocket, status, reason, reasonLength), ERROR_PROC_NOT_FOUND)
URK_WINHTTP_FORWARD(BOOL, WinHttpWriteData, (HINTERNET request, LPCVOID buffer, DWORD bytes, LPDWORD written),
                    (request, buffer, bytes, written), FALSE)
URK_WINHTTP_FORWARD(DWORD, WinHttpWriteProxySettings,
                    (HINTERNET session, BOOL forceUpdate, WINHTTP_PROXY_SETTINGS *proxySettings),
                    (session, forceUpdate, proxySettings), ERROR_PROC_NOT_FOUND)

} // extern "C"

#undef URK_WINHTTP_FORWARD_OPAQUE
#undef URK_WINHTTP_FORWARD_VOID
#undef URK_WINHTTP_FORWARD
