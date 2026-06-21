#include "audio/runtime/ProjectRenderer.h"

#include <algorithm>
#include <cmath>

namespace composer::audio {
namespace {

constexpr double kPi = 3.14159265358979323846;
constexpr double kTwoPi = 2.0 * kPi;

// Standard equal-temperament: MIDI 69 == 440 Hz.
[[nodiscard]] double pitchToFrequency(const domain::Pitch pitch) noexcept {
    return 440.0 * std::pow(2.0, (static_cast<double>(pitch) - 69.0) / 12.0);
}

}  // namespace

ProjectRenderer::ProjectRenderer(ProjectSnapshot snapshot) noexcept
    : snapshot_(std::move(snapshot)) {}

void ProjectRenderer::prepare(const AudioSpec& spec) {
    spec_ = spec;
    seekTo(0);
}

void ProjectRenderer::seekTo(const domain::ProjectSample sample) noexcept {
    for (auto& voice : voices_) {
        voice = Voice{};
    }
    // Position the cursor at the first note that starts at or after `sample`. Notes already
    // sounding at the seek point are not retriggered in S1 (acceptable for a fresh seek/loop).
    cursor_ = 0;
    while (cursor_ < snapshot_.notes.size()
        && snapshot_.notes[cursor_].startSample < sample) {
        ++cursor_;
    }
    nextSample_ = sample;
}

void ProjectRenderer::startDueNotes(const domain::ProjectSample sample) noexcept {
    while (cursor_ < snapshot_.notes.size()
        && snapshot_.notes[cursor_].startSample == sample) {
        const auto& note = snapshot_.notes[cursor_];
        ++cursor_;
        if (note.amplitude <= 0.0F || note.endSample <= note.startSample) {
            continue;
        }
        // Find a free voice; if the pool is saturated, steal nothing (deterministic drop). The
        // snapshot caps simultaneous notes well under kMaxVoices for S1 fixtures.
        for (auto& voice : voices_) {
            if (!voice.active) {
                voice.active = true;
                voice.endSample = note.endSample;
                voice.phase = 0.0;
                voice.phaseIncrement = kTwoPi * pitchToFrequency(note.pitch) / spec_.sampleRate;
                voice.amplitude = note.amplitude;
                break;
            }
        }
    }
}

float ProjectRenderer::renderSample(const domain::ProjectSample sample) noexcept {
    // Activate any notes that begin exactly at this sample. Notes whose start sits between block
    // boundaries are still caught because we step sample-by-sample.
    startDueNotes(sample);

    float mixed = 0.0F;
    for (auto& voice : voices_) {
        if (!voice.active) {
            continue;
        }
        if (sample >= voice.endSample) {
            voice.active = false;
            continue;
        }
        // Short linear release over the final portion keeps the sum bounded and deterministic.
        const auto remaining = voice.endSample - sample;
        const float release = remaining < 480 ? static_cast<float>(remaining) / 480.0F : 1.0F;
        mixed += voice.amplitude * 0.2F * release * static_cast<float>(std::sin(voice.phase));
        voice.phase += voice.phaseIncrement;
        if (voice.phase >= kTwoPi) {
            voice.phase -= kTwoPi;
        }
    }
    return std::clamp(mixed, -1.0F, 1.0F);
}

void ProjectRenderer::render(const RenderBlock& block) noexcept {
    // If the caller jumped the timeline (loop wrap or transport seek), resync the note cursor and
    // voices so playback stays correct. Contiguous block rendering (the fixture path) never seeks.
    if (block.startSample != nextSample_) {
        seekTo(block.startSample);
    }

    for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
        const auto sample = block.startSample + static_cast<domain::ProjectSample>(frame);
        float value = 0.0F;
        if (sample >= 0 && sample < snapshot_.lengthSamples) {
            value = renderSample(sample);
        }
        for (std::size_t channel = 0; channel < block.outputChannels; ++channel) {
            block.outputs[channel][frame] = value;
        }
    }
    nextSample_ = block.startSample + static_cast<domain::ProjectSample>(block.frameCount);
}

}  // namespace composer::audio
