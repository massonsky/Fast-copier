#pragma once

#include <filesystem>
#include <expected>
#include <cstddef>
#include <string_view>
#include "infra/error_handler/error.hpp"
#include <fstream>
#include <system_error>
#include <fmt/core.h>

#ifdef _WIN32
    #include <windows.h>
    #include <io.h>
    #include <fcntl.h>
#else
    #include <sys/mman.h>
    #include <sys/stat.h>
    #include <fcntl.h>
    #include <unistd.h>
#endif

namespace cclone::adapters::fs {

enum class CopyStrategy {
    Buffered,    // < 1 MB
    MMap,        // 1 MB – 100 MB
    DirectIO,    // > 100 MB (Linux), large_aligned (Windows)
    Async        // будущее расширение
};

[[nodiscard]] auto select_strategy(std::uintmax_t file_size) -> CopyStrategy;

[[nodiscard]] auto copy_file(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    CopyStrategy strategy = CopyStrategy::Buffered
) -> std::expected<void, infra::Error>;

// Вспомогательные функции (для chunked copying)
[[nodiscard]] auto copy_file_buffered(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error>;

[[nodiscard]] auto copy_file_mmap(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error>;

[[nodiscard]] auto copy_file_direct(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error>;
// Асинхронное копирование — возвращает future
[[nodiscard]] auto copy_file_async(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    CopyStrategy strategy = CopyStrategy::Buffered
) -> std::future<std::expected<void, infra::Error>>;

auto select_strategy(std::uintmax_t file_size) -> CopyStrategy {
    if (file_size < 1'000'000) return CopyStrategy::Buffered;      // < 1 MB
    if (file_size < 100'000'000) return CopyStrategy::MMap;        // < 100 MB
    return CopyStrategy::DirectIO;                                 // >= 100 MB
}

// =============== Buffered I/O ===============
auto copy_file_buffered(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error> {
    std::ifstream ifs(src, std::ios::binary);
    std::ofstream ofs(dst, std::ios::binary);
    if (!ifs || !ofs) {
        return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, "Failed to open file"));
    }

    constexpr size_t buffer_size = 64 * 1024;
    std::vector<char> buffer(buffer_size);
    while (ifs.read(buffer.data(), buffer_size)) {
        ofs.write(buffer.data(), ifs.gcount());
    }
    ofs.write(buffer.data(), ifs.gcount());
    return {};
}

// =============== Memory-mapped I/O ===============
auto copy_file_mmap(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error> {
#ifndef _WIN32
    // Linux/macOS
    int src_fd = ::open(src.c_str(), O_RDONLY);
    if (src_fd == -1) {
        return std::unexpected(infra::make_error(infra::ErrorCode::FileNotFound, "Cannot open source for mmap"));
    }

    struct stat sb;
    if (::fstat(src_fd, &sb) == -1) {
        ::close(src_fd);
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "fstat failed"));
    }

    void* src_map = ::mmap(nullptr, sb.st_size, PROT_READ, MAP_PRIVATE, src_fd, 0);
    ::close(src_fd);
    if (src_map == MAP_FAILED) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "mmap failed"));
    }

    int dst_fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        ::munmap(src_map, sb.st_size);
        return std::unexpected(infra::make_error(infra::ErrorCode::PermissionDenied, "Cannot create destination"));
    }

    ssize_t written = ::write(dst_fd, src_map, sb.st_size);
    ::close(dst_fd);
    ::munmap(src_map, sb.st_size);

    if (written != static_cast<ssize_t>(sb.st_size)) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "Incomplete write in mmap copy"));
    }
    return {};
#else
    // Windows fallback to buffered
    return copy_file_buffered(src, dst);
#endif
}

// =============== Direct I/O ===============
auto copy_file_direct(
    const std::filesystem::path& src,
    const std::filesystem::path& dst
) -> std::expected<void, infra::Error> {
#ifdef __linux__
    // Linux: O_DIRECT требует выравнивания
    const size_t buffer_size = 4 * 1024 * 1024; // 4MB
    alignas(4096) std::vector<char> buffer(buffer_size);

    int src_fd = ::open(src.c_str(), O_RDONLY | O_DIRECT);
    if (src_fd == -1) {
        // Fallback to buffered if O_DIRECT not supported
        return copy_file_buffered(src, dst);
    }

    int dst_fd = ::open(dst.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
    if (dst_fd == -1) {
        ::close(src_fd);
        return copy_file_buffered(src, dst);
    }

    ssize_t bytes_read;
    while ((bytes_read = ::read(src_fd, buffer.data(), buffer_size)) > 0) {
        // Выравнивание для O_DIRECT
        size_t aligned_write = (bytes_read + 4095) & ~4095;
        if (aligned_write > buffer_size) aligned_write = buffer_size;
        if (::write(dst_fd, buffer.data(), aligned_write) != static_cast<ssize_t>(aligned_write)) {
            ::close(src_fd); ::close(dst_fd);
            return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "Direct I/O write failed"));
        }
    }

    ::close(src_fd);
    ::close(dst_fd);
    return {};
#else
    // Windows / macOS: fallback to buffered
    return copy_file_buffered(src, dst);
#endif
}

// =============== Unified copy_file ===============
auto copy_file(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    CopyStrategy strategy
) -> std::expected<void, infra::Error> {
    switch (strategy) {
        case CopyStrategy::MMap:
            return copy_file_mmap(src, dst);
        case CopyStrategy::DirectIO:
            return copy_file_direct(src, dst);
        case CopyStrategy::Buffered:
        default:
            return copy_file_buffered(src, dst);
    }
}

#ifdef __linux__
#include <liburing.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <thread>

namespace {
    constexpr size_t RING_SIZE = 64;
    constexpr size_t CHUNK_SIZE = 4 * 1024 * 1024; // 4 MB

    auto copy_with_uring(const char* src_path, const char* dst_path)
        -> std::expected<void, infra::Error>
    {
        io_uring ring;
        if (io_uring_queue_init(RING_SIZE, &ring, 0) < 0) {
            return copy_file_buffered(src_path, dst_path); // fallback
        }

        int src_fd = open(src_path, O_RDONLY | O_DIRECT);
        int dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC | O_DIRECT, 0644);
        if (src_fd < 0 || dst_fd < 0) {
            io_uring_queue_exit(&ring);
            return copy_file_buffered(src_path, dst_path);
        }

        struct stat sb;
        if (fstat(src_fd, &sb) < 0) {
            close(src_fd); close(dst_fd);
            io_uring_queue_exit(&ring);
            return std::unexpected(infra::make_error(infra::ErrorCode::Unknown, "fstat failed"));
        }

        off_t offset = 0;
        size_t remaining = sb.st_size;
        while (remaining > 0) {
            size_t to_read = std::min(remaining, CHUNK_SIZE);
            void* buf;
            if (posix_memalign(&buf, 4096, (to_read + 4095) & ~4095) != 0) {
                break;
            }

            // Подготовка SQE
            io_uring_sqe* sqe = io_uring_get_sqe(&ring);
            io_uring_prep_read(sqe, src_fd, buf, to_read, offset);
            sqe->user_data = 1;

            if (io_uring_submit_and_wait(&ring, 1) < 0) {
                free(buf);
                break;
            }

            io_uring_cqe* cqe;
            if (io_uring_wait_cqe(&ring, &cqe) < 0) {
                free(buf);
                break;
            }

            if (cqe->res < 0) {
                io_uring_cqe_seen(&ring, cqe);
                free(buf);
                break;
            }

            // Запись
            io_uring_prep_write(sqe, dst_fd, buf, cqe->res, offset);
            io_uring_submit_and_wait(&ring, 1);
            io_uring_wait_cqe(&ring, &cqe);
            io_uring_cqe_seen(&ring, cqe);

            offset += cqe->res;
            remaining -= cqe->res;
            free(buf);
        }

        close(src_fd);
        close(dst_fd);
        io_uring_queue_exit(&ring);
        return {};
    }
}
#endif

auto copy_file_async(
    const std::filesystem::path& src,
    const std::filesystem::path& dst,
    CopyStrategy strategy
) -> std::future<std::expected<void, infra::Error>>
{
    return std::async(std::launch::async, [=]() -> std::expected<void, infra::Error> {
        if (infra::is_interrupted()) {
            return std::unexpected(infra::make_error(infra::ErrorCode::Interrupted, "Cancelled"));
        }

#ifdef __linux__
        if (strategy == CopyStrategy::DirectIO) {
            return copy_with_uring(src.c_str(), dst.c_str());
        }
#endif

        return copy_file(src, dst, strategy);
    });
}
} // namespace cclone::adapters::fs
