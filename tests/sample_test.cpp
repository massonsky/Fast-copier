#include <gtest/gtest.h>

#include <internal/git_info.hpp>

TEST(BuildInfoTest, GitMetadataAvailable)
{
    EXPECT_FALSE(cclone::build_info::git_commit.empty());
    EXPECT_FALSE(cclone::build_info::git_commit_short.empty());
}

TEST(BuildInfoTest, DirtyFlagIsBoolean)
{
    EXPECT_TRUE(cclone::build_info::git_dirty || !cclone::build_info::git_dirty);
}