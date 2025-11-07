#pragma once

#include <filesystem>
#include <expected>
#include <fstream>
#include <vector>
#include "../error_handler/error.hpp"
#include <xxhash.h>

namespace cclone::infra {

class XXHashVerifier {
public:
    // Вычисляет xxHash64 для файла
    static auto hash_file(const std::filesystem::path& path) 
        -> std::expected<XXH64_hash_t, Error>;

    // Сравнивает хеши двух файлов
    static auto verify_files(const std::filesystem::path& src,
                            const std::filesystem::path& dst)
        -> std::expected<bool, Error>;

private:
    static constexpr size_t BUFFER_SIZE = 4 * 1024 * 1024; // 4MB buffer
};

} // namespace cclone::infra
