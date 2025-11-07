#include <iostream>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "infra/config/config.hpp"
#include "infra/error_handler/error.hpp"
#include "infra/interrupt.hpp"
#include "infra/monitoring/monitoring.hpp"
#include "cli/args_parser/args_parser.hpp"
#include "core/copy_engine/copy_engine.hpp"
#include <git_info.hpp>
#include <spdlog/spdlog.h>
#include <chrono>

using GIT = cclone::build_info::GitInfo;
using ARGS = cclone::args_parser::CLIArgs;
using GIT_VERSE = cclone::build_info::GitInfo;

constexpr auto load_from_cli = cclone::infra::config_from_cli;
constexpr auto load_config_file = cclone::infra::load_config_from_file;
constexpr auto args_parser = cclone::args_parser::parse_args;
constexpr auto git =  cclone::build_info::get_git_info();
[[nodiscard]] 
static auto 
__out_git_verse(const GIT& git) 
-> void {
    fmt::print("Git branch: {}\n", git.branch);
    fmt::print("Git commit: {}\n", git.commit);
    fmt::print("Git commit short: {}\n", git.commit_short);
    fmt::print("Git dirty: {}\n", git.dirty ? "yes" : "no");
    fmt::print("Build timestamp (UTC): {}\n", git.timestamp);
}

[[nodiscard]] 
static auto 
__out_args_verse(const ARGS& args) 
-> void {
    fmt::print("Arguments:\n");
    fmt::print("Sources: {}\n", args.sources); // fmt::print поддерживает vector<string> из коробки!
    fmt::print("Destination: {}\n", args.destination);
    fmt::print("Recursive: {}\n", args.recursive ? "yes" : "no");
    fmt::print("Follow symlinks: {}\n", args.follow_symlinks ? "yes" : "no");
    fmt::print("Verify: {}\n", args.verify ? "yes" : "no");
    fmt::print("Progress: {}\n", args.progress ? "yes" : "no");
    fmt::print("Quiet: {}\n", args.quiet ? "yes" : "no");
    fmt::print("Resume: {}\n", args.resume ? "yes" : "no");
    fmt::print("Threads: {}\n", args.threads.value_or(0) ? fmt::to_string(*args.threads) : "auto");
    fmt::print("Buffer size: {}\n", args.buffer_size.value_or(0) ? fmt::to_string(*args.buffer_size) : "default");
}

int main(int argc, char** argv)
{
    try {
        spdlog::set_level(spdlog::level::info);
        spdlog::set_pattern("[%Y-%m-%d %H:%M:%S] [%l] %v");

        cclone::infra::install_signal_handler();
        
        if (cclone::infra::is_interrupted()) {
            spdlog::warn("Interrupted during startup");
            return 130;
        }

        
        auto args_opt = args_parser(argc, argv);
        if (!args_opt) {
            return 1; // --help или ошибка
        }
        const auto& args = *args_opt;
        
        // 1. Загрузить из файла
        auto config_res = load_config_file();
        if (!config_res) {
            spdlog::error("Config error: {}", config_res.error());
            return 1;
        }
        auto config = config_res.value();

        // 2. Переопределить из CLI
        auto cli_config = load_from_cli(args);
        
        spdlog::debug("Merging CLI config with file config...");
        config.merge_with(cli_config); // CLI имеет приоритет
        
        // Выводим информацию о версии
        if (!args.quiet) {
            __out_git_verse(git);
            spdlog::info("Starting file copy operation...");
        }
        
        // Преобразуем sources в vector<path>
        std::vector<std::filesystem::path> source_paths;
        for (const auto& src : args.sources) {
            source_paths.emplace_back(src);
        }
        std::filesystem::path destination_path(args.destination);
        
        // Проверяем существование источников
        for (const auto& src : source_paths) {
            if (!std::filesystem::exists(src)) {
                spdlog::error("Source does not exist: {}", src.string());
                return 1;
            }
        }
        
        spdlog::debug("Creating Progress Monitor...");
        // Создаём Progress Monitor
        cclone::infra::ProgressMonitor monitor(config.progress);
        
        spdlog::debug("Creating Copy Engine...");
        // Создаём Copy Engine
        cclone::core::CopyEngine engine(config, monitor);
        
        // Запускаем копирование с замером времени
        auto start_time = std::chrono::steady_clock::now();
        
        spdlog::debug("Starting copy operation...");
        auto result = engine.run(source_paths, destination_path);
        
        auto end_time = std::chrono::steady_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_time - start_time);
        
        if (!result) {
            spdlog::error("Copy operation failed: {}", result.error().message);
            return 1;
        }
        
        // Выводим статистику
        const auto& stats = *result;
        
        if (!args.quiet) {
            spdlog::info("Copy operation completed successfully!");
            spdlog::info("Files copied: {}", stats.files_copied);
            spdlog::info("Bytes copied: {} ({:.2f} MB)", 
                        stats.bytes_copied, 
                        stats.bytes_copied / 1024.0 / 1024.0);
            spdlog::info("Files skipped: {}", stats.files_skipped);
            spdlog::info("Errors: {}", stats.errors);
            spdlog::info("Time elapsed: {:.2f} seconds", duration.count() / 1000.0);
            
            if (stats.bytes_copied > 0 && duration.count() > 0) {
                double speed_mbps = (stats.bytes_copied / 1024.0 / 1024.0) / (duration.count() / 1000.0);
                spdlog::info("Average speed: {:.2f} MB/s", speed_mbps);
            }
        }
        
        return stats.errors > 0 ? 1 : 0;
    } catch (const std::exception& e) {
        spdlog::error("Fatal error: {}", e.what());
        return 1;
    } catch (...) {
        spdlog::error("Unknown fatal error");
        return 1;
    }
}