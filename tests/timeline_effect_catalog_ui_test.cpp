#include "photo_editor/pipeline/photo_effect_registry.h"

#include <QCoreApplication>
#include <QFile>
#include <QJSEngine>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <iostream>

namespace {
QString read(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString{};
}
QString between(const QString& text, const QString& begin, const QString& end) {
  const auto start = text.indexOf(begin);
  const auto finish = start < 0 ? -1 : text.indexOf(end, start + begin.size());
  return finish < 0 ? QString{} : text.mid(start + begin.size(), finish - start - begin.size());
}
int fail(const char* message) { std::cerr << message << '\n'; return 1; }
}

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto view = read(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/workspaces/TimelineView.qml"));
  const auto panel = read(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR "/apps/desktop/qml/components/TimelineEffectsPanel.qml"));
  const auto legacy = between(view, "readonly property var legacyEffectLibraryModel: ",
      "\n    readonly property var effectLibraryModel:");
  const auto catalog = between(view, "readonly property var effectLibraryModel: {",
      "\n    function addSharedTimelineEffect");
  const auto add = between(panel, "    function addQuickEffect", "\n    function assignTransition");
  const auto compatible = between(panel, "    function isEffectCompatible", "\n    function effectMatchesSearch");
  if (legacy.isEmpty() || catalog.isEmpty() || add.isEmpty() || compatible.isEmpty())
    return fail("production effect discovery functions unavailable");
  const auto selection_expression = between(panel, "readonly property bool hasSelection: ", "\n").trimmed();
  QJSEngine selection_engine;
  const auto selection_function = selection_engine.evaluate(
      "(function(selectedClipIndex, selectionCount) { return " + selection_expression + "; })");
  auto selection = selection_function;
  if (!selection.isCallable() || selection.call({0, 0}).toBool() ||
      !selection.call({0, 1}).toBool() || selection.call({-1, 1}).toBool())
    return fail("Effects must not enable edits for a stale primary index without an actual selection");
  QJsonArray registry;
  for (const auto& effect : photo_editor::pipeline::photo_effect_registry) {
    const auto text = [](std::string_view value) {
      return QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size()));
    };
    registry.append(QJsonObject{{"type", text(effect.type_id)}, {"name", text(effect.display_name)},
        {"category", text(effect.category)}, {"description", text(effect.description)}});
  }
  QJSEngine engine;
  const auto source = QStringLiteral(R"JS(
var root = this;
var hasSelection = true, hasSingleSelection = true, hasMultiSelection = false, hasClip = true;
var sharedPhotoToolsAvailable = true, clipState = {mediaType: 'video'};
var legacyCalls = [], sharedCalls = [], recents = [];
var documentController = {addSelectedClipEffect: function(name) { legacyCalls.push(name); }};
function effectUsed(name) { recents.push(name); }
function appliedEffectControlsRequested() {}
function sharedEffectRequested(type, name) { sharedCalls.push({type: type, name: name}); }
var sharedColorEditor = {availableEffects: %1};
var legacyEffectLibraryModel = %2;
var effectLibraryModel = (function() {%3)();
var effectLibraryItems = effectLibraryModel;
function addQuickEffect%4
function isEffectCompatible%5
)JS").arg(QString::fromUtf8(QJsonDocument(registry).toJson(QJsonDocument::Compact)),
      legacy, catalog, add, compatible);
  const auto evaluated = engine.evaluate(source);
  if (evaluated.isError()) { std::cerr << evaluated.toString().toStdString() << '\n'; return 1; }
  const auto check = [&](const QString& expression) { return engine.evaluate(expression).toBool(); };
  if (!check("effectLibraryModel.length === 42")) return fail("the real timeline model must contain 7 legacy plus 35 shared effects");
  if (!check(R"JS(effectLibraryModel.every(function(effect) {
      return !!effect.name && !!effect.category && !!effect.description && isEffectCompatible(effect);
    }))JS")) return fail("all catalog entries must be named, categorized and compatible with video");
  if (!check(R"JS((function() {
      effectLibraryModel.forEach(function(effect) { addQuickEffect(effect.name); });
      return legacyCalls.length === 7 && recents.length === 7 && sharedCalls.length === 35 &&
        sharedCalls.every(function(call, index) { return call.type === sharedColorEditor.availableEffects[index].type; });
    })())JS")) return fail("shared effect Add must dispatch its stable type to the shared recipe controller");
  if (!check(R"JS((function() {
      clipState.mediaType = 'title';
      addQuickEffect('Duotone');
      return !isEffectCompatible(effectLibraryModel[7]) && sharedCalls.length === 35;
    })())JS")) return fail("shared photo effects must reject unsupported title clips");
  if (!check(R"JS((function() {
      clipState.mediaType = 'video'; hasMultiSelection = true; hasSingleSelection = false;
      addQuickEffect('Duotone'); hasSelection = false; addQuickEffect('Exposure');
      return !isEffectCompatible(effectLibraryModel[7]) && sharedCalls.length === 35 && legacyCalls.length === 7;
    })())JS")) return fail("selection guards must prevent silent or misdirected additions");
  if (!check(R"JS((function() {
      hasSelection = true; hasSingleSelection = true; hasMultiSelection = false;
      clipState.mediaType = 'photo';
      return !isEffectCompatible(effectLibraryModel[7]) && isEffectCompatible(effectLibraryModel[0]);
    })())JS")) return fail("photo clips must not advertise recipe effects that their Timeline path rejects");
  std::cout << "PASS: production discovery model exposes 42 effects; Add routes 35 stable shared types and seven legacy effects; unsupported and multi-selection additions rejected\n";
  return 0;
}
