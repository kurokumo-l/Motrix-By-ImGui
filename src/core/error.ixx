export module motrix.core:error;

import std;

namespace motrix::core {

export enum class ErrorDomain {
    Core,
    Domain,
    Engine,
    Platform,
    Ipc,
    App,
};

export enum class ErrorCode {
    Ok = 0,
    InvalidArgument,
    NotFound,
    IoFailure,
    ParseFailure,
    Timeout,
    ConnectionFailed,
    RpcError,
    EngineExited,
    Unsupported,
    Cancelled,
};

export struct AppError {
    ErrorDomain domain = ErrorDomain::Core;
    ErrorCode code = ErrorCode::Ok;
    std::string message;
    std::string detail;
};

export template <typename T> using Result = std::expected<T, AppError>;

export [[nodiscard]] inline AppError
MakeError(ErrorDomain domain, ErrorCode code, std::string message, std::string detail = {}) {
    return AppError{
        .domain = domain,
        .code = code,
        .message = std::move(message),
        .detail = std::move(detail),
    };
}

export [[nodiscard]] inline std::string_view ErrorDomainName(ErrorDomain domain) noexcept {
    switch (domain) {
        case ErrorDomain::Core: return "Core";
        case ErrorDomain::Domain: return "Domain";
        case ErrorDomain::Engine: return "Engine";
        case ErrorDomain::Platform: return "Platform";
        case ErrorDomain::Ipc: return "Ipc";
        case ErrorDomain::App: return "App";
    }
    return "Unknown";
}

}  // namespace motrix::core
