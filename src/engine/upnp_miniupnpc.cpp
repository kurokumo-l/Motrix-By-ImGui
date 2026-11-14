module;

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include "miniupnpc.h"
#include "upnpcommands.h"
#include "upnperrors.h"
// clang-format on

module motrix.engine;

import std;
import motrix.core;

namespace motrix::engine {

namespace {

struct ScopedWsaStartup {
    bool initialized = false;

    ScopedWsaStartup() noexcept {
        WSADATA wsa{};
        if (::WSAStartup(MAKEWORD(2, 2), &wsa) == 0) {
            initialized = true;
        }
    }
    ~ScopedWsaStartup() {
        if (initialized) {
            ::WSACleanup();
        }
    }

    ScopedWsaStartup(const ScopedWsaStartup&) = delete;
    ScopedWsaStartup& operator=(const ScopedWsaStartup&) = delete;
};

struct UniqueDevList {
    UPNPDev* ptr = nullptr;

    UniqueDevList() noexcept = default;
    explicit UniqueDevList(UPNPDev* p) noexcept : ptr(p) {}
    ~UniqueDevList() { Reset(); }

    UniqueDevList(const UniqueDevList&) = delete;
    UniqueDevList& operator=(const UniqueDevList&) = delete;

    void Reset(UPNPDev* p = nullptr) noexcept {
        if (ptr != nullptr) {
            ::freeUPNPDevlist(ptr);
        }
        ptr = p;
    }

    [[nodiscard]] UPNPDev* Get() const noexcept { return ptr; }
};

struct UniqueUpnpUrls {
    UPNPUrls urls{};
    bool active = false;

    UniqueUpnpUrls() noexcept = default;
    UniqueUpnpUrls(UniqueUpnpUrls&& other) noexcept : urls(other.urls), active(other.active) {
        other.urls = UPNPUrls{};
        other.active = false;
    }
    UniqueUpnpUrls& operator=(UniqueUpnpUrls&& other) noexcept {
        if (this != &other) {
            Reset();
            urls = other.urls;
            active = other.active;
            other.urls = UPNPUrls{};
            other.active = false;
        }
        return *this;
    }
    ~UniqueUpnpUrls() { Reset(); }

    UniqueUpnpUrls(const UniqueUpnpUrls&) = delete;
    UniqueUpnpUrls& operator=(const UniqueUpnpUrls&) = delete;

    void Reset() noexcept {
        if (active) {
            ::FreeUPNPUrls(&urls);
            active = false;
        }
        urls = UPNPUrls{};
    }
};

struct ActivePortMapping {
    std::string port;
    std::string proto;
};

struct DiscoveredIgd {
    UniqueUpnpUrls urls;
    IGDdatas data{};
    std::string lanAddr;
    std::string wanAddr;
};

[[nodiscard]] motrix::core::Result<DiscoveredIgd> DiscoverValidIgd(motrix::core::Logger& logger) {
    int discoverErr = UPNPDISCOVER_SUCCESS;
    UniqueDevList devlist(::upnpDiscover(
        1500, nullptr, nullptr, UPNP_LOCAL_PORT_ANY, 0, static_cast<unsigned char>(2), &discoverErr));
    if (devlist.Get() == nullptr) {
        logger.Debug("upnp", "UPnP discovery found no IGD (err={})", discoverErr);
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::NotFound,
                                    std::format("No UPnP IGD discovered (code {})", discoverErr)));
    }
    DiscoveredIgd igd{};
    char lanBuf[64]{};
    char wanBuf[64]{};
    const int status = ::UPNP_GetValidIGD(devlist.Get(),
                                          &igd.urls.urls,
                                          &igd.data,
                                          lanBuf,
                                          static_cast<int>(sizeof(lanBuf)),
                                          wanBuf,
                                          static_cast<int>(sizeof(wanBuf)));
    if (status != 0) {
        igd.urls.active = true;
    }
    if (status != UPNP_CONNECTED_IGD && status != UPNP_PRIVATEIP_IGD) {
        logger.Debug("upnp", "No connected UPnP IGD found (status={})", status);
        return std::unexpected(
            motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                    motrix::core::ErrorCode::NotFound,
                                    std::format("No connected UPnP IGD available (status {})", status)));
    }
    if (igd.urls.urls.controlURL == nullptr || igd.urls.urls.controlURL[0] == '\0') {
        return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                       motrix::core::ErrorCode::NotFound,
                                                       "UPnP IGD control URL is empty"));
    }
    igd.lanAddr = lanBuf;
    igd.wanAddr = wanBuf;
    return igd;
}

[[nodiscard]] motrix::core::Result<std::vector<ActivePortMapping>>
ApplyPortMappings(motrix::core::Logger& logger, const DiscoveredIgd& igd, int btPort, int dhtPort) {
    std::vector<ActivePortMapping> requested;
    requested.push_back({std::to_string(btPort), "TCP"});
    requested.push_back({std::to_string(btPort), "UDP"});
    if (dhtPort > 0 && dhtPort != btPort) {
        requested.push_back({std::to_string(dhtPort), "UDP"});
    }
    std::vector<ActivePortMapping> added;
    for (const auto& item : requested) {
        const int rc = ::UPNP_AddPortMapping(igd.urls.urls.controlURL,
                                             igd.data.first.servicetype,
                                             item.port.c_str(),
                                             item.port.c_str(),
                                             igd.lanAddr.c_str(),
                                             "Motrix Native",
                                             item.proto.c_str(),
                                             nullptr,
                                             "0");
        if (rc == UPNPCOMMAND_SUCCESS) {
            added.push_back(item);
            continue;
        }
        const char* errText = ::strupnperror(rc);
        const std::string detail = errText ? errText : "unknown";
        logger.Warn(
            "upnp", "UPNP_AddPortMapping({}/{}) failed (rc={}): {}", item.port, item.proto, rc, detail);
        for (const auto& prev : added) {
            (void)::UPNP_DeletePortMapping(igd.urls.urls.controlURL,
                                           igd.data.first.servicetype,
                                           prev.port.c_str(),
                                           prev.proto.c_str(),
                                           nullptr);
        }
        return std::unexpected(motrix::core::MakeError(
            motrix::core::ErrorDomain::Engine,
            motrix::core::ErrorCode::IoFailure,
            std::format("UPnP AddPortMapping failed for {}/{}: {}", item.port, item.proto, detail)));
    }
    return added;
}

class MiniUpnpPortMapper final : public IUpnpPortMapper {
public:
    explicit MiniUpnpPortMapper(motrix::core::Logger& logger) : logger_(logger) {}

    ~MiniUpnpPortMapper() override { UnmapAll(); }

    motrix::core::Result<UpnpMappedPorts> EnsureMapped(int btPort, int dhtPort) override {
        if (btPort <= 0 || btPort > 65535 || dhtPort < 0 || dhtPort > 65535) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::InvalidArgument,
                                                           "Invalid UPnP port number"));
        }

        std::lock_guard lock(mutex_);
        if (state_.mapped && state_.btPort == btPort && state_.dhtPort == dhtPort) {
            return state_;
        }
        UnmapLocked();

        if (!wsa_.initialized) {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::IoFailure,
                                                           "Winsock initialization failed for UPnP"));
        }

        auto igd = DiscoverValidIgd(logger_);
        if (!igd) {
            return std::unexpected(igd.error());
        }
        auto added = ApplyPortMappings(logger_, *igd, btPort, dhtPort);
        if (!added) {
            return std::unexpected(added.error());
        }

        controlUrl_ = igd->urls.urls.controlURL;
        serviceType_ = igd->data.first.servicetype;
        activeMappings_ = std::move(*added);
        state_ = UpnpMappedPorts{
            .btPort = btPort,
            .dhtPort = dhtPort,
            .mapped = true,
            .externalIp = std::move(igd->wanAddr),
            .gatewayDesc =
                igd->urls.urls.rootdescURL ? igd->urls.urls.rootdescURL : igd->data.first.servicetype,
        };
        logger_.Info("upnp", "UPnP mapped BT={} DHT={} extIp={}", btPort, dhtPort, state_.externalIp);
        return state_;
    }

    void UnmapAll() override {
        std::lock_guard lock(mutex_);
        UnmapLocked();
    }

    UpnpMappedPorts CurrentState() const override {
        std::lock_guard lock(mutex_);
        return state_;
    }

private:
    void UnmapLocked() {
        if (!controlUrl_.empty() && !serviceType_.empty()) {
            for (const auto& item : activeMappings_) {
                const int rc = ::UPNP_DeletePortMapping(controlUrl_.c_str(),
                                                        serviceType_.c_str(),
                                                        item.port.c_str(),
                                                        item.proto.c_str(),
                                                        nullptr);
                if (rc != UPNPCOMMAND_SUCCESS) {
                    const char* errText = ::strupnperror(rc);
                    logger_.Debug("upnp",
                                  "UPNP_DeletePortMapping({}/{}) returned {}: {}",
                                  item.port,
                                  item.proto,
                                  rc,
                                  errText ? errText : "unknown");
                }
            }
        }
        activeMappings_.clear();
        controlUrl_.clear();
        serviceType_.clear();
        state_ = UpnpMappedPorts{};
    }

    motrix::core::Logger& logger_;
    ScopedWsaStartup wsa_;
    mutable std::mutex mutex_;
    std::string controlUrl_;
    std::string serviceType_;
    std::vector<ActivePortMapping> activeMappings_;
    UpnpMappedPorts state_{};
};

}  // namespace

std::unique_ptr<IUpnpPortMapper> CreateUpnpPortMapper(motrix::core::Logger& logger) {
    return std::make_unique<MiniUpnpPortMapper>(logger);
}

}  // namespace motrix::engine
