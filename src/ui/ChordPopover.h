#pragma once

#include "domain/Chord.h"
#include "domain/Types.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace composer::ui {

struct ChordRequest final {
    domain::ChordSpec spec;
    domain::Tick duration{domain::kPpq};
};

class ChordPopover final : public juce::Component {
public:
    ChordPopover(int defaultOctave, domain::Tick defaultDuration);

    std::function<void(const ChordRequest& request)> onPreview;
    std::function<bool(const ChordRequest& request)> onInsert;
    std::function<void()> onCancel;

    void setStatus(const juce::String& text, bool isError = false);
    void paint(juce::Graphics& graphics) override;
    void resized() override;

private:
    [[nodiscard]] ChordRequest request() const;
    void dismiss();

    juce::Label title_;
    juce::ComboBox root_;
    juce::ComboBox quality_;
    juce::ComboBox extension_;
    juce::ComboBox inversion_;
    juce::ComboBox octave_;
    juce::ComboBox length_;
    juce::Slider velocity_;
    juce::TextButton preview_{"Preview"};
    juce::TextButton insert_{"Insert notes"};
    juce::TextButton cancel_{"Cancel"};
    juce::Label status_;
};

}  // namespace composer::ui
