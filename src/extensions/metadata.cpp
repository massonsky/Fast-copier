// metadata.cpp
#include <filesystem>
#include <expected>
#include "metadata.hpp"
namespace cclone::extensions {

auto copy_metadata(const std::filesystem::path& src,
                   const std::filesystem::path& dst)
    -> std::expected<void, infra::Error>
{
    std::error_code ec;

    // Временные метки
    auto time = std::filesystem::last_write_time(src, ec);
    if (!ec) {
        std::filesystem::last_write_time(dst, time, ec);
    }

    // Права (только POSIX)
#ifndef _WIN32
    auto perms = std::filesystem::status(src, ec).permissions();
    if (!ec) {
        std::filesystem::permissions(dst, perms, ec);
    }
#endif

    if (ec) {
        return std::unexpected(infra::make_error(infra::ErrorCode::Unknown,
                             fmt::format("Metadata copy failed: {}", ec.message())));
    }
    return {};
}

} // namespace cclone::extensions