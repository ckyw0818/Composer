#include "audio/runtime/CanonicalVerse.h"
#include "domain/Types.h"

#include <juce_core/juce_core.h>

#include <iostream>

namespace {

class FoundationContractTest final : public juce::UnitTest {
public:
    FoundationContractTest() : UnitTest("S0 foundation contract", "unit") {}

    void runTest() override {
        beginTest("time authority is frozen");
        expect(composer::domain::kPpq == 960);

        beginTest("canonical fixture is eight bars and four instrument tracks");
        expect(composer::audio::CanonicalVerse::bars == 8);
        expect(composer::audio::CanonicalVerse::trackNames.size() == 4);
        expect(composer::audio::CanonicalVerse::makeSnapshot().revision == 1);

        beginTest("canonical project builds through the command layer");
        const auto project = composer::audio::CanonicalVerse::makeProject();
        expect(project.tracks.size() == 4);
    }
};

FoundationContractTest foundationContractTest;

}  // namespace

int main() {
    juce::UnitTestRunner runner;
    runner.runTestsInCategory("unit");
    int failures = 0;
    for (int index = 0; index < runner.getNumResults(); ++index) {
        const auto* result = runner.getResult(index);
        failures += result->failures;
        std::cout << result->unitTestName << ": " << result->passes << " passed, "
                  << result->failures << " failed\n";
    }
    return failures == 0 ? 0 : 1;
}
