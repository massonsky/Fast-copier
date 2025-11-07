#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <memory>
#include <thread>

namespace cclone::infra {

class ProgressMonitor {
public:
    struct Stats {
        std::uint64_t total_files = 0;
        std::uint64_t processed_files = 0;
        std::uint64_t total_bytes = 0;
        std::uint64_t processed_bytes = 0;
        std::chrono::steady_clock::time_point start_time{};
    };

    explicit ProgressMonitor(bool enabled = true, bool quiet = false);
    ~ProgressMonitor();

    void set_total(std::uint64_t files, std::uint64_t bytes);
    void update(std::uint64_t files = 0, std::uint64_t bytes = 0);

    [[nodiscard]] auto get_stats() const -> Stats;
    [[nodiscard]] auto is_enabled() const -> bool { return enabled_; }

private:
    void render_() const;
    void start_rendering_thread_();
    void stop_rendering_thread_();

    // Атомики для thread-safe обновления
    std::atomic<std::uint64_t> processed_files_{0};
    std::atomic<std::uint64_t> processed_bytes_{0};
    std::uint64_t total_files_ = 0;
    std::uint64_t total_bytes_ = 0;

    const bool enabled_;
    const bool quiet_;
    std::chrono::steady_clock::time_point start_time_;
    mutable std::atomic<bool> shutdown_{false};
    mutable std::unique_ptr<std::jthread> render_thread_;
};

} // namespace cclone::infra