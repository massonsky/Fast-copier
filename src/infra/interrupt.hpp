#pragma once

#include <atomic>
#include <csignal>

namespace cclone::infra {

extern std::atomic<bool> g_interrupted;

void install_signal_handler();

inline bool is_interrupted() {
    return g_interrupted.load(std::memory_order_relaxed);
}

} // namespace cclone::infra