#include <QJSEngine>
#include <QJSValue>
#include <QCoreApplication>
#include <QString>

#include <cctype>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

std::string read_text(const std::filesystem::path& path) {
  std::ifstream input(path);
  if (!input) {
    throw std::runtime_error("unable to read " + path.string());
  }
  std::ostringstream contents;
  contents << input.rdbuf();
  return contents.str();
}

std::string extract_function(const std::string& source,
                             const std::string& function_name) {
  const auto signature = "function " + function_name + "(";
  const auto start = source.find(signature);
  if (start == std::string::npos) {
    throw std::runtime_error("missing QML helper " + function_name);
  }
  const auto opening = source.find('{', start + signature.size());
  if (opening == std::string::npos) {
    throw std::runtime_error("missing body for QML helper " + function_name);
  }

  enum class LexicalState { kCode, kSingleQuoted, kDoubleQuoted, kTemplate,
                            kLineComment, kBlockComment, kRegularExpression };
  auto state = LexicalState::kCode;
  bool escaped = false;
  std::size_t depth = 0;
  for (std::size_t index = opening; index < source.size(); ++index) {
    const char current = source[index];
    const char next = index + 1 < source.size() ? source[index + 1] : '\0';

    if (state == LexicalState::kLineComment) {
      if (current == '\n') state = LexicalState::kCode;
      continue;
    }
    if (state == LexicalState::kBlockComment) {
      if (current == '*' && next == '/') {
        state = LexicalState::kCode;
        ++index;
      }
      continue;
    }
    if (state == LexicalState::kSingleQuoted ||
        state == LexicalState::kDoubleQuoted ||
        state == LexicalState::kTemplate) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (current == '\\') {
        escaped = true;
        continue;
      }
      if ((state == LexicalState::kSingleQuoted && current == '\'') ||
          (state == LexicalState::kDoubleQuoted && current == '"') ||
          (state == LexicalState::kTemplate && current == '`')) {
        state = LexicalState::kCode;
      }
      continue;
    }
    if (state == LexicalState::kRegularExpression) {
      if (escaped) {
        escaped = false;
        continue;
      }
      if (current == '\\') {
        escaped = true;
        continue;
      }
      if (current == '/') state = LexicalState::kCode;
      continue;
    }

    if (current == '/' && next == '/') {
      state = LexicalState::kLineComment;
      ++index;
      continue;
    }
    if (current == '/') {
      auto previous = index;
      while (previous > opening &&
             std::isspace(static_cast<unsigned char>(source[previous - 1]))) {
        --previous;
      }
      const char previous_character =
          previous > opening ? source[previous - 1] : '\0';
      if (previous_character == '(' || previous_character == '[' ||
          previous_character == '{' || previous_character == ',' ||
          previous_character == ':' || previous_character == '=' ||
          previous_character == '!' || previous_character == '?' ||
          previous_character == ';') {
        state = LexicalState::kRegularExpression;
        continue;
      }
    }
    if (current == '/' && next == '*') {
      state = LexicalState::kBlockComment;
      ++index;
      continue;
    }
    if (current == '\'') {
      state = LexicalState::kSingleQuoted;
      continue;
    }
    if (current == '"') {
      state = LexicalState::kDoubleQuoted;
      continue;
    }
    if (current == '`') {
      state = LexicalState::kTemplate;
      continue;
    }
    if (current == '{') ++depth;
    if (current == '}' && --depth == 0) {
      return source.substr(start, index - start + 1);
    }
  }
  throw std::runtime_error("unterminated QML helper " + function_name);
}

void evaluate(QJSEngine& engine, const std::string& source,
              const std::string& label) {
  const QJSValue result = engine.evaluate(QString::fromStdString(source),
                                          QString::fromStdString(label));
  if (result.isError()) {
    throw std::runtime_error(label + ": " + result.toString().toStdString());
  }
}

void require(bool condition, const std::string& message) {
  if (!condition) throw std::runtime_error(message);
}

int fail(const std::string& message) {
  std::cerr << "timeline workspace lifecycle behavior test failed: " << message
            << '\n';
  return 1;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  (void)application;
  try {
    const std::filesystem::path root(PHOTO_EDITOR_SOURCE_DIR);
    const auto main_qml = read_text(root / "apps/desktop/qml/Main.qml");
    const auto timeline_qml =
        read_text(root / "apps/desktop/qml/workspaces/TimelineView.qml");
    const auto source_panel =
        read_text(root / "apps/desktop/qml/components/TimelineSourcePanel.qml");
    const auto color_panel =
        read_text(root / "apps/desktop/qml/components/TimelineColorConsole.qml");
    const auto audio_panel =
        read_text(root / "apps/desktop/qml/components/TimelineAudioMixer.qml");
    const auto editorial_panel =
        read_text(root / "apps/desktop/qml/components/TimelineEditorialPanel.qml");

    QJSEngine engine;
    evaluate(engine,
             "var timelineWorkspaceIndex = 3;"
             "var editWorkspaceIndex = 1;"
             "var currentWorkspace = 3;"
             "var candidateWorkspace = null;"
             "function activeWorkspaceItem() { return candidateWorkspace; }",
             "route test setup");
    evaluate(engine, extract_function(main_qml, "activeTimelineView"),
             "Main.qml activeTimelineView");
    evaluate(engine, extract_function(main_qml, "timelineHasActiveRenderJobs"),
             "Main.qml timelineHasActiveRenderJobs");

    const auto global = engine.globalObject();
    evaluate(engine,
             "candidateWorkspace = {objectName: 'editStudioWorkspace', "
             "hasActiveTimelineRenderJobs: function() { return true; }};",
             "stale edit setup");
    require(global.property("activeTimelineView").call().isNull(),
            "a stale Edit workspace must not be exposed as a Timeline view");
    require(!global.property("timelineHasActiveRenderJobs").call().toBool(),
            "render availability must remain safe while the Timeline loader is stale");

    evaluate(engine,
             "candidateWorkspace = {objectName: 'timelineWorkspace', "
             "hasActiveTimelineRenderJobs: function() { return true; }};",
             "active timeline setup");
    const auto active_view = global.property("activeTimelineView").call();
    require(active_view.property("objectName").toString() ==
                QStringLiteral("timelineWorkspace"),
            "the active Timeline workspace must remain available");
    require(global.property("timelineHasActiveRenderJobs").call().toBool(),
            "active Timeline render jobs must still disable duplicate rendering");

    evaluate(engine,
             "candidateWorkspace = {objectName: 'timelineSetupWorkspace', "
             "timelineSetupOnly: true};",
             "timeline setup page");
    require(global.property("activeTimelineView").call().isNull(),
            "the Timeline setup page must not expose editing-only actions");

    evaluate(engine,
             "var workspaceTransitionActive = true;"
             "var pendingWorkspaceIndex = 3;"
             "var timelineWorkspaceIndex = 3;"
             "var editWorkspaceIndex = 1;"
             "var routeReadyCalls = 0;"
             "var finishCalls = 0;"
             "var routeWorkspace = {pageContentReady: false};"
             "function loadedWorkspaceItem() { return routeWorkspace; }"
             "function workspaceLoaded() { return true; }"
             "function logQmlRouteReady() { ++routeReadyCalls; }"
             "function finishShellTransitionSoon() { ++finishCalls; }",
             "readiness test setup");
    evaluate(engine, extract_function(main_qml, "maybeFinishWorkspaceTransition"),
             "Main.qml maybeFinishWorkspaceTransition");
    global.property("maybeFinishWorkspaceTransition").call(
        QJSValueList{QJSValue(3)});
    require(global.property("routeReadyCalls").toInt() == 0 &&
                global.property("finishCalls").toInt() == 0,
            "route readiness must wait while the inner Timeline page is loading");
    evaluate(engine, "routeWorkspace.pageContentReady = true;",
             "Timeline page ready");
    global.property("maybeFinishWorkspaceTransition").call(
        QJSValueList{QJSValue(3)});
    require(global.property("routeReadyCalls").toInt() == 1 &&
                global.property("finishCalls").toInt() == 1,
            "route readiness must resume when the inner Timeline page finishes");

    evaluate(engine, extract_function(timeline_qml, "timelineClipWaveformVisible"),
             "TimelineView.qml timelineClipWaveformVisible");
    const auto waveform_visible = global.property("timelineClipWaveformVisible");
    require(waveform_visible.call(QJSValueList{}).isBool() &&
                !waveform_visible.call(QJSValueList{}).toBool(),
            "missing clip metadata must produce a valid false waveform visibility value");
    const auto audio_clip = engine.evaluate("({hasAudio: true})");
    require(waveform_visible.call(QJSValueList{audio_clip}).isBool() &&
                waveform_visible.call(QJSValueList{audio_clip}).toBool(),
            "an audio clip must keep its waveform visible");
    const auto silent_clip = engine.evaluate("({hasAudio: true, silentPlaceholder: true})");
    require(!waveform_visible.call(QJSValueList{silent_clip}).toBool(),
            "silent placeholders must not show a waveform");

    evaluate(engine, "var theme = {fontUi: 'System UI'};",
             "font fallback setup");
    evaluate(engine, extract_function(timeline_qml, "resolvedTimelineFontFamily"),
             "TimelineView.qml resolvedTimelineFontFamily");
    const auto resolve_font = global.property("resolvedTimelineFontFamily");
    require(resolve_font.call(QJSValueList{QJSValue(QStringLiteral("Sans Serif"))}).toString() ==
                QStringLiteral("System UI") &&
                resolve_font.call(QJSValueList{QJSValue(QStringLiteral("Inter"))}).toString() ==
                QStringLiteral("Inter"),
            "the generic font alias must fall back without overriding a named font");

    evaluate(engine, extract_function(timeline_qml, "escapedCaptionText"),
             "TimelineView.qml escapedCaptionText");
    evaluate(engine, extract_function(timeline_qml, "captionAuthoredLineBreakIndices"),
             "TimelineView.qml captionAuthoredLineBreakIndices");
    evaluate(engine, extract_function(timeline_qml, "captionCasedText"),
             "TimelineView.qml captionCasedText");
    evaluate(engine, extract_function(timeline_qml, "captionVisibleWordIndices"),
             "TimelineView.qml captionVisibleWordIndices");
    evaluate(engine, extract_function(timeline_qml, "captionEffectiveEmphasisMode"),
             "TimelineView.qml captionEffectiveEmphasisMode");
    evaluate(engine, extract_function(timeline_qml, "programCaptionPlainText"),
             "TimelineView.qml programCaptionPlainText");
    evaluate(engine, extract_function(timeline_qml, "programCaptionRichText"),
             "TimelineView.qml programCaptionRichText");
    evaluate(engine,
             "var root = {escapedCaptionText: escapedCaptionText, "
             "captionCasedText: captionCasedText, "
             "captionVisibleWordIndices: captionVisibleWordIndices, "
             "captionAuthoredLineBreakIndices: captionAuthoredLineBreakIndices, "
             "captionEffectiveEmphasisMode: captionEffectiveEmphasisMode};"
             "var authoredCaption = {titleText: 'One two\\r\\nThree', "
             "captionWords: [{text: 'One'}, {text: 'two'}, {text: 'Three'}]};"
             "var richCaption = {titleText: 'Top & <bold>\\r\\nSecond', "
             "captionWords: [], captionCasing: 'none'};"
             "var plainCaption = {titleText: 'One two\\r\\nThree', "
             "captionCasing: 'none', startSeconds: 0, trimmedDurationSeconds: 3, "
             "captionWords: [{text: 'One', startSeconds: 0, endSeconds: 1}, "
             "{text: 'two', startSeconds: 1, endSeconds: 2}, "
             "{text: 'Three', startSeconds: 2, endSeconds: 3}]};",
             "caption helper setup");
    const auto escape_caption = global.property("escapedCaptionText");
    require(escape_caption.call(QJSValueList{
                QJSValue(QStringLiteral("A < B & \"C\""))}).toString() ==
                QStringLiteral("A &lt; B &amp; &quot;C&quot;"),
            "caption text must escape markup and quotes before entering rich text");
    const auto authored_breaks = global.property("captionAuthoredLineBreakIndices")
            .call(QJSValueList{global.property("authoredCaption")});
    require(authored_breaks.isArray() && authored_breaks.property(0).toInt() == 2,
            "caption authored CRLF breaks must map to the matching timed-word boundary");
    require(global.property("captionCasedText")
                    .call(QJSValueList{QJSValue(QStringLiteral("hello world")),
                                       QJSValue(QStringLiteral("title"))}).toString() ==
                QStringLiteral("Hello World"),
            "caption title casing must continue to capitalize word boundaries");
    require(global.property("programCaptionRichText")
                    .call(QJSValueList{global.property("richCaption")}).toString() ==
                QStringLiteral("Top &amp; &lt;bold&gt;<br/>Second"),
            "caption rendering must escape markup and preserve authored line breaks");
    require(global.property("programCaptionPlainText")
                    .call(QJSValueList{global.property("plainCaption")}).toString() ==
                QStringLiteral("One two\nThree"),
            "plain caption rendering must emit an actual newline at authored breaks");

    evaluate(engine,
             "var retainedPanelSessionState = ({});"
             "var retainedCaptionDraftByClipId = ({});",
             "panel session test setup");
    evaluate(engine, extract_function(timeline_qml, "rememberPanelSession"),
             "TimelineView.qml rememberPanelSession");
    evaluate(engine, extract_function(timeline_qml, "restorePanelSession"),
             "TimelineView.qml restorePanelSession");
    evaluate(engine, extract_function(editorial_panel, "rememberCaptionDraft"),
             "TimelineEditorialPanel.qml rememberCaptionDraft");
    evaluate(engine, extract_function(editorial_panel, "captionDraftForClip"),
             "TimelineEditorialPanel.qml captionDraftForClip");
    evaluate(engine, extract_function(editorial_panel, "clearCaptionDraft"),
             "TimelineEditorialPanel.qml clearCaptionDraft");
    evaluate(engine, extract_function(source_panel, "suspendForRelease"),
             "TimelineSourcePanel.qml suspendForRelease");
    const auto suspend_source_panel = global.property("suspendForRelease");
    evaluate(engine, extract_function(color_panel, "suspendForRelease"),
             "TimelineColorConsole.qml suspendForRelease");
    const auto suspend_color_panel = global.property("suspendForRelease");
    evaluate(engine, extract_function(color_panel, "endContinuousEdit"),
             "TimelineColorConsole.qml endContinuousEdit");
    evaluate(engine, extract_function(audio_panel, "suspendForRelease"),
             "TimelineAudioMixer.qml suspendForRelease");
    const auto suspend_audio_panel = global.property("suspendForRelease");
    evaluate(engine, extract_function(audio_panel, "stopVoiceoverPlayback"),
             "TimelineAudioMixer.qml stopVoiceoverPlayback");
    const auto caption = engine.evaluate("({clipId: 'caption-17', titleText: 'Original'})");
    global.property("rememberCaptionDraft").call(
        QJSValueList{caption, QJSValue(QStringLiteral("unfinished caption text"))});
    evaluate(engine,
             "var authoredPanel = {retainedCaptionDraftByClipId: "
             "retainedCaptionDraftByClipId};"
             "rememberPanelSession('captions', authoredPanel);"
             "retainedCaptionDraftByClipId = ({});"
             "var restoredPanel = {retainedCaptionDraftByClipId: ({})};"
             "restorePanelSession('captions', restoredPanel);"
             "retainedCaptionDraftByClipId = restoredPanel.retainedCaptionDraftByClipId;",
             "caption panel snapshot and reload");
    require(global.property("captionDraftForClip").call(QJSValueList{caption}).toString() ==
                QStringLiteral("unfinished caption text"),
            "an unfinished caption edit must survive panel snapshot and reload");
    global.property("clearCaptionDraft").call(
        QJSValueList{QJSValue(QStringLiteral("caption-17"))});
    require(global.property("captionDraftForClip").call(QJSValueList{caption}).toString() ==
                QStringLiteral("Original"),
            "a committed caption must return to controller-owned text");

    evaluate(engine,
             "var mediaPanel = {takeComparisonAssets: [{assetId: 'take-8'}], "
             "takeComparisonPlaying: true, mediaUsageFilter: 'duplicate', "
             "mediaUsageCanonicalAssetId: 'asset-2'};"
             "rememberPanelSession('source', mediaPanel);"
             "var restoredMediaPanel = {takeComparisonAssets: [], "
             "takeComparisonPlaying: false, mediaUsageFilter: 'all', "
             "mediaUsageCanonicalAssetId: ''};"
             "restorePanelSession('source', restoredMediaPanel);",
             "source panel state snapshot and reload");
    require(global.property("restoredMediaPanel").property("takeComparisonAssets")
                    .property(static_cast<quint32>(0)).property("assetId").toString() ==
                    QStringLiteral("take-8") &&
                global.property("restoredMediaPanel").property("mediaUsageFilter").toString() ==
                    QStringLiteral("duplicate") &&
                global.property("restoredMediaPanel")
                        .property("mediaUsageCanonicalAssetId").toString() ==
                    QStringLiteral("asset-2"),
            "media selection and usage filters must survive source panel unload");

    evaluate(engine,
             "var effectPanel = {selectedEffectPresetPreview: {found: true, "
             "presetId: 'preset-3'}, presetEffectSelection: {Exposure: true}, "
             "effectPresetNameDraft: 'Night look'};"
             "rememberPanelSession('effects', effectPanel);"
             "var restoredEffectPanel = {selectedEffectPresetPreview: {}, "
             "presetEffectSelection: {}, effectPresetNameDraft: ''};"
             "restorePanelSession('effects', restoredEffectPanel);",
             "effects panel state snapshot and reload");
    require(global.property("restoredEffectPanel")
                    .property("selectedEffectPresetPreview").property("presetId").toString() ==
                QStringLiteral("preset-3") &&
                global.property("restoredEffectPanel")
                        .property("presetEffectSelection").property("Exposure").toBool() &&
                global.property("restoredEffectPanel")
                        .property("effectPresetNameDraft").toString() ==
                    QStringLiteral("Night look"),
            "an in-progress effect preset choice must survive inspector unload");

    evaluate(engine,
             "var sourceTapeAutoAdvance = true;"
             "var takeComparisonStopCount = 0;"
             "var sourceMonitorPauseCount = 0;"
             "function setTakeComparisonPlaying(value) {"
             "if (value === false) ++takeComparisonStopCount; }"
             "function pauseSourceMonitor() { ++sourceMonitorPauseCount; }"
             "var continuousEditOpen = true;"
             "var continuousEditEndCount = 0;"
             "var timelineDocument = {endContinuousEdit: function() {"
             "++continuousEditEndCount; }};"
             "var voiceoverPlaybackStartedByPanel = true;"
             "var pausePlaybackCount = 0;"
             "var timelinePlayback = {pause: function() { ++pausePlaybackCount; }};",
             "panel suspension test setup");
    suspend_source_panel.call(QJSValueList{});
    require(!global.property("sourceTapeAutoAdvance").toBool() &&
                global.property("takeComparisonStopCount").toInt() == 1 &&
                global.property("sourceMonitorPauseCount").toInt() == 1,
            "hidden source playback must stop before the source panel unloads");
    suspend_color_panel.call(QJSValueList{});
    require(!global.property("continuousEditOpen").toBool() &&
                global.property("continuousEditEndCount").toInt() == 1,
            "a hidden color panel must close its active undo group before unload");
    suspend_audio_panel.call(QJSValueList{});
    require(!global.property("voiceoverPlaybackStartedByPanel").toBool() &&
                global.property("pausePlaybackCount").toInt() == 1,
            "audio playback started by a hidden mixer must stop before unload");

    std::cout << "timeline_workspace_lifecycle_behavior: stale route, Timeline page readiness, strict waveform bools, generic font fallback, panel session restore, caption draft restore, and panel suspension verified\n";
    return 0;
  } catch (const std::exception& error) {
    return fail(error.what());
  }
}
