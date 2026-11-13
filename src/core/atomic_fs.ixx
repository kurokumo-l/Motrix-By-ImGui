export module motrix.core:atomic_fs;

import std;
import :error;

namespace motrix::core {

export [[nodiscard]] Result<std::string> ReadFileUtf8(const std::filesystem::path& path);
export [[nodiscard]] Result<std::vector<std::uint8_t>> ReadFileBytes(const std::filesystem::path& path);
export [[nodiscard]] Result<void> WriteFileAtomic(const std::filesystem::path& path,
                                                  std::span<const std::uint8_t> bytes);
export [[nodiscard]] Result<void> WriteTextAtomic(const std::filesystem::path& path,
                                                  std::string_view content);

}  // namespace motrix::core
