#include "error.hpp"
#include <fmt/core.h>

namespace cclone::infra {

bool Error::is_fatal() const {
    switch (code) {
        case ErrorCode::FileNotFound:
        case ErrorCode::PermissionDenied:
        case ErrorCode::InvalidPath:
        case ErrorCode::UnsupportedFeature:
            return true;
        default:
            return false;
    }
}

int Error::to_exit_code() const {
    if (is_fatal()) return EXIT_FAILURE;
    switch (code) {
        case ErrorCode::DiskFull:       return 20;
        case ErrorCode::FileLocked:     return 21;
        case ErrorCode::ChecksumMismatch: return 22;
        case ErrorCode::Interrupted:    return 130; // SIGINT
        default:                        return EXIT_FAILURE;
    }
}

const char* Error::what() const {
    return message.c_str();
}

Error make_error(ErrorCode code, std::string_view message,
                 const std::source_location& loc) {
    return Error{code, std::string(message), loc};
}

Error log_and_return(Error&& err) {
    auto level = err.is_fatal() ? spdlog::level::err : spdlog::level::warn;
    spdlog::log(level,
        "[{}:{} in {}] {}: {}",
        err.file, err.line, err.function,
        static_cast<int>(err.code), err.message
    );
    return std::move(err);
}

} // namespace cclone::infra