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

std::atomic<std::uint64_t> gUpdaterTempSeq{1};

[[nodiscard]] bool HasControlOrBackslash(std::string_view text) noexcept {
    for (const char ch : text) {
        const auto uch = static_cast<unsigned char>(ch);
        if (ch == '\\' || uch < 0x20u || uch == 0x7Fu) {
            return true;
        }
    }
    return false;
}

[[nodiscard]] bool IsHexDigitAscii(char ch) noexcept {
    return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
}

[[nodiscard]] std::string NormalizeSha256Hex(std::string_view raw) {
    const std::string_view trimmed = motrix::core::TrimAscii(raw);
    if (trimmed.size() != 64u) {
        return {};
    }
    for (const char ch : trimmed) {
        if (!IsHexDigitAscii(ch)) {
            return {};
        }
    }
    return motrix::core::AsciiLower(trimmed);
}

struct ParsedAuthorityUrl {
    std::string_view scheme;
    std::string_view host;
};

[[nodiscard]] std::optional<ParsedAuthorityUrl> ParseHttpUrlAuthority(std::string_view url) noexcept {
    const std::string_view trimmed = motrix::core::TrimAscii(url);
    if (trimmed.empty() || HasControlOrBackslash(trimmed)) {
        return std::nullopt;
    }
    const std::size_t schemeEnd = trimmed.find("://");
    if (schemeEnd == std::string_view::npos || schemeEnd == 0u) {
        return std::nullopt;
    }
    const std::string_view scheme = trimmed.substr(0, schemeEnd);
    if (!motrix::core::EqualsAsciiInsensitive(scheme, "https")
        && !motrix::core::EqualsAsciiInsensitive(scheme, "http")) {
        return std::nullopt;
    }
    const std::string_view rest = trimmed.substr(schemeEnd + 3u);
    const std::size_t authEnd = rest.find_first_of("/?#\\");
    const std::string_view authority =
        (authEnd == std::string_view::npos) ? rest : rest.substr(0, authEnd);
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return std::nullopt;
    }

    std::string_view host;
    if (authority.starts_with('[')) {
        const std::size_t closeBracket = authority.find(']');
        if (closeBracket == std::string_view::npos || closeBracket <= 1u) {
            return std::nullopt;
        }
        const std::string_view tail = authority.substr(closeBracket + 1u);
        if (!tail.empty() && !tail.starts_with(':')) {
            return std::nullopt;
        }
        host = authority.substr(0, closeBracket + 1u);
    } else {
        const std::size_t colon = authority.find(':');
        host = (colon == std::string_view::npos) ? authority : authority.substr(0, colon);
        if (colon != std::string_view::npos) {
            const std::string_view portPart = authority.substr(colon + 1u);
            if (portPart.empty()) {
                return std::nullopt;
            }
            for (const char c : portPart) {
                if (c < '0' || c > '9') {
                    return std::nullopt;
                }
            }
        }
    }
    if (host.empty()) {
        return std::nullopt;
    }
    return ParsedAuthorityUrl{.scheme = scheme, .host = host};
}

[[nodiscard]] bool IsLoopbackHost(std::string_view host) noexcept {
    return motrix::core::EqualsAsciiInsensitive(host, "localhost") || host == "127.0.0.1"
           || host == "[::1]";
}

[[nodiscard]] bool IsLoopbackUrl(std::string_view url) noexcept {
    const auto parsed = ParseHttpUrlAuthority(url);
    return parsed.has_value() && IsLoopbackHost(parsed->host);
}

[[nodiscard]] bool IsAllowedUpdateUrl(std::string_view url, bool allowLoopbackHttp) noexcept {
    const auto parsed = ParseHttpUrlAuthority(url);
    if (!parsed) {
        return false;
    }
    if (motrix::core::EqualsAsciiInsensitive(parsed->scheme, "https")) {
        return true;
    }
    return allowLoopbackHttp && IsLoopbackHost(parsed->host);
}

[[nodiscard]] bool IsValidHttpsMirrorPrefix(std::string_view mirrorPrefix) noexcept {
    const std::string_view trimmed = motrix::core::TrimAscii(mirrorPrefix);
    if (trimmed.empty()) {
        return true;
    }
    const auto parsed = ParseHttpUrlAuthority(trimmed);
    return parsed.has_value() && motrix::core::EqualsAsciiInsensitive(parsed->scheme, "https");
}

[[nodiscard]] bool IsGitHubHostUrl(std::string_view url) noexcept {
    const auto parsed = ParseHttpUrlAuthority(url);
    if (!parsed || !motrix::core::EqualsAsciiInsensitive(parsed->scheme, "https")) {
        return false;
    }
    const std::string_view host = parsed->host;
    if (host.starts_with('.')) {
        return false;
    }
    return motrix::core::EqualsAsciiInsensitive(host, "github.com")
           || motrix::core::EndsWithAsciiInsensitive(host, ".github.com")
           || motrix::core::EqualsAsciiInsensitive(host, "githubusercontent.com")
           || motrix::core::EndsWithAsciiInsensitive(host, ".githubusercontent.com");
}

[[nodiscard]] std::string ExtractAssetSha256(const nlohmann::json& item) {
    const auto dIt = item.find("digest");
    if (dIt == item.end() || !dIt->is_string()) {
        return {};
    }
    const std::string d = motrix::core::AsciiLower(dIt->get_ref<const std::string&>());
    if (!d.starts_with("sha256:")) {
        return {};
    }
    return NormalizeSha256Hex(std::string_view(d).substr(7u));
}

[[nodiscard]] bool IsDirectExeUrl(std::string_view url) noexcept {
    const std::size_t q = url.find('?');
    const std::string_view pathPart = (q == std::string_view::npos) ? url : url.substr(0, q);
    return motrix::core::EndsWithAsciiInsensitive(pathPart, ".exe");
}

[[nodiscard]] int ScoreWindowsX64ExeAsset(std::string_view name) noexcept {
    using motrix::core::ContainsAsciiInsensitive;
    if (!motrix::core::EndsWithAsciiInsensitive(name, ".exe")) {
        return -1000;
    }
    if (ContainsAsciiInsensitive(name, "arm64") || ContainsAsciiInsensitive(name, "aarch64")
        || ContainsAsciiInsensitive(name, "386") || ContainsAsciiInsensitive(name, "i686")
        || ContainsAsciiInsensitive(name, "ia64") || ContainsAsciiInsensitive(name, "x86.")
        || ContainsAsciiInsensitive(name, "x86-") || ContainsAsciiInsensitive(name, "win32")) {
        return -50;
    }
    int score = 10;
    if (ContainsAsciiInsensitive(name, "windows")) {
        score += 100;
    }
    if (ContainsAsciiInsensitive(name, "x86_64") || ContainsAsciiInsensitive(name, "x64")
        || ContainsAsciiInsensitive(name, "amd64")) {
        score += 50;
    }
    if (ContainsAsciiInsensitive(name, "aria2-next")) {
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

[[nodiscard]] std::filesystem::path MakeUniqueSiblingPath(const std::filesystem::path& targetExePath,
                                                          std::string_view suffix) {
    const std::uint64_t seq = gUpdaterTempSeq.fetch_add(1u, std::memory_order_relaxed);
    const std::uint32_t pid = CurrentHostProcessId();
    const std::filesystem::path dir = targetExePath.parent_path();
    const std::string name = std::format("aria2-next.{}.{}.{}", pid, seq, suffix);
    return dir.empty() ? std::filesystem::path(name) : (dir / name);
}

[[nodiscard]] motrix::core::Result<Aria2NextReleaseInfo>
ParseReleaseJsonChecked(std::string_view jsonText,
                        std::string_view mirrorPrefix,
                        bool requireGitHubAssetHost,
                        bool allowLoopbackHttp) {
    if (!IsValidHttpsMirrorPrefix(mirrorPrefix)) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::InvalidArgument,
                                    "aria2-next mirror prefix must be a valid https:// URL"));
    }

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
    if (!IsAllowedUpdateUrl(rawUrl, allowLoopbackHttp)) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Release asset download URL violates HTTPS/security policy"));
    }
    if (requireGitHubAssetHost && !IsGitHubHostUrl(rawUrl)) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "GitHub release asset URL does not point to a GitHub HTTPS host"));
    }

    info.downloadUrl = ApplyGitHubMirrorPrefix(rawUrl, mirrorPrefix);
    return info;
}

[[nodiscard]] motrix::core::Result<Aria2NextStageOutcome>
DownloadAndVerifyBinary(const Aria2NextUpdateSpec& spec,
                        const Aria2NextReleaseInfo& release,
                        const std::function<void(Aria2NextUpdatePhase)>& onPhase) {
    const std::string requiredSha256 = NormalizeSha256Hex(release.expectedSha256);
    if (requiredSha256.empty()) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Missing required SHA-256 digest for aria2-next binary"));
    }

    if (onPhase) {
        onPhase(Aria2NextUpdatePhase::Downloading);
    }
    const std::filesystem::path stagePath = MakeUniqueSiblingPath(spec.targetExePath, "stage.exe");
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
    const auto shaRes = detail::ComputeFileSha256Hex(stagePath);
    if (!shaRes || *shaRes != requiredSha256) {
        CleanupFileQuietly(stagePath);
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::ParseFailure,
                                    "Downloaded aria2-next SHA-256 digest verification failed"));
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
    out.release.expectedSha256 = requiredSha256;
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

[[nodiscard]] std::uint16_t ReadLe16(const std::uint8_t* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) | (static_cast<std::uint16_t>(p[1]) << 8);
}

[[nodiscard]] std::uint32_t ReadLe32(const std::uint8_t* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) | (static_cast<std::uint32_t>(p[1]) << 8)
           | (static_cast<std::uint32_t>(p[2]) << 16) | (static_cast<std::uint32_t>(p[3]) << 24);
}

[[nodiscard]] std::error_code MoveOrCopyToTarget(const std::filesystem::path& stagedExePath,
                                                 const std::filesystem::path& targetExePath) {
    std::error_code ec;
    std::filesystem::rename(stagedExePath, targetExePath, ec);
    if (!ec) {
        return {};
    }

    const std::filesystem::path tempCopy = MakeUniqueSiblingPath(targetExePath, "copy.tmp");
    CleanupFileQuietly(tempCopy);
    std::error_code copyEc;
    std::filesystem::copy_file(
        stagedExePath, tempCopy, std::filesystem::copy_options::overwrite_existing, copyEc);
    if (copyEc) {
        CleanupFileQuietly(tempCopy);
        return ec;
    }
    std::filesystem::rename(tempCopy, targetExePath, copyEc);
    if (copyEc) {
        CleanupFileQuietly(tempCopy);
        return copyEc;
    }
    CleanupFileQuietly(stagedExePath);
    return {};
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

std::string ApplyGitHubMirrorPrefix(std::string_view assetUrl, std::string_view mirrorPrefix) {
    const std::string_view url = motrix::core::TrimAscii(assetUrl);
    const std::string_view mirror = motrix::core::TrimAscii(mirrorPrefix);
    if (mirror.empty() || url.empty() || !IsValidHttpsMirrorPrefix(mirror) || !IsGitHubHostUrl(url)) {
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

motrix::core::Result<Aria2NextReleaseInfo> ParseAria2NextReleaseJson(std::string_view jsonText,
                                                                     std::string_view mirrorPrefix) {
    return ParseReleaseJsonChecked(jsonText, mirrorPrefix, false, true);
}

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
    const bool isX64Exe = (machine == 0x8664u) && ((chars & 0x0002u) != 0) && ((chars & 0x2000u) == 0);
    const bool validOpt = (magic == 0x020Bu) && optSize >= 0x60u
                          && static_cast<std::uint64_t>(peOffset) + 24 + optSize <= sz;
    return isX64Exe && sections >= 1 && validOpt;
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
    if (!IsValidHttpsMirrorPrefix(spec.mirrorPrefix)) {
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::InvalidArgument,
                                    "aria2-next mirror prefix must be a valid https:// URL"));
    }
    if (!IsAllowedUpdateUrl(url, true)) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::InvalidArgument,
                                                       "aria2-next update URL must use HTTPS"));
    }
    const bool allowLoopbackHttp = IsLoopbackUrl(url);

    if (IsDirectExeUrl(url)) {
        Aria2NextReleaseInfo direct{};
        direct.assetName = FilenameFromUrl(url);
        direct.downloadUrl = ApplyGitHubMirrorPrefix(url, spec.mirrorPrefix);
        direct.expectedSha256 = NormalizeSha256Hex(spec.expectedSha256);
        return DownloadAndVerifyBinary(spec, direct, onPhase);
    }

    if (onPhase) {
        onPhase(Aria2NextUpdatePhase::Checking);
    }
    detail::HttpRequestSpec jsonReq{
        .url = std::string(url),
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

    const bool requireGitHubAssetHost = IsGitHubHostUrl(url);
    auto relRes =
        ParseReleaseJsonChecked(*jsonRes, spec.mirrorPrefix, requireGitHubAssetHost, allowLoopbackHttp);
    if (!relRes) {
        return std::unexpected(relRes.error());
    }
    if (relRes->expectedSha256.empty() && !spec.expectedSha256.empty()) {
        relRes->expectedSha256 = NormalizeSha256Hex(spec.expectedSha256);
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

    const std::filesystem::path backupPath = MakeUniqueSiblingPath(targetExePath, "bak");
    CleanupFileQuietly(backupPath);
    const bool hadExisting = std::filesystem::exists(targetExePath, ec);
    if (ec) {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to inspect target aria2-next.exe",
                                                       ec.message()));
    }
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

    const std::error_code installEc = MoveOrCopyToTarget(stagedExePath, targetExePath);
    if (installEc) {
        std::error_code restoreEc;
        if (hadExisting) {
            std::filesystem::rename(backupPath, targetExePath, restoreEc);
        }
        if (!hadExisting || !restoreEc) {
            CleanupFileQuietly(stagedExePath);
        }
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::IoFailure,
                                                       "Failed to install staged aria2-next.exe",
                                                       installEc.message()));
    }

    CleanupFileQuietly(backupPath);
    return {};
}

}  // namespace motrix::engine
