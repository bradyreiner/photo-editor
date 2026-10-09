#include "creative_fonts.h"

#include <QGuiApplication>
#include <QMetaObject>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QVariantMap>
#include <iostream>
#include <memory>

class TypographyPresetController final : public QObject {
  Q_OBJECT
 public:
  explicit TypographyPresetController(QVariantMap style, QObject* parent = nullptr)
      : QObject(parent), style_(std::move(style)) {}

  Q_INVOKABLE QVariantMap photoLayerTypography(int) const { return style_; }
  Q_INVOKABLE QVariantMap photoLayerTextLayoutInfo(int) const { return {}; }
  Q_INVOKABLE QStringList availablePhotoTextFonts() const { return QFontDatabase::families(); }
  Q_INVOKABLE QStringList availablePhotoTextFaces(const QString& family) const {
    return QFontDatabase::styles(family);
  }
  Q_INVOKABLE QVariantList availablePhotoTextAxes(const QString&, const QString&) const { return {}; }
  Q_INVOKABLE bool setPhotoLayerTypography(int, const QVariantMap& changes) {
    ++apply_count_;
    last_changes_ = changes;
    for (auto it = changes.cbegin(); it != changes.cend(); ++it) style_.insert(it.key(), it.value());
    if (changes.value(QStringLiteral("material")).toString() == QStringLiteral("none")) {
      for (const auto* key : {"material", "materialTint", "materialBlur", "materialTransmission",
                              "materialEdgeSoftness", "materialDepth", "materialLightAngle"})
        style_.remove(QString::fromLatin1(key));
    }
    return true;
  }
  Q_INVOKABLE void beginCoalescedEdit() {}
  Q_INVOKABLE void endCoalescedEdit() {}
  Q_INVOKABLE bool setPhotoLayerTextPathFromActiveSelection(int, int) { return false; }
  Q_INVOKABLE bool clearPhotoLayerTextPath(int) { return false; }

  int applyCount() const { return apply_count_; }
  QVariantMap lastChanges() const { return last_changes_; }
  QVariantMap style() const { return style_; }

 private:
  QVariantMap style_;
  QVariantMap last_changes_;
  int apply_count_ = 0;
};

namespace {
int fail(const char* message) {
  std::cerr << message << '\n';
  return 1;
}

bool same(const QVariant& left, const QVariant& right) {
  return left == right;
}
}  // namespace

int main(int argc, char** argv) {
  QGuiApplication app(argc, argv);
  if (!photo_editor::desktop::register_creative_fonts().isEmpty())
    return fail("Bundled creative fonts should register for preset validation");

  QVariantList path_points{
      QVariantMap{{QStringLiteral("x"), 0.15}, {QStringLiteral("y"), 0.72}},
      QVariantMap{{QStringLiteral("x"), 0.82}, {QStringLiteral("y"), 0.68}}};
  QVariantMap original_style{
      {QStringLiteral("family"), QStringLiteral("Space Grotesk")},
      {QStringLiteral("face"), QStringLiteral("" )},
      {QStringLiteral("size"), 83.0},
      {QStringLiteral("weight"), 800},
      {QStringLiteral("italic"), false},
      {QStringLiteral("underline"), true},
      {QStringLiteral("tracking"), 3.5},
      {QStringLiteral("lineHeight"), 1.3},
      {QStringLiteral("paragraphSpacing"), 11.0},
      {QStringLiteral("mode"), QStringLiteral("paragraph")},
      {QStringLiteral("color"), QStringLiteral("#ffffffff")},
      {QStringLiteral("axes"), QVariantMap{{QStringLiteral("wght"), 800.0}}},
      {QStringLiteral("strokeWidth"), 8.0},
      {QStringLiteral("strokeColor"), QStringLiteral("#ffff0000")},
      {QStringLiteral("shadowX"), 12.0},
      {QStringLiteral("shadowY"), -5.0},
      {QStringLiteral("shadowColor"), QStringLiteral("#ff00ff00")},
      {QStringLiteral("material"), QStringLiteral("frosted_glass")},
      {QStringLiteral("materialTint"), QStringLiteral("#ff00ffff")},
      {QStringLiteral("materialBlur"), 27.0},
      {QStringLiteral("pathPoints"), path_points},
      {QStringLiteral("pathStart"), 0.1},
      {QStringLiteral("pathEnd"), 0.9},
      {QStringLiteral("pathOffset"), 6.0},
      {QStringLiteral("pathSpacing"), 2.0},
      {QStringLiteral("pathSide"), QStringLiteral("right")}};
  TypographyPresetController controller(original_style);

  QQmlEngine engine;
  QQmlComponent component(&engine, QUrl::fromLocalFile(QStringLiteral(
      PHOTO_TEXT_TEST_ROOT "/apps/desktop/qml/components/PhotoTypographyPanel.qml")));
  if (component.isError()) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Typography panel should compile");
  }
  const QVariantMap layer_data{
      {QStringLiteral("kind"), QStringLiteral("text")},
      {QStringLiteral("text"), QStringLiteral("Keep this exact editorial copy.")},
      {QStringLiteral("textTypography"), original_style}};
  std::unique_ptr<QObject> panel(component.createWithInitialProperties({
      {QStringLiteral("controller"), QVariant::fromValue(static_cast<QObject*>(&controller))},
      {QStringLiteral("layerIndex"), 0},
      {QStringLiteral("layerData"), layer_data}}));
  if (!panel) {
    std::cerr << component.errorString().toStdString() << '\n';
    return fail("Typography panel should instantiate with the controller fixture");
  }

  const auto presets = panel->property("stylePresets").toList();
  if (presets.size() != 21) return fail("Typography panel should expose 21 named presets");
  auto* apply_button = panel->findChild<QObject*>(QStringLiteral("photoTypographyStylePresetApply"));
  if (!apply_button) return fail("Typography preset Apply control should exist");

  for (qsizetype index = 0; index < presets.size(); ++index) {
    if (!panel->setProperty("selectedPresetIndex", index) ||
        !QMetaObject::invokeMethod(panel.get(), "applyStylePreset"))
      return fail("Each typography preset should be applicable");
    const QVariantMap preset = presets.at(index).toMap();
    const QVariantMap patch = controller.lastChanges();
    const QVariantMap effective = controller.style();
    if (!QFontDatabase::families().contains(patch.value(QStringLiteral("family")).toString()))
      return fail("Preset should resolve to a font family installed in the runtime");
    if (!patch.value(QStringLiteral("axes")).toMap().isEmpty())
      return fail("Preset should clear old variable-font axes");
    if (patch.value(QStringLiteral("strokeWidth")).toDouble() != preset.value(QStringLiteral("strokeWidth")).toDouble() ||
        patch.value(QStringLiteral("strokeColor")).toString() != preset.value(QStringLiteral("strokeColor"), QStringLiteral("#ff000000")).toString() ||
        patch.value(QStringLiteral("shadowX")).toDouble() != preset.value(QStringLiteral("shadowX")).toDouble() ||
        patch.value(QStringLiteral("shadowY")).toDouble() != preset.value(QStringLiteral("shadowY")).toDouble() ||
        patch.value(QStringLiteral("shadowColor")).toString() != preset.value(QStringLiteral("shadowColor")).toString() ||
        patch.value(QStringLiteral("material")).toString() != preset.value(QStringLiteral("material")).toString() ||
        patch.value(QStringLiteral("materialTint")).toString() != preset.value(QStringLiteral("materialTint"), QStringLiteral("#ff9eb8c8")).toString() ||
        patch.value(QStringLiteral("materialBlur")).toDouble() != preset.value(QStringLiteral("materialBlur"), 6.0).toDouble() ||
        patch.value(QStringLiteral("materialTransmission")).toDouble() != preset.value(QStringLiteral("materialTransmission"), 0.68).toDouble() ||
        patch.value(QStringLiteral("materialEdgeSoftness")).toDouble() != preset.value(QStringLiteral("materialEdgeSoftness"), 1.0).toDouble() ||
        patch.value(QStringLiteral("materialDepth")).toDouble() != preset.value(QStringLiteral("materialDepth"), 4.0).toDouble() ||
        patch.value(QStringLiteral("materialLightAngle")).toDouble() != preset.value(QStringLiteral("materialLightAngle"), -45.0).toDouble())
      return fail("Preset should set every stroke, shadow, and material field explicitly");
    if (!same(effective.value(QStringLiteral("size")), original_style.value(QStringLiteral("size"))) ||
        !same(effective.value(QStringLiteral("mode")), original_style.value(QStringLiteral("mode"))) ||
        !same(effective.value(QStringLiteral("paragraphSpacing")), original_style.value(QStringLiteral("paragraphSpacing"))) ||
        !same(effective.value(QStringLiteral("pathPoints")), original_style.value(QStringLiteral("pathPoints"))) ||
        !same(effective.value(QStringLiteral("pathStart")), original_style.value(QStringLiteral("pathStart"))) ||
        !same(effective.value(QStringLiteral("pathEnd")), original_style.value(QStringLiteral("pathEnd"))) ||
        !same(effective.value(QStringLiteral("pathOffset")), original_style.value(QStringLiteral("pathOffset"))) ||
        !same(effective.value(QStringLiteral("pathSpacing")), original_style.value(QStringLiteral("pathSpacing"))) ||
        !same(effective.value(QStringLiteral("pathSide")), original_style.value(QStringLiteral("pathSide"))))
      return fail("Applying style should preserve size, layout, paragraph, and path geometry");
    if (preset.value(QStringLiteral("material")).toString() == QStringLiteral("none") &&
        (effective.contains(QStringLiteral("materialTint")) || effective.contains(QStringLiteral("materialBlur"))))
      return fail("Non-material presets should remove prior material settings");
    if (layer_data.value(QStringLiteral("text")).toString() != QStringLiteral("Keep this exact editorial copy."))
      return fail("Applying style should preserve the text copy");
  }

  const auto materials = panel->property("textMaterialOptions").toList();
  if (materials.size() != 14)
    return fail("Typography material picker should expose three existing finishes and ten metals");
  for (qsizetype index = 4; index < materials.size(); ++index) {
    if (!QMetaObject::invokeMethod(panel.get(), "chooseTextMaterial",
            Q_ARG(QVariant, QVariant(static_cast<int>(index)))))
      return fail("Each metal finish should be selectable through the typography material control");
    const auto option = materials.at(index).toMap();
    const auto patch = controller.lastChanges();
    if (patch.value(QStringLiteral("material")).toString() != option.value(QStringLiteral("id")).toString() ||
        patch.value(QStringLiteral("materialTint")).toString() != option.value(QStringLiteral("tint")).toString() ||
        patch.value(QStringLiteral("materialDepth")).toDouble() != option.value(QStringLiteral("depth")).toDouble() ||
        patch.value(QStringLiteral("materialLightAngle")).toDouble() != option.value(QStringLiteral("angle")).toDouble() ||
        patch.value(QStringLiteral("materialTransmission")).toDouble() < 0.5)
      return fail("Metal selection should apply editable tint, reflection, bevel depth, and light defaults");
  }

  const int applied_before_lock = controller.applyCount();
  const QVariantMap style_before_lock = controller.style();
  panel->setProperty("locked", true);
  if (!QMetaObject::invokeMethod(panel.get(), "applyStylePreset"))
    return fail("Typography panel should expose its guarded Apply function");
  if (controller.applyCount() != applied_before_lock || controller.style() != style_before_lock ||
      apply_button->property("enabled").toBool())
    return fail("Locked text layers should reject typography presets");

  std::cout << "21 typography presets and 10 editable metal finishes resolve cleanly, preserve text geometry, and respect locks\n";
  return 0;
}

#include "creative_typography_styles_test.moc"
