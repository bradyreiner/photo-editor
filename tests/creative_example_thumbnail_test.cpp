#include "app_bootstrap.h"
#include "creative_example_thumbnail_image_provider.h"
#include "creative_example_thumbnail_service.h"
#include "edit_adjustment_patch.h"
#include "editor_preview_controller.h"
#include "photo_layer_effect_codec.h"
#include "photo_render_utils.h"
#include "render_plan_store.h"
#include "thumbnail_canonical_renderer.h"

#include "photo_editor/pipeline/photo_effect_registry.h"
#include "photo_editor/pipeline/photo_effect_types.h"
#include "photo_editor/pipeline/edit_recipe_v13.h"
#include "photo_editor/pipeline/recipe_compiler.h"

#include <QColorSpace>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQuickImageResponse>
#include <QQuickItem>
#include <QQuickTextureFactory>
#include <QQuickWindow>
#include <QSettings>
#include <QSGRendererInterface>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>
#include <QTest>

#include <algorithm>
#include <cstring>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>
#include <span>
#include <vector>

namespace photo_editor::desktop {
struct EditorPreviewControllerTestAccess final {
  static EditState state(const EditorPreviewController& controller) {
    return controller.currentState();
  }

  static bool can_undo(const EditorPreviewController& controller) {
    return controller.canUndo();
  }

  static EditRecipeState preset_state(
      const EditorPreviewController& controller,
      const QString& name) {
    const auto user = controller.preset_service_.userPresets().constFind(name);
    if (user != controller.preset_service_.userPresets().cend()) return user.value();
    const auto built_in = controller.preset_service_.builtInPresets().constFind(name);
    if (built_in != controller.preset_service_.builtInPresets().cend())
      return built_in.value();
    return {};
  }
};
}  // namespace photo_editor::desktop

using namespace photo_editor::desktop;

#define CHECK(condition, message) \
  do { \
    if (!(condition)) { \
      std::cerr << message << '\n'; \
      return 1; \
    } \
  } while (false)

namespace {

constexpr int kThumbnailWidth = 640;
constexpr int kThumbnailHeight = 432;

bool wait_until(const std::function<bool()>& predicate, int timeout_ms = 60000) {
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

QQuickItem* find_visual_item(QQuickItem* parent, const QString& name) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == name) return parent;
  for (auto* child : parent->childItems()) {
    if (auto* match = find_visual_item(child, name)) return match;
  }
  return nullptr;
}

QQuickItem* find_visible_preset_card(QQuickItem* parent, const QString& title) {
  if (parent == nullptr) return nullptr;
  if (parent->objectName() == QStringLiteral("studioPresetExampleCard") &&
      parent->isVisible() && parent->property("title").toString() == title) {
    return parent;
  }
  for (auto* child : parent->childItems()) {
    if (auto* match = find_visible_preset_card(child, title)) return match;
  }
  return nullptr;
}

bool click_quick_item(QQuickWindow* window, QQuickItem* item) {
  if (window == nullptr || item == nullptr || !item->isVisible() || !item->isEnabled())
    return false;
  const auto point = item->mapToScene(item->boundingRect().center()).toPoint();
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return true;
}

QImage sample_fixture() {
  QImage source(QStringLiteral(":/photo-editor/presets/mountain-preview.jpg"));
  if (source.isNull()) return {};
  const QColorSpace srgb(QColorSpace::SRgb);
  if (!source.colorSpace().isValid()) source.setColorSpace(srgb);
  const auto scaled = source.scaled(
      kThumbnailWidth,
      kThumbnailHeight,
      Qt::KeepAspectRatioByExpanding,
      Qt::SmoothTransformation)
                        .convertToFormat(QImage::Format_ARGB32);
  if (scaled.isNull()) return {};
  const QRect crop(
      (scaled.width() - kThumbnailWidth) / 2,
      (scaled.height() - kThumbnailHeight) / 2,
      kThumbnailWidth,
      kThumbnailHeight);
  auto image = scaled.copy(crop);
  if (image.colorSpace().isValid() && image.colorSpace() != srgb)
    image = image.convertedToColorSpace(srgb);
  else
    image.setColorSpace(srgb);
  return image;
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

QImage render_reference(const EditRecipeState& source_state) {
  auto state = normalized_example_recipe(source_state);
  const auto fixture = sample_fixture();
  if (fixture.isNull()) return {};
  const auto identity = adjustment_patch_identity_json(state);
  const auto digest = QString::fromLatin1(
      QCryptographicHash::hash(identity.toUtf8(), QCryptographicHash::Sha256).toHex());
  const auto admission = RenderPlanStore::shared().admit(
      QStringLiteral("builtin:creative-example-reference"),
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

EditRecipeState gaussian_example_state() {
  using photo_editor::pipeline::PhotoEffectInputScope;
  photo_editor::pipeline::PhotoEffectInstance effect;
  effect.id = "example-effect";
  effect.type_id = "gaussian_blur";
  effect.enabled = true;
  effect.amount = 1.0;
  effect.input_scope = PhotoEffectInputScope::CompositeBelow;
  QString error;
  const auto envelope = encode_photo_layer_effects(
      std::span<const photo_editor::pipeline::PhotoEffectInstance>(&effect, 1),
      &error);
  if (!envelope) return {};
  EditRecipeState state;
  state.layers = QVariantList{QVariantMap{
      {QStringLiteral("id"), QStringLiteral("creative-example-effect")},
      {QStringLiteral("name"), QStringLiteral("effect:gaussian_blur")},
      {QStringLiteral("kind"), QStringLiteral("adjustment")},
      {QStringLiteral("enabled"), true},
      {QStringLiteral("opacity"), 1.0},
      {QStringLiteral("blendMode"), QStringLiteral("normal")},
      {QStringLiteral("effectStack"), *envelope}}};
  return state;
}

bool same_pixels(const QImage& left, const QImage& right) {
  if (left.size() != right.size()) return false;
  const auto a = left.convertToFormat(QImage::Format_ARGB32);
  const auto b = right.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < a.height(); ++y) {
    if (std::memcmp(a.constScanLine(y), b.constScanLine(y),
                    static_cast<std::size_t>(a.width()) * 4U) != 0) {
      return false;
    }
  }
  return true;
}

struct ImageDifference final {
  qsizetype changed_pixels = 0;
  quint64 total_channel_delta = 0;
};

ImageDifference image_difference(const QImage& left, const QImage& right) {
  ImageDifference result;
  if (left.size() != right.size()) return result;
  const auto a = left.convertToFormat(QImage::Format_ARGB32);
  const auto b = right.convertToFormat(QImage::Format_ARGB32);
  for (int y = 0; y < a.height(); ++y) {
    const auto* a_row = reinterpret_cast<const QRgb*>(a.constScanLine(y));
    const auto* b_row = reinterpret_cast<const QRgb*>(b.constScanLine(y));
    for (int x = 0; x < a.width(); ++x) {
      const auto a_pixel = a_row[x];
      const auto b_pixel = b_row[x];
      const auto red_delta = qAbs(qRed(a_pixel) - qRed(b_pixel));
      const auto green_delta = qAbs(qGreen(a_pixel) - qGreen(b_pixel));
      const auto blue_delta = qAbs(qBlue(a_pixel) - qBlue(b_pixel));
      if (red_delta == 0 && green_delta == 0 && blue_delta == 0) continue;
      ++result.changed_pixels;
      result.total_channel_delta += static_cast<quint64>(
          red_delta + green_delta + blue_delta);
    }
  }
  return result;
}

bool meaningfully_differs_from(const QImage& image, const QImage& baseline) {
  const auto difference = image_difference(image, baseline);
  const auto changed_fraction = image.isNull()
                                    ? 0.0
                                    : static_cast<double>(difference.changed_pixels) /
                                          static_cast<double>(image.width() * image.height());
  const auto mean_rgb_delta = difference.changed_pixels == 0
                                  ? 0.0
                                  : static_cast<double>(difference.total_channel_delta) /
                                        static_cast<double>(difference.changed_pixels);
  return changed_fraction >= 0.0025 && mean_rgb_delta >= 2.0;
}

QString provider_id(const QString& source) {
  const auto prefix = QStringLiteral("image://creative-example/");
  if (!source.startsWith(prefix)) return {};
  return source.mid(prefix.size());
}

QQuickImageResponse* request_response(
    CreativeExampleThumbnailImageProvider* provider,
    const QString& source) {
  const auto id = provider_id(source);
  if (provider == nullptr || id.isEmpty()) return nullptr;
  return provider->requestImageResponse(id, QSize(kThumbnailWidth, kThumbnailHeight));
}

bool wait_for_responses(
    const std::vector<QQuickImageResponse*>& responses,
    int timeout_ms = 90000) {
  if (responses.empty()) return true;
  QEventLoop loop;
  QTimer timer;
  timer.setSingleShot(true);
  int finished_count = 0;
  const auto expected_count = static_cast<int>(responses.size());
  for (auto* response : responses) {
    if (response == nullptr) return false;
    QObject::connect(response, &QQuickImageResponse::finished, &loop, [&]() {
      ++finished_count;
      if (finished_count >= expected_count) loop.quit();
    });
  }
  QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
  timer.start(timeout_ms);
  loop.exec();
  return finished_count == expected_count;
}

QImage example_strip(const QList<QImage>& images) {
  if (images.isEmpty()) return {};
  constexpr auto maximum_count = std::numeric_limits<int>::max() / kThumbnailWidth;
  if (images.size() > maximum_count) return {};
  const auto image_count = static_cast<int>(images.size());
  QImage strip(image_count * kThumbnailWidth, kThumbnailHeight,
               QImage::Format_ARGB32);
  strip.fill(QColor(QStringLiteral("#101114")));
  QPainter painter(&strip);
  for (int index = 0; index < image_count; ++index) {
    if (!images.at(index).isNull())
      painter.drawImage(index * kThumbnailWidth, 0, images.at(index));
  }
  return strip;
}

}  // namespace

int main(int argc, char** argv) {
  QTemporaryDir app_data;
  if (!app_data.isValid()) return 1;
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", app_data.path().toUtf8());
  qputenv("PHOTO_EDITOR_DISABLE_RECIPE_PERSISTENCE", "1");
  QGuiApplication app(argc, argv);
  QCoreApplication::setOrganizationName(QStringLiteral("LooksyUiTests"));
  QCoreApplication::setApplicationName(QStringLiteral("CreativeExampleThumbnail"));
  QSettings::setDefaultFormat(QSettings::IniFormat);
  QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, app_data.path());
  QQuickWindow::setGraphicsApi(QSGRendererInterface::Software);
  if (!AppBootstrap::ensureRuntimeDirectories()) {
    std::cerr << "Runtime directories could not be initialized.\n";
    return 1;
  }
  CHECK(clear_creative_example_thumbnail_service_for_tests(),
        "Thumbnail service should start idle");

  EditorPreviewController controller;
  const auto state_before = EditorPreviewControllerTestAccess::state(controller);
  const auto can_undo_before = EditorPreviewControllerTestAccess::can_undo(controller);
  const auto preset_names = controller.presetNames();
  const auto preset_name = preset_names.contains(QStringLiteral("Landscape Pop"))
                               ? QStringLiteral("Landscape Pop")
                               : (preset_names.isEmpty() ? QString{} : preset_names.front());
  CHECK(!preset_name.isEmpty(), "A built-in preset is required for the thumbnail proof");

  const auto original_source = controller.exampleThumbnailOriginalSource();
  const auto gaussian_source =
      controller.effectExampleThumbnailSource(QStringLiteral("gaussian_blur"));
  const auto look_source =
      controller.effectExampleThumbnailSource(QStringLiteral("dream_haze"), true);
  const auto preset_source = controller.presetExampleThumbnailSource(preset_name);
  CHECK(original_source.startsWith(QStringLiteral("image://creative-example/v1/")),
        "Original example should be an asynchronous provider URL");
  CHECK(gaussian_source.startsWith(QStringLiteral("image://creative-example/v1/")),
        "Effect example should be an asynchronous provider URL");
  CHECK(look_source.startsWith(QStringLiteral("image://creative-example/v1/")),
        "Look example should be an asynchronous provider URL");
  CHECK(preset_source.startsWith(QStringLiteral("image://creative-example/v1/")),
        "Preset example should be an asynchronous provider URL");
  CHECK(controller.effectExampleThumbnailSource(QStringLiteral("unknown-effect")).isEmpty() &&
            controller.effectExampleThumbnailSource(QStringLiteral("unknown-look"), true).isEmpty() &&
            controller.presetExampleThumbnailSource(QStringLiteral("unknown-preset")).isEmpty(),
        "Unknown effect, Look, and preset names should return empty sources");
  CHECK(creative_example_thumbnail_service_snapshot().in_flight_jobs == 0,
        "Creating image URLs must not render eagerly");
  CHECK(EditorPreviewControllerTestAccess::state(controller) == state_before &&
            EditorPreviewControllerTestAccess::can_undo(controller) == can_undo_before,
        "Creating example URLs must not mutate the document or history");

  QQmlEngine engine;
  register_creative_example_thumbnail_provider(&engine);
  auto* provider = dynamic_cast<CreativeExampleThumbnailImageProvider*>(
      engine.imageProvider(QStringLiteral("creative-example")));
  CHECK(provider != nullptr, "The real example image provider should be registered");
  engine.rootContext()->setContextProperty(QStringLiteral("testController"), &controller);

  QQmlComponent component(&engine);
  component.setData(R"QML(
import QtQuick
Item {
    width: 1280
    height: 280
    Repeater {
        model: [
            { title: "Original", source: testController.exampleThumbnailOriginalSource() },
            { title: "Effect example", source: testController.effectExampleThumbnailSource("gaussian_blur", false) },
            { title: "Look example", source: testController.effectExampleThumbnailSource("dream_haze", true) },
            { title: "Preset example", source: testController.presetExampleThumbnailSource("Landscape Pop") }
        ]
        delegate: Rectangle {
            id: exampleTile
            required property var modelData
            required property int index
            x: exampleTile.index * 320
            width: 320
            height: 280
            color: "#17191d"
            Text {
                x: 12
                y: 8
                text: modelData.title
                color: "#f1eee9"
                font.pixelSize: 16
            }
            Image {
                objectName: exampleTile.index === 0 ? "exampleOriginalImage"
                           : exampleTile.index === 1 ? "exampleEffectImage"
                           : exampleTile.index === 2 ? "exampleLookImage"
                           : "examplePresetImage"
                x: 8
                y: 36
                width: 304
                height: 216
                source: exampleTile.modelData.source
                sourceSize: Qt.size(640, 432)
                fillMode: Image.PreserveAspectFit
                asynchronous: true
                cache: false
            }
        }
    }
}
)QML", QUrl(QStringLiteral("file:///creative-example-thumbnail-test.qml")));
  CHECK(component.isReady(), "The real-provider QML gallery should compile");
  auto* gallery = qobject_cast<QQuickItem*>(component.create());
  CHECK(gallery != nullptr, "The real-provider QML gallery should create");
  const auto proof_directory = qEnvironmentVariable("PHOTO_CREATIVE_EXAMPLE_PROOF_DIR");
  const auto require_native_exposure =
      !proof_directory.isEmpty() &&
      QGuiApplication::platformName() == QStringLiteral("cocoa");
  QQuickWindow window;
  window.setWidth(1280);
  window.setHeight(280);
  gallery->setParentItem(window.contentItem());
  gallery->setWidth(1280);
  gallery->setHeight(280);
  window.show();
  if (require_native_exposure) {
    window.requestActivate();
    CHECK(wait_until([&]() { return window.isExposed(); }, 5000),
          "The Cocoa real-provider gallery window did not become exposed after activation");
  }

  QQuickItem* original_image = nullptr;
  QQuickItem* effect_image = nullptr;
  QQuickItem* look_image = nullptr;
  QQuickItem* preset_image = nullptr;
  CHECK(wait_until([&]() {
          original_image = find_visual_item(gallery, QStringLiteral("exampleOriginalImage"));
          effect_image = find_visual_item(gallery, QStringLiteral("exampleEffectImage"));
          look_image = find_visual_item(gallery, QStringLiteral("exampleLookImage"));
          preset_image = find_visual_item(gallery, QStringLiteral("examplePresetImage"));
          return original_image != nullptr && effect_image != nullptr &&
                 look_image != nullptr && preset_image != nullptr;
        }),
        "QML should create the original/effect/Look/preset Image elements");
  CHECK(wait_until([&]() {
          return original_image->property("status").toInt() == 1 &&
                 effect_image->property("status").toInt() == 1 &&
                 look_image->property("status").toInt() == 1 &&
                 preset_image->property("status").toInt() == 1;
        }),
        "Real provider Image elements should reach Ready");

  const auto original = creative_example_thumbnail_cached_image(original_source);
  const auto gaussian = creative_example_thumbnail_cached_image(gaussian_source);
  const auto look = creative_example_thumbnail_cached_image(look_source);
  const auto preset = creative_example_thumbnail_cached_image(preset_source);
  const auto baseline = original;
  CHECK(!original.isNull() && !gaussian.isNull() && !look.isNull() && !preset.isNull(),
        "Real Image.ready requests should populate the result cache");
  CHECK(original.size() == QSize(kThumbnailWidth, kThumbnailHeight),
        "Example outputs should retain the 640 by 432 sample dimensions");
  CHECK(same_pixels(gaussian, render_reference(gaussian_example_state())),
        "Gaussian example pixels should match the shared canonical engine render exactly");
  CHECK(meaningfully_differs_from(gaussian, original),
        "The Gaussian example should visibly differ from the neutral canonical sample");
  const auto preset_state = EditorPreviewControllerTestAccess::preset_state(controller, preset_name);
  CHECK(same_pixels(preset, render_reference(preset_state)),
        "Preset example should render the stored preset recipe through the shared engine");
  CHECK(meaningfully_differs_from(preset, original),
        "The selected preset example should visibly differ from the neutral canonical sample");

  QStringList built_in_preset_names;
  for (const auto& name : controller.presetNames()) {
    if (!controller.isUserPreset(name)) built_in_preset_names.push_back(name);
  }
  CHECK(!built_in_preset_names.isEmpty(), "Built-in presets should be available for the real browser");
  std::vector<std::pair<QString, QImage>> preset_recipe_parity_samples;
  for (qsizetype index = 0; index < built_in_preset_names.size(); ++index) {
    const auto& name = built_in_preset_names.at(index);
    const auto source = controller.presetExampleThumbnailSource(name);
    CHECK(source.startsWith(QStringLiteral("image://creative-example/v1/")),
          "Every built-in preset should have a real example URL");
    auto image = creative_example_thumbnail_cached_image(source);
    if (image.isNull()) {
      auto* response = request_response(provider, source);
      CHECK(response != nullptr, "Built-in preset should create an async response");
      const auto completed = wait_for_responses({response});
      image = creative_example_thumbnail_cached_image(source);
      delete response;
      CHECK(completed, "Built-in preset example should finish through the image provider");
    }
    if (image.isNull() || image.size() != QSize(kThumbnailWidth, kThumbnailHeight)) {
      std::cerr << "Built-in preset did not produce a 640 by 432 example: "
                << name.toStdString() << '\n';
      return 1;
    }
    if (index == 0 || index == built_in_preset_names.size() / 2 ||
        index + 1 == built_in_preset_names.size()) {
      preset_recipe_parity_samples.push_back({name, image});
    }
  }
  for (const auto& [name, image] : preset_recipe_parity_samples) {
    const auto stored_recipe =
        EditorPreviewControllerTestAccess::preset_state(controller, name);
    CHECK(same_pixels(image, render_reference(stored_recipe)),
          "Representative built-in examples should match their stored canonical recipes");
  }

  std::vector<std::pair<QString, QString>> renderable_effects;
  for (const auto& value : controller.availablePhotoEffects()) {
    const auto item = value.toMap();
    const auto type = item.value(QStringLiteral("type")).toString();
    const auto source = controller.effectExampleThumbnailSource(type);
    if (type == QStringLiteral("displacement_map")) {
      CHECK(source.isEmpty(), "Displacement examples require an imported map asset");
      continue;
    }
    CHECK(source.startsWith(QStringLiteral("image://creative-example/v1/")),
          "Every other registered effect should have a real example URL");
    renderable_effects.push_back({type, source});
  }
  CHECK(renderable_effects.size() + 1 ==
            static_cast<std::size_t>(photo_editor::pipeline::photo_effect_registry.size()),
        "Only the asset-bound displacement operator should be omitted");
  CHECK(controller.effectExampleThumbnailSource(QStringLiteral("oil_paint")).size() > 0,
        "The cancellation/re-request fixture should be valid");

  // Cancel a queued request while two real effect renders occupy the workers,
  // then immediately request the same key again. The fresh generation must
  // complete even if the canceled worker finishes after it is registered.
  auto* long_effect_a = request_response(
      provider,
      controller.effectExampleThumbnailSource(QStringLiteral("spin_blur")));
  auto* long_effect_b = request_response(
      provider,
      controller.effectExampleThumbnailSource(QStringLiteral("oil_paint")));
  CHECK(long_effect_a != nullptr && long_effect_b != nullptr,
        "Heavy effect examples should create async responses");
  CHECK(wait_until([] {
          return creative_example_thumbnail_service_snapshot().active_workers >= 2;
        }, 20000),
        "Two bounded thumbnail workers should be active for cancellation proof");
  constexpr int kCancellationGenerationStressCount = 64;
  int peak_generation_stress_pending_jobs = 0;
  const auto generation_stress_source =
      CreativeExampleThumbnailService::shared().sourceForRecipe(
          QStringLiteral("cancel-generation-pressure"), EditRecipeState{});
  const auto generation_stress_id = provider_id(generation_stress_source);
  CHECK(!generation_stress_id.isEmpty(),
        "Cancellation-generation pressure URL should be valid");
  for (int generation = 0;
       generation < kCancellationGenerationStressCount;
       ++generation) {
    auto ticket = CreativeExampleThumbnailService::shared().request(
        generation_stress_id);
    CHECK(ticket.error.isEmpty(),
          "Cancelled generations should remain within the bounded queue");
    if (ticket.release) ticket.release(true);
    const auto pending = creative_example_thumbnail_service_snapshot().in_flight_jobs;
    peak_generation_stress_pending_jobs =
        qMax(peak_generation_stress_pending_jobs, pending);
    CHECK(pending <= 256,
          "Pending work must count canceled generations toward the queue cap");
  }
  const auto retry_source = controller.effectExampleThumbnailSource(QStringLiteral("watercolor"));
  auto* cancelled_response = request_response(provider, retry_source);
  CHECK(cancelled_response != nullptr, "Queued thumbnail response should exist");
  cancelled_response->cancel();
  auto* retry_response = request_response(provider, retry_source);
  CHECK(retry_response != nullptr, "A canceled image key should be requestable again");
  std::vector<QQuickImageResponse*> cancellation_responses{
      long_effect_a, long_effect_b, cancelled_response, retry_response};
  CHECK(wait_for_responses(cancellation_responses),
        "Cancelled and retried thumbnail responses should settle");
  CHECK(retry_response->textureFactory() != nullptr &&
            !creative_example_thumbnail_cached_image(retry_source).isNull(),
        "The fresh request should complete and cache after its predecessor is cancelled");
  CHECK(cancelled_response->textureFactory() == nullptr,
        "A canceled response should never publish a texture");
  for (auto* response : cancellation_responses) delete response;

  for (const auto& [type, source] : renderable_effects) {
    Q_UNUSED(type)
    auto image = creative_example_thumbnail_cached_image(source);
    if (image.isNull()) {
      auto* response = request_response(provider, source);
      CHECK(response != nullptr, "Registered effect should create an async response");
      CHECK(wait_for_responses({response}),
            "Every registered effect thumbnail should complete");
      image = creative_example_thumbnail_cached_image(source);
      delete response;
    }
    CHECK(!image.isNull(), "Every renderable effect should have image pixels");
    const auto difference = image_difference(image, baseline);
    if (!meaningfully_differs_from(image, baseline)) {
      const auto fraction = image.isNull() ? 0.0 :
          static_cast<double>(difference.changed_pixels) /
              static_cast<double>(image.width() * image.height());
      const auto mean_delta = difference.changed_pixels == 0 ? 0.0 :
          static_cast<double>(difference.total_channel_delta) /
              static_cast<double>(difference.changed_pixels);
      std::cerr << "Effect example did not visibly differ from the neutral sample: "
                << type.toStdString() << " changed=" << difference.changed_pixels
                << " fraction=" << fraction
                << " meanRgbDelta=" << mean_delta << '\n';
      return 1;
    }
  }

  const auto look_names = controller.availablePhotoEffectLooks();
  const QStringList representative_looks{
      QStringLiteral("dream_haze"), QStringLiteral("poster_pop"),
      QStringLiteral("warm_35mm"), QStringLiteral("city_pan"),
      QStringLiteral("sepia_ink")};
  for (const auto& look_id : representative_looks) {
    const auto found = std::any_of(look_names.cbegin(), look_names.cend(),
        [&look_id](const QVariant& value) {
          return value.toMap().value(QStringLiteral("id")).toString() == look_id;
        });
    CHECK(found, "Representative Look must exist in the registered Look set");
    const auto source = controller.effectExampleThumbnailSource(look_id, true);
    auto* response = request_response(provider, source);
    CHECK(response != nullptr, "Representative Look should create an async response");
    CHECK(wait_for_responses({response}), "Representative Look should render");
    const auto image = creative_example_thumbnail_cached_image(source);
    CHECK(!image.isNull() && meaningfully_differs_from(image, baseline),
          "Representative Look should visibly differ from the neutral sample");
    delete response;
  }

  const auto repeated_source =
      controller.effectExampleThumbnailSource(QStringLiteral("sepia_ink"), true);
  const auto cache_before_repeat = creative_example_thumbnail_service_snapshot();
  auto* repeated_response = request_response(provider, repeated_source);
  CHECK(repeated_response != nullptr, "Repeated URL should create a response");
  CHECK(wait_for_responses({repeated_response}), "Cached URL response should complete");
  const auto cache_after_repeat = creative_example_thumbnail_service_snapshot();
  CHECK(cache_after_repeat.cache_hits > cache_before_repeat.cache_hits,
        "Repeated thumbnails should hit the bounded result cache");
  delete repeated_response;

  CHECK(EditorPreviewControllerTestAccess::state(controller) == state_before &&
            EditorPreviewControllerTestAccess::can_undo(controller) == can_undo_before,
        "Thumbnail rendering must not mutate document or history state");
  const auto service_snapshot = creative_example_thumbnail_service_snapshot();
  CHECK(service_snapshot.peak_active_workers <= 2 && service_snapshot.in_flight_jobs == 0,
        "Thumbnail concurrency should remain bounded and drain after completion");
  CHECK(service_snapshot.cached_images <= 96 && service_snapshot.cached_bytes <= 32 * 1024 * 1024,
        "Thumbnail result cache should stay within its count and byte limits");

  if (!proof_directory.isEmpty()) {
    if (QGuiApplication::platformName() == QStringLiteral("cocoa") &&
        !window.isExposed()) {
      std::cerr << "The Cocoa thumbnail proof window is not exposed.\n";
      return 1;
    }
    if (!QDir().mkpath(proof_directory)) {
      std::cerr << "Cannot create creative example proof directory.\n";
      return 1;
    }
    const auto save = [&](const QString& name, const QImage& image) {
      return !image.isNull() && image.save(QDir(proof_directory).filePath(name));
    };
    CHECK(save(QStringLiteral("original.png"), original), "Save original example pixels");
    CHECK(save(QStringLiteral("effect-gaussian-blur.png"), gaussian), "Save effect example pixels");
    CHECK(save(QStringLiteral("look-dream-haze.png"), look), "Save Look example pixels");
    CHECK(save(QStringLiteral("preset-landscape-pop.png"), preset), "Save preset example pixels");
    const auto native_capture = window.grabWindow();
    CHECK(save(QStringLiteral("real-provider-gallery.png"), native_capture),
          "Save native real-provider gallery capture");
    CHECK(save(QStringLiteral("effect-example-suite.png"),
               example_strip({original, gaussian, look, preset})),
          "Save side-by-side example pixel strip");
  }

  window.hide();
  delete gallery;

  QQmlEngine preset_engine;
  register_creative_example_thumbnail_provider(&preset_engine);
  preset_engine.rootContext()->setContextProperty(
      QStringLiteral("realPresetController"), &controller);
  QQmlComponent app_shell_component(&preset_engine);
  app_shell_component.setData(R"QML(
import QtQml
QtObject {
    property real interfaceScale: 1
    property string interfaceDensity: "balanced"
    property string systemUiFontFamily: ""
    property bool reducedMotion: true
    property bool highContrast: false
    property string colorVisionMode: "standard"
}
)QML", QUrl());
  std::unique_ptr<QObject> app_shell(app_shell_component.create());
  CHECK(app_shell != nullptr, "Production preset browser should receive isolated app settings");
  app_shell->setProperty("systemUiFontFamily", app.font().family());
  preset_engine.rootContext()->setContextProperty(QStringLiteral("appShell"), app_shell.get());

  const auto preset_browser_path = QDir(QStringLiteral(PHOTO_EDITOR_SOURCE_DIR))
      .filePath(QStringLiteral("apps/desktop/qml/components/PresetBrowser.qml"));
  QQmlComponent preset_browser_component(
      &preset_engine, QUrl::fromLocalFile(preset_browser_path));
  CHECK(preset_browser_component.isReady(),
        preset_browser_component.errorString().toStdString().c_str());
  std::unique_ptr<QObject> preset_browser_object(preset_browser_component.create());
  if (!preset_browser_object) {
    std::cerr << preset_browser_component.errorString().toStdString() << '\n';
    return 1;
  }
  auto* preset_browser = qobject_cast<QQuickItem*>(preset_browser_object.get());
  CHECK(preset_browser != nullptr, "Create production PresetBrowser.qml");
  const auto preset_pack = controller.presetPackName(preset_name);
  const auto preset_category = controller.presetCategoryName(preset_name);
  const QVariantList production_preset_rows{
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("pack")},
                  {QStringLiteral("label"), preset_pack.isEmpty()
                       ? QStringLiteral("Built-in presets") : preset_pack},
                  {QStringLiteral("count"), 1}},
      QVariantMap{{QStringLiteral("kind"), QStringLiteral("preset")},
                  {QStringLiteral("name"), preset_name}},
  };
  CHECK(preset_browser->setProperty(
            "controller", QVariant::fromValue(static_cast<QObject*>(&controller))) &&
            preset_browser->setProperty("presetRows", production_preset_rows) &&
            preset_browser->setProperty("presetNames", QStringList{preset_name}) &&
            preset_browser->setProperty(
                "categoryFilters", QStringList{QStringLiteral("All"), preset_category}) &&
            preset_browser->setProperty(
                "packFilters", QStringList{QStringLiteral("All"), preset_pack}) &&
            preset_browser->setProperty("selectedPresetName", preset_name) &&
            preset_browser->setProperty("selectedMetaText", preset_category) &&
            preset_browser->setProperty("targetSummary", QStringLiteral("Photo preview")) &&
            preset_browser->setProperty("selectedTab", QStringLiteral("All")) &&
            preset_browser->setProperty("hasAsset", true) &&
            preset_browser->setProperty("canApply", true),
        "Bind actual built-in preset rows, metadata, filters, and controller");

  QQuickWindow preset_window;
  preset_window.setWidth(500);
  preset_window.setHeight(850);
  preset_window.setColor(QColor(QStringLiteral("#17191d")));
  preset_browser->setParentItem(preset_window.contentItem());
  preset_browser->setWidth(460);
  preset_browser->setHeight(820);
  preset_window.show();
  if (require_native_exposure) {
    preset_window.requestActivate();
    CHECK(wait_until([&]() { return preset_window.isExposed(); }, 5000),
          "The Cocoa production PresetBrowser window did not become exposed after activation");
  }

  QQuickItem* production_preset_card = nullptr;
  QQuickItem* production_preset_image = nullptr;
  const auto production_card_ready = wait_until([&]() {
    production_preset_card = find_visible_preset_card(preset_browser, preset_name);
    production_preset_image = production_preset_card == nullptr
        ? nullptr
        : find_visual_item(production_preset_card,
                           QStringLiteral("studioPresetExampleCardThumbnail"));
    return production_preset_card != nullptr &&
           production_preset_image != nullptr &&
           production_preset_card->property("previewReady").toBool() &&
           production_preset_image->property("status").toInt() == 1;
  }, 15000);
  if (!production_card_ready) {
    const auto candidate = find_visual_item(
        preset_browser, QStringLiteral("studioPresetExampleCard"));
    const auto candidate_title = candidate == nullptr
        ? QStringLiteral("<missing>")
        : candidate->property("title").toString();
    const auto candidate_source = candidate == nullptr
        ? QStringLiteral("<missing>")
        : candidate->property("thumbnailSource").toString();
    auto* candidate_image = candidate == nullptr
        ? nullptr
        : find_visual_item(candidate,
                           QStringLiteral("studioPresetExampleCardThumbnail"));
    const auto candidate_status = candidate_image == nullptr
        ? QStringLiteral("<missing>")
        : QString::number(candidate_image->property("status").toInt());
    const auto matched_title = production_preset_card == nullptr
        ? QStringLiteral("<missing>")
        : production_preset_card->property("title").toString();
    const auto matched_source = production_preset_card == nullptr
        ? QStringLiteral("<missing>")
        : production_preset_card->property("thumbnailSource").toString();
    const auto matched_status = production_preset_image == nullptr
        ? QStringLiteral("<missing>")
        : QString::number(production_preset_image->property("status").toInt());
    std::cerr << "Production preset card timeout expectedTitle="
              << preset_name.toStdString()
              << " matchedVisibleTitle=" << matched_title.toStdString()
              << " matchedSource=" << matched_source.toStdString()
              << " matchedImageStatus=" << matched_status.toStdString()
              << " candidateTitle=" << candidate_title.toStdString()
              << " source=" << candidate_source.toStdString()
              << " imageStatus=" << candidate_status.toStdString() << '\n';
    return 1;
  }
  const auto production_preset_source =
      production_preset_card->property("thumbnailSource").toString();
  CHECK(production_preset_source.startsWith(
            QStringLiteral("image://creative-example/v1/")) &&
            production_preset_image->property("status").toInt() == 1 &&
            production_preset_image->property("source").toString() ==
                production_preset_source,
        "Production preset card should show its real async example URL at Image.Ready");
  CHECK(preset_browser->property("selectedPresetName").toString() == preset_name &&
            !preset_browser->property("showOriginalSamples").toBool(),
        "Production preset browser should start on the selected processed built-in example");

  auto* original_sample_button = find_visual_item(
      preset_browser, QStringLiteral("studioPresetOriginalSampleButton"));
  CHECK(original_sample_button != nullptr &&
            click_quick_item(&preset_window, original_sample_button),
        "Production browser original-sample control should accept a real pointer click");
  const auto expected_original_source = controller.exampleThumbnailOriginalSource();
  CHECK(wait_until([&]() {
          return preset_browser->property("showOriginalSamples").toBool() &&
                 production_preset_card->property("comparingOriginal").toBool() &&
                 production_preset_card->property("displayedSource").toString() ==
                     expected_original_source &&
                 production_preset_image->property("status").toInt() == 1;
        }),
        "Original-sample toggle should display the real neutral reference in its card");
  CHECK(click_quick_item(&preset_window, original_sample_button) &&
            wait_until([&]() {
              return !preset_browser->property("showOriginalSamples").toBool() &&
                     !production_preset_card->property("comparingOriginal").toBool() &&
                     production_preset_card->property("previewReady").toBool();
            }),
        "Production preset browser should restore the processed example after comparison");

  if (!proof_directory.isEmpty()) {
    if (QGuiApplication::platformName() == QStringLiteral("cocoa") &&
        !preset_window.isExposed()) {
      std::cerr << "The production preset browser proof window is not exposed.\n";
      return 1;
    }
    if (!QDir().mkpath(proof_directory) ||
        !preset_window.grabWindow().save(
            QDir(proof_directory).filePath(QStringLiteral("actual-preset-browser.png")))) {
      std::cerr << "Cannot save the production preset browser proof.\n";
      return 1;
    }
  }
  CHECK(EditorPreviewControllerTestAccess::state(controller) == state_before &&
            EditorPreviewControllerTestAccess::can_undo(controller) == can_undo_before,
        "Production browser and original comparison must not mutate document or history state");

  std::cout << "Creative example thumbnails: real QQml Image.ready, canonical pixel parity, "
               "all renderable effects, representative Looks, preset recipe, cache/cancel/re-request, "
               "production PresetBrowser real-provider/original comparison, bounded workers, "
               "and nonmutation passed; cancellation generations="
            << kCancellationGenerationStressCount
            << " peak pending=" << peak_generation_stress_pending_jobs
            << " built-in presets=" << built_in_preset_names.size() << ".\n";
  return 0;
}
