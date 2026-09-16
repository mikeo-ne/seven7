#include "WebShell.h"

#include <S7UIData.h>

#include <cstring>
#include <unordered_map>

namespace s7::desktop {

namespace {

const char* mimeForExtension(const juce::String& ext) {
  static const std::unordered_map<juce::String, const char*> kMime = {
      {"html", "text/html"},       {"htm", "text/html"},       {"js", "text/javascript"},
      {"mjs", "text/javascript"},  {"css", "text/css"},        {"json", "application/json"},
      {"map", "application/json"}, {"svg", "image/svg+xml"},   {"png", "image/png"},
      {"jpg", "image/jpeg"},       {"jpeg", "image/jpeg"},     {"ico", "image/vnd.microsoft.icon"},
      {"woff", "font/woff"},       {"woff2", "font/woff2"},    {"ttf", "font/ttf"},
      {"wasm", "application/wasm"}, {"txt", "text/plain"},
  };
  const auto it = kMime.find(ext.toLowerCase());
  return it != kMime.end() ? it->second : "application/octet-stream";
}

std::vector<std::byte> toBytes(const char* data, int size) {
  std::vector<std::byte> out(static_cast<std::size_t>(size));
  if (size > 0) std::memcpy(out.data(), data, static_cast<std::size_t>(size));
  return out;
}

juce::String devServerUrl() {
#ifdef S7_UI_DEV_SERVER
  return juce::String(S7_UI_DEV_SERVER).trim();
#else
  return {};
#endif
}

struct SinglePageBrowser final : juce::WebBrowserComponent {
  using juce::WebBrowserComponent::WebBrowserComponent;
  juce::String allowed;
  // Keep the app single-page: refuse navigation away from our origin (external
  // links are opened in the system browser instead).
  bool pageAboutToLoad(const juce::String& newURL) override {
    if (newURL.startsWith(getResourceProviderRoot()) || (allowed.isNotEmpty() && newURL.startsWith(allowed))) return true;
    if (newURL.startsWith("http")) juce::URL(newURL).launchInDefaultBrowser();
    return false;
  }
};

}  // namespace

WebShell::WebShell(app::Controller& controller) : controller_(controller) {
  using juce::WebBrowserComponent;
  using juce::var;

  const juce::String dev = devServerUrl();

  auto options =
      WebBrowserComponent::Options{}
          .withBackend(WebBrowserComponent::Options::Backend::webview2)
          .withWinWebView2Options(WebBrowserComponent::Options::WinWebView2{}.withUserDataFolder(
              juce::File::getSpecialLocation(juce::File::tempDirectory).getChildFile("seven7-webview2")))
          .withKeepPageLoadedWhenBrowserIsHidden()
          .withNativeIntegrationEnabled()
          .withInitialisationData("s7Version", var(JUCE_APPLICATION_VERSION_STRING))
          .withInitialisationData("s7Platform", var(juce::SystemStats::getOperatingSystemName()))
          .withNativeFunction("s7Command",
                              [this](const juce::Array<var>& args, WebBrowserComponent::NativeFunctionCompletion done) {
                                const juce::String text = args.size() > 0 ? args[0].toString() : juce::String("{}");
                                done(var(runCommand(text)));
                              })
          .withNativeFunction("s7State",
                              [this](const juce::Array<var>&, WebBrowserComponent::NativeFunctionCompletion done) {
                                done(var(juce::String(controller_.state_json().dump())));
                              })
          .withNativeFunction("s7Status",
                              [this](const juce::Array<var>&, WebBrowserComponent::NativeFunctionCompletion done) {
                                done(var(juce::String(controller_.status_json().dump())));
                              })
          .withNativeFunction("s7Peaks",
                              [this](const juce::Array<var>& a, WebBrowserComponent::NativeFunctionCompletion done) {
                                const auto source = a.size() > 0 ? static_cast<s7::domain::Id>(static_cast<juce::int64>(a[0])) : 0;
                                const auto from = a.size() > 1 ? static_cast<std::int64_t>(static_cast<juce::int64>(a[1])) : 0;
                                const auto to = a.size() > 2 ? static_cast<std::int64_t>(static_cast<juce::int64>(a[2])) : 0;
                                const auto buckets = a.size() > 3 ? static_cast<int>(a[3]) : 0;
                                done(var(juce::String(controller_.peaks_json(source, from, to, buckets).dump())));
                              })
          .withNativeFunction("s7ChooseFile",
                              [this](const juce::Array<var>& a, WebBrowserComponent::NativeFunctionCompletion done) {
                                chooseFile(a.size() > 0 ? a[0].toString() : juce::String("open-project"), std::move(done));
                              })
          .withNativeFunction("s7AudioSettings",
                              [this](const juce::Array<var>&, WebBrowserComponent::NativeFunctionCompletion done) {
                                if (onOpenAudioSettings) onOpenAudioSettings();
                                done(var(true));
                              })
          .withResourceProvider([this](const auto& url) { return provideResource(url); },
                                dev.isNotEmpty() ? std::optional<juce::String>(juce::URL(dev).getOrigin()) : std::nullopt);

  auto browser = std::make_unique<SinglePageBrowser>(options);
  browser->allowed = dev;
  browser_ = std::move(browser);
  addAndMakeVisible(*browser_);

  browser_->goToURL(dev.isNotEmpty() ? dev : WebBrowserComponent::getResourceProviderRoot());
  lastVersion_ = controller_.state_version();
  startTimerHz(30);
}

WebShell::~WebShell() { stopTimer(); }

void WebShell::resized() { browser_->setBounds(getLocalBounds()); }

void WebShell::paint(juce::Graphics& g) { g.fillAll(juce::Colour(0xff17181c)); }

juce::String WebShell::runCommand(const juce::String& jsonText) {
  const auto result = controller_.command_text(jsonText.toStdString());
  // Structural edits bump the version; the 30 Hz status push carries it, but
  // announce immediately so the UI never renders a stale region list.
  if (controller_.state_version() != lastVersion_) notifyStateChanged();
  return juce::String(result.dump());
}

void WebShell::notifyStateChanged() {
  lastVersion_ = controller_.state_version();
  browser_->emitEventIfBrowserIsVisible("s7StateChanged", juce::var(static_cast<juce::int64>(lastVersion_)));
}

void WebShell::pushLog(const juce::String& line) { browser_->emitEventIfBrowserIsVisible("s7Log", juce::var(line)); }

void WebShell::sendMenuAction(const juce::String& action) { browser_->emitEventIfBrowserIsVisible("s7Menu", juce::var(action)); }

void WebShell::timerCallback() {
  // Service finished takes (ARC-RT-01: recordings become regions on the message thread).
  if (controller_.tick() || controller_.state_version() != lastVersion_) notifyStateChanged();
  browser_->emitEventIfBrowserIsVisible("s7Status", juce::var(juce::String(controller_.status_json().dump())));
}

std::optional<juce::WebBrowserComponent::Resource> WebShell::provideResource(const juce::String& url) {
  // "/" → index.html; "/assets/index-abc.js" → the binary-data entry whose
  // original filename matches the last path segment (Vite hashes are unique).
  const juce::String path = url == "/" ? juce::String("index.html") : url.fromFirstOccurrenceOf("/", false, false);
  const juce::String file = path.fromLastOccurrenceOf("/", false, false);

  for (int i = 0; i < S7UIData::namedResourceListSize; ++i) {
    const char* name = S7UIData::namedResourceList[i];
    if (juce::String(S7UIData::getNamedResourceOriginalFilename(name)) != file) continue;
    int size = 0;
    const char* data = S7UIData::getNamedResource(name, size);
    if (data == nullptr) break;
    return juce::WebBrowserComponent::Resource{toBytes(data, size), juce::String(mimeForExtension(file.fromLastOccurrenceOf(".", false, false)))};
  }
  // SPA fallback: unknown paths get index.html so client-side routes work.
  if (!file.contains(".")) return provideResource("/");
  return std::nullopt;
}

void WebShell::chooseFile(const juce::String& kind, juce::WebBrowserComponent::NativeFunctionCompletion done) {
  using juce::FileBrowserComponent;
  int flags = 0;
  juce::String title;
  juce::String pattern;
  juce::File start = juce::File::getSpecialLocation(juce::File::userMusicDirectory);

  if (kind == "save-project") {
    flags = FileBrowserComponent::saveMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectFiles |
            FileBrowserComponent::warnAboutOverwriting;
    title = "Save seven7 project";
    pattern = "*.s7proj";
  } else if (kind == "import-audio") {
    flags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles;
    title = "Import audio";
    pattern = "*.wav;*.wave;*.bwf;*.aif;*.aiff";
  } else {
    // .s7proj is a directory bundle — allow picking the folder (or its manifest).
    flags = FileBrowserComponent::openMode | FileBrowserComponent::canSelectDirectories | FileBrowserComponent::canSelectFiles;
    title = "Open seven7 project";
    pattern = "*.s7proj;*.json";
  }

  chooser_ = std::make_unique<juce::FileChooser>(title, start, pattern);
  auto completion = std::make_shared<juce::WebBrowserComponent::NativeFunctionCompletion>(std::move(done));
  chooser_->launchAsync(flags, [completion, kind](const juce::FileChooser& fc) {
    juce::File f = fc.getResult();
    if (f == juce::File()) { (*completion)(juce::var("")); return; }
    if (kind != "import-audio") {
      if (f.getFileName() == "manifest.json" || f.getFileName() == "project.json") f = f.getParentDirectory();
      if (kind == "save-project" && !f.hasFileExtension("s7proj")) f = f.withFileExtension("s7proj");
    }
    (*completion)(juce::var(f.getFullPathName()));
  });
}

}  // namespace s7::desktop
