#include <iostream>
#include <fmt/core.h>
#include <fmt/ranges.h>

#include "infra/config/config.hpp"
#include "cli/args_parser/args_parser.hpp"
#include <git_info.hpp>
#include <spdlog/spdlog.h>

using __GIT = cclone::build_info::GitInfo;
using __GIT_VERSE = cclone::build_info::GitInfo;

constexpr auto __load_from_cli = cclone::infra::config_from_cli;
constexpr auto __load_config_file = cclone::infra::load_config_from_file;
constexpr auto args_parser = cclone::args_parser::parse_args;
constexpr auto git =  cclone::build_info::get_git_info();
[[nodiscard]] 
static auto 
__out_git_verse(const __GIT& git) 
-> void {
    fmt::print("Git branch: {}\n", git.branch);
    fmt::print("Git commit: {}\n", git.commit);
    fmt::print("Git commit short: {}\n", git.commit_short);
    fmt::print("Git dirty: {}\n", git.dirty ? "yes" : "no");
    fmt::print("Build timestamp (UTC): {}\n", git.timestamp);
}

[[nodiscard]] 
static auto 
__out_args_verse(const cclone::args_parser::CLIArgs& args) 
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
    auto args_opt = args_parser(argc, argv);
    if (!args_opt) {
        return 1; // --help или ошибка
    }
    const auto& args = *args_opt;
    
    // 1. Загрузить из файла
    auto config_res = __load_config_file();
    if (!config_res) {
        spdlog::error("Config error: {}", config_res.error());
        return 1;
    }
    auto config = config_res.value();

    // 2. Переопределить из CLI
    auto cli_config = __load_from_cli(args);
    config.merge_with(cli_config); // CLI имеет приоритет
    __out_git_verse(git);
    __out_args_verse(args);
    return 0;
}