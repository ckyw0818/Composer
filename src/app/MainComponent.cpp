#include "app/MainComponent.h"

#include <algorithm>
#include <array>

namespace composer::app {

MainComponent::MainComponent() {
    setSize(1024, 700);
    setAudioChannels(0, 2);
    deviceReady_ = deviceManager.getCurrentAudioDevice() != nullptr;
}

MainComponent::~MainComponent() {
    shutdownAudio();
}

void MainComponent::prepareToPlay(const int samplesPerBlockExpected, const double sampleRate) {
    renderer_.prepare({
        .sampleRate = sampleRate,
        .maximumBlockSize = static_cast<std::size_t>(samplesPerBlockExpected),
        .outputChannels = 2});
    playhead_ = 0;
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
    renderer_.render({
        .outputs = outputs.data(),
        .outputChannels = static_cast<std::size_t>(channelCount),
        .frameCount = static_cast<std::size_t>(bufferToFill.numSamples),
        .startSample = playhead_});
    playhead_ += bufferToFill.numSamples;
    if (playhead_ >= renderer_.snapshot().lengthSamples) {
        playhead_ = 0;
    }
}

void MainComponent::releaseResources() {}

void MainComponent::paint(juce::Graphics& graphics) {
    graphics.fillAll(juce::Colour{0xff17191d});
    graphics.setColour(juce::Colour{0xfff1f3f5});
    graphics.setFont(juce::FontOptions{28.0F});
    graphics.drawText("Composer", getLocalBounds().reduced(32), juce::Justification::topLeft);
    graphics.setFont(juce::FontOptions{16.0F});
    graphics.drawText(
        deviceReady_
            ? "S0 Foundation - 8-bar canonical click through the selected audio device"
            : "No output device. Run the canonical fake fixture, then check Windows audio settings.",
        getLocalBounds().reduced(32, 88),
        juce::Justification::topLeft);
}

void MainComponent::resized() {}

}  // namespace composer::app
