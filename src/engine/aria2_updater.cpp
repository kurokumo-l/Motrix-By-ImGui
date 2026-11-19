module;

#include <nlohmann/json.hpp>

module motrix.engine;

import std;
import motrix.core;
import :aria2_updater;
import :http_winhttp;
import :process_manager;
import :win32_util;

namespace motrix::engine {

namespace {

constexpr std::size_t kMaxReleaseJsonBytes = 2u * 1024u * 1024u;
constexpr std::size_t kMaxEngineBinaryBytes = 64u * 1024u * 1024u;

[[nodiscard]] bool IsDirectExeUrl(std::string_view url) noexcept {
    const std::size_t q = url.find('?');
    const std::string_view pathPart = (q == std::string_view::npos) ? url : url.substr(0, q);
    return motrix::core::AsciiLower(pathPart).ends_with(".exe");
}

[[nodiscard]] int ScoreWindowsX64ExeAsset(std::string_view name) noexcept {
    const std::string lower = motrix::core::AsciiLower(name);
    if (!lower.ends_with(".exe")) {
        return -1000;
    }
    if (lower.find("arm64") != std::string::npos || lower.find("aarch64") != std::string::npos
        || lower.find("386") != std::string::npos || lower.find("x86.") != std::string::npos
        || lower.find("win32") != std::string::npos) {
        return -50;
    }
    int score = 10;
    if (lower.find("windows") != std::string::npos) {
        score += 100;
    }
    if (lower.find("x86_64") != std::string::npos || lower.find("x64") != std::string::npos
        || lower.find("amd64") != std::string::npos) {
        score += 50;
    }
    if (lower.find("aria2-next") != std::string::npos) {
        score += 20;
    }
    return score;
}

[[nodiscard]] std::string FilenameFromUrl(std::string_view url) {
    const std::size_t q = url.find('?');
    const std::string_view pathPart = (q == std::string_view::npos) ? url : url.substr(0, q);
    const std::size_t slash = pathPart.find_last_of('/');
    if (slash == std::string_view::npos || slash + 1 >= pathPart.size()) {
        return "aria2-next-windows-x86_64.exe";
    }
    return std::string(pathPart.substr(slash + 1));
}

void CleanupFileQuietly(const std::filesystem::path& path) noexcept {
    if (path.empty()) {
        return;
    }
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

[[nodiscard]] motrix::core::Result<Aria2NextStageOutcome>
DownloadAndVerifyBinary(const Aria2NextUpdateSpec& spec,
                        const Aria2NextReleaseInfo& release,
                        const std::function<void(Aria2NextUpdatePhase)>& onPhase) {
    if (onPhase) {
        onPhase(Aria2NextUpdatePhase::Downloading);
    }
    const std::filesystem::path stagePath =
        spec.targetExePath.parent_path() / "aria2-next.exe.stage.exe";
    CleanupFileQuietly(stagePath);

    detail::HttpRequestSpec binReq{
        .url = release.downloadUrl,
        .proxyUrl = spec.proxyUrl,
        .acceptHeader = "application/octet-stream",
        .timeout = spec.timeout,
        .maxBytes = kMaxEngineBinaryBytes,
        .stopToken = spec.stopToken,
    };
    const auto dlRes = detail::HttpDownloadToFile(binReq, stagePath);
    if (!dlRes) {
        return std::unexpected(dlRes.error());
    }

    if (onPhase) {
        onPhase(Aria2NextUpdatePhase::Verifying);
    }
    if (!LooksLikePeExecutableHeader(stagePath)) {
        CleanupFileQuietly(stagePath);
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Downloaded file is not a valid Windows PE executable (.exe)"));
    }
    if (!release.expectedSha256.empty()) {
        const auto shaRes = detail::ComputeFileSha256Hex(stagePath);
        if (!shaRes || *shaRes != release.expectedSha256) {
            CleanupFileQuietly(stagePath);
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::ParseFailure,
                                        "Downloaded aria2-next SHA-256 digest verification failed"));
        }
    }

    const auto probe = ProbeEngineBinaryVersion(stagePath, std::chrono::milliseconds(4000));
    if (!probe || probe->version.empty()) {
        CleanupFileQuietly(stagePath);
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Downloaded executable failed aria2 --version verification",
                                    probe ? std::string{} : probe.error().message));
    }
    if (probe->kind != EngineKind::Aria2Next) {
        CleanupFileQuietly(stagePath);
        return std::unexpected(motrix::core::MakeError(
            motrix::core::ErrorDomain::Engine,
            motrix::core::ErrorCode::ParseFailure,
            std::format("Downloaded binary identified as '{}' instead of aria2-next", probe->product)));
    }

    Aria2NextStageOutcome out{};
    out.release = release;
    if (out.release.normalizedVersion.empty()) {
        out.release.normalizedVersion = probe->version;
    }
    out.detectedVersion = probe->version;
    out.detectedKind = probe->kind;

    std::error_code ec;
    const bool targetExists = std::filesystem::is_regular_file(spec.targetExePath, ec);
    if (targetExists && !spec.forceReinstall && spec.currentKind == EngineKind::Aria2Next
        && !spec.currentVersion.empty()
        && NormalizeVersionTag(spec.currentVersion) == NormalizeVersionTag(probe->version)) {
        CleanupFileQuietly(stagePath);
        out.alreadyUpToDate = true;
        return out;
    }

    out.stagedExePath = stagePath;
    return out;
}

}  // namespace

std::filesystem::path ManagedAria2NextExecutablePath(const std::filesystem::path& appDataDir) {
    if (appDataDir.empty()) {
        return {};
    }
    return appDataDir / "engine" / "aria2-next.exe";
}

std::string NormalizeVersionTag(std::string_view tag) {
    std::string_view t = motrix::core::TrimAscii(tag);
    while (!t.empty() && (t.front() == 'v' || t.front() == 'V')) {
        t.remove_prefix(1);
    }
    return std::string(t);
}

[[nodiscard]] bool IsGitHubHostUrl(std::string_view url) noexcept {
    const std::size_t schemeEnd = url.find(":\x2f\x2f");
    if (schemeEnd == std::string_view::npos) {
        return false;
    }
    const std::string_view rest = url.substr(schemeEnd + 3);
    const std::size_t authEnd = rest.find_first_of("/?#");
    std::string_view hostPort = (authEnd == std::string_view::npos) ? rest : rest.substr(0, authEnd);
    const std::size_t at = hostPort.rfind('@');
    if (at != std::string_view::npos) {
        hostPort.remove_prefix(at + 1);
    }
    const std::size_t colon = hostPort.find(':');
    const std::string host = motrix::core::AsciiLower(
        (colon == std::string_view::npos) ? hostPort : hostPort.substr(0, colon));
    return host == "github.com" || host.ends_with(".github.com") || host == "githubusercontent.com"
           || host.ends_with(".githubusercontent.com");
}

std::string ApplyGitHubMirrorPrefix(std::string_view assetUrl, std::string_view mirrorPrefix) {
    const std::string_view url = motrix::core::TrimAscii(assetUrl);
    const std::string_view mirror = motrix::core::TrimAscii(mirrorPrefix);
    if (mirror.empty() || url.empty() || !IsGitHubHostUrl(url)) {
        return std::string(url);
    }
    if (url.starts_with(mirror)) {
        return std::string(url);
    }
    if (mirror.ends_with('/') || mirror.ends_with('=') || mirror.ends_with('?')) {
        return std::string(mirror) + std::string(url);
    }
    return std::string(mirror) + "/" + std::string(url);
}

[[nodiscard]] std::string ExtractAssetSha256(const nlohmann::json& item) {
    const auto dIt = item.find("digest");
    if (dIt == item.end() || !dIt->is_string()) {
        return {};
    }
    const std::string d = motrix::core::AsciiLower(dIt->get_ref<const std::string&>());
    if (d.starts_with("sha256:") && d.size() == 71u) {
        return d.substr(7);
    }
    return {};
}

motrix::core::Result<Aria2NextReleaseInfo> ParseAria2NextReleaseJson(std::string_view jsonText,
                                                                     std::string_view mirrorPrefix) {
    const auto root = nlohmann::json::parse(jsonText.begin(), jsonText.end(), nullptr, false);
    if (root.is_discarded() || !root.is_object()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Invalid GitHub release JSON response"));
    }

    Aria2NextReleaseInfo info{};
    if (const auto it = root.find("tag_name"); it != root.end() && it->is_string()) {
        info.tagName = it->get_ref<const std::string&>();
        info.normalizedVersion = NormalizeVersionTag(info.tagName);
    }

    const auto assetsIt = root.find("assets");
    if (assetsIt == root.end() || !assetsIt->is_array()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::ParseFailure,
                                                       "Release JSON contains no assets array"));
    }

    int bestScore = -1000;
    std::string rawUrl;
    for (const auto& item : *assetsIt) {
        if (!item.is_object()) {
            continue;
        }
        const auto nameIt = item.find("name");
        const auto urlIt = item.find("browser_download_url");
        if (nameIt == item.end() || !nameIt->is_string() || urlIt == item.end() || !urlIt->is_string()) {
            continue;
        }
        const std::string& candidateName = nameIt->get_ref<const std::string&>();
        const int score = ScoreWindowsX64ExeAsset(candidateName);
        if (score > bestScore) {
            bestScore = score;
            info.assetName = candidateName;
            rawUrl = urlIt->get_ref<const std::string&>();
            info.expectedSha256 = ExtractAssetSha256(item);
        }
    }

    if (bestScore < 0 || rawUrl.empty()) {
        return std::unexpected(motrix::core::MakeError(
            motrix::core::ErrorDomain::Engine,
            motrix::core::ErrorCode::NotFound,
            "No compatible Windows x86_64 .exe asset found in aria2-next release"));
    }

    info.downloadUrl = ApplyGitHubMirrorPrefix(rawUrl, mirrorPrefix);
    return info;
}

namespace {

[[nodiscard]] std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

}  // namespace

bool LooksLikePeExecutableHeader(const std::filesystem::path& filePath) {
    std::error_code ec;
    const auto sz = std::filesystem::file_size(filePath, ec);
    if (ec || sz < 4096 || sz > kMaxEngineBinaryBytes) {
        return false;
    }
    std::ifstream in(filePath, std::ios::binary);
    if (!in) {
        return false;
    }
    std::array<std::uint8_t, 0x40> dosHdr{};
    in.read(reinterpret_cast<char*>(dosHdr.data()), dosHdr.size());
    if (in.gcount() != static_cast<std::streamsize>(dosHdr.size()) || dosHdr[0] != 'M'
        || dosHdr[1] != 'Z') {
        return false;
    }
    const std::uint32_t peOffset = ReadLe32(dosHdr.data() + 0x3C);
    if (peOffset < 0x40 || peOffset > 4096 || static_cast<std::uint64_t>(peOffset) + 26 > sz) {
        return false;
    }
    in.seekg(static_cast<std::streamoff>(peOffset), std::ios::beg);
    std::array<std::uint8_t, 26> nt{};
    in.read(reinterpret_cast<char*>(nt.data()), nt.size());
    if (in.gcount() != static_cast<std::streamsize>(nt.size())) {
        return false;
    }
    if (nt[0] != 'P' || nt[1] != 'E' || nt[2] != 0 || nt[3] != 0) {
        return false;
    }
    const std::uint16_t machine = ReadLe16(nt.data() + 4);
    const std::uint16_t sections = ReadLe16(nt.data() + 6);
    const std::uint16_t optSize = ReadLe16(nt.data() + 20);
    const std::uint16_t chars = ReadLe16(nt.data() + 22);
    const std::uint16_t magic = ReadLe16(nt.data() + 24);
    const bool knownArch = (machine == 0x8664u || machine == 0x014Cu || machine == 0xAA64u);
    const bool validOpt = (magic == 0x020Bu || magic == 0x010Bu) && optSize >= 0x60u
                          && static_cast<std::uint64_t>(peOffset) + 24 + optSize <= sz;
    return knownArch && sections >= 1 && (chars & 0x0002u) != 0 && validOpt;
}

motrix::core::Result<Aria2NextStageOutcome>
StageAria2NextUpdate(const Aria2NextUpdateSpec& spec,
                     const std::function<void(Aria2NextUpdatePhase)>& onPhase) {
    const std::string_view url = motrix::core::TrimAscii(spec.updateUrl);
    if (url.empty() || spec.targetExePath.empty()) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::InvalidArgument,
                                                       "aria2-next update URL or target path is empty"));
    }

    if (IsDirectExeUrl(url)) {
        Aria2NextReleaseInfo direct{};
        direct.assetName = FilenameFromUrl(url);
        direct.downloadUrl = ApplyGitHubMirrorPrefix(url, spec.mirrorPrefix);
        return DownloadAndVerifyBinary(spec, direct, onPhase);
    }

    if (onPhase) {
        onPhase(Aria2NextUpdatePhase::Checking);
    }
    detail::HttpRequestSpec jsonReq{
        .url = ApplyGitHubMirrorPrefix(url, spec.mirrorPrefix),
        .proxyUrl = spec.proxyUrl,
        .acceptHeader = "application/vnd.github+json",
        .timeout = spec.timeout,
        .maxBytes = kMaxReleaseJsonBytes,
        .stopToken = spec.stopToken,
    };
    const auto jsonRes = detail::HttpGetUtf8OrBytes(jsonReq);
    if (!jsonRes) {
        return std::unexpected(jsonRes.error());
    }

    const auto relRes = ParseAria2NextReleaseJson(*jsonRes, spec.mirrorPrefix);
    if (!relRes) {
        return std::unexpected(relRes.error());
    }

    std::error_code ec;
    const bool targetExists = std::filesystem::is_regular_file(spec.targetExePath, ec);
    const std::string normCurrent = NormalizeVersionTag(spec.currentVersion);
    if (targetExists && !spec.forceReinstall && spec.currentKind == EngineKind::Aria2Next
        && !normCurrent.empty() && !relRes->normalizedVersion.empty()
        && normCurrent == relRes->normalizedVersion) {
        return Aria2NextStageOutcome{.alreadyUpToDate = true, .release = *relRes};
    }

    return DownloadAndVerifyBinary(spec, *relRes, onPhase);
}

motrix::core::Result<void> InstallStagedAria2Next(const std::filesystem::path& stagedExePath,
                                                  const std::filesystem::path& targetExePath) {
    std::error_code ec;
    if (!std::filesystem::is_regular_file(stagedExePath, ec)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::NotFound,
                                                       "Staged aria2-next binary does not exist"));
    }
    if (targetExePath.has_parent_path()) {
        std::filesystem::create_directories(targetExePath.parent_path(), ec);
        if (ec) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::IoFailure,
                                                           "Failed to create engine target directory",
                                                           ec.message()));
        }
    }

    const std::filesystem::path backupPath = targetExePath.parent_path() / "aria2-next.exe.bak";
    CleanupFileQuietly(backupPath);
    const bool hadExisting = std::filesystem::exists(targetExePath, ec);
    if (hadExisting) {
        std::filesystem::rename(targetExePath, backupPath, ec);
        if (ec) {
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::IoFailure,
                                        "Failed to replace running aria2-next.exe (file may be locked)",
                                        ec.message()));
        }
    }

    std::filesystem::rename(stagedExePath, targetExePath, ec);
    if (ec) {
        if (hadExisting) {
            std::error_code restoreEc;
            std::filesystem::rename(backupPath, targetExePath, restoreEc);
        }
        CleanupFileQuietly(stagedExePath);
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to install staged aria2-next.exe",
                                                       ec.message()));
    }

    CleanupFileQuietly(backupPath);
    return {};
}

}  // namespace motrix::engine
