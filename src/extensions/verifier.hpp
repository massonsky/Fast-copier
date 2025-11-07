// include/cclone/extensions/verifier.hpp
#pragma once

#include <filesystem>
#include <expected>
#include <string_view>
#include "infra/error_handler/error.hpp"

namespace cclone::extensions {

[[nodiscard]] auto compute_xxh64(std::string_view file_path)
    -> std::expected<uint64_t, infra::Error>;

[[nodiscard]] auto verify_files_equal(const std::filesystem::path& src,
                                      const std::filesystem::path& dst)
    -> std::expected<bool, infra::Error>;

} // namespace cclone::extensions
// verifier.cpp (или в .hpp)
#include <xxhash.h>
#include <fstream>
#include <vector>

namespace cclone::extensions {

auto compute_xxh64(std::string_view file_path)
    -> std::expected<uint64_t, infra::Error>
{
    std::ifstream file(file_path.data(), std::ios::binary);
    if (!file) {
        return infra::CC_ERR(infra::ErrorCode::FileNotFound, "Cannot open for hashing");
    }

    XXH64_hash_t state;
    XXH64_reset(&state, 0);

    constexpr size_t buffer_size = 64 * 1024;
    std::vector<char> buffer(buffer_size);
    while (file.read(buffer.data(), buffer_size)) {
        XXH64_update(&state, buffer.data(), file.gcount());
    }
    if (file.gcount() > 0) {
        XXH64_update(&state, buffer.data(), file.gcount());
    }

    return XXH64_digest(&state);
}

auto verify_files_equal(const std::filesystem::path& src,
                        const std::filesystem::path& dst)
    -> std::expected<bool, infra::Error>
{
    auto src_hash = compute_xxh64(src.string());
    if (!src_hash) return src_hash.error();

    auto dst_hash = compute_xxh64(dst.string());
    if (!dst_hash) return dst_hash.error();

    return *src_hash == *dst_hash;
}

} // namespace cclone::extensions