#pragma once

#include "audio/runtime/CanonicalVerse.h"
#include "audio/runtime/ClickRenderer.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace composer::app {

class MainComponent final : public juce::AudioAppComponent {
public:
    MainComponent();
    ~MainComponent() override;

    void prepareToPlay(int samplesPerBlockExpected, double sampleRate) override;
    void getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) override;
    void releaseResources() override;
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    audio::ClickRenderer renderer_{audio::CanonicalVerse::makeSnapshot()};
    domain::ProjectSample playhead_{};
    bool deviceReady_{};
};

}  // namespace composer::app
