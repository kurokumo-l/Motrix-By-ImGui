export module motrix.core:encoding;

import std;

namespace motrix::core {

export [[nodiscard]] std::wstring Utf8ToWide(std::string_view utf8);
export [[nodiscard]] std::string WideToUtf8(std::wstring_view wide);
export [[nodiscard]] std::string PathToUtf8(const std::filesystem::path& path);
export [[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view utf8);
export [[nodiscard]] std::string AsciiLower(std::string_view text);
export [[nodiscard]] std::string_view TrimAscii(std::string_view text) noexcept;
export [[nodiscard]] bool StartsWithAsciiInsensitive(std::string_view text,
                                                     std::string_view prefix) noexcept;
export [[nodiscard]] bool EndsWithAsciiInsensitive(std::string_view text,
                                                   std::string_view suffix) noexcept;
export [[nodiscard]] std::string_view TrimQuotedArg(std::string_view text) noexcept;
export [[nodiscard]] std::optional<int> ParseStrictInt(std::string_view text) noexcept;
export [[nodiscard]] std::filesystem::path CurrentExecutablePath();

export class UniqueHandle {
public:
    UniqueHandle() noexcept = default;
    explicit UniqueHandle(void* raw) noexcept : handle_(raw) {}
    ~UniqueHandle();

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;

    UniqueHandle(UniqueHandle&& other) noexcept;
    UniqueHandle& operator=(UniqueHandle&& other) noexcept;

    [[nodiscard]] void* Get() const noexcept { return handle_; }
    [[nodiscard]] bool IsValid() const noexcept;
    void Reset(void* newHandle = nullptr) noexcept;
    [[nodiscard]] void* Release() noexcept;

private:
    void* handle_ = nullptr;
};

export void FreeWin32Local(void* ptr) noexcept;

export struct LocalFreeDeleter {
    void operator()(void* ptr) const noexcept { FreeWin32Local(ptr); }
};

export template <typename T = void> using UniqueLocalPtr = std::unique_ptr<T, LocalFreeDeleter>;

}  // namespace motrix::core
