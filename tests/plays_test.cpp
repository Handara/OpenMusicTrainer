#include "doctest/doctest.h"

#include "core/plays.h"

#include <filesystem>

TEST_CASE("plays: each exercise's count and last day, kept in one file"){
    std::filesystem::path path = std::filesystem::temp_directory_path() / "lahn_tests" / "plays.txt";
    std::filesystem::create_directories(path.parent_path());
    std::filesystem::remove(path);
    CHECK(loadPlays(path.string()).empty());
    std::string error;
    REQUIRE_MESSAGE(recordPlay(path.string(), "builtin-intervals-up", "2026-10-04", error), error);
    REQUIRE(recordPlay(path.string(), "builtin-intervals-up", "2026-10-05", error));
    REQUIRE(recordPlay(path.string(), "user-mine", "2026-10-05", error));
    std::map<std::string, PlayCount> plays = loadPlays(path.string());
    CHECK(plays["builtin-intervals-up"].times == 2);
    CHECK(plays["builtin-intervals-up"].last == "2026-10-05");
    CHECK(plays["user-mine"].times == 1);
}
