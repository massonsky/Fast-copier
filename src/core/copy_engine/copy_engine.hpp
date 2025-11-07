#pragma once

#include <filesystem>
#include <string>
#include <vector>
#include <expected>
#include <atomic>
#include "../../infra/config/config.hpp"
#include "../../infra/error_handler/error.hpp"
#include "../../infra/monitoring/monitoring.hpp"
#include "../../infra/thread_pool/thread_pool.hpp"

namespace cclone::core {

struct CopyFileResult {
    bool copied = true; // true если файл скопирован, false если пропущен
};

struct CopyStatsSnapshot {
    std::uint64_t files_copied = 0;
    std::uint64_t bytes_copied = 0;
    std::uint64_t files_skipped = 0;
    std::uint64_t errors = 0;
};

struct CopyStats {
    std::atomic<std::uint64_t> files_copied{0};
    std::atomic<std::uint64_t> bytes_copied{0};
    std::atomic<std::uint64_t> files_skipped{0};
    std::atomic<std::uint64_t> errors{0};
    
    // Конструктор по умолчанию
    CopyStats() = default;
    
    // Запрещаем копирование и перемещение (из-за atomic)
    CopyStats(const CopyStats&) = delete;
    CopyStats& operator=(const CopyStats&) = delete;
    CopyStats(CopyStats&&) = delete;
    CopyStats& operator=(CopyStats&&) = delete;
};

class CopyEngine {
public:
    explicit CopyEngine(const infra::Config& config,
                        infra::ProgressMonitor& monitor);

    [[nodiscard]] auto run(const std::vector<std::filesystem::path>& sources,
                           const std::filesystem::path& destination)
        -> std::expected<CopyStatsSnapshot, infra::Error>;

private:
    const infra::Config& config_;
    infra::ProgressMonitor& monitor_;

    // Внутренние методы
    void copy_directory(const std::filesystem::path& src_dir,
                        const std::filesystem::path& dst_dir);
    std::expected<CopyFileResult, infra::Error> copy_file(const std::filesystem::path& src,
                                                const std::filesystem::path& dst);
    std::expected<void, infra::Error> copy_chunked(const std::filesystem::path& src,
                                                   const std::filesystem::path& dst);
    bool should_exclude(const std::filesystem::path& path) const;
    bool should_include(const std::filesystem::path& path) const;

    // Статистика
    CopyStats stats_{};
};

} // namespace cclone::core