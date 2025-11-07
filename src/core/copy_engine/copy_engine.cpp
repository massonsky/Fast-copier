#include "copy_engine.hpp"
#include <filesystem>
#include <fstream>
#include <algorithm>
#include <ranges>
#include <atomic>
#include <mutex>
#include <optional>
#include <future>
#include "../../infra/error_handler/error.hpp"
#include "../../infra/monitoring/monitoring.hpp"
#include "../../infra/retry.hpp"
#include "../../infra/thread_pool/thread_pool.hpp"
#include "../../infra/interrupt.hpp"
#include "../../infra/hash/xxhash_verifier.hpp"
#include "../../adapters/fs.hpp"
#include "../../extensions/metadata.hpp"
#include "../../extensions/resumer.hpp"
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
    -> std::expected<CopyStatsSnapshot, infra::Error>
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

            auto file_size = std::filesystem::file_size(file);
            auto res = copy_file(file, dst);
            
            if (res) {
                if (res->copied) {
                    stats_.files_copied.fetch_add(1, std::memory_order_relaxed);
                    stats_.bytes_copied.fetch_add(file_size, std::memory_order_relaxed);
                } else {
                    stats_.files_skipped.fetch_add(1, std::memory_order_relaxed);
                }
                monitor_.update(1, file_size);
            } else {
                stats_.errors.fetch_add(1, std::memory_order_relaxed);
                (void)infra::log_and_return(std::move(res.error()));
                monitor_.update(1, 0); // учитываем файл как обработанный
            }
        });
    }

    pool.wait();

    if (infra::is_interrupted()) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, "User interrupted"));
    }

    // Возвращаем снимок статистики
    return CopyStatsSnapshot{
        .files_copied = stats_.files_copied.load(),
        .bytes_copied = stats_.bytes_copied.load(),
        .files_skipped = stats_.files_skipped.load(),
        .errors = stats_.errors.load()
    };
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
            if (res) {
                if (res->copied) {
                    stats_.files_copied.fetch_add(1, std::memory_order_relaxed);
                    stats_.bytes_copied.fetch_add(std::filesystem::file_size(entry.path()), std::memory_order_relaxed);
                } else {
                    stats_.files_skipped.fetch_add(1, std::memory_order_relaxed);
                }
            } else {
                stats_.errors.fetch_add(1, std::memory_order_relaxed);
                (void)infra::log_and_return(std::move(res.error()));
            }
        } else if (entry.is_directory()) {
            copy_directory(entry.path(), dst_dir / entry.path().filename());
        }
    }
}

auto CopyEngine::copy_file(const std::filesystem::path& src,
                           const std::filesystem::path& dst)
    -> std::expected<CopyFileResult, infra::Error>
{
    if (std::filesystem::exists(dst)) {
        if (config_.resume) {
            // Проверяем, можно ли возобновить (по размеру или checksum)
            // Для простоты — пропускаем, если файл полный
            if (std::filesystem::file_size(src) == std::filesystem::file_size(dst)) {
                return CopyFileResult{.copied = false}; // файл пропущен
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
        // Для больших файлов используем асинхронное копирование с DirectIO
        const auto strategy = adapters::fs::CopyStrategy::DirectIO;
        auto future = adapters::fs::copy_file_async(src, dst, strategy);
        auto res = future.get();
        if (!res) {
            return std::unexpected(std::move(res.error()));
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
        
        // Копируем метаданные после успешной верификации
        if (config_.preserve_metadata) {
            auto metadata_res = extensions::copy_metadata(src, dst);
            if (!metadata_res) {
                spdlog::warn("Failed to copy metadata for {}: {}", 
                            src.string(), metadata_res.error().message);
            }
        }
        
        return CopyFileResult{.copied = true};
    }

    // Буферизованное копирование для маленьких файлов
    const auto file_size = std::filesystem::file_size(src);
    const auto strategy = adapters::fs::select_strategy(file_size);

    auto res = adapters::fs::copy_file(src, dst, strategy);
    if (!res) {
        return std::unexpected(std::move(res.error()));
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
    
    // Копируем метаданные после успешной верификации
    if (config_.preserve_metadata) {
        auto metadata_res = extensions::copy_metadata(src, dst);
        if (!metadata_res) {
            spdlog::warn("Failed to copy metadata for {}: {}", 
                        src.string(), metadata_res.error().message);
        }
    }

    return CopyFileResult{.copied = true};
}

auto CopyEngine::copy_chunked(const std::filesystem::path& src,
                              const std::filesystem::path& dst)
    -> std::expected<void, infra::Error>
{
    const size_t chunk_size = config_.buffer_size.value_or(4 * 1024 * 1024); // 4MB per chunk
    const auto file_size = std::filesystem::file_size(src);
    const size_t num_chunks = (file_size + chunk_size - 1) / chunk_size;
    
    // Для маленьких файлов или если threads == 1, используем однопоточное копирование
    const size_t num_threads = config_.threads.value_or(1);
    if (num_threads == 1 || num_chunks <= 1) {
        // Используем асинхронное копирование даже для однопоточного режима
        const auto strategy = adapters::fs::CopyStrategy::DirectIO;
        auto future = adapters::fs::copy_file_async(src, dst, strategy);
        auto res = future.get();
        if (!res) return res;
        
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
        
        // Копируем метаданные после успешной верификации
        if (config_.preserve_metadata) {
            auto metadata_res = extensions::copy_metadata(src, dst);
            if (!metadata_res) {
                spdlog::warn("Failed to copy metadata for {}: {}", 
                            src.string(), metadata_res.error().message);
            }
        }
        
        return {};
    }

    // Создаём выходной файл нужного размера
    {
        std::ofstream ofs(dst, std::ios::binary);
        if (!ofs) {
            return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, 
                                 "Cannot create destination file"));
        }
        // Устанавливаем размер файла
        ofs.seekp(file_size - 1);
        ofs.write("", 1);
    }

    // Многопоточное копирование чанков
    std::vector<std::future<std::expected<void, infra::Error>>> futures;
    std::vector<int> completed_chunks;
    std::mutex completed_mutex;
    futures.reserve(num_chunks);

    for (size_t i = 0; i < num_chunks; ++i) {
        futures.push_back(std::async(std::launch::async, [&, i, chunk_size, file_size]() 
            -> std::expected<void, infra::Error> {
            
            if (infra::is_interrupted()) {
                return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, "Interrupted"));
            }

            const size_t offset = i * chunk_size;
            const size_t current_chunk_size = std::min(chunk_size, file_size - offset);
            
            std::vector<char> buffer(current_chunk_size);

            // Читаем чанк из исходного файла
            {
                std::ifstream ifs(src, std::ios::binary);
                if (!ifs) {
                    return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, 
                                             "Cannot open source file"));
                }
                
                ifs.seekg(offset);
                if (!ifs.read(buffer.data(), current_chunk_size)) {
                    return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, 
                                             fmt::format("Read error at offset {}", offset)));
                }
            }

            // Записываем чанк в выходной файл
            {
                std::fstream ofs(dst, std::ios::binary | std::ios::in | std::ios::out);
                if (!ofs) {
                    return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, 
                                             "Cannot open destination file"));
                }
                
                ofs.seekp(offset);
                if (!ofs.write(buffer.data(), current_chunk_size)) {
                    return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, 
                                             fmt::format("Write error at offset {}", offset)));
                }
            }
            
            // Отмечаем чанк как завершённый
            {
                std::lock_guard lock(completed_mutex);
                completed_chunks.push_back(static_cast<int>(i));
            }
            
            return {};
        }));
    }

    // Ждём завершения всех futures и проверяем ошибки
    std::uint64_t copied_bytes = 0;
    for (size_t i = 0; i < futures.size(); ++i) {
        auto result = futures[i].get();
        if (!result) {
            // При ошибке сохраняем resume info
            if (config_.resume) {
                extensions::ResumeInfo resume_info{
                    .source = src,
                    .destination = dst,
                    .copied_bytes = copied_bytes,
                    .total_bytes = file_size,
                    .completed_chunks = completed_chunks
                };
                extensions::save_resume_info(resume_info);
                spdlog::info("Saved resume info to .cclone.resume");
            }
            std::filesystem::remove(dst);
            return result;
        }
        copied_bytes += std::min(chunk_size, file_size - i * chunk_size);
    }

    if (infra::is_interrupted()) {
        // Сохраняем resume info при прерывании
        if (config_.resume) {
            extensions::ResumeInfo resume_info{
                .source = src,
                .destination = dst,
                .copied_bytes = copied_bytes,
                .total_bytes = file_size,
                .completed_chunks = completed_chunks
            };
            extensions::save_resume_info(resume_info);
            spdlog::info("Interrupted: Saved resume info to .cclone.resume");
        }
        std::filesystem::remove(dst);
        return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, 
                             "Interrupted during chunked copy"));
    }

    if (config_.verify) {
        auto verify_result = infra::XXHashVerifier::verify_files(src, dst);
        if (!verify_result) {
            return std::unexpected(std::move(verify_result.error()));
        }
        if (!*verify_result) {
            return std::unexpected(infra::make_error(infra::ErrorCode::Unknown,
                                 fmt::format("Chunked verification failed for {}", src.string())));
        }
    }
    
    // Копируем метаданные после успешной верификации
    if (config_.preserve_metadata) {
        auto metadata_res = extensions::copy_metadata(src, dst);
        if (!metadata_res) {
            spdlog::warn("Failed to copy metadata for {}: {}", 
                        src.string(), metadata_res.error().message);
        }
    }

    return {};
}

bool CopyEngine::should_exclude(const std::filesystem::path& path) const {
    for (const auto& pattern : config_.exclude_patterns) {
        try {
            if (std::regex_match(path.filename().string(), std::regex(pattern))) {
                return true;
            }
        } catch (const std::regex_error& e) {
            spdlog::warn("Invalid exclude pattern '{}': {}", pattern, e.what());
            continue;
        }
    }
    return false;
}

bool CopyEngine::should_include(const std::filesystem::path& path) const {
    if (config_.include_patterns.empty()) return true;
    for (const auto& pattern : config_.include_patterns) {
        try {
            if (std::regex_match(path.filename().string(), std::regex(pattern))) {
                return true;
            }
        } catch (const std::regex_error& e) {
            spdlog::warn("Invalid include pattern '{}': {}", pattern, e.what());
            continue;
        }
    }
    return false;
}

} // namespace cclone::core