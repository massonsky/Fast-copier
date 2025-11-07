// resumer.cpp
#include "resumer.hpp"
#include <fstream>
#include <sstream>
#include <yaml-cpp/yaml.h>

namespace cclone::extensions {

auto load_resume_info(const std::filesystem::path& resume_file)
    -> std::optional<ResumeInfo>
{
    if (!std::filesystem::exists(resume_file)) {
        return std::nullopt;
    }

    try {
        YAML::Node node = YAML::LoadFile(resume_file.string());
        
        ResumeInfo info;
        info.source = node["source"].as<std::string>();
        info.destination = node["destination"].as<std::string>();
        info.copied_bytes = node["copied_bytes"].as<std::uint64_t>();
        info.total_bytes = node["total_bytes"].as<std::uint64_t>();
        
        if (node["completed_chunks"]) {
            info.completed_chunks = node["completed_chunks"].as<std::vector<int>>();
        }
        
        return info;
    } catch (const YAML::Exception& e) {
        return std::nullopt;
    }
}

void save_resume_info(const ResumeInfo& info,
                      const std::filesystem::path& resume_file)
{
    YAML::Node node;
    node["source"] = info.source.string();
    node["destination"] = info.destination.string();
    node["copied_bytes"] = info.copied_bytes;
    node["total_bytes"] = info.total_bytes;
    node["completed_chunks"] = info.completed_chunks;
    
    std::ofstream ofs(resume_file);
    ofs << node;
}

bool should_resume(const std::filesystem::path& src,
                   const std::filesystem::path& dst)
{
    if (!std::filesystem::exists(dst)) {
        return false;
    }
    
    std::error_code ec;
    auto src_size = std::filesystem::file_size(src, ec);
    if (ec) return false;
    
    auto dst_size = std::filesystem::file_size(dst, ec);
    if (ec) return false;
    
    // Можем возобновить, если файл назначения меньше исходного
    return dst_size < src_size;
}

} // namespace cclone::extensions
