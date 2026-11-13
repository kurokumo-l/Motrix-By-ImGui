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

std::string AsciiLower(std::string_view text) {
    std::string out(text);
    for (char& ch : out) {
        if (ch >= 'A' && ch <= 'Z') {
            ch = static_cast<char>(ch - 'A' + 'a');
        }
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

}  // namespace motrix::core
