export module motrix.engine:win32_util;

import std;
import motrix.core;

namespace motrix::engine::detail {

export using ScopedWinHandle = motrix::core::UniqueHandle;

export [[nodiscard]] std::wstring QuoteWin32Arg(std::wstring_view arg);
export [[nodiscard]] std::wstring BuildCommandLineW(const std::filesystem::path& exe,
                                                    const std::vector<std::string>& args);
export [[nodiscard]] std::filesystem::path GetExecutableDir();
export [[nodiscard]] std::filesystem::path GetLocalAppDataDir();
export [[nodiscard]] bool IsRegularFileSafe(const std::filesystem::path& p);

export struct ChildProcessLaunchSpec {
    std::filesystem::path executablePath;
    std::wstring commandLine;
    std::filesystem::path workingDir;
    void* stdOutErrHandle = nullptr;
    std::uint32_t creationFlags = 0;
    bool suppressErrorDialogs = false;
};

export struct ChildProcessHandles {
    void* process = nullptr;
    void* thread = nullptr;
    std::uint32_t processId = 0;
};

export [[nodiscard]] motrix::core::Result<ChildProcessHandles>
LaunchChildProcessWithHandleList(ChildProcessLaunchSpec& spec);
export [[nodiscard]] motrix::core::Result<std::string>
ComputeFileSha256Hex(const std::filesystem::path& filePath);

}  // namespace motrix::engine::detail
