#pragma once
// seven7 — WebShell: the React UI (ui/) hosted in a juce::WebBrowserComponent.
//
// Native functions exposed to JS (ui/src/bridge/index.ts, JuceBridge):
//   s7Command(jsonText)                    → jsonText   Controller::command
//   s7State()                              → jsonText   Controller::state_json
//   s7Status()                             → jsonText   Controller::status_json
//   s7Peaks(source, from, to, buckets)     → jsonText   Controller::peaks_json
//   s7ChooseFile(kind)                     → path | ""  native file chooser
// Events pushed to JS:
//   s7Status        (jsonText, ~30 Hz)     transport + meters
//   s7StateChanged  (version)              project state changed → UI refetches
//   s7Log           (text)                 engine log line
//   s7Menu          (action)               native menu item chosen
//
// The UI bundle (ui/dist) is embedded as S7UIData and served through the
// WebBrowserComponent resource provider; S7_UI_DEV_SERVER switches to Vite.

#include <juce_gui_extra/juce_gui_extra.h>

#include <functional>
#include <memory>
#include <optional>

#include "app/controller.h"

namespace s7::desktop {

class WebShell final : public juce::Component, private juce::Timer {
public:
  explicit WebShell(app::Controller& controller);
  ~WebShell() override;

  void resized() override;
  void paint(juce::Graphics&) override;

  /// Called by the app shell (menus, key commands) to run a UI-protocol op.
  juce::String runCommand(const juce::String& jsonText);
  /// Tell the UI that the project state changed outside a command (open/recording).
  void notifyStateChanged();
  void pushLog(const juce::String& line);
  /// Native menu → UI command (ui/src/lib/commands.ts handles "s7Menu" actions).
  void sendMenuAction(const juce::String& action);

  /// Optional hook to open native dialogs (audio settings) from the UI (op "host_settings").
  std::function<void()> onOpenAudioSettings;

private:
  void timerCallback() override;
  std::optional<juce::WebBrowserComponent::Resource> provideResource(const juce::String& url);
  void chooseFile(const juce::String& kind, juce::WebBrowserComponent::NativeFunctionCompletion done);

  app::Controller& controller_;
  std::unique_ptr<juce::WebBrowserComponent> browser_;
  std::unique_ptr<juce::FileChooser> chooser_;
  std::uint64_t lastVersion_ = 0;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(WebShell)
};

}  // namespace s7::desktop
