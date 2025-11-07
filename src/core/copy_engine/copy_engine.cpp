#include "copy_engine.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <ranges>
#include "../../infra/error_handler/error.hpp"
#include "../../infra/monitoring/monitoring.hpp"
#include "../../infra/retry.hpp"
#include "../../infra/thread_pool/thread_pool.hpp"
#include "../../infra/interrupt.hpp"
#include "../../infra/hash/xxhash_verifier.hpp"
#include <regex>

namespace cclone::core {

namespace {

std::uintmax_t total_size(const std::vector<std::filesystem::path>& files) {
    std::uintmax_t size = 0;
    for (const auto& file : files) {
        std::error_code ec;
        auto file_size = std::filesystem::file_size(file, ec);
        if (!ec) {
            size += file_size;
        } else {
            spdlog::warn("Failed to get size for {}: {}", file.string(), ec.message());
        }
    }
    return size;
}

} // namespace

CopyEngine::CopyEngine(const infra::Config& config,
                       infra::ProgressMonitor& monitor)
    : config_(config), monitor_(monitor) {}

auto CopyEngine::run(const std::vector<std::filesystem::path>& sources,
                     const std::filesystem::path& destination)
    -> std::expected<CopyStats, infra::Error>
{
    if (!std::filesystem::exists(destination)) {
        std::error_code ec;
        std::filesystem::create_directories(destination, ec);
        if (ec) {
            return std::unexpected(infra::make_error(infra::ErrorCode::PermissionDenied,
                                 fmt::format("Cannot create destination: {}", ec.message())));
        }
    }

    // Сканируем файлы
    std::vector<std::filesystem::path> all_files;
    for (const auto& src : sources) {
        if (std::filesystem::is_directory(src)) {
            if (config_.recursive) {
                for (const auto& entry : std::filesystem::recursive_directory_iterator(src)) {
                    if (should_exclude(entry.path())) continue;
                    if (!should_include(entry.path()) && !config_.include_patterns.empty()) continue;
                    if (entry.is_regular_file() || entry.is_symlink()) {
                        all_files.push_back(entry.path());
                    }
                }
            } else {
                // Только файлы в корне
                for (const auto& entry : std::filesystem::directory_iterator(src)) {
                    if (should_exclude(entry.path())) continue;
                    if (!should_include(entry.path()) && !config_.include_patterns.empty()) continue;
                    if (entry.is_regular_file() || entry.is_symlink()) {
                        all_files.push_back(entry.path());
                    }
                }
            }
        } else if (std::filesystem::is_regular_file(src)) {
            all_files.push_back(src);
        } else {
            spdlog::warn("Skipping non-file: {}", src.string());
        }
    }

    monitor_.set_total(all_files.size(), total_size(all_files));

    // Создаём пул потоков
    infra::ThreadPool pool{config_.threads.value_or(std::jthread::hardware_concurrency())};

    for (const auto& file : all_files) {
        pool.enqueue([&, file]() {
            if (infra::is_interrupted()) {
                return;
            }

            auto dst = destination / std::filesystem::relative(file, sources[0]);
            if (file == sources[0]) { // если source — файл, а не директория
                dst = destination / file.filename();
            }

            auto res = copy_file(file, dst);
            if (res) {
                stats_.files_copied++;
                stats_.bytes_copied += std::filesystem::file_size(file);
                monitor_.update(1, std::filesystem::file_size(file));
            } else {
                stats_.errors++;
                (void)infra::log_and_return(std::move(res.error()));
            }
        });
    }

    pool.wait();

    if (infra::is_interrupted()) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, "User interrupted"));
    }

    return stats_;
}

void CopyEngine::copy_directory(const std::filesystem::path& src_dir,
                                const std::filesystem::path& dst_dir)
{
    std::error_code ec;
    std::filesystem::create_directories(dst_dir, ec);
    if (ec) {
        spdlog::error("Failed to create dir {}: {}", dst_dir.string(), ec.message());
        return;
    }

    for (const auto& entry : std::filesystem::directory_iterator(src_dir)) {
        if (entry.is_regular_file()) {
            auto dst = dst_dir / entry.path().filename();
            auto res = copy_file(entry.path(), dst);
            if (!res) {
                (void)infra::log_and_return(std::move(res.error()));
            }
        } else if (entry.is_directory()) {
            copy_directory(entry.path(), dst_dir / entry.path().filename());
        }
    }
}

auto CopyEngine::copy_file(const std::filesystem::path& src,
                           const std::filesystem::path& dst)
    -> std::expected<void, infra::Error>
{
    if (std::filesystem::exists(dst)) {
        if (config_.resume) {
            // Проверяем, можно ли возобновить (по размеру или checksum)
            // Для простоты — пропускаем, если файл полный
            if (std::filesystem::file_size(src) == std::filesystem::file_size(dst)) {
                return {};
            }
        } else {
            std::error_code ec;
            std::filesystem::remove(dst, ec);
            if (ec) {
                return std::unexpected(infra::make_error(infra::ErrorCode::PermissionDenied,
                                     fmt::format("Cannot remove existing file: {}", ec.message())));
            }
        }
    }

    if (std::filesystem::file_size(src) > 100'000'000) { // >100MB
        return copy_chunked(src, dst);
    }

    // Буферизованное копирование для маленьких файлов
    std::error_code ec;
    std::filesystem::copy_file(src, dst, std::filesystem::copy_options::overwrite_existing, ec);
    if (ec) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown,
                             fmt::format("Copy failed: {}", ec.message())));
    }

    if (config_.verify) {
        auto verify_result = infra::XXHashVerifier::verify_files(src, dst);
        if (!verify_result) {
            return std::unexpected(std::move(verify_result.error()));
        }
        if (!*verify_result) {
            return std::unexpected(infra::make_error(infra::ErrorCode::Unknown,
                                 fmt::format("Verification failed for {}", src.string())));
        }
    }

    return {};
}

auto CopyEngine::copy_chunked(const std::filesystem::path& src,
                              const std::filesystem::path& dst)
    -> std::expected<void, infra::Error>
{
    // TODO: реализовать многопоточное chunked copying
    // Для начала — однопоточное с большим буфером
    const size_t buffer_size = config_.buffer_size.value_or(4 * 1024 * 1024); // 4MB
    std::vector<char> buffer(buffer_size);

    std::ifstream ifs(src, std::ios::binary);
    std::ofstream ofs(dst, std::ios::binary);

    if (!ifs || !ofs) {
        return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, "Cannot open file"));
    }

    while (ifs.read(buffer.data(), buffer.size())) {
        ofs.write(buffer.data(), ifs.gcount());
        if (infra::is_interrupted()) {
            return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, "Interrupted during chunked copy"));
        }
    }

    if (ifs.eof()) {
        ofs.write(buffer.data(), ifs.gcount());
    } else {
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "Read error"));
    }

    if (config_.verify) {
        auto verify_result = infra::XXHashVerifier::verify_files(src, dst);
        if (!verify_result) {
            return std::unexpected(std::move(verify_result.error()));
        }
        if (!*verify_result) {
            return std::unexpected(infra::make_error(infra::ErrorCode::Unknown,
                                 fmt::format("Chunked copy verification failed for {}", src.string())));
        }
    }

    return {};
}

bool CopyEngine::should_exclude(const std::filesystem::path& path) const {
    for (const auto& pattern : config_.exclude_patterns) {
        if (std::regex_match(path.filename().string(), std::regex(pattern))) {
            return true;
        }
    }
    return false;
}

bool CopyEngine::should_include(const std::filesystem::path& path) const {
    if (config_.include_patterns.empty()) return true;
    for (const auto& pattern : config_.include_patterns) {
        if (std::regex_match(path.filename().string(), std::regex(pattern))) {
            return true;
        }
    }
    return false;
}

} // namespace cclone::core