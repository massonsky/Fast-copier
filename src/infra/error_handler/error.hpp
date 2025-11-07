#pragma once

#include <string>
#include <string_view>
#include <system_error>
#include <source_location>
#include <expected>
#include <spdlog/spdlog.h>

namespace cclone::infra {

enum class ErrorCode {
    // Фатальные ошибки (программа завершается)
    FileNotFound,
    PermissionDenied,
    InvalidPath,
    UnsupportedFeature,

    // Восстанавливаемые (можно retry или пропустить)
    DiskFull,
    FileLocked,
    ChecksumMismatch,
    Interrupted,

    // Системные
    Unknown,
    NetworkTimeout,  // ← transient (если будет поддержка сетевых копий)
};

struct Error {
    ErrorCode code;
    std::string message;
    std::string file;
    int line;
    std::string function;

    // Конструктор с автоматическим захватом location
    Error(ErrorCode c, std::string_view msg,
          const std::source_location& loc = std::source_location::current())
        : code(c)
        , message(msg)
        , file(loc.file_name())
        , line(static_cast<int>(loc.line()))
        , function(loc.function_name())
    {}

    [[nodiscard]] auto is_fatal() const -> bool;
    [[nodiscard]] auto to_exit_code() const -> int;
    [[nodiscard]] auto what() const -> const char*;

    [[nodiscard]] auto is_transient() const -> bool {
    return code == ErrorCode::FileLocked ||
           code == ErrorCode::NetworkTimeout;
    }
};

// Псевдонимы для удобства
template<typename T>
using Result = std::expected<T, Error>;

using VoidResult = Result<void>;

// Вспомогательные функции-конструкторы
[[nodiscard]] auto make_error(
    ErrorCode code,
    std::string_view message,
    const std::source_location& loc = std::source_location::current()
) -> Error;

// Логирование ошибки и возврат
[[nodiscard]] auto log_and_return(Error&& err) -> Error;

// Макрос для удобства (необязательно, но практичен)
const auto CC_ERR = ::cclone::infra::make_error;

} // namespace cclone::infra