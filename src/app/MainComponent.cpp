#include "app/MainComponent.h"

#include "ui/Theme.h"

#include <algorithm>
#include <array>

namespace composer::app {

MainComponent::MainComponent() {
    setSize(1280, 800);

    // Load the canonical verse into an editable session. New edits use the session id source so
    // their ids never collide with the fixture's "verse-" ids.
    editor_ = std::make_unique<commands::ProjectEditor>(audio::CanonicalVerse::makeProject(), ids_);

    transport_ = std::make_unique<ui::TransportBar>(
        engine_, static_cast<double>(audio::CanonicalVerse::tempoBpm));
    trackList_ = std::make_unique<ui::TrackList>(*editor_);
    pianoRoll_ = std::make_unique<ui::PianoRoll>(
        *editor_, static_cast<double>(audio::CanonicalVerse::tempoBpm));

    trackList_->onTrackSelected = [this](const domain::EntityId& trackId) { selectTrack(trackId); };
    pianoRoll_->onProjectChanged = [this] {
        publishProject();
        trackList_->refreshFromProject();
    };

    addAndMakeVisible(*transport_);
    addAndMakeVisible(*trackList_);
    addAndMakeVisible(*pianoRoll_);

    publishProject();
    if (!editor_->project().tracks.empty()) {
        selectTrack(editor_->project().tracks.front().id);
    }

    setAudioChannels(0, 2);
    deviceReady_ = deviceManager.getCurrentAudioDevice() != nullptr;
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::selectTrack(const domain::EntityId& trackId) {
    for (const auto& track : editor_->project().tracks) {
        if (track.id == trackId && !track.clips.empty()) {
            pianoRoll_->showClip(trackId, track.clips.front().id);
            return;
        }
    }
    pianoRoll_->showClip({}, {});
}

void MainComponent::publishProject() {
    engine_.setProject(editor_->project(), editor_->revision());
}

void MainComponent::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) {
    engine_.prepare({
        .sampleRate = sampleRate,
        .maximumBlockSize = static_cast<std::size_t>(samplesPerBlockExpected),
        .outputChannels = 2});
}

void MainComponent::getNextAudioBlock(const juce::AudioSourceChannelInfo& bufferToFill) {
    const auto availableChannels = bufferToFill.buffer->getNumChannels();
    if (availableChannels <= 0) {
        return;
    }
    const auto channelCount = std::min(availableChannels, 2);
    std::array<float*, 2> outputs{
        bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample),
        channelCount > 1
            ? bufferToFill.buffer->getWritePointer(1, bufferToFill.startSample)
            : bufferToFill.buffer->getWritePointer(0, bufferToFill.startSample)};
    // PlaybackEngine owns the playhead; startSample is unused by process() and passed as 0.
    engine_.process({
        .outputs = outputs.data(),
        .outputChannels = static_cast<std::size_t>(channelCount),
        .frameCount = static_cast<std::size_t>(bufferToFill.numSamples),
        .startSample = 0});
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(ui::theme::canvas);
}

void MainComponent::resized() {
    auto area = getLocalBounds();
    transport_->setBounds(area.removeFromTop(56));
    trackList_->setBounds(area.removeFromLeft(240));
    pianoRoll_->setBounds(area);
}

}  // namespace composer::app
