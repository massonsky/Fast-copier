// include/cclone/extensions/metadata.hpp
#pragma once

#include <filesystem>
#include "../infra/error_handler/error.hpp"

namespace cclone::extensions {

[[nodiscard]] auto copy_metadata(const std::filesystem::path& src,
                                 const std::filesystem::path& dst)
    -> std::expected<void, infra::Error>;

} // namespace cclone::extensions