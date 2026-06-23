#include "application/Result.h"
#include "domain/Types.h"
#include "tests/TestRunner.h"

#include <array>
#include <cstdint>
#include <utility>
#include <variant>

int runContractTests() {
    composer::tests::TestContext tests{"S0 domain and boundary contracts"};
    tests.expect(composer::domain::kPpq == 960, "musical time must use 960 PPQ");
    tests.expect(sizeof(composer::domain::Tick) == sizeof(std::int64_t),
        "ticks must be signed 64-bit values");

    constexpr composer::domain::HalfOpenTickRange range{.start = 10, .end = 20};
    tests.expect(range.isValid(), "half-open range must validate");
    tests.expect(range.contains(10), "half-open range must include start");
    tests.expect(range.contains(19), "half-open range must include final interior tick");
    tests.expect(!range.contains(20), "half-open range must exclude end");
    tests.expect(!range.contains(9), "half-open range must exclude values before start");
    constexpr composer::domain::HalfOpenTickRange invalidRange{.start = 20, .end = 10};
    tests.expect(!invalidRange.isValid(), "reversed range must be invalid");

    composer::application::Result<int> result = composer::application::Error{
        .code = composer::application::ErrorCode::staleRevision,
        .message = "stale runtime snapshot"};
    tests.expect(std::holds_alternative<composer::application::Error>(result),
        "boundary failure must remain a typed variant");
    tests.expect(std::get<composer::application::Error>(result).stableId() == "APP-REVISION-002",
        "typed error must retain its stable ID");

    constexpr std::array mappings{
        std::pair{composer::application::ErrorCode::invalidArgument, "APP-ARGUMENT-001"},
        std::pair{composer::application::ErrorCode::staleRevision, "APP-REVISION-002"},
        std::pair{composer::application::ErrorCode::deviceUnavailable, "AUDIO-DEVICE-001"},
        std::pair{composer::application::ErrorCode::callbackDeadlineExceeded, "AUDIO-DEADLINE-002"},
        std::pair{composer::application::ErrorCode::dependencyUnavailable, "DEPENDENCY-001"}};
    for (const auto& [code, expectedId] : mappings) {
        tests.expect(composer::application::errorId(code) == expectedId,
            "every typed error must have a stable mapping");
    }
    return tests.finish();
}
