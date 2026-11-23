export module motrix.engine:aria2_updater;

import std;
import motrix.core;
import :dialect;

namespace motrix::engine {

export enum class Aria2NextUpdatePhase {
    Checking = 0,
    Downloading,
    Verifying,
};

export struct Aria2NextReleaseInfo {
    std::string tagName;
    std::string normalizedVersion;
    std::string assetName;
    std::string downloadUrl;
    std::string expectedSha256;
};

export struct Aria2NextUpdateSpec {
    std::string updateUrl;
    std::string mirrorPrefix;
    std::string proxyUrl;
    std::filesystem::path targetExePath;
    std::string currentVersion;
    EngineKind currentKind = EngineKind::Unknown;
    std::chrono::milliseconds timeout = std::chrono::milliseconds(30000);
    bool forceReinstall = false;
    std::stop_token stopToken = {};
    std::string expectedSha256;
};

export struct Aria2NextStageOutcome {
    bool alreadyUpToDate = false;
    Aria2NextReleaseInfo release;
    std::filesystem::path stagedExePath;
    std::string detectedVersion;
    EngineKind detectedKind = EngineKind::Unknown;
};

export [[nodiscard]] std::filesystem::path
ManagedAria2NextExecutablePath(const std::filesystem::path& appDataDir);

export [[nodiscard]] std::string NormalizeVersionTag(std::string_view tag);

export [[nodiscard]] std::string ApplyGitHubMirrorPrefix(std::string_view assetUrl,
                                                         std::string_view mirrorPrefix);

export [[nodiscard]] motrix::core::Result<Aria2NextReleaseInfo>
ParseAria2NextReleaseJson(std::string_view jsonText, std::string_view mirrorPrefix = {});

export [[nodiscard]] bool LooksLikePeExecutableHeader(const std::filesystem::path& filePath);

export [[nodiscard]] motrix::core::Result<Aria2NextStageOutcome>
StageAria2NextUpdate(const Aria2NextUpdateSpec& spec,
                     const std::function<void(Aria2NextUpdatePhase)>& onPhase = {});

export [[nodiscard]] motrix::core::Result<void>
InstallStagedAria2Next(const std::filesystem::path& stagedExePath,
                       const std::filesystem::path& targetExePath);

}  // namespace motrix::engine
