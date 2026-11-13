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

        int discoverErr = UPNPDISCOVER_SUCCESS;
        UniqueDevList devlist(::upnpDiscover(1500,
                                             nullptr,
                                             nullptr,
                                             UPNP_LOCAL_PORT_ANY,
                                             0,
                                             static_cast<unsigned char>(2),
                                             &discoverErr));
        if (devlist.Get() == nullptr) {
            logger_.Debug("upnp", "UPnP discovery found no IGD (err={})", discoverErr);
            return std::unexpected(
                motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                        motrix::core::ErrorCode::NotFound,
                                        std::format("No UPnP IGD discovered (code {})", discoverErr)));
        }

        UniqueUpnpUrls urls;
        IGDdatas data{};
        char lanAddr[64]{};
        char wanAddr[64]{};
        const int igdStatus = ::UPNP_GetValidIGD(devlist.Get(),
                                                 &urls.urls,
                                                 &data,
                                                 lanAddr,
                                                 static_cast<int>(sizeof(lanAddr)),
                                                 wanAddr,
                                                 static_cast<int>(sizeof(wanAddr)));
        if (igdStatus != 0) {
            urls.active = true;
        }
        if (igdStatus != UPNP_CONNECTED_IGD && igdStatus != UPNP_PRIVATEIP_IGD) {
            logger_.Debug("upnp", "No connected UPnP IGD found (status={})", igdStatus);
            return std::unexpected(motrix::core::MakeError(
                motrix::core::ErrorDomain::Engine,
                motrix::core::ErrorCode::NotFound,
                std::format("No connected UPnP IGD available (status {})", igdStatus)));
        }
        if (urls.urls.controlURL == nullptr || urls.urls.controlURL[0] == '\0') {
            return std::unexpected(motrix::core::MakeError(motrix::core::ErrorDomain::Engine,
                                                           motrix::core::ErrorCode::NotFound,
                                                           "UPnP IGD control URL is empty"));
        }

        std::vector<ActivePortMapping> requested;
        requested.push_back({std::to_string(btPort), "TCP"});
        requested.push_back({std::to_string(btPort), "UDP"});
        if (dhtPort > 0 && dhtPort != btPort) {
            requested.push_back({std::to_string(dhtPort), "UDP"});
        }

        std::vector<ActivePortMapping> added;
        for (const auto& item : requested) {
            const int rc = ::UPNP_AddPortMapping(urls.urls.controlURL,
                                                 data.first.servicetype,
                                                 item.port.c_str(),
                                                 item.port.c_str(),
                                                 lanAddr,
                                                 "Motrix Native",
                                                 item.proto.c_str(),
                                                 nullptr,
                                                 "0");
            if (rc != UPNPCOMMAND_SUCCESS) {
                const char* errText = ::strupnperror(rc);
                const std::string detail = errText ? errText : "unknown";
                logger_.Warn("upnp",
                             "UPNP_AddPortMapping({}/{}) failed (rc={}): {}",
                             item.port,
                             item.proto,
                             rc,
                             detail);
                for (const auto& prev : added) {
                    (void)::UPNP_DeletePortMapping(urls.urls.controlURL,
                                                   data.first.servicetype,
                                                   prev.port.c_str(),
                                                   prev.proto.c_str(),
                                                   nullptr);
                }
                return std::unexpected(motrix::core::MakeError(
                    motrix::core::ErrorDomain::Engine,
                    motrix::core::ErrorCode::IoFailure,
                    std::format(
                        "UPnP AddPortMapping failed for {}/{}: {}", item.port, item.proto, detail)));
            }
            added.push_back(item);
        }

        controlUrl_ = urls.urls.controlURL;
        serviceType_ = data.first.servicetype;
        activeMappings_ = std::move(added);
        state_ = UpnpMappedPorts{
            .btPort = btPort,
            .dhtPort = dhtPort,
            .mapped = true,
            .externalIp = wanAddr,
            .gatewayDesc = urls.urls.rootdescURL ? urls.urls.rootdescURL : data.first.servicetype,
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
