// include/cclone/extensions/resumer.hpp
#pragma once

#include <filesystem>
#include <optional>
#include <vector>

namespace cclone::extensions {

struct ResumeInfo {
    std::filesystem::path source;
    std::filesystem::path destination;
    std::uint64_t copied_bytes = 0;
    std::uint64_t total_bytes = 0;
    std::vector<int> completed_chunks;
};

[[nodiscard]] auto load_resume_info(const std::filesystem::path& resume_file)
    -> std::optional<ResumeInfo>;

void save_resume_info(const ResumeInfo& info,
                      const std::filesystem::path& resume_file = ".cclone.resume");

bool should_resume(const std::filesystem::path& src,
                   const std::filesystem::path& dst);

} // namespace cclone::extensions