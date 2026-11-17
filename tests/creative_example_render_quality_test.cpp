#include "creative_example_thumbnail_image_provider.h"
#include "creative_example_thumbnail_service.h"
#include "edit_adjustment_patch.h"
#include "photo_layer_effect_codec.h"
#include "photo_render_utils.h"
#include "preset_registry.h"
#include "render_plan_store.h"
#include "thumbnail_canonical_renderer.h"

#include "photo_editor/pipeline/photo_effect_types.h"

#include <QColorSpace>
#include <QColor>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRect>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QThread>
#include <QUrl>
#include <QVariantList>
#include <QVariantMap>

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <optional>
#include <span>

using namespace photo_editor::desktop;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      std::cerr << message << '\n'; \
      return 1; \
    } \
  } while (false)

namespace {

constexpr int kWidth = 640;
constexpr int kHeight = 432;
constexpr qsizetype kMaximumCachedBytes = 32 * 1024 * 1024;

bool wait_until(const std::function<bool()>& predicate, int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    if (predicate()) return true;
    QThread::msleep(2);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return predicate();
}

QImage fixture_image() {
  QImage source(QStringLiteral(":/photo-editor/presets/mountain-preview.jpg"));
  if (source.isNull()) return {};
  const QColorSpace srgb(QColorSpace::SRgb);
  if (!source.colorSpace().isValid()) source.setColorSpace(srgb);
  const auto scaled = source.scaled(
      kWidth,
      kHeight,
      Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation)
                          .convertToFormat(QImage::Format_ARGB32);
  if (scaled.isNull()) return {};
  const QRect crop(
      (scaled.width() - kWidth) / 2,
      (scaled.height() - kHeight) / 2,
      kWidth,
      kHeight);
  auto result = scaled.copy(crop);
  if (result.colorSpace().isValid() && result.colorSpace() != srgb)
    result = result.convertedToColorSpace(srgb);
  else
    result.setColorSpace(srgb);
  return result;
}

EditRecipeState normalized_example_recipe(EditRecipeState state) {
  state.raw_exposure = 0.0;
  state.preview_color_space = QStringLiteral("sRGB");
  state.preview_output_mode = QStringLiteral("Monitor");
  state.monitor_color_profile.clear();
  state.preview_icc_profile_asset = {};
  state.proof_icc_profile_asset = {};
  state.export_icc_profile_asset = {};
  state.output_color_space = QStringLiteral("sRGB");
  state.soft_proof_enabled = false;
  state.soft_proof_profile = QStringLiteral("sRGB");
  state.proof_simulate_paper_white = false;
  state.proof_simulate_ink_black = false;
  return state;
}

QImage canonical_reference(const EditRecipeState& source_state) {
  const auto state = normalized_example_recipe(source_state);
  const auto fixture = fixture_image();
  if (fixture.isNull()) return {};
  const auto identity = adjustment_patch_identity_json(state);
  const auto digest = QString::fromLatin1(
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("builtin:creative-example-quality-reference"),
      QStringLiteral("builtin:creative-example-mountain"),
      state,
      fixture.size(),
      photo_editor::pipeline::RenderPurpose::Thumbnail,
      photo_editor::pipeline::DisplayRenderingTarget::SrgbSimulation,
      std::nullopt,
      nullptr,
      digest);
  if (normalized_photographic_rendering_model_name(state.photographic_rendering_model) ==
          QStringLiteral("aces2-ocio-v1") &&
      !admission.admitted) {
    return {};
  }
  return render_canonical_thumbnail_image(
      fixture,
      state,
      QColorSpace(QColorSpace::SRgb),
      admission.admitted ? &*admission.plan : nullptr);
}

EditRecipeState gaussian_example_recipe() {
  using photo_editor::pipeline::PhotoEffectInputScope;
  photo_editor::pipeline::PhotoEffectInstance effect;
  effect.id = "retina-quality-gaussian";
  effect.type_id = "gaussian_blur";
  effect.enabled = true;
  effect.amount = 1.0;
  effect.input_scope = PhotoEffectInputScope::CompositeBelow;
  QString error;
  const auto encoded = encode_photo_layer_effects(
      std::span<const photo_editor::pipeline::PhotoEffectInstance>(&effect, 1),
      &error);
  if (!encoded) return {};

  EditRecipeState state;
  state.layers = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("retina-quality-effect")},
      {QStringLiteral("name"), QStringLiteral("effect:gaussian_blur")},
      {QStringLiteral("kind"), QStringLiteral("adjustment")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
      {QStringLiteral("effectStack"), *encoded}}};
  return state;
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_ARGB32);
  const auto b = right.convertToFormat(QImage::Format_ARGB32);
  const auto bytes = static_cast<std::size_t>(a.width()) * 4U;
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y), bytes) != 0)
      return false;
  }
  return true;
}

bool visibly_differs(const QImage& image, const QImage& baseline) {
  if (image.size() != baseline.size() || image.isNull()) return false;
  const auto a = image.convertToFormat(QImage::Format_ARGB32);
  const auto b = baseline.convertToFormat(QImage::Format_ARGB32);
  quint64 changed = 0;
  quint64 delta = 0;
  for (int y = 0; y < a.height(); ++y) {
    const auto* a_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const auto* b_row = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const auto red = qAbs(qRed(a_row[x]) - qRed(b_row[x]));
      const auto green = qAbs(qGreen(a_row[x]) - qGreen(b_row[x]));
      const auto blue = qAbs(qBlue(a_row[x]) - qBlue(b_row[x]));
      if (red == 0 && green == 0 && blue == 0) continue;
      ++changed;
      delta += static_cast<quint64>(red + green + blue);
    }
  }
  const auto pixel_count = static_cast<double>(image.width()) * image.height();
  const auto changed_fraction = static_cast<double>(changed) / pixel_count;
  const auto mean_delta = changed == 0
                              ? 0.0
                              : static_cast<double>(delta) / static_cast<double>(changed);
  return changed_fraction >= 0.0025 && mean_delta >= 2.0;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir app_data;
  if (!app_data.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");

  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CreativeExampleRenderQuality"));
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);

  auto& service = CreativeExampleThumbnailService::shared();
  CHECK(service.clearForTests(), "Thumbnail service must start without active work");

  QQmlEngine engine;
  register_creative_example_thumbnail_provider(&engine);
  QQmlComponent component(&engine);
  component.setData(R"qml(
import QtQuick
Item {
    id: root
    objectName: "qualityLoader"
    width: 640
    height: 432
    property url previewSource: ""
    readonly property int imageStatus: sample.status
    readonly property bool imageReady: sample.status === Image.Ready
    readonly property bool imageFinished: sample.status === Image.Ready || sample.status === Image.Error
    readonly property string loadedSource: sample.source.toString()
    Image {
        id: sample
        objectName: "qualityImage"
        anchors.fill: parent
        source: root.previewSource
        sourceSize.width: 640
        sourceSize.height: 432
        asynchronous: true
        cache: false
        fillMode: Image.PreserveAspectFit
    }
}
)qml", QUrl(QStringLiteral("file:///creative-example-quality.qml")));
  CHECK(wait_until([&]() { return component.status() != QQmlComponent::Loading; }, 5000),
        "Finish loading the production-provider Image fixture");
  if (!component.isReady()) std::cerr << component.errorString().toStdString();
  CHECK(component.isReady(), "Compile the small production-provider Image fixture");
  auto* loader = qobject_cast<QQuickItem*>(component.create());
  CHECK(loader != nullptr, "Create the production-provider Image fixture");

  QQuickWindow window;
  window.setWidth(kWidth);
  window.setHeight(kHeight);
  window.setColor(QColor(QStringLiteral("#15171a")));
  loader->setParentItem(window.contentItem());
  window.show();

  auto load_source = [&](const QString& name,
                         const QString& source,
                         int timeout_ms,
                         QImage* output) {
    if (source.isEmpty() || output == nullptr) {
      std::cerr << "Empty example source for " << name.toStdString() << '\n';
      return false;
    }
    loader->setProperty("previewSource", QUrl(source));
    const auto completed = wait_until(
        [&]() {
          return loader->property("loadedSource").toString() == source &&
                 loader->property("imageFinished").toBool();
        },
        timeout_ms);
    if (!completed || !loader->property("imageReady").toBool()) {
      std::cerr << "QML Image did not become Ready for " << name.toStdString()
                << " source=" << source.toStdString()
                << " status=" << loader->property("imageStatus").toInt() << '\n';
      return false;
    }
    *output = service.cachedImageForSource(source);
    if (output->size() != QSize(kWidth, kHeight) || output->isNull()) {
      std::cerr << "Provider image has wrong dimensions for " << name.toStdString()
                << " actual=" << output->width() << 'x' << output->height() << '\n';
      return false;
    }
    return true;
  };

  const auto original_state = EditRecipeState{};
  const auto original_source = service.sourceForRecipe(
      QStringLiteral("retina-quality:original"), original_state);
  QImage original;
  CHECK(load_source(QStringLiteral("original"), original_source, 30000, &original),
        "Load the neutral example through QML at 640 by 432");
  CHECK(same_pixels(original, canonical_reference(original_state)),
        "The full-size neutral example must match the canonical renderer pixels");

  const auto gaussian_state = gaussian_example_recipe();
  CHECK(!gaussian_state.layers.isEmpty(), "Encode an actual Gaussian effect recipe");
  const auto gaussian_source = service.sourceForRecipe(
      QStringLiteral("retina-quality:effect:gaussian_blur"), gaussian_state);
  QImage gaussian;
  CHECK(load_source(QStringLiteral("Gaussian effect"), gaussian_source, 30000, &gaussian),
        "Load the Gaussian example through QML at 640 by 432");
  CHECK(visibly_differs(gaussian, original),
        "The real Gaussian effect example must visibly differ from neutral");
  CHECK(same_pixels(gaussian, canonical_reference(gaussian_state)),
        "The full-size Gaussian example must match the canonical renderer pixels");

  const auto presets = built_in_preset_map();
  CHECK(presets.size() == 82,
        "The retina fixture should cover all 82 current built-in preset recipes");
  const auto preset_names = presets.keys();
  const auto middle_name = preset_names.at(preset_names.size() / 2);
  QImage middle_preset_image;
  QElapsedTimer preset_timer;
  preset_timer.start();
  constexpr int kPresetRenderBudgetMs = 150000;
  int ready_presets = 0;
  for (auto iterator = presets.cbegin(); iterator != presets.cend(); ++iterator) {
    const auto source = service.sourceForRecipe(
        QStringLiteral("retina-quality:preset:%1").arg(iterator.key()), iterator.value());
    QImage rendered;
    const auto remaining = kPresetRenderBudgetMs - static_cast<int>(preset_timer.elapsed());
    CHECK(remaining > 0, "All built-in retina examples should finish within 150 seconds");
    CHECK(load_source(iterator.key(), source, std::min(remaining, 30000), &rendered),
          "Every built-in preset must produce a ready 640 by 432 example");
    if (iterator.key() == middle_name) middle_preset_image = rendered;
    ++ready_presets;
  }
  CHECK(ready_presets == 82, "Every built-in preset example must be rendered");
  const auto middle_state = presets.value(middle_name);
  CHECK(same_pixels(middle_preset_image, canonical_reference(middle_state)),
        "A representative built-in preset must match the canonical renderer pixels");

  const auto snapshot = service.snapshot();
  CHECK(snapshot.cached_bytes <= kMaximumCachedBytes,
        "640 by 432 examples must stay inside the 32 MiB result cache budget");
  CHECK(snapshot.registered_requests <= 512 && snapshot.in_flight_jobs == 0 &&
            snapshot.active_workers == 0 && snapshot.peak_active_workers <= 2,
        "Retina preset requests must finish without exceeding bounded service work");

  std::cout << "Creative example render quality: QML Image.Ready at 640x432, canonical neutral/Gaussian/preset pixel parity, visibly changed Gaussian effect, all "
            << ready_presets << " built-in presets, cache bytes=" << snapshot.cached_bytes
            << ", peak workers=" << snapshot.peak_active_workers << ".\n";
  return 0;
}
