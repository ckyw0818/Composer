#pragma once

#include "commands/ProjectEditor.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <filesystem>
#include <functional>
#include <vector>

namespace composer::ui {

class WaveformEditor final : public juce::Component {
public:
    explicit WaveformEditor(commands::ProjectEditor& editor);

    void setAssetRoot(std::filesystem::path assetRoot);
    void showTrack(const domain::EntityId& trackId);
    void refreshFromProject();

    std::function<void()> onProjectChanged;
    std::function<void(const juce::String& text, bool isError)> onStatus;

    void paint(juce::Graphics& graphics) override;
    void resized() override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;

private:
    [[nodiscard]] const domain::AudioTrack* selectedTrack() const;
    [[nodiscard]] const domain::AudioClip* selectedClip() const;
    [[nodiscard]] juce::Rectangle<int> clipBounds(const domain::AudioClip& clip,
        juce::Rectangle<int> waveformArea) const;
    [[nodiscard]] double samplesPerPixel(juce::Rectangle<int> waveformArea) const;
    void rebuildPeaks();
    void commitStatus(const juce::String& text, bool error = false);

    commands::ProjectEditor& editor_;
    std::filesystem::path assetRoot_;
    domain::EntityId trackId_;
    domain::EntityId clipId_;
    std::vector<float> peaks_;
    juce::TextButton trimStart_{"Trim start"};
    juce::TextButton trimEnd_{"Trim end"};
    juce::TextButton split_{"Split"};
    juce::TextButton fade_{"Fade"};
    juce::TextButton crossfade_{"Crossfade"};
    juce::ComboBox monitor_;
    juce::Slider latency_;
    juce::Slider gain_;
    int dragStartX_{};
    domain::ProjectSample dragOriginalStart_{};
    bool dragging_{};
};

}  // namespace composer::ui
