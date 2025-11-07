#include <yaml-cpp/yaml.h>
#include <spdlog/spdlog.h>
#include <fstream>
#include <system_error>


#ifdef _WIN32
    #include <shlobj.h>
    #include <knownfolders.h>
#else
    #include <unistd.h>
    #include <pwd.h>
#endif

#include "config.hpp"
#include "../../cli/args_parser/args_parser.hpp"

namespace cclone::infra {
    void Config::merge_with(const Config& other) {
        if (other.threads) threads = other.threads;
        if (other.buffer_size) buffer_size = other.buffer_size;
        if (other.recursive) recursive = true;
        if (other.follow_symlinks) follow_symlinks = true;
        if (other.verify) verify = true;
        if (other.resume) resume = true;
        if (!other.progress) progress = false; // CLI может отключить
        if (other.quiet) quiet = true;

        if (!other.exclude_patterns.empty()) exclude_patterns = other.exclude_patterns;
        if (!other.include_patterns.empty()) include_patterns = other.include_patterns;
    }

    static auto get_config_paths() -> std::vector<std::filesystem::path> {
        std::vector<std::filesystem::path> paths;

        // 1. Локальный файл
        paths.push_back(".cclone.yaml");

        // 2. Глобальный файл
    #ifdef _WIN32
        PWSTR appdata_path = nullptr;
        if (SUCCEEDED(SHGetKnownFolderPath(FOLDERID_RoamingAppData, 0, nullptr, &appdata_path))) {
            paths.push_back(std::filesystem::path(appdata_path) / "cclone" / "config.yaml");
            CoTaskMemFree(appdata_path);
        }
    #else
        const char* config_home = std::getenv("XDG_CONFIG_HOME");
        if (config_home && std::filesystem::exists(config_home)) {
            paths.push_back(std::filesystem::path(config_home) / "cclone" / "config.yaml");
        } else {
            const char* home = std::getenv("HOME");
            if (home) {
                paths.push_back(std::filesystem::path(home) / ".config" / "cclone" / "config.yaml");
            }
        }
    #endif

        return paths;
    }

    auto load_config_from_file() -> std::expected<Config, std::string> {
        for (const auto& path : get_config_paths()) {
            if (!std::filesystem::exists(path)) continue;

            try {
                YAML::Node config = YAML::LoadFile(path.string());
                Config cfg{};

                if (config["threads"]) cfg.threads = config["threads"].as<uint32_t>();
                if (config["buffer_size"]) cfg.buffer_size = config["buffer_size"].as<size_t>();

                if (config["recursive"]) cfg.recursive = config["recursive"].as<bool>();
                if (config["follow_symlinks"]) cfg.follow_symlinks = config["follow_symlinks"].as<bool>();
                if (config["verify"]) cfg.verify = config["verify"].as<bool>();
                if (config["resume"]) cfg.resume = config["resume"].as<bool>();
                if (config["progress"]) cfg.progress = config["progress"].as<bool>();
                if (config["quiet"]) cfg.quiet = config["quiet"].as<bool>();

                if (config["exclude"]) {
                    for (const auto& pat : config["exclude"]) {
                        cfg.exclude_patterns.push_back(pat.as<std::string>());
                    }
                }
                if (config["include"]) {
                    for (const auto& pat : config["include"]) {
                        cfg.include_patterns.push_back(pat.as<std::string>());
                    }
                }

                spdlog::debug("Loaded config from {}", path.string());
                return cfg;

            } catch (const std::exception& e) {
                return std::unexpected(fmt::format("Failed to parse {}: {}", path.string(), e.what()));
            }
        }

        // Файл не найден — возвращаем пустой конфиг (не ошибка!)
        return Config{};
    }

  
    [[nodiscard]]
    auto config_from_cli(const __CLI& args) -> Config {
        Config cfg{};
        cfg.threads = args.threads;
        cfg.buffer_size = args.buffer_size;
        cfg.recursive = args.recursive;
        cfg.follow_symlinks = args.follow_symlinks;
        cfg.verify = args.verify;
        cfg.resume = args.resume;
        cfg.progress = args.progress;
        cfg.quiet = args.quiet;
        // include/exclude можно добавить позже
        return cfg;
    }

} // namespace cclone::infra