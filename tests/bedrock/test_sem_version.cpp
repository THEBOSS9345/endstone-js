// Copyright (c) 2024, The Endstone Project. (https://endstone.dev) All Rights Reserved.
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <gtest/gtest.h>

#include "bedrock/core/sem_ver/sem_version.h"
TEST(SemVersionTest, DefaultConstructor)
{
    SemVersion version;
    EXPECT_FALSE(version.isValid());
    EXPECT_FALSE(version.isAnyVersion());
    EXPECT_EQ(version.asString(), "0.0.0");
}

TEST(SemVersionTest, AnyVersionConstructor)
{
    SemVersion version(SemVersion::AnyVersionConstructor);
    EXPECT_TRUE(version.isValid());
    EXPECT_TRUE(version.isAnyVersion());
    EXPECT_EQ(version.asString(), "*");
}

TEST(SemVersionTest, ParameterizedConstructor)
{
    SemVersion version(1, 2, 3, "alpha", "build123");
    EXPECT_TRUE(version.isValid());
    EXPECT_FALSE(version.isAnyVersion());
    EXPECT_EQ(version.getMajor(), 1);
    EXPECT_EQ(version.getMinor(), 2);
    EXPECT_EQ(version.getPatch(), 3);
    EXPECT_EQ(version.getPreRelease(), "alpha");
    EXPECT_EQ(version.getBuildMeta(), "build123");
    EXPECT_EQ(version.asString(), "1.2.3-alpha+build123");
}

TEST(SemVersionTest, NoPreReleaseOrBuildMeta)
{
    SemVersion version(1, 2, 3);
    EXPECT_TRUE(version.isValid());
    EXPECT_FALSE(version.isAnyVersion());
    EXPECT_EQ(version.getMajor(), 1);
    EXPECT_EQ(version.getMinor(), 2);
    EXPECT_EQ(version.getPatch(), 3);
    EXPECT_EQ(version.getPreRelease(), nullptr);
    EXPECT_EQ(version.getBuildMeta(), nullptr);
    EXPECT_EQ(version.asString(), "1.2.3");
}

TEST(SemVersionTest, OnlyPreRelease)
{
    SemVersion version(1, 2, 3, "beta");
    EXPECT_EQ(version.asString(), "1.2.3-beta");
}

TEST(SemVersionTest, OnlyBuildMeta)
{
    SemVersion version(1, 2, 3, "", "build456");
    EXPECT_EQ(version.asString(), "1.2.3+build456");
}

TEST(SemVersionTest, ZeroVersionWithPreReleaseAndBuild)
{
    SemVersion version(0, 0, 0, "pre", "meta");
    EXPECT_EQ(version.asString(), "0.0.0-pre+meta");
}

TEST(SemVersionTest, EqualityOperator)
{
    SemVersion v1(1, 2, 3, "alpha", "build123");
    SemVersion v2(1, 2, 3, "alpha", "build123");
    SemVersion v3(1, 2, 3, "beta", "build123");
    SemVersion v4(1, 2, 3, "alpha");
    SemVersion v5(SemVersion::AnyVersionConstructor);
    SemVersion v6(SemVersion::AnyVersionConstructor);

    EXPECT_EQ(v1, v2);
    EXPECT_EQ(v1, v4);
    EXPECT_NE(v1, v3);
    EXPECT_EQ(v5, v6);
    EXPECT_NE(v1, v5);
    EXPECT_NE(v5, v1);
}

TEST(SemVersionTest, InequalityOperator)
{
    EXPECT_TRUE(SemVersion(1, 0, 0, "alpha") < SemVersion(1, 0, 0, "alpha.1"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "alpha.1") < SemVersion(1, 0, 0, "alpha.beta"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "alpha.beta") < SemVersion(1, 0, 0, "beta"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "beta") < SemVersion(1, 0, 0, "beta.2"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "beta.2") < SemVersion(1, 0, 0, "beta.11"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "beta.11") < SemVersion(1, 0, 0, "rc.1"));
    EXPECT_TRUE(SemVersion(1, 0, 0, "rc.1") < SemVersion(1, 0, 0));
}

TEST(SemVersionTest, RelationalOperators)
{
    const SemVersion older(1, 26, 40);
    const SemVersion newer(1, 26, 44);
    const SemVersion same(1, 26, 44);

    EXPECT_TRUE(older < newer);
    EXPECT_TRUE(older <= newer);
    EXPECT_TRUE(newer > older);
    EXPECT_TRUE(newer >= older);

    EXPECT_FALSE(newer < older);
    EXPECT_FALSE(newer <= older);
    EXPECT_FALSE(older > newer);
    EXPECT_FALSE(older >= newer);

    EXPECT_FALSE(newer > same);
    EXPECT_TRUE(newer >= same);
    EXPECT_TRUE(newer <= same);
}

TEST(SemVersionFromStringTest, FullVersion)
{
    SemVersion version;
    EXPECT_EQ(SemVersion::fromString("1.26.44", version, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::Full);
    EXPECT_TRUE(version.isValid());
    EXPECT_FALSE(version.isAnyVersion());
    EXPECT_EQ(version.getMajor(), 1);
    EXPECT_EQ(version.getMinor(), 26);
    EXPECT_EQ(version.getPatch(), 44);
    EXPECT_EQ(version.asString(), "1.26.44");
}

TEST(SemVersionFromStringTest, PreReleaseAndBuildMeta)
{
    SemVersion version;
    EXPECT_EQ(SemVersion::fromString("1.2.3-alpha.1+build.456", version, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::Full);
    // STREQ, not EQ: StaticOptimizedString decays to const char *, so EQ compares pointers.
    EXPECT_STREQ(version.getPreRelease(), "alpha.1");
    EXPECT_STREQ(version.getBuildMeta(), "build.456");
    EXPECT_EQ(version.asString(), "1.2.3-alpha.1+build.456");
}

// A missing minor or patch still parses, but only as a partial match.
TEST(SemVersionFromStringTest, PartialVersion)
{
    SemVersion version;
    EXPECT_EQ(SemVersion::fromString("1.26", version, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::Partial);
    EXPECT_EQ(version.getMajor(), 1);
    EXPECT_EQ(version.getMinor(), 26);
    EXPECT_EQ(version.getPatch(), 0);

    EXPECT_EQ(SemVersion::fromString("1", version, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::Partial);
    EXPECT_EQ(version.getMajor(), 1);
    EXPECT_EQ(version.getMinor(), 0);
    EXPECT_EQ(version.getPatch(), 0);
}

TEST(SemVersionFromStringTest, WildcardDependsOnParseOption)
{
    SemVersion version;
    EXPECT_EQ(SemVersion::fromString("*", version, SemVersion::ParseOption::AllowWildcards),
              SemVersion::MatchType::Full);
    EXPECT_TRUE(version.isValid());
    EXPECT_TRUE(version.isAnyVersion());
    EXPECT_EQ(version.asString(), "*");

    SemVersion rejected;
    EXPECT_EQ(SemVersion::fromString("*", rejected, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::None);
    EXPECT_FALSE(rejected.isValid());
    EXPECT_FALSE(rejected.isAnyVersion());
}

TEST(SemVersionFromStringTest, BetaTag)
{
    SemVersion version;
    EXPECT_EQ(SemVersion::fromString("beta", version, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::Full);
    EXPECT_TRUE(version.isValid());
    EXPECT_EQ(version.getMajor(), SemVersion::BetaVersionNumber);
    EXPECT_EQ(version.getMinor(), SemVersion::BetaVersionNumber);
    EXPECT_EQ(version.getPatch(), SemVersion::BetaVersionNumber);
    EXPECT_STREQ(version.getPreRelease(), "beta");
}

TEST(SemVersionFromStringTest, RejectsMalformed)
{
    SemVersion version;
    for (const auto *src : {"", "abc", "1.2.3.4", "01", "1.", "1.2.3-", ".1.2", "1.2.3 "}) {
        EXPECT_EQ(SemVersion::fromString(src, version, SemVersion::ParseOption::NoWildcards),
                  SemVersion::MatchType::None)
            << "expected \"" << src << "\" to be rejected";
    }
}

// The whole point of the SetScorePacket gate: an older client must compare less.
TEST(SemVersionFromStringTest, OrdersParsedVersions)
{
    SemVersion older;
    SemVersion newer;
    ASSERT_NE(SemVersion::fromString("1.26.40", older, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::None);
    ASSERT_NE(SemVersion::fromString("1.26.44", newer, SemVersion::ParseOption::NoWildcards),
              SemVersion::MatchType::None);
    EXPECT_TRUE(older < newer);
    EXPECT_FALSE(newer < older);
    EXPECT_FALSE(newer < SemVersion(1, 26, 44));
}
