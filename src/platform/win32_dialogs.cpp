module;

// clang-format off
#include <windows.h>
#include <shobjidl.h>
#include <shellapi.h>
// clang-format on

module motrix.platform;

import std;
import motrix.core;
import :win32;
import :win32_internal;

namespace motrix::platform {
namespace {

std::optional<std::string>
RunOpenItemDialog(void* ownerHwnd, bool folderMode, std::string_view initialDir) {
    ScopedComInit com;
    if (!com.IsUsable()) {
        return std::nullopt;
    }

    ComPtr<IFileOpenDialog> dialog;
    const HRESULT hrCreate = ::CoCreateInstance(
        CLSID_FileOpenDialog, nullptr, CLSCTX_ALL, IID_PPV_ARGS(dialog.ReleaseAndGetAddressOf()));
    if (FAILED(hrCreate) || !dialog) {
        return std::nullopt;
    }

    DWORD opts = 0;
    if (SUCCEEDED(dialog->GetOptions(&opts))) {
        opts |= FOS_FORCEFILESYSTEM | FOS_PATHMUSTEXIST;
        if (folderMode) {
            opts |= FOS_PICKFOLDERS;
        } else {
            opts |= FOS_FILEMUSTEXIST;
        }
        (void)dialog->SetOptions(opts);
    }

    if (!folderMode) {
        static constexpr COMDLG_FILTERSPEC kFilters[] = {
            {L"BitTorrent Files (*.torrent)", L"*.torrent"},
            {L"All Files (*.*)", L"*.*"},
        };
        (void)dialog->SetFileTypes(2, kFilters);
        (void)dialog->SetFileTypeIndex(1);
        (void)dialog->SetDefaultExtension(L"torrent");
        (void)dialog->SetTitle(L"Select Torrent File");
    } else {
        (void)dialog->SetTitle(L"Select Download Folder");
    }

    if (!initialDir.empty()) {
        std::filesystem::path initPath = motrix::core::PathFromUtf8(initialDir);
        initPath.make_preferred();
        const std::wstring wideDir = initPath.wstring();
        ComPtr<IShellItem> folderItem;
        if (SUCCEEDED(::SHCreateItemFromParsingName(
                wideDir.c_str(), nullptr, IID_PPV_ARGS(folderItem.ReleaseAndGetAddressOf())))
            && folderItem) {
            (void)dialog->SetFolder(folderItem.Get());
        }
    }

    if (FAILED(dialog->Show(static_cast<HWND>(ownerHwnd)))) {
        return std::nullopt;
    }

    ComPtr<IShellItem> resultItem;
    if (FAILED(dialog->GetResult(resultItem.ReleaseAndGetAddressOf())) || !resultItem) {
        return std::nullopt;
    }

    wchar_t* rawPath = nullptr;
    if (FAILED(resultItem->GetDisplayName(SIGDN_FILESYSPATH, &rawPath)) || !rawPath) {
        return std::nullopt;
    }
    const UniqueCoTaskPtr<wchar_t> pathGuard(rawPath);
    return motrix::core::WideToUtf8(pathGuard.get());
}

}  // namespace

std::optional<std::string> Win32PickTorrentFile(void* ownerHwnd) {
    return RunOpenItemDialog(ownerHwnd, false, {});
}

std::optional<std::string> Win32PickFolder(void* ownerHwnd, std::string_view initialDir) {
    return RunOpenItemDialog(ownerHwnd, true, initialDir);
}

void Win32SetWindowTitle(void* hwnd, std::string_view utf8Title) {
    if (hwnd) {
        const std::wstring wide = motrix::core::Utf8ToWide(utf8Title);
        ::SetWindowTextW(static_cast<HWND>(hwnd), wide.c_str());
    }
}

bool Win32IsWindowZoomed(void* hwnd) noexcept {
    return hwnd != nullptr && ::IsZoomed(static_cast<HWND>(hwnd)) != FALSE;
}

bool Win32IsWindowIconic(void* hwnd) noexcept {
    return hwnd != nullptr && ::IsIconic(static_cast<HWND>(hwnd)) != FALSE;
}

void Win32CopyTextToClipboard(void* hwnd, std::string_view utf8) {
    if (!::OpenClipboard(static_cast<HWND>(hwnd))) {
        return;
    }
    ::EmptyClipboard();
    const std::wstring wide = motrix::core::Utf8ToWide(utf8);
    const std::size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
    HGLOBAL mem = ::GlobalAlloc(GMEM_MOVEABLE, bytes);
    if (mem) {
        if (void* ptr = ::GlobalLock(mem)) {
            std::memcpy(ptr, wide.c_str(), bytes);
            ::GlobalUnlock(mem);
            if (!::SetClipboardData(CF_UNICODETEXT, mem)) {
                ::GlobalFree(mem);
            }
        } else {
            ::GlobalFree(mem);
        }
    }
    ::CloseClipboard();
}

void Win32OpenPathInExplorer(std::string_view utf8Path) {
    if (utf8Path.empty()) {
        return;
    }
    if (std::ranges::any_of(utf8Path, [](char ch) { return ch == '\0'; })) {
        return;
    }
    if (motrix::core::StartsWithAsciiInsensitive(utf8Path, "http://")
        || motrix::core::StartsWithAsciiInsensitive(utf8Path, "https://")) {
        if (std::ranges::any_of(utf8Path, [](char ch) { return ch == '\r' || ch == '\n'; })) {
            return;
        }
        const std::wstring wide = motrix::core::Utf8ToWide(utf8Path);
        ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
        return;
    }
    std::filesystem::path p = motrix::core::PathFromUtf8(utf8Path);
    p.make_preferred();
    std::error_code ec;
    if (std::filesystem::is_regular_file(p, ec)) {
        const std::wstring arg = L"/select,\"" + p.wstring() + L"\"";
        ::ShellExecuteW(nullptr, L"open", L"explorer.exe", arg.c_str(), nullptr, SW_SHOWNORMAL);
        return;
    }
    std::filesystem::path target = std::filesystem::is_directory(p, ec) ? p : p.parent_path();
    target.make_preferred();
    ec.clear();
    if (!target.empty() && std::filesystem::is_directory(target, ec)) {
        ::ShellExecuteW(nullptr, L"explore", target.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    }
}

}  // namespace motrix::platform
