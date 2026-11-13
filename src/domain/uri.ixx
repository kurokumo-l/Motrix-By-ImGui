export module motrix.domain:uri;

import std;
import motrix.core;

namespace motrix::domain {

export enum class DeepLinkAction {
    None = 0,
    TaskList,
    NewTask,
    NewBtTask,
    PauseAll,
    ResumeAll,
    Preferences,
    About,
};

export struct ParsedDeepLink {
    DeepLinkAction action = DeepLinkAction::None;
    std::string uriParam;
};

export enum class StartupArgKind {
    None = 0,
    ResourceUri,
    TorrentFile,
    DeepLink,
};

export struct ClassifiedStartupArg {
    StartupArgKind kind = StartupArgKind::None;
    std::string uri;
    std::filesystem::path filePath;
    ParsedDeepLink deepLink;
};

export [[nodiscard]] motrix::core::Result<std::string> DecodeThunderUri(std::string_view uri);
export [[nodiscard]] std::vector<std::string> SplitAndNormalizeUris(std::string_view rawInput);
export [[nodiscard]] bool IsMagnetUri(std::string_view uri) noexcept;
export [[nodiscard]] std::string ExtractMagnetDisplayName(std::string_view uri);
export [[nodiscard]] bool IsSupportedDownloadUri(std::string_view uri) noexcept;
export [[nodiscard]] ParsedDeepLink ParseMotrixDeepLink(std::string_view uri);
export [[nodiscard]] ClassifiedStartupArg ClassifyStartupArg(std::string_view arg);

}  // namespace motrix::domain
