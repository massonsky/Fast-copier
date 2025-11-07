#pragma once

#include <string>
#include <vector>
#include <optional>
#include <cstdint>
#include <expected>
#include <filesystem>

namespace cclone::args_parser{
    class CLIArgs;
}

namespace cclone::infra {

    
struct Config {
    // I/O
    std::optional<std::uint32_t> threads;
    std::optional<std::size_t> buffer_size;   // bytes

    // Behavior
    bool recursive = false;
    bool follow_symlinks = false;
    bool verify = false;
    bool resume = false;
    bool progress = true;
    bool quiet = false;

    // Paths
    std::vector<std::string> exclude_patterns;
    std::vector<std::string> include_patterns;

    // Слияние с другим Config (например, из CLI)
    void merge_with(const Config& other);
};

/// Загружает конфигурацию из файла YAML.
/// Ищет файл в порядке:
///   1. ./.cclone.yaml
///   2. ~/.config/cclone/config.yaml (Linux/macOS)
///   3. %APPDATA%/cclone/config.yaml (Windows)
/// Возвращает пустой Config, если файл не найден.
[[nodiscard]] auto load_config_from_file() -> std::expected<Config, std::string>;

/// Создаёт Config из CLI аргументов (структура из args_parser)
[[nodiscard]] auto config_from_cli(const struct cclone::args_parser::CLIArgs& args) -> Config;

} // namespace cclone::infra