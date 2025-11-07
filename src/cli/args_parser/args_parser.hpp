#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <optional>



namespace cclone::args_parser {
    struct CLIArgs
{
    std::vector<std::string> sources;       // -s или позиционные аргументы
    std::string destination;                // -d или последний аргумент
    bool recursive{false};                  // -r, --recursive
    bool follow_symlinks{false};            // --follow-symlinks
    bool verify{false};                     // --verify
    bool progress{false};                   // --progress
    bool quiet{false};                      // -q, --quiet
    bool resume{false};                     // --resume
    bool preserve_metadata{true};           // --preserve-metadata / --no-preserve-metadata
    std::optional<std::uint32_t> threads;   // --threads=N
    std::optional<std::size_t> buffer_size; // --buffer-size=SIZE
    bool help{false};                       // -h, --help (autogeneration CLI11)
};



/// Parses command-line arguments and returns a CLIArgs struct.
std::optional<CLIArgs> parse_args(int argc, char const* const* argv);

} // namespace cclone::args_parser

using __CLI = cclone::args_parser::CLIArgs;