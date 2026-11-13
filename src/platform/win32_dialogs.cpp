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

class ScopedComInit {
public:
    ScopedComInit() noexcept {
        const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED | COINIT_DISABLE_OLE1DDE);
        shouldUninit_ = (hr == S_OK || hr == S_FALSE);
        usable_ = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
    }
    ~ScopedComInit() {
        if (shouldUninit_) {
            ::CoUninitialize();
        }
    }
    ScopedComInit(const ScopedComInit&) = delete;
    ScopedComInit& operator=(const ScopedComInit&) = delete;

    [[nodiscard]] bool IsUsable() const noexcept { return usable_; }

private:
    bool shouldUninit_ = false;
    bool usable_ = false;
};

struct CoTaskMemDeleter {
    void operator()(wchar_t* ptr) const noexcept {
        if (ptr) {
            ::CoTaskMemFree(ptr);
        }
    }
};

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
        const std::wstring wideDir = motrix::core::Utf8ToWide(initialDir);
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
    const std::unique_ptr<wchar_t, CoTaskMemDeleter> pathGuard(rawPath);
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
            ::SetClipboardData(CF_UNICODETEXT, mem);
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
    const std::wstring wide = motrix::core::Utf8ToWide(utf8Path);
    ::ShellExecuteW(nullptr, L"open", wide.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
}

}  // namespace motrix::platform
