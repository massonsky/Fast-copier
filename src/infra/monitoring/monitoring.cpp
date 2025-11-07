#include "monitoring.hpp"
#include <fmt/core.h>
#include <spdlog/spdlog.h>
#include <iostream>
#include <cmath>
#include <thread>
#include <iomanip>

namespace cclone::infra {

ProgressMonitor::ProgressMonitor(bool enabled, bool quiet)
    : enabled_(enabled && !quiet)
    , quiet_(quiet)
    , start_time_(std::chrono::steady_clock::now())
{
    if (enabled_) {
        start_rendering_thread_();
    }
}

ProgressMonitor::~ProgressMonitor() {
    if (render_thread_) {
        stop_rendering_thread_();
    }
    if (enabled_ && !quiet_) {
        render_();
        std::cout << "\n"; // финальный перенос
    }
}

void ProgressMonitor::set_total(std::uint64_t files, std::uint64_t bytes) {
    total_files_ = files;
    total_bytes_ = bytes;
}

void ProgressMonitor::update(std::uint64_t files, std::uint64_t bytes) {
    processed_files_ += files;
    processed_bytes_ += bytes;
}

auto ProgressMonitor::get_stats() const -> Stats {
    return Stats{
        .total_files = total_files_,
        .processed_files = processed_files_.load(),
        .total_bytes = total_bytes_,
        .processed_bytes = processed_bytes_.load(),
        .start_time = start_time_
    };
}

void ProgressMonitor::start_rendering_thread_() {
    render_thread_ = std::make_unique<std::jthread>([this](std::stop_token st) {
        while (!st.stop_requested() && !shutdown_.load()) {
            render_();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        render_();
    });
}

void ProgressMonitor::stop_rendering_thread_() {
    shutdown_.store(true);
    if (render_thread_) {
        render_thread_->request_stop();
        // jthread автоматически join() в деструкторе, не вызываем вручную
    }
}

void ProgressMonitor::render_() const {
    if (quiet_ || !enabled_) return;

    auto stats = get_stats();
    if (stats.total_files == 0) return;

    // Вычисляем проценты
    const double file_progress = static_cast<double>(stats.processed_files) / stats.total_files;
    const int bar_width = 20;
    const int filled = static_cast<int>(file_progress * bar_width);

    // Скорость (байт/сек)
    auto now = std::chrono::steady_clock::now();
    auto elapsed_sec = std::chrono::duration<double>(now - stats.start_time).count();
    double bytes_per_sec = elapsed_sec > 0 ? stats.processed_bytes / elapsed_sec : 0.0;

    // ETA
    double eta_sec = 0.0;
    if (bytes_per_sec > 0 && stats.processed_bytes > 0) {
        double remaining_bytes = stats.total_bytes - stats.processed_bytes;
        eta_sec = remaining_bytes / bytes_per_sec;
    }

    // Форматирование скорости
    const char* unit = "B/s";
    double speed = bytes_per_sec;
    if (speed > 1024*1024*1024) { speed /= 1024*1024*1024; unit = "GB/s"; }
    else if (speed > 1024*1024) { speed /= 1024*1024; unit = "MB/s"; }
    else if (speed > 1024) { speed /= 1024; unit = "KB/s"; }

    // Форматирование ETA
    std::string eta_str = "inf";
    if (std::isfinite(eta_sec) && eta_sec > 0) {
        int seconds = static_cast<int>(eta_sec);
        int hours = seconds / 3600;
        int minutes = (seconds % 3600) / 60;
        seconds = seconds % 60;
        if (hours > 0) {
            eta_str = fmt::format("{:02d}:{:02d}:{:02d}", hours, minutes, seconds);
        } else {
            eta_str = fmt::format("{:02d}:{:02d}", minutes, seconds);
        }
    }

    // Очистка строки и вывод
    std::cout << "\r\033[K"; // ANSI: очистить строку

    std::string bar = std::string(filled, '█') + std::string(bar_width - filled, '░');
    fmt::print(
        "[{}] {:.1f} {}/s | ETA: {} | {}/{} files",
        bar,
        speed, unit,
        eta_str,
        stats.processed_files,
        stats.total_files
    );
    std::cout << std::flush;
}

} // namespace cclone::infra