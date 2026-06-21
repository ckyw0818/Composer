#include "app/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace composer::app {

class ComposerApplication final : public juce::JUCEApplication {
public:
    [[nodiscard]] const juce::String getApplicationName() override { return "Composer"; }
    [[nodiscard]] const juce::String getApplicationVersion() override { return "0.1.0-s0"; }

    void initialise(const juce::String&) override {
        window_ = std::make_unique<MainWindow>(getApplicationName());
    }

    void shutdown() override { window_.reset(); }

private:
    class MainWindow final : public juce::DocumentWindow {
    public:
        explicit MainWindow(const juce::String& name)
            : DocumentWindow(
                  name,
                  juce::Colour{0xff17191d},
                  DocumentWindow::allButtons) {
            setUsingNativeTitleBar(true);
            setContentOwned(new MainComponent{}, true);
            setResizable(true, true);
            centreWithSize(getWidth(), getHeight());
            setVisible(true);
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> window_;
};

}  // namespace composer::app

START_JUCE_APPLICATION(composer::app::ComposerApplication)
