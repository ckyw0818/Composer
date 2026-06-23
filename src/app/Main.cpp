#include "app/MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

#include <memory>

namespace composer::app {

class ComposerApplication final : public juce::JUCEApplication {
public:
    [[nodiscard]] const juce::String getApplicationName() override { return "Composer"; }
    [[nodiscard]] const juce::String getApplicationVersion() override { return "0.2.0-s2"; }

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
            setResizeLimits(1024, 700, 3840, 2160);
            // Guarantee an on-screen size even if the content reports zero before first layout.
            if (getWidth() < 320 || getHeight() < 240) {
                centreWithSize(1280, 800);
            } else {
                centreWithSize(getWidth(), getHeight());
            }
            setVisible(true);

            // Some Windows sessions create the peer but leave it behind other windows or off the
            // active desktop. Once the message loop is running, force the window to the front.
            juce::Component::SafePointer<MainWindow> self{this};
            juce::MessageManager::callAsync([self]() mutable {
                if (self != nullptr) {
                    self->setVisible(true);
                    self->toFront(true);
                }
            });
        }

        void closeButtonPressed() override {
            juce::JUCEApplication::getInstance()->systemRequestedQuit();
        }
    };

    std::unique_ptr<MainWindow> window_;
};

}  // namespace composer::app

START_JUCE_APPLICATION(composer::app::ComposerApplication)
