#pragma once

#include "error_handler/error.hpp"
#include <chrono>
#include <thread>
#include <functional>
#include <optional>

namespace cclone::infra {
/*

auto res = infra::with_retry([&]() {
    return copy_file_chunk(src, dst, offset, size);
}, infra::RetryPolicy{ .max_attempts = 5 });


*/
struct RetryPolicy {
    int max_attempts = 3;
    std::chrono::milliseconds initial_delay = std::chrono::milliseconds(100);
    double backoff_factor = 2.0; // exponential backoff
};

template<typename F>
[[nodiscard]] auto with_retry(F&& operation, const RetryPolicy& policy = {})
    -> decltype(operation())
{
    using ResultType = decltype(operation());

    for (int attempt = 0; attempt < policy.max_attempts; ++attempt) {
        auto result = operation();
        if (result.has_value()) {
            return result; // успех
        }

        const auto& err = result.error();
        if (!err.is_transient() || attempt == policy.max_attempts - 1) {
            return result; // фатальная ошибка или последняя попытка
        }

        // Экспоненциальная задержка
        auto delay = policy.initial_delay * static_cast<long>(std::pow(policy.backoff_factor, attempt));
        std::this_thread::sleep_for(delay);
    }

    // Управление сюда не дойдёт, но компилятор требует
    return operation();
}

} // namespace cclone::infra