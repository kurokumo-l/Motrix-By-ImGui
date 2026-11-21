module;

#include <windows.h>

module motrix.core;

import std;

namespace motrix::core {

std::wstring Utf8ToWide(std::string_view utf8) {
    if (utf8.empty()) {
        return {};
    }
    const int srcLen = static_cast<int>(utf8.size());
    const int needed = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, nullptr, 0);
    if (needed <= 0) {
        return {};
    }
    std::wstring result(static_cast<std::size_t>(needed), L'\0');
    const int written = ::MultiByteToWideChar(CP_UTF8, 0, utf8.data(), srcLen, result.data(), needed);
    if (written <= 0) {
        return {};
    }
    return result;
}

std::string WideToUtf8(std::wstring_view wide) {
    if (wide.empty()) {
        return {};
    }
    const int srcLen = static_cast<int>(wide.size());
    const int needed =
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), srcLen, nullptr, 0, nullptr, nullptr);
    if (needed <= 0) {
        return {};
    }
    std::string result(static_cast<std::size_t>(needed), '\0');
    const int written =
        ::WideCharToMultiByte(CP_UTF8, 0, wide.data(), srcLen, result.data(), needed, nullptr, nullptr);
    if (written <= 0) {
        return {};
    }
    return result;
}

std::string PathToUtf8(const std::filesystem::path& path) {
    return WideToUtf8(path.wstring());
}

std::filesystem::path PathFromUtf8(std::string_view utf8) {
    return std::filesystem::path(Utf8ToWide(utf8));
}

namespace {

constexpr char AsciiLowerChar(char c) noexcept {
    return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c;
}

}  // namespace

std::string AsciiLower(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        ch = AsciiLowerChar(ch);
    }
    return out;
}

std::string_view TrimAscii(std::string_view text) noexcept {
    while (!text.empty()
           && (text.front() == ' ' || text.front() == '\t' || text.front() == '\r'
               || text.front() == '\n')) {
        text.remove_prefix(1);
    }
    while (
        !text.empty()
        && (text.back() == ' ' || text.back() == '\t' || text.back() == '\r' || text.back() == '\n')) {
        text.remove_suffix(1);
    }
    return text;
}

bool EqualsAsciiInsensitive(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (AsciiLowerChar(a[i]) != AsciiLowerChar(b[i])) {
            return false;
        }
    }
    return true;
}

bool StartsWithAsciiInsensitive(std::string_view text, std::string_view prefix) noexcept {
    return text.size() >= prefix.size() && EqualsAsciiInsensitive(text.substr(0, prefix.size()), prefix);
}

bool EndsWithAsciiInsensitive(std::string_view text, std::string_view suffix) noexcept {
    return text.size() >= suffix.size()
           && EqualsAsciiInsensitive(text.substr(text.size() - suffix.size()), suffix);
}

bool ContainsAsciiInsensitive(std::string_view haystack, std::string_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const std::size_t limit = haystack.size() - needle.size();
    for (std::size_t i = 0; i <= limit; ++i) {
        if (EqualsAsciiInsensitive(haystack.substr(i, needle.size()), needle)) {
            return true;
        }
    }
    return false;
}

std::string_view TrimQuotedArg(std::string_view text) noexcept {
    text = TrimAscii(text);
    while (text.size() >= 2
           && ((text.front() == '"' && text.back() == '"')
               || (text.front() == '\'' && text.back() == '\''))) {
        text.remove_prefix(1);
        text.remove_suffix(1);
        text = TrimAscii(text);
    }
    while (!text.empty() && text.front() == '"') {
        text.remove_prefix(1);
    }
    while (!text.empty() && text.back() == '"') {
        text.remove_suffix(1);
    }
    return TrimAscii(text);
}

std::optional<int> ParseStrictInt(std::string_view text) noexcept {
    const std::string_view trimmed = TrimAscii(text);
    if (trimmed.empty()) {
        return std::nullopt;
    }
    int value = 0;
    const char* begin = trimmed.data();
    const char* end = begin + trimmed.size();
    const auto [ptr, ec] = std::from_chars(begin, end, value);
    if (ec != std::errc{} || ptr != end) {
        return std::nullopt;
    }
    return value;
}

std::filesystem::path CurrentExecutablePath() {
    std::size_t capacity = 512;
    while (capacity <= 32768) {
        std::wstring buf(capacity, L'\0');
        const DWORD len = ::GetModuleFileNameW(nullptr, buf.data(), static_cast<DWORD>(buf.size()));
        if (len == 0) {
            return {};
        }
        if (len < buf.size()) {
            buf.resize(len);
            return std::filesystem::path(std::move(buf));
        }
        if (::GetLastError() != ERROR_INSUFFICIENT_BUFFER) {
            return {};
        }
        capacity *= 2;
    }
    return {};
}

UniqueHandle::~UniqueHandle() {
    Reset();
}

UniqueHandle::UniqueHandle(UniqueHandle&& other) noexcept : handle_(other.handle_) {
    other.handle_ = nullptr;
}

UniqueHandle& UniqueHandle::operator=(UniqueHandle&& other) noexcept {
    if (this != &other) {
        Reset();
        handle_ = other.handle_;
        other.handle_ = nullptr;
    }
    return *this;
}

bool UniqueHandle::IsValid() const noexcept {
    return handle_ != nullptr && handle_ != INVALID_HANDLE_VALUE;
}

void UniqueHandle::Reset(void* newHandle) noexcept {
    if (IsValid()) {
        ::CloseHandle(handle_);
    }
    handle_ = newHandle;
}

void* UniqueHandle::Release() noexcept {
    void* prev = handle_;
    handle_ = nullptr;
    return prev;
}

void FreeWin32Local(void* ptr) noexcept {
    if (ptr) {
        ::LocalFree(ptr);
    }
}

}  // namespace motrix::core
