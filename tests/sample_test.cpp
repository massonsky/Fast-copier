#include <gtest/gtest.h>

#include <git_info.hpp>

TEST(BuildInfoTest, GitMetadataAvailable)
{
    EXPECT_FALSE(cclone::build_info::git_commit.empty());
    EXPECT_FALSE(cclone::build_info::git_commit_short.empty());
    // Optional: check length
    EXPECT_EQ(cclone::build_info::git_commit_short.size(), 7); // typical short SHA
}

// Optional: just verify it compiles and is usable
TEST(BuildInfoTest, DirtyFlagIsBoolean)
{
    [[maybe_unused]] bool dirty = cclone::build_info::git_dirty;
    SUCCEED(); // Just ensuring no crash and type is valid
}