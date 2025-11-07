#include "xxhash_verifier.hpp"
#include <spdlog/spdlog.h>

namespace cclone::infra {

auto XXHashVerifier::hash_file(const std::filesystem::path& path)
    -> std::expected<XXH64_hash_t, Error>
{
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        return std::unexpected(make_error(ErrorCode::FileNotFound,
                                         fmt::format("Cannot open file for hashing: {}", path.string())));
    }

    XXH64_state_t* state = XXH64_createState();
    if (!state) {
        return std::unexpected(make_error(ErrorCode::Unknown, "Failed to create XXH64 state"));
    }

    XXH64_reset(state, 0); // seed = 0

    std::vector<char> buffer(BUFFER_SIZE);
    while (file.read(buffer.data(), buffer.size()) || file.gcount() > 0) {
        XXH64_update(state, buffer.data(), file.gcount());
    }

    XXH64_hash_t hash = XXH64_digest(state);
    XXH64_freeState(state);

    if (file.bad() && !file.eof()) {
        return std::unexpected(make_error(ErrorCode::Unknown,
                                         fmt::format("Error reading file: {}", path.string())));
    }

    return hash;
}

auto XXHashVerifier::verify_files(const std::filesystem::path& src,
                                  const std::filesystem::path& dst)
    -> std::expected<bool, Error>
{
    auto src_hash = hash_file(src);
    if (!src_hash) {
        return std::unexpected(std::move(src_hash.error()));
    }

    auto dst_hash = hash_file(dst);
    if (!dst_hash) {
        return std::unexpected(std::move(dst_hash.error()));
    }

    bool match = (*src_hash == *dst_hash);
    
    if (!match) {
        spdlog::warn("Hash mismatch: {} (src: {:016x}) vs {} (dst: {:016x})",
                     src.string(), *src_hash,
                     dst.string(), *dst_hash);
    }

    return match;
}

} // namespace cclone::infra
