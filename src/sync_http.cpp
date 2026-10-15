#include "pinyin/dictionary_sync.hpp"
#include "sync_storage.hpp"
#include <winhttp.h>
#include <array>

namespace pinyin {
namespace {
struct InternetHandle {
    HINTERNET handle;
    explicit InternetHandle(HINTERNET value) : handle(value) { if (!handle) throw sync_storage::failure("cannot open sync connection"); }
    ~InternetHandle() { WinHttpCloseHandle(handle); }
    InternetHandle(const InternetHandle&) = delete;
    InternetHandle& operator=(const InternetHandle&) = delete;
};
URL_COMPONENTS components(const std::wstring& url) {
    URL_COMPONENTS parts{}; parts.dwStructSize = sizeof(parts);
    parts.dwHostNameLength = parts.dwUrlPathLength = parts.dwExtraInfoLength = parts.dwUserNameLength = parts.dwPasswordLength = static_cast<DWORD>(-1);
    if (!WinHttpCrackUrl(url.c_str(), static_cast<DWORD>(url.size()), 0, &parts) ||
        (parts.nScheme != INTERNET_SCHEME_HTTPS && parts.nScheme != INTERNET_SCHEME_HTTP) ||
        !parts.dwHostNameLength || parts.dwUserNameLength || parts.dwPasswordLength || url.find(L'#') != std::wstring::npos)
        throw std::runtime_error("sync URL must be an HTTP or HTTPS file URL without embedded credentials or a fragment");
    return parts;
}
std::string etag(HINTERNET request) {
    std::array<wchar_t, 1024> buffer{}; DWORD bytes = static_cast<DWORD>(sizeof(buffer));
    if (!WinHttpQueryHeaders(request, WINHTTP_QUERY_ETAG, WINHTTP_HEADER_NAME_BY_INDEX, buffer.data(), &bytes, WINHTTP_NO_HEADER_INDEX)) {
        if (GetLastError() == ERROR_WINHTTP_HEADER_NOT_FOUND) return {};
        throw sync_storage::failure("cannot read sync ETag");
    }
    std::string result;
    for (const wchar_t value : std::wstring(buffer.data())) {
        if (value < 32 || value > 126) throw std::runtime_error("invalid sync ETag");
        result += static_cast<char>(value);
    }
    return result;
}
}
void validate_sync_settings(const SyncSettings& settings) {
    if (settings.url.size() > 8192 || settings.token.size() > 8192 ||
        settings.url.find_first_of("\r\n\t ") != std::string::npos || settings.url.find('\0') != std::string::npos)
        throw std::runtime_error("invalid sync URL");
    if (!settings.url.empty()) components(sync_storage::wide(settings.url));
    else if (settings.enabled) throw std::runtime_error("enter a sync URL before enabling sync");
    for (const unsigned char value : settings.token)
        if (value < 33 || value > 126) throw std::runtime_error("sync token must contain only visible ASCII characters");
}
SyncReply sync_http_request(const std::string& method, const SyncSettings& settings,
    const std::string& body, const std::string& condition) {
    validate_sync_settings(settings);
    if (body.size() > sync_storage::maximum_bytes || (method != "GET" && method != "PUT"))
        throw std::runtime_error("invalid sync request");
    const auto url = sync_storage::wide(settings.url);
    const auto parts = components(url);
    const std::wstring host(parts.lpszHostName, parts.dwHostNameLength);
    std::wstring path(parts.lpszUrlPath, parts.dwUrlPathLength);
    if (path.empty()) path = L"/";
    if (parts.dwExtraInfoLength) path.append(parts.lpszExtraInfo, parts.dwExtraInfoLength);
    InternetHandle session(WinHttpOpen(L"PrivatePinyinSync/1", WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,
        WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0));
    if (!WinHttpSetTimeouts(session.handle, 5000, 5000, 10000, 10000)) throw sync_storage::failure("cannot set sync timeouts");
    InternetHandle connection(WinHttpConnect(session.handle, host.c_str(), parts.nPort, 0));
    InternetHandle request(WinHttpOpenRequest(connection.handle, sync_storage::wide(method).c_str(), path.c_str(),
        nullptr, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, parts.nScheme == INTERNET_SCHEME_HTTPS ? WINHTTP_FLAG_SECURE : 0));
    DWORD redirect = WINHTTP_OPTION_REDIRECT_POLICY_NEVER;
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_REDIRECT_POLICY, &redirect, sizeof(redirect)))
        throw sync_storage::failure("cannot disable sync redirects");
    DWORD autologon = WINHTTP_AUTOLOGON_SECURITY_LEVEL_HIGH;
    if (!WinHttpSetOption(request.handle, WINHTTP_OPTION_AUTOLOGON_POLICY, &autologon, sizeof(autologon)))
        throw sync_storage::failure("cannot disable implicit Windows authentication");
    std::wstring headers = L"Accept: text/tab-separated-values\r\nCache-Control: no-cache\r\n";
    if (!settings.token.empty()) headers += L"Authorization: Bearer " + sync_storage::wide(settings.token) + L"\r\n";
    if (method == "PUT") {
        if (condition.empty() || condition.find_first_of("\r\n") != std::string::npos) throw std::runtime_error("conditional PUT is required");
        headers += L"Content-Type: text/tab-separated-values; charset=utf-8\r\n";
        headers += condition == "*" ? L"If-None-Match: *\r\n" : L"If-Match: " + sync_storage::wide(condition) + L"\r\n";
    }
    if (!WinHttpSendRequest(request.handle, headers.c_str(), static_cast<DWORD>(headers.size()),
        body.empty() ? WINHTTP_NO_REQUEST_DATA : const_cast<char*>(body.data()), static_cast<DWORD>(body.size()),
        static_cast<DWORD>(body.size()), 0) || !WinHttpReceiveResponse(request.handle, nullptr))
        throw sync_storage::failure("sync network request failed");
    DWORD code = 0, size = sizeof(code);
    if (!WinHttpQueryHeaders(request.handle, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
        WINHTTP_HEADER_NAME_BY_INDEX, &code, &size, WINHTTP_NO_HEADER_INDEX)) throw sync_storage::failure("cannot read sync HTTP status");
    SyncReply reply; reply.status = code; reply.etag = etag(request.handle);
    std::array<char, 8192> buffer{};
    const auto deadline = GetTickCount64() + 30000;
    for (;;) {
        DWORD copied = 0;
        if (GetTickCount64() > deadline) throw std::runtime_error("sync response timed out");
        if (!WinHttpReadData(request.handle, buffer.data(), static_cast<DWORD>(buffer.size()), &copied))
            throw sync_storage::failure("cannot read sync response");
        if (!copied) break;
        if (reply.body.size() + copied > sync_storage::maximum_bytes) throw std::runtime_error("sync response exceeds 32 MiB");
        reply.body.append(buffer.data(), copied);
    }
    return reply;
}
}
