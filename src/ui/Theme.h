#pragma once

#include <juce_graphics/juce_graphics.h>

namespace composer::ui::theme {

// Semantic tokens from DESIGN.md Pass 5 (dark surface set). UI components pull colours from here
// instead of inventing local values.
inline const juce::Colour canvas{0xff14171b};
inline const juce::Colour panel{0xff1d2228};
inline const juce::Colour raised{0xff252b32};
inline const juce::Colour border{0xff3b444f};

inline const juce::Colour textPrimary{0xfff2f4f7};
inline const juce::Colour textSecondary{0xffb7c0cb};
inline const juce::Colour textDisabled{0xff737d89};

inline const juce::Colour selection{0xff68a7ff};
inline const juce::Colour record{0xffff5c67};
inline const juce::Colour success{0xff62c995};
inline const juce::Colour warning{0xfff2c14e};

}  // namespace composer::ui::theme
