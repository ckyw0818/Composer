#pragma once

#include "audio/runtime/PlaybackEngine.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace composer::ui {

// Top transport: play/stop, rewind, loop toggle, tempo and a position readout. Always visible per
// the design's first-screen constraint. Polls the PlaybackEngine on a timer to reflect the moving
// playhead and to keep button state in sync when playback stops at the end.
class TransportBar final : public juce::Component, private juce::Timer {
public:
    explicit TransportBar(audio::PlaybackEngine& engine, double tempoBpm);
    ~TransportBar() override;

    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();

    audio::PlaybackEngine& engine_;
    double tempoBpm_{120.0};

    juce::TextButton rewindButton_{"|<"};
    juce::TextButton playButton_{"Play"};
    juce::TextButton loopButton_{"Loop"};
    juce::Label positionLabel_;
    juce::Label tempoLabel_;
};

}  // namespace composer::ui
