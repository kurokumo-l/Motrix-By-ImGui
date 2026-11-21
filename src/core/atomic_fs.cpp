module motrix.core;

import std;
import :encoding;
import :error;

namespace motrix::core {

Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path) {
    std::error_code ec;
    if (!std::filesystem::exists(path, ec)) {
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
    if (path.empty()) {
        return std::unexpected(
            MakeError(ErrorDomain::Core, ErrorCode::InvalidArgument, "Target path is empty"));
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

    std::filesystem::path tmpPath = path;
    tmpPath += std::format(".{}.tmp", std::this_thread::get_id());

    {
        std::ofstream out(tmpPath, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return std::unexpected(MakeError(ErrorDomain::Core,
                                             ErrorCode::IoFailure,
                                             "Failed to open temp file",
                                             PathToUtf8(tmpPath)));
        }
        if (!bytes.empty()) {
            out.write(reinterpret_cast<const char*>(bytes.data()),
                      static_cast<std::streamsize>(bytes.size()));
        }
        out.flush();
        if (!out.good()) {
            out.close();
            std::filesystem::remove(tmpPath, ec);
            return std::unexpected(MakeError(ErrorDomain::Core,
                                             ErrorCode::IoFailure,
                                             "Failed writing temp file",
                                             PathToUtf8(tmpPath)));
        }
    }

    std::filesystem::rename(tmpPath, path, ec);
    if (ec) {
        std::error_code rmEc;
        std::filesystem::remove(tmpPath, rmEc);
        return std::unexpected(
            MakeError(ErrorDomain::Core, ErrorCode::IoFailure, "Failed atomic rename", ec.message()));
    }
    return {};
}

Result<void> WriteTextAtomic(const std::filesystem::path& path, std::string_view content) {
    const auto span = std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(content.data()), content.size());
    return WriteFileAtomic(path, span);
}

}  // namespace motrix::core
