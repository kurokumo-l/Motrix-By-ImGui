module;

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

module motrix.core;

import std;
import :encoding;
import :error;

namespace motrix::core {

namespace {

std::atomic<std::uint64_t> g_atomicTempSeq{0};

[[nodiscard]] std::string FormatWin32ErrorDetail(DWORD code) {
    return std::format("win32 error {}", code);
}

[[nodiscard]] Result<std::pair<std::filesystem::path, UniqueHandle>>
CreateUniqueTempFile(const std::filesystem::path& targetPath) {
    const DWORD pid = ::GetCurrentProcessId();
    constexpr int kMaxAttempts = 64;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        const std::uint64_t seq = g_atomicTempSeq.fetch_add(1, std::memory_order_relaxed);
        std::filesystem::path candidate = targetPath;
        candidate += std::format(".{}.{}.tmp", pid, seq);
        HANDLE raw = ::CreateFileW(
            candidate.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (raw != INVALID_HANDLE_VALUE) {
            return std::make_pair(std::move(candidate), UniqueHandle(raw));
        }
        const DWORD err = ::GetLastError();
        if (err != ERROR_FILE_EXISTS && err != ERROR_ALREADY_EXISTS) {
            return std::unexpected(MakeError(ErrorDomain::Core,
                                             ErrorCode::IoFailure,
                                             "Failed to open temp file",
                                             FormatWin32ErrorDetail(err)));
        }
    }
    return std::unexpected(MakeError(ErrorDomain::Core,
                                     ErrorCode::IoFailure,
                                     "Failed to allocate unique temp file",
                                     PathToUtf8(targetPath)));
}

[[nodiscard]] Result<void> WriteAllAndFlush(HANDLE handle, std::span<const std::uint8_t> bytes) {
    constexpr std::size_t kMaxChunk = 64u * 1024u * 1024u;
    std::size_t offset = 0;
    while (offset < bytes.size()) {
        const DWORD toWrite = static_cast<DWORD>(std::min(kMaxChunk, bytes.size() - offset));
        DWORD written = 0;
        if (!::WriteFile(handle, bytes.data() + offset, toWrite, &written, nullptr) || written == 0) {
            const DWORD err = ::GetLastError();
            return std::unexpected(MakeError(ErrorDomain::Core,
                                             ErrorCode::IoFailure,
                                             "Failed writing temp file",
                                             FormatWin32ErrorDetail(err)));
        }
        offset += written;
    }
    if (!::FlushFileBuffers(handle)) {
        const DWORD err = ::GetLastError();
        return std::unexpected(MakeError(ErrorDomain::Core,
                                         ErrorCode::IoFailure,
                                         "Failed flushing temp file",
                                         FormatWin32ErrorDetail(err)));
    }
    return {};
}

[[nodiscard]] Result<void> MoveTempIntoPlace(const std::filesystem::path& tmpPath,
                                             const std::filesystem::path& targetPath) {
    constexpr int kMaxAttempts = 5;
    DWORD lastErr = ERROR_SUCCESS;
    for (int attempt = 0; attempt < kMaxAttempts; ++attempt) {
        if (::MoveFileExW(tmpPath.c_str(),
                          targetPath.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
            return {};
        }
        lastErr = ::GetLastError();
        if (lastErr != ERROR_SHARING_VIOLATION && lastErr != ERROR_ACCESS_DENIED) {
            break;
        }
        if (attempt + 1 < kMaxAttempts) {
            std::this_thread::sleep_for(std::chrono::milliseconds(15));
        }
    }
    return std::unexpected(MakeError(ErrorDomain::Core,
                                     ErrorCode::IoFailure,
                                     "Failed atomic rename",
                                     FormatWin32ErrorDetail(lastErr)));
}

}  // namespace

Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) {
        return std::unexpected(
            MakeError(ErrorDomain::Core, ErrorCode::IoFailure, "Failed to inspect file", ec.message()));
    }
    if (!exists) {
        return std::unexpected(
            MakeError(ErrorDomain::Core, ErrorCode::NotFound, "File does not exist", PathToUtf8(path)));
    }

    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file.is_open()) {
        return std::unexpected(MakeError(ErrorDomain::Core,
                                         ErrorCode::IoFailure,
                                         "Failed to open file for reading",
                                         PathToUtf8(path)));
    }

    const auto size = file.tellg();
    if (size < 0) {
        return std::unexpected(MakeError(
            ErrorDomain::Core, ErrorCode::IoFailure, "Failed to determine file size", PathToUtf8(path)));
    }

    std::vector<std::uint8_t> data(static_cast<std::size_t>(size));
    file.seekg(0, std::ios::beg);
    if (size > 0 && !file.read(reinterpret_cast<char*>(data.data()), size)) {
        return std::unexpected(MakeError(
            ErrorDomain::Core, ErrorCode::IoFailure, "Failed while reading file", PathToUtf8(path)));
    }
    return data;
}

Result<std::string> ReadFileUtf8(const std::filesystem::path& path) {
    auto bytes = ReadFileBytes(path);
    if (!bytes) {
        return std::unexpected(bytes.error());
    }
    std::string out(reinterpret_cast<const char*>(bytes->data()), bytes->size());
    if (out.size() >= 3 && static_cast<unsigned char>(out[0]) == 0xEF
        && static_cast<unsigned char>(out[1]) == 0xBB && static_cast<unsigned char>(out[2]) == 0xBF) {
        out.erase(0, 3);
    }
    return out;
}

Result<void> WriteFileAtomic(const std::filesystem::path& path, std::span<const std::uint8_t> bytes) {
    if (path.empty() || std::wstring_view(path.native()).find(L'\0') != std::wstring_view::npos) {
        return std::unexpected(
            MakeError(ErrorDomain::Core, ErrorCode::InvalidArgument, "Target path is invalid"));
    }

    std::error_code ec;
    if (path.has_parent_path()) {
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec) {
            return std::unexpected(MakeError(ErrorDomain::Core,
                                             ErrorCode::IoFailure,
                                             "Failed to create parent directory",
                                             ec.message()));
        }
    }

    auto created = CreateUniqueTempFile(path);
    if (!created) {
        return std::unexpected(created.error());
    }
    std::filesystem::path tmpPath = std::move(created->first);
    UniqueHandle tmpFile = std::move(created->second);

    if (const auto writeRes = WriteAllAndFlush(tmpFile.Get(), bytes); !writeRes) {
        tmpFile.Reset();
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return std::unexpected(writeRes.error());
    }

    const HANDLE rawHandle = tmpFile.Release();
    if (!::CloseHandle(rawHandle)) {
        const DWORD err = ::GetLastError();
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return std::unexpected(MakeError(ErrorDomain::Core,
                                         ErrorCode::IoFailure,
                                         "Failed closing temp file",
                                         FormatWin32ErrorDetail(err)));
    }

    if (const auto moveRes = MoveTempIntoPlace(tmpPath, path); !moveRes) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return std::unexpected(moveRes.error());
    }
    return {};
}

Result<void> WriteTextAtomic(const std::filesystem::path& path, std::string_view content) {
    const auto span = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
    return WriteFileAtomic(path, span);
}

}  // namespace motrix::core
