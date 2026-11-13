export module motrix.engine:upnp;

import std;
import motrix.core;

namespace motrix::engine {

export struct UpnpMappedPorts {
    int btPort = 0;
    int dhtPort = 0;
    bool mapped = false;
    std::string externalIp;
    std::string gatewayDesc;
};

export class IUpnpPortMapper {
public:
    virtual ~IUpnpPortMapper() = default;

    [[nodiscard]] virtual motrix::core::Result<UpnpMappedPorts> EnsureMapped(int btPort,
                                                                             int dhtPort) = 0;
    virtual void UnmapAll() = 0;
    [[nodiscard]] virtual UpnpMappedPorts CurrentState() const = 0;
};

export [[nodiscard]] std::unique_ptr<IUpnpPortMapper> CreateUpnpPortMapper(motrix::core::Logger& logger);

}  // namespace motrix::engine
