#include "interrupt.hpp"
#include <spdlog/spdlog.h>

namespace cclone::infra {

std::atomic<bool> g_interrupted{false};

void signal_handler(int sig) {
    if (sig == SIGINT || sig == SIGTERM) {
        spdlog::warn("Received interrupt signal. Shutting down gracefully...");
        g_interrupted.store(true, std::memory_order_relaxed);
    }
}

void install_signal_handler() {
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
}

} // namespace cclone::infra