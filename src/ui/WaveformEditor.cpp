#include "ui/WaveformEditor.h"

#include "recording/WavFile.h"
#include "ui/Theme.h"

#include <algorithm>
#include <cmath>
#include <variant>

namespace composer::ui {

WaveformEditor::WaveformEditor(commands::ProjectEditor& editor) : editor_(editor) {
    for (auto* button : {&trimStart_, &trimEnd_, &split_, &fade_, &crossfade_}) {
        addAndMakeVisible(*button);
    }
    gain_.setRange(0.0, 4.0, 0.01);
    gain_.setValue(1.0, juce::dontSendNotification);
    gain_.setSliderStyle(juce::Slider::LinearHorizontal);
    gain_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 56, 24);
    gain_.setTitle("Audio clip gain");
    addAndMakeVisible(gain_);
    monitor_.addItem("Monitor off", 1);
    monitor_.addItem("Direct", 2);
    monitor_.addItem("Software", 3);
    monitor_.setSelectedId(1, juce::dontSendNotification);
    monitor_.setTitle("Input monitoring mode");
    addAndMakeVisible(monitor_);
    latency_.setRange(0.0, 48000.0, 1.0);
    latency_.setValue(0.0, juce::dontSendNotification);
    latency_.setSliderStyle(juce::Slider::LinearHorizontal);
    latency_.setTextBoxStyle(juce::Slider::TextBoxRight, false, 64, 24);
    latency_.setTextValueSuffix(" spl");
    latency_.setTitle("Latency compensation samples");
    addAndMakeVisible(latency_);

    const auto commitRoute = [this] {
        const auto* track = selectedTrack();
        if (track == nullptr) return;
        domain::MonitoringMode mode = domain::MonitoringMode::off;
        if (monitor_.getSelectedId() == 2) mode = domain::MonitoringMode::direct;
        if (monitor_.getSelectedId() == 3) mode = domain::MonitoringMode::software;
        const auto result = editor_.setAudioInputRoute(track->id, track->input.deviceId,
            track->input.channelIndex, mode, static_cast<std::int64_t>(latency_.getValue()));
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        if (onProjectChanged) onProjectChanged();
        commitStatus("Audio input monitoring and latency route updated.");
    };
    monitor_.onChange = commitRoute;
    latency_.onDragEnd = commitRoute;

    trimStart_.onClick = [this] {
        if (clipId_.empty()) return;
        const auto result = editor_.trimAudioClip(trackId_, clipId_, 480, 0);
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Audio clip trimmed from start.");
    };
    trimEnd_.onClick = [this] {
        if (clipId_.empty()) return;
        const auto result = editor_.trimAudioClip(trackId_, clipId_, 0, 480);
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Audio clip trimmed from end.");
    };
    split_.onClick = [this] {
        const auto* clip = selectedClip();
        if (clip == nullptr) return;
        const auto result = editor_.splitAudioClip(trackId_, clipId_, clip->lengthFrames / 2);
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        clipId_ = std::get<domain::EntityId>(result);
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Audio clip split into two editable clips.");
    };
    fade_.onClick = [this] {
        const auto* clip = selectedClip();
        if (clip == nullptr) return;
        const auto frames = std::min<std::int64_t>(2400, clip->lengthFrames / 4);
        const auto result = editor_.setAudioClipFades(trackId_, clipId_, frames, frames);
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Fade-in and fade-out applied.");
    };
    crossfade_.onClick = [this] {
        const auto* track = selectedTrack();
        if (track == nullptr || track->clips.size() < 2 || clipId_.empty()) return;
        const auto current = std::find_if(track->clips.begin(), track->clips.end(),
            [&](const domain::AudioClip& clip) { return clip.id == clipId_; });
        if (current == track->clips.end()) return;
        const auto next = current + 1 == track->clips.end() ? track->clips.begin() : current + 1;
        const auto result = editor_.setAudioCrossfade(trackId_, current->id, next->id, 1200);
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Crossfade applied between adjacent audio clips.");
    };
    gain_.onDragEnd = [this] {
        if (clipId_.empty()) return;
        const auto result = editor_.setAudioClipGain(trackId_, clipId_,
            static_cast<float>(gain_.getValue()));
        if (std::holds_alternative<application::Error>(result)) {
            commitStatus(std::get<application::Error>(result).message, true);
            return;
        }
        refreshFromProject();
        if (onProjectChanged) onProjectChanged();
        commitStatus("Audio clip gain updated.");
    };
}

void WaveformEditor::setAssetRoot(std::filesystem::path assetRoot) {
    assetRoot_ = std::move(assetRoot);
    rebuildPeaks();
}

void WaveformEditor::showTrack(const domain::EntityId& trackId) {
    trackId_ = trackId;
    const auto* track = selectedTrack();
    clipId_ = track != nullptr && !track->clips.empty() ? track->clips.front().id : domain::EntityId{};
    refreshFromProject();
}

const domain::AudioTrack* WaveformEditor::selectedTrack() const {
    const auto& tracks = editor_.project().audioTracks;
    const auto position = std::find_if(tracks.begin(), tracks.end(),
        [&](const domain::AudioTrack& track) { return track.id == trackId_; });
    return position == tracks.end() ? nullptr : &*position;
}

const domain::AudioClip* WaveformEditor::selectedClip() const {
    const auto* track = selectedTrack();
    if (track == nullptr) return nullptr;
    const auto position = std::find_if(track->clips.begin(), track->clips.end(),
        [&](const domain::AudioClip& clip) { return clip.id == clipId_; });
    return position == track->clips.end() ? nullptr : &*position;
}

void WaveformEditor::refreshFromProject() {
    const auto* track = selectedTrack();
    if (track == nullptr || track->clips.empty()) {
        clipId_ = {};
        peaks_.clear();
    } else if (selectedClip() == nullptr) {
        clipId_ = track->clips.front().id;
    }
    const auto* clip = selectedClip();
    if (track != nullptr) {
        int monitorId = 1;
        if (track->input.monitoring == domain::MonitoringMode::direct) monitorId = 2;
        if (track->input.monitoring == domain::MonitoringMode::software) monitorId = 3;
        monitor_.setSelectedId(monitorId, juce::dontSendNotification);
        latency_.setValue(static_cast<double>(track->input.latencyCompensationSamples),
            juce::dontSendNotification);
    }
    monitor_.setEnabled(track != nullptr);
    latency_.setEnabled(track != nullptr);
    gain_.setEnabled(clip != nullptr);
    gain_.setValue(clip != nullptr ? clip->gain : 1.0F, juce::dontSendNotification);
    for (auto* button : {&trimStart_, &trimEnd_, &split_, &fade_, &crossfade_}) {
        button->setEnabled(clip != nullptr);
    }
    rebuildPeaks();
    repaint();
}

void WaveformEditor::rebuildPeaks() {
    peaks_.clear();
    const auto* clip = selectedClip();
    if (clip == nullptr || clip->assetPath.empty()) return;
    const auto path = std::filesystem::path{clip->assetPath}.is_absolute()
        ? std::filesystem::path{clip->assetPath}
        : assetRoot_ / clip->assetPath;
    auto peaks = recording::WavFile::readMonoPeaks(path, 256);
    if (std::holds_alternative<application::Error>(peaks)) return;
    peaks_ = std::get<std::vector<float>>(std::move(peaks));
}

void WaveformEditor::paint(juce::Graphics& graphics) {
    graphics.fillAll(theme::panel);
    graphics.setColour(theme::border);
    graphics.drawRect(getLocalBounds());
    auto waveform = getLocalBounds().reduced(12, 10);
    waveform.removeFromTop(38);
    const auto* track = selectedTrack();
    if (track == nullptr) {
        graphics.setColour(theme::textSecondary);
        graphics.drawText("Select or add an audio track to record and edit waveform clips.",
            waveform, juce::Justification::centred);
        return;
    }
    if (track->clips.empty()) {
        graphics.setColour(theme::textSecondary);
        graphics.drawText("Arm this audio track, press Record, then the new take appears here.",
            waveform, juce::Justification::centred);
        return;
    }
    graphics.setColour(theme::raised);
    graphics.fillRoundedRectangle(waveform.toFloat(), 4.0F);
    for (const auto& clip : track->clips) {
        const auto bounds = clipBounds(clip, waveform).reduced(2, 6);
        const bool selected = clip.id == clipId_;
        graphics.setColour(selected ? theme::selection : theme::border);
        graphics.fillRoundedRectangle(bounds.toFloat(), 3.0F);
        graphics.setColour(theme::canvas);
        const auto centre = bounds.getCentreY();
        const auto width = std::max(1, bounds.getWidth());
        for (int x = 0; x < width; ++x) {
            const auto peak = peaks_.empty()
                ? 0.35F
                : peaks_[static_cast<std::size_t>(x) * peaks_.size()
                    / static_cast<std::size_t>(width)];
            const int height = static_cast<int>(peak * static_cast<float>(bounds.getHeight()) * 0.45F);
            graphics.drawVerticalLine(bounds.getX() + x,
                static_cast<float>(centre - height), static_cast<float>(centre + height));
        }
        graphics.setColour(theme::textPrimary);
        graphics.drawText(clip.name, bounds.reduced(8, 4).removeFromTop(18),
            juce::Justification::topLeft);
    }
}

void WaveformEditor::resized() {
    auto controls = getLocalBounds().reduced(8, 6).removeFromTop(30);
    trimStart_.setBounds(controls.removeFromLeft(86));
    controls.removeFromLeft(4);
    trimEnd_.setBounds(controls.removeFromLeft(80));
    controls.removeFromLeft(4);
    split_.setBounds(controls.removeFromLeft(58));
    controls.removeFromLeft(4);
    fade_.setBounds(controls.removeFromLeft(58));
    controls.removeFromLeft(4);
    crossfade_.setBounds(controls.removeFromLeft(88));
    controls.removeFromLeft(8);
    monitor_.setBounds(controls.removeFromLeft(112));
    controls.removeFromLeft(4);
    latency_.setBounds(controls.removeFromLeft(200));
    controls.removeFromLeft(8);
    gain_.setBounds(controls.removeFromLeft(220));
}

double WaveformEditor::samplesPerPixel(const juce::Rectangle<int> waveformArea) const {
    const auto* track = selectedTrack();
    domain::ProjectSample end = 48000 * 10;
    if (track != nullptr) {
        for (const auto& clip : track->clips) end = std::max(end, clip.endSample());
    }
    return static_cast<double>(std::max<domain::ProjectSample>(1, end))
        / static_cast<double>(std::max(1, waveformArea.getWidth()));
}

juce::Rectangle<int> WaveformEditor::clipBounds(
    const domain::AudioClip& clip, juce::Rectangle<int> waveformArea) const {
    const double scale = samplesPerPixel(waveformArea);
    const int x = waveformArea.getX() + static_cast<int>(static_cast<double>(clip.startSample) / scale);
    const int width = std::max(4, static_cast<int>(static_cast<double>(clip.lengthFrames) / scale));
    return {x, waveformArea.getY(), width, waveformArea.getHeight()};
}

void WaveformEditor::mouseDown(const juce::MouseEvent& event) {
    auto waveform = getLocalBounds().reduced(12, 10);
    waveform.removeFromTop(38);
    const auto* track = selectedTrack();
    if (track == nullptr) return;
    for (const auto& clip : track->clips) {
        if (clipBounds(clip, waveform).contains(event.position.toInt())) {
            clipId_ = clip.id;
            dragStartX_ = event.x;
            dragOriginalStart_ = clip.startSample;
            dragging_ = true;
            refreshFromProject();
            return;
        }
    }
}

void WaveformEditor::mouseDrag(const juce::MouseEvent&) {
    if (dragging_) repaint();
}

void WaveformEditor::mouseUp(const juce::MouseEvent& event) {
    if (!dragging_ || clipId_.empty()) return;
    auto waveform = getLocalBounds().reduced(12, 10);
    waveform.removeFromTop(38);
    const auto delta = static_cast<domain::ProjectSample>(
        std::llround(static_cast<double>(event.x - dragStartX_) * samplesPerPixel(waveform)));
    dragging_ = false;
    const auto result = editor_.moveAudioClip(trackId_, clipId_,
        std::max<domain::ProjectSample>(0, dragOriginalStart_ + delta));
    if (std::holds_alternative<application::Error>(result)) {
        commitStatus(std::get<application::Error>(result).message, true);
        return;
    }
    refreshFromProject();
    if (onProjectChanged) onProjectChanged();
    commitStatus("Audio clip moved on the timeline.");
}

void WaveformEditor::commitStatus(const juce::String& text, const bool error) {
    if (onStatus) onStatus(text, error);
}

}  // namespace composer::ui
