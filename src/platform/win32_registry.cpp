module;

// clang-format off
#include <windows.h>
#include <shlobj.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :win32;
import :win32_internal;

namespace motrix::platform {

namespace {

constexpr wchar_t kRunSubkey[] = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
constexpr wchar_t kRunValueName[] = L"MotrixNative";

class UniqueHKey {
public:
    UniqueHKey() noexcept = default;
    explicit UniqueHKey(HKEY key) noexcept : key_(key) {}
    ~UniqueHKey() { Reset(); }

    UniqueHKey(const UniqueHKey&) = delete;
    UniqueHKey& operator=(const UniqueHKey&) = delete;

    UniqueHKey(UniqueHKey&& other) noexcept : key_(other.Release()) {}
    UniqueHKey& operator=(UniqueHKey&& other) noexcept {
        if (this != &other) {
            Reset(other.Release());
        }
        return *this;
    }

    [[nodiscard]] HKEY Get() const noexcept { return key_; }
    [[nodiscard]] HKEY* Put() noexcept {
        Reset();
        return &key_;
    }

    void Reset(HKEY newKey = nullptr) noexcept {
        if (key_) {
            ::RegCloseKey(key_);
        }
        key_ = newKey;
    }

    [[nodiscard]] HKEY Release() noexcept {
        HKEY raw = key_;
        key_ = nullptr;
        return raw;
    }

private:
    HKEY key_ = nullptr;
};

[[nodiscard]] motrix::core::AppError MakeRegistryError(std::string_view context, LSTATUS status) {
    return motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                   motrix::core::ErrorCode::IoFailure,
                                   std::string(context),
                                   "Win32 error code: " + std::to_string(status));
}

[[nodiscard]] std::optional<std::wstring>
ReadRegString(HKEY root, const wchar_t* subkey, const wchar_t* valueName) {
    DWORD byteSize = 0;
    const LSTATUS probe = ::RegGetValueW(
        root, subkey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, nullptr, &byteSize);
    if (probe != ERROR_SUCCESS || byteSize < sizeof(wchar_t)) {
        return std::nullopt;
    }

    std::wstring buf(byteSize / sizeof(wchar_t), L'\0');
    DWORD readBytes = byteSize;
    const LSTATUS readSt = ::RegGetValueW(
        root, subkey, valueName, RRF_RT_REG_SZ | RRF_RT_REG_EXPAND_SZ, nullptr, buf.data(), &readBytes);
    if (readSt != ERROR_SUCCESS) {
        return std::nullopt;
    }

    while (!buf.empty() && buf.back() == L'\0') {
        buf.pop_back();
    }
    return buf;
}

[[nodiscard]] motrix::core::Result<void> WriteRegString(HKEY root,
                                                        const std::wstring& subkey,
                                                        const wchar_t* valueName,
                                                        const std::wstring& value) {
    UniqueHKey key;
    const LSTATUS createSt = ::RegCreateKeyExW(root,
                                               subkey.c_str(),
                                               0,
                                               nullptr,
                                               REG_OPTION_NON_VOLATILE,
                                               KEY_SET_VALUE,
                                               nullptr,
                                               key.Put(),
                                               nullptr);
    if (createSt != ERROR_SUCCESS) {
        return std::unexpected(MakeRegistryError("Failed to open registry key for writing", createSt));
    }

    const DWORD bytes = static_cast<DWORD>((value.size() + 1) * sizeof(wchar_t));
    const LSTATUS setSt = ::RegSetValueExW(
        key.Get(), valueName, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.c_str()), bytes);
    if (setSt != ERROR_SUCCESS) {
        return std::unexpected(MakeRegistryError("Failed to write registry string value", setSt));
    }
    return {};
}

[[nodiscard]] bool ContainsInsensitive(std::wstring_view haystack, std::wstring_view needle) noexcept {
    if (needle.empty() || haystack.size() < needle.size()) {
        return false;
    }
    const std::size_t limit = haystack.size() - needle.size();
    const int needleLen = static_cast<int>(needle.size());
    for (std::size_t i = 0; i <= limit; ++i) {
        if (::CompareStringOrdinal(haystack.data() + i, needleLen, needle.data(), needleLen, TRUE)
            == CSTR_EQUAL) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] motrix::core::Result<bool> SetOrCleanScheme(std::wstring_view scheme,
                                                          std::wstring_view description,
                                                          const std::filesystem::path& exePath,
                                                          bool enable) {
    const std::wstring baseKey = L"Software\\Classes\\" + std::wstring(scheme);
    const std::wstring cmdSubkey = baseKey + L"\\shell\\open\\command";
    const auto existingCmd = ReadRegString(HKEY_CURRENT_USER, cmdSubkey.c_str(), nullptr);
    if (enable) {
        const std::wstring exeWide = exePath.wstring();
        const std::wstring desiredCmd = L"\"" + exeWide + L"\" \"%1\"";
        const auto urlProto = ReadRegString(HKEY_CURRENT_USER, baseKey.c_str(), L"URL Protocol");
        if (existingCmd && *existingCmd == desiredCmd && urlProto.has_value()) {
            return false;
        }
        if (auto res = WriteRegString(HKEY_CURRENT_USER, baseKey, nullptr, std::wstring(description));
            !res) {
            return std::unexpected(res.error());
        }
        if (auto res = WriteRegString(HKEY_CURRENT_USER, baseKey, L"URL Protocol", L""); !res) {
            return std::unexpected(res.error());
        }
        if (auto res = WriteRegString(
                HKEY_CURRENT_USER, baseKey + L"\\DefaultIcon", nullptr, L"\"" + exeWide + L"\",0");
            !res) {
            return std::unexpected(res.error());
        }
        if (auto res = WriteRegString(HKEY_CURRENT_USER, cmdSubkey, nullptr, desiredCmd); !res) {
            return std::unexpected(res.error());
        }
        return true;
    }

    if (!existingCmd) {
        return false;
    }

    const std::wstring exeWide = exePath.wstring();
    const std::wstring exeName = exePath.filename().wstring();
    const bool ownedBySelf = ContainsInsensitive(*existingCmd, L"MotrixNative")
                             || (!exeWide.empty() && ContainsInsensitive(*existingCmd, exeWide))
                             || (!exeName.empty() && ContainsInsensitive(*existingCmd, exeName));
    if (!ownedBySelf) {
        return false;
    }

    const LSTATUS delSt = ::RegDeleteTreeW(HKEY_CURRENT_USER, baseKey.c_str());
    if (delSt != ERROR_SUCCESS && delSt != ERROR_FILE_NOT_FOUND) {
        return std::unexpected(MakeRegistryError("Failed to remove protocol registration", delSt));
    }
    return delSt == ERROR_SUCCESS;
}

}  // namespace

std::filesystem::path CurrentExecutablePath() {
    return motrix::core::CurrentExecutablePath();
}

motrix::core::Result<void> SetOpenAtLoginEnabled(bool enabled, const std::filesystem::path& exePath) {
    if (enabled) {
        const std::filesystem::path targetExe = exePath.empty() ? CurrentExecutablePath() : exePath;
        if (targetExe.empty()) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                           motrix::core::ErrorCode::InvalidArgument,
                                                           "Executable path is empty"));
        }
        const std::wstring cmd = L"\"" + targetExe.wstring() + L"\" --opened-at-login=1";
        if (const auto existing = ReadRegString(HKEY_CURRENT_USER, kRunSubkey, kRunValueName);
            existing && *existing == cmd) {
            return {};
        }
        return WriteRegString(HKEY_CURRENT_USER, kRunSubkey, kRunValueName, cmd);
    }

    UniqueHKey key;
    const LSTATUS openSt = ::RegOpenKeyExW(HKEY_CURRENT_USER, kRunSubkey, 0, KEY_SET_VALUE, key.Put());
    if (openSt == ERROR_FILE_NOT_FOUND) {
        return {};
    }
    if (openSt != ERROR_SUCCESS) {
        return std::unexpected(MakeRegistryError("Failed to open startup Run registry key", openSt));
    }

    const LSTATUS delSt = ::RegDeleteValueW(key.Get(), kRunValueName);
    if (delSt != ERROR_SUCCESS && delSt != ERROR_FILE_NOT_FOUND) {
        return std::unexpected(MakeRegistryError("Failed to delete startup Run value", delSt));
    }
    return {};
}

bool IsOpenAtLoginEnabled() {
    const auto val = ReadRegString(HKEY_CURRENT_USER, kRunSubkey, kRunValueName);
    return val.has_value() && !val->empty();
}

motrix::core::Result<void> ApplyProtocolAssociations(const std::filesystem::path& exePath,
                                                     bool enableMotrix,
                                                     bool enableMagnet,
                                                     bool enableThunder) {
    const std::filesystem::path targetExe = exePath.empty() ? CurrentExecutablePath() : exePath;
    if (targetExe.empty() && (enableMotrix || enableMagnet || enableThunder)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Platform,
                                                       motrix::core::ErrorCode::InvalidArgument,
                                                       "Executable path is empty"));
    }

    struct SchemeRule {
        std::wstring_view scheme;
        std::wstring_view desc;
        bool enable = false;
    };
    const std::array<SchemeRule, 4> rules{{
        {L"motrix", L"URL:Motrix Protocol", enableMotrix},
        {L"mo", L"URL:Motrix Protocol", enableMotrix},
        {L"magnet", L"URL:Magnet Protocol", enableMagnet},
        {L"thunder", L"URL:Thunder Protocol", enableThunder},
    }};
    bool anyChanged = false;
    for (const auto& r : rules) {
        const auto res = SetOrCleanScheme(r.scheme, r.desc, targetExe, r.enable);
        if (!res) {
            return std::unexpected(res.error());
        }
        anyChanged = anyChanged || *res;
    }
    if (anyChanged) {
        ::SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
    }
    return {};
}

bool IsProtocolRegisteredForSelf(std::string_view scheme, const std::filesystem::path& exePath) {
    if (scheme.empty()) {
        return false;
    }
    const std::filesystem::path targetExe = exePath.empty() ? CurrentExecutablePath() : exePath;
    if (targetExe.empty()) {
        return false;
    }

    const std::wstring wideScheme = motrix::core::Utf8ToWide(scheme);
    if (wideScheme.empty()) {
        return false;
    }

    const std::wstring cmdSubkey = L"Software\\Classes\\" + wideScheme + L"\\shell\\open\\command";
    const auto cmd = ReadRegString(HKEY_CURRENT_USER, cmdSubkey.c_str(), nullptr);
    if (!cmd) {
        return false;
    }
    return ContainsInsensitive(*cmd, targetExe.wstring());
}

namespace {

bool QueryPersonalizeDarkFlag(const wchar_t* valueName, bool fallbackWhenMissing) noexcept {
    constexpr wchar_t kPersonalizeSubkey[] =
        L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize";
    DWORD lightTheme = 1;
    DWORD size = sizeof(lightTheme);
    const LSTATUS st = ::RegGetValueW(
        HKEY_CURRENT_USER, kPersonalizeSubkey, valueName, RRF_RT_REG_DWORD, nullptr, &lightTheme, &size);
    if (st != ERROR_SUCCESS) {
        return fallbackWhenMissing;
    }
    return lightTheme == 0;
}

}  // namespace

bool QuerySystemDarkMode() noexcept {
    return QueryPersonalizeDarkFlag(L"AppsUseLightTheme", false);
}

bool QueryTaskbarDarkMode() noexcept {
    return QueryPersonalizeDarkFlag(L"SystemUsesLightTheme", true);
}

motrix::core::Locale QuerySystemPreferredLocale() noexcept {
    wchar_t buf[LOCALE_NAME_MAX_LENGTH]{};
    if (::GetUserDefaultLocaleName(buf, LOCALE_NAME_MAX_LENGTH) > 0) {
        const std::wstring_view name(buf);
        if (name.starts_with(L"zh-") || name == L"zh") {
            return motrix::core::Locale::ZhCN;
        }
    }
    const LANGID langId = ::GetUserDefaultUILanguage();
    if ((langId & 0x3FFu) == LANG_CHINESE) {
        return motrix::core::Locale::ZhCN;
    }
    return motrix::core::Locale::EnUS;
}

}  // namespace motrix::platform
