// seven7 — desktop application entry point.
//
//   JUCEApplication ─ owns Controller (project + undo + engine session)
//        ├─ AudioHost   (AudioDeviceManager → Session::process on the RT thread)
//        └─ MainWindow  (DocumentWindow) ─ WebShell (React UI in WebBrowserComponent)
//
// Native menus/commands stay thin: they forward to the UI as "s7Menu" events so
// the exact same command code (ui/src/lib/commands.ts) runs whether the user
// clicks a toolbar button, presses a shortcut, or uses the macOS menu bar.
// The web view owns keyboard focus, so shortcuts are handled in ui/src/lib/keys.ts;
// the native key mappings only matter while a native dialog has focus.

#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_gui_extra/juce_gui_extra.h>

#include "AudioHost.h"
#include "WebShell.h"
#include "app/controller.h"

namespace s7::desktop {

enum MenuCommands : juce::CommandID {
  cmdNewProject = 0x5701,
  cmdOpenProject,
  cmdSaveProject,
  cmdSaveProjectAs,
  cmdImportAudio,
  cmdUndo,
  cmdRedo,
  cmdToggleMode,
  cmdAudioSettings,
};

class MainWindow final : public juce::DocumentWindow {
public:
  MainWindow(const juce::String& name, app::Controller& controller)
      : juce::DocumentWindow(name, juce::Colour(0xff17181c), juce::DocumentWindow::allButtons) {
    setUsingNativeTitleBar(true);
    shell_ = std::make_unique<WebShell>(controller);
    setContentNonOwned(shell_.get(), false);  // window shows the shell; shell_ keeps ownership
    setResizable(true, false);
    setResizeLimits(1024, 640, 10000, 10000);
    centreWithSize(1440, 900);
    setVisible(true);
  }

  ~MainWindow() override { clearContentComponent(); }  // detach before shell_ is destroyed

  WebShell& shell() { return *shell_; }

  void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }

private:
  std::unique_ptr<WebShell> shell_;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
};

// JUCEApplication is already an ApplicationCommandTarget (it handles quit).
class Seven7Application final : public juce::JUCEApplication, public juce::MenuBarModel {
public:
  const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
  const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
  bool moreThanOneInstanceAllowed() override { return false; }

  void initialise(const juce::String& commandLine) override {
    controller_ = std::make_unique<app::Controller>();
    controller_->on_log = [this](const std::string& line) {
      juce::Logger::writeToLog(line);
      if (window_) window_->shell().pushLog(juce::String(line));
    };

    // Engine first (prepare happens when the device reports its rate/block),
    // then the project, then the UI — the UI's first state fetch sees a full project.
    audio_ = std::make_unique<AudioHost>(*controller_);
    audio_->onDeviceRestarted = [this](double sr, int block) {
      if (controller_->on_log) controller_->on_log("audio: " + audio_->deviceName().toStdString() + " @ " + std::to_string(static_cast<int>(sr)) + " Hz / " + std::to_string(block) + " smp");
    };
    const juce::String audioError = audio_->start(2, 2);
    if (audioError.isNotEmpty()) {
      juce::Logger::writeToLog("audio: " + audioError + " (running without a device)");
      controller_->prepare(48000, 256);
    }

    const juce::File toOpen = fileFromCommandLine(commandLine);
    if (toOpen != juce::File()) openBundle(toOpen);
    else controller_->new_demo_project();

    commands_.registerAllCommandsForTarget(this);
    commands_.setFirstCommandTarget(this);
    window_ = std::make_unique<MainWindow>(getApplicationName(), *controller_);
    window_->shell().onOpenAudioSettings = [this] { showAudioSettings(); };
    window_->addKeyListener(commands_.getKeyMappings());

#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(this);
#else
    window_->setMenuBar(this);
#endif
  }

  void shutdown() override {
#if JUCE_MAC
    juce::MenuBarModel::setMacMainMenu(nullptr);
#else
    if (window_) window_->setMenuBar(nullptr);
#endif
    window_.reset();
    audio_.reset();
    controller_.reset();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String& commandLine) override {
    const juce::File f = fileFromCommandLine(commandLine);
    if (f != juce::File()) {
      openBundle(f);
      if (window_) window_->shell().notifyStateChanged();
    }
  }

  // ── MenuBarModel ──────────────────────────────────────────────────────────
  juce::StringArray getMenuBarNames() override { return {"File", "Edit", "View", "Options"}; }

  juce::PopupMenu getMenuForIndex(int, const juce::String& name) override {
    juce::PopupMenu m;
    if (name == "File") {
      m.addCommandItem(&commands_, cmdNewProject);
      m.addCommandItem(&commands_, cmdOpenProject);
      m.addSeparator();
      m.addCommandItem(&commands_, cmdSaveProject);
      m.addCommandItem(&commands_, cmdSaveProjectAs);
      m.addSeparator();
      m.addCommandItem(&commands_, cmdImportAudio);
#if !JUCE_MAC
      m.addSeparator();
      m.addCommandItem(&commands_, juce::StandardApplicationCommandIDs::quit);
#endif
    } else if (name == "Edit") {
      m.addCommandItem(&commands_, cmdUndo);
      m.addCommandItem(&commands_, cmdRedo);
    } else if (name == "View") {
      m.addCommandItem(&commands_, cmdToggleMode);
    } else if (name == "Options") {
      m.addCommandItem(&commands_, cmdAudioSettings);
    }
    return m;
  }

  void menuItemSelected(int, int) override {}

  // ── ApplicationCommandTarget (via JUCEApplication) ────────────────────────
  void getAllCommands(juce::Array<juce::CommandID>& c) override {
    juce::JUCEApplication::getAllCommands(c);  // keeps StandardApplicationCommandIDs::quit
    c.addArray({cmdNewProject, cmdOpenProject, cmdSaveProject, cmdSaveProjectAs, cmdImportAudio, cmdUndo, cmdRedo, cmdToggleMode, cmdAudioSettings});
  }

  void getCommandInfo(juce::CommandID id, juce::ApplicationCommandInfo& r) override {
    using juce::ModifierKeys;
    const int cmd = ModifierKeys::commandModifier;
    switch (id) {
      case cmdNewProject: r.setInfo("New Project…", "Start a new project", "File", 0); r.addDefaultKeypress('n', cmd); break;
      case cmdOpenProject: r.setInfo("Open…", "Open a .s7proj bundle", "File", 0); r.addDefaultKeypress('o', cmd); break;
      case cmdSaveProject: r.setInfo("Save", "Save the project", "File", 0); r.addDefaultKeypress('s', cmd); break;
      case cmdSaveProjectAs: r.setInfo("Save As…", "Save the project to a new bundle", "File", 0); r.addDefaultKeypress('s', cmd | ModifierKeys::shiftModifier); break;
      case cmdImportAudio: r.setInfo("Import Audio…", "Import a WAV/BWF file", "File", 0); r.addDefaultKeypress('i', cmd | ModifierKeys::shiftModifier); break;
      case cmdUndo: r.setInfo("Undo", "Undo the last edit", "Edit", 0); r.addDefaultKeypress('z', cmd); break;
      case cmdRedo: r.setInfo("Redo", "Redo", "Edit", 0); r.addDefaultKeypress('z', cmd | ModifierKeys::shiftModifier); break;
      case cmdToggleMode: r.setInfo("Toggle Canvas / Precision", "Switch workspace mode", "View", 0); r.addDefaultKeypress('m', cmd | ModifierKeys::ctrlModifier); break;
      case cmdAudioSettings: r.setInfo("Audio Settings…", "Choose the audio device", "Options", 0); r.addDefaultKeypress(',', cmd); break;
      default: juce::JUCEApplication::getCommandInfo(id, r); break;
    }
  }

  bool perform(const InvocationInfo& info) override {
    // Everything except the native audio dialog is forwarded to the UI so the
    // web layer stays the single source of truth for command behaviour.
    switch (info.commandID) {
      case cmdNewProject: forwardToUI("newProject"); return true;
      case cmdOpenProject: forwardToUI("openProject"); return true;
      case cmdSaveProject: forwardToUI("saveProject"); return true;
      case cmdSaveProjectAs: forwardToUI("saveProjectAs"); return true;
      case cmdImportAudio: forwardToUI("importAudio"); return true;
      case cmdUndo: forwardToUI("undo"); return true;
      case cmdRedo: forwardToUI("redo"); return true;
      case cmdToggleMode: forwardToUI("toggleMode"); return true;
      case cmdAudioSettings: showAudioSettings(); return true;
      default: return juce::JUCEApplication::perform(info);
    }
  }

private:
  static juce::File fileFromCommandLine(const juce::String& commandLine) {
    juce::StringArray tokens;
    tokens.addTokens(commandLine, true);
    tokens.removeEmptyStrings();
    for (int i = 0; i < tokens.size(); ++i) {
      juce::String t = tokens[i].unquoted();
      if (t == "--open" && i + 1 < tokens.size()) t = tokens[i + 1].unquoted();
      if (t.startsWith("-")) continue;
      juce::File f(t);
      if (f.getFileName() == "manifest.json" || f.getFileName() == "project.json") f = f.getParentDirectory();
      if (f.hasFileExtension("s7proj") && f.isDirectory()) return f;
    }
    return {};
  }

  void openBundle(const juce::File& f) {
    std::string err;
    if (!controller_->load_bundle(f.getFullPathName().toStdString(), &err)) {
      juce::Logger::writeToLog("open: " + err);
      controller_->new_demo_project();
    }
  }

  void forwardToUI(const juce::String& action) {
    if (window_) window_->shell().sendMenuAction(action);
  }

  void showAudioSettings() {
    if (!audio_) return;
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(audio_->deviceManager(), 0, 32, 2, 32, true, false, true, false);
    selector->setSize(560, 520);
    juce::DialogWindow::LaunchOptions o;
    o.content.setOwned(selector.release());
    o.dialogTitle = "Audio Settings";
    o.dialogBackgroundColour = juce::Colour(0xff1e2026);
    o.escapeKeyTriggersCloseButton = true;
    o.useNativeTitleBar = true;
    o.resizable = false;
    o.launchAsync();
  }

  std::unique_ptr<app::Controller> controller_;
  std::unique_ptr<AudioHost> audio_;
  std::unique_ptr<MainWindow> window_;
  juce::ApplicationCommandManager commands_;
};

}  // namespace s7::desktop

START_JUCE_APPLICATION(s7::desktop::Seven7Application)
