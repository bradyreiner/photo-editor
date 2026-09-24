#include "agent_edit_controller.h"
#include "agent_runtime_paths.h"

#include "app_bootstrap.h"
#include "catalog_asset_model.h"
#include "edit_adjustment_patch.h"
#include "edit_recipe_state.h"
#include "export_job_controller.h"
#include "observability.h"
#include "photo_render_utils.h"
#include "photo_text_style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <iostream>
#include <utility>

#include <QCoreApplication>
#include <QColor>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTemporaryDir>
#include <QThread>
#include <QTime>
#include <QTimer>
#include <QUrl>

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

QString adjustment_patch_for_asset(
    const QVariantMap& asset) {
  return asset
      .value(
          QStringLiteral(
              "adjustmentPatchJson"))
      .toString();
}

photo_editor::desktop::EditRecipeState
adjustment_state_from_patch(
    const QString& patch_json) {
  const auto decoded =
      photo_editor::desktop::
          parse_adjustment_patch_json(
              patch_json);
  return decoded.state.value_or(
      photo_editor::desktop::
          EditRecipeState{});
}

photo_editor::desktop::EditRecipeState
adjustment_state_for_asset(
    const QVariantMap& asset) {
  return adjustment_state_from_patch(
      adjustment_patch_for_asset(
          asset));
}

photo_editor::desktop::CatalogAssetItem make_asset(
    const QString& asset_id,
    const QString& file_path,
    const QString& recipe_json) {
  photo_editor::desktop::CatalogAssetItem item;
  item.asset_id = asset_id;
  item.camera_label = QStringLiteral("Test Camera");
  item.file_path = file_path;
  item.capture_date = QStringLiteral("Jun 29, 2026");
  item.capture_date_value = QDate(2026, 6, 29);
  item.lens_model = QStringLiteral("RF 50mm F1.2L USM");
  item.iso_label = QStringLiteral("ISO 200");
  item.shutter_speed = QStringLiteral("1/500 s");
  item.aperture_label = QStringLiteral("f/2");
  item.imported_at = QStringLiteral("Jun 29, 2026  1:00 PM");
  item.imported_at_value = QDateTime(QDate(2026, 6, 29), QTime(13, 0));
  item.media_type = QStringLiteral("photo");
  item.adjustment_patch_json =
      photo_editor::desktop::
          canonical_asset_bound_adjustment_patch_json(
              recipe_json);
  item.online = true;
  return item;
}

bool write_fixture_image(const QString& file_path) {
  QImage image(24, 16, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(92 + x, 105 + y, 118));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

bool write_retouch_fixture_image(const QString& file_path) {
  QImage image(240, 160, QImage::Format_ARGB32);
  for (int y = 0; y < image.height(); ++y) {
    for (int x = 0; x < image.width(); ++x) {
      image.setPixelColor(x, y, QColor(92 + x / 4, 105 + y / 4, 148));
    }
  }
  for (int y = 30; y <= 36; ++y) {
    for (int x = 183; x <= 189; ++x) {
      image.setPixelColor(x, y, QColor(12, 14, 18));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

bool write_semantic_fixture_image(const QString& file_path) {
  QImage image(256, 192, QImage::Format_ARGB32);
  image.fill(QColor(32, 62, 92));
  for (int y = 60; y < 132; ++y) {
    for (int x = 96; x < 160; ++x) {
      image.setPixelColor(x, y, QColor(216, 128, 76));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

bool write_ambiguous_object_fixture_image(const QString& file_path) {
  QImage image(320, 192, QImage::Format_ARGB32);
  image.fill(QColor(92, 100, 108));
  for (int y = 58; y < 136; ++y) {
    for (int x = 54; x < 112; ++x) {
      image.setPixelColor(x, y, QColor(154, 116, 92));
    }
    for (int x = 208; x < 266; ++x) {
      image.setPixelColor(x, y, QColor(154, 116, 92));
    }
  }
  const QFileInfo info(file_path);
  if (!QDir().mkpath(info.dir().absolutePath())) {
    return false;
  }
  return image.save(file_path, "PNG");
}

QJsonObject read_json_object(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return QJsonDocument::fromJson(file.readAll()).object();
}

QByteArray read_file_bytes(const QString& file_path) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return {};
  }
  return file.readAll();
}

QByteArray fake_ollama_response(const QJsonObject& content_object) {
  return QJsonDocument(QJsonObject{
      {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
      {QStringLiteral("done"), true},
      {QStringLiteral("message"),
       QJsonObject{
           {QStringLiteral("role"), QStringLiteral("assistant")},
           {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(content_object).toJson(QJsonDocument::Compact))},
       }},
  }).toJson(QJsonDocument::Compact);
}

int observability_event_count(const QString& file_path, const QString& event_name) {
  QFile file(file_path);
  if (!file.open(QIODevice::ReadOnly)) {
    return 0;
  }

  auto count = 0;
  while (!file.atEnd()) {
    const auto record = QJsonDocument::fromJson(file.readLine()).object();
    if (record.value(QStringLiteral("event")).toString() == event_name) {
      ++count;
    }
  }
  return count;
}

bool has_citation_field(const QVariantList& citations, const QString& field) {
  for (const auto& citation_value : citations) {
    const auto citation = citation_value.toMap();
    if (citation.value(QStringLiteral("field")).toString() == field) {
      return true;
    }
  }
  return false;
}

bool json_array_contains_string(const QJsonArray& values, const QString& expected) {
  for (const auto& value : values) {
    if (value.toString() == expected) {
      return true;
    }
  }
  return false;
}

QVariantMap model_by_id(const QVariantList& models, const QString& id) {
  for (const auto& model_value : models) {
    const auto model = model_value.toMap();
    if (model.value(QStringLiteral("id")).toString() == id) {
      return model;
    }
  }
  return {};
}

bool wait_for_condition(
    const std::function<bool()>& condition,
    int timeout_ms) {
  QElapsedTimer timer;
  timer.start();
  while (!condition() && timer.elapsed() < timeout_ms) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(1);
  }
  QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
  return condition();
}

QString resolved_manifest_path(
    const QFileInfo& manifest_info,
    const QString& value) {
  const QFileInfo candidate(value);
  if (candidate.isAbsolute()) {
    return candidate.absoluteFilePath();
  }
  return QDir(manifest_info.absolutePath()).absoluteFilePath(value);
}

double protected_region_mean_rgb_delta(
    const QImage& before,
    const QImage& after,
    const QJsonObject& region) {
  if (before.isNull() || after.isNull() || before.size() != after.size()) {
    return -1.0;
  }
  const auto before_rgb = before.convertToFormat(QImage::Format_RGB32);
  const auto after_rgb = after.convertToFormat(QImage::Format_RGB32);
  const auto x0 = std::clamp(
      static_cast<int>(std::floor(region.value(QStringLiteral("x")).toDouble() * before.width())),
      0,
      before.width() - 1);
  const auto y0 = std::clamp(
      static_cast<int>(std::floor(region.value(QStringLiteral("y")).toDouble() * before.height())),
      0,
      before.height() - 1);
  const auto x1 = std::clamp(
      static_cast<int>(std::ceil(
          (region.value(QStringLiteral("x")).toDouble() +
           region.value(QStringLiteral("width")).toDouble()) * before.width())),
      x0 + 1,
      before.width());
  const auto y1 = std::clamp(
      static_cast<int>(std::ceil(
          (region.value(QStringLiteral("y")).toDouble() +
           region.value(QStringLiteral("height")).toDouble()) * before.height())),
      y0 + 1,
      before.height());
  double sum = 0.0;
  qint64 channel_count = 0;
  for (auto y = y0; y < y1; ++y) {
    for (auto x = x0; x < x1; ++x) {
      const auto before_pixel = before_rgb.pixelColor(x, y);
      const auto after_pixel = after_rgb.pixelColor(x, y);
      sum += std::abs(before_pixel.red() - after_pixel.red());
      sum += std::abs(before_pixel.green() - after_pixel.green());
      sum += std::abs(before_pixel.blue() - after_pixel.blue());
      channel_count += 3;
    }
  }
  return channel_count > 0
             ? sum / (static_cast<double>(channel_count) * 255.0)
             : -1.0;
}

bool write_corpus_ollama_model_manifest(const QString& artifact_root) {
  const auto model_manifest_path =
      QDir(artifact_root).filePath(QStringLiteral("agent-models/models.json"));
  if (!QDir().mkpath(QFileInfo(model_manifest_path).absolutePath())) {
    return false;
  }
  QSaveFile model_manifest(model_manifest_path);
  if (!model_manifest.open(QIODevice::WriteOnly)) {
    return false;
  }
  const auto payload = QJsonDocument(QJsonObject{
      {QStringLiteral("models"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("ollama-corpus-retouch")},
               {QStringLiteral("name"), QStringLiteral("Corpus retouch planner")},
               {QStringLiteral("backend"), QStringLiteral("ollama")},
               {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
               {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
               {QStringLiteral("vision"), true},
               {QStringLiteral("available"), true},
           },
       }},
  }).toJson(QJsonDocument::Compact);
  return model_manifest.write(payload) == payload.size() &&
         model_manifest.commit();
}

QJsonObject corpus_retouch_intent(const QJsonObject& evaluation_case) {
  const auto retouch = evaluation_case.value(QStringLiteral("retouch")).toObject();
  const auto region = retouch.value(QStringLiteral("region")).toObject();
  const auto target_label =
      retouch.value(QStringLiteral("target_label")).toString();
  const auto operation = retouch.value(QStringLiteral("operation")).toString();
  return QJsonObject{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"),
       evaluation_case.value(QStringLiteral("instructions")).toString()},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.retouch")},
               {QStringLiteral("reason"),
                QStringLiteral("The corpus style explicitly permits reviewed retouching.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"),
            evaluation_case.value(QStringLiteral("instructions")).toString()},
           {QStringLiteral("colorMode"), QStringLiteral("preserve")},
           {QStringLiteral("contrast"), QStringLiteral("preserve")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("preserve")},
           {QStringLiteral("saturation"), QStringLiteral("preserve")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("preserve")},
           {QStringLiteral("mood"), QStringLiteral("natural")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"),
            QJsonArray{QStringLiteral("retouch")}},
           {QStringLiteral("requiresApproval"),
            QJsonArray{QStringLiteral("retouch")}},
           {QStringLiteral("confidence"), 0.96},
           {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
           {QStringLiteral("visualFindings"), QJsonArray{}},
           {QStringLiteral("localizedActions"), QJsonArray{}},
           {QStringLiteral("retouchActions"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("distraction")},
                    {QStringLiteral("operation"), operation},
                    {QStringLiteral("targetLabel"), target_label},
                    {QStringLiteral("reason"),
                     QStringLiteral("The bounded corpus target is the only approved cleanup region.")},
                    {QStringLiteral("confidence"), 0.96},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"),
                          region.value(QStringLiteral("center_x"))},
                         {QStringLiteral("centerY"),
                          region.value(QStringLiteral("center_y"))},
                         {QStringLiteral("radiusX"),
                          region.value(QStringLiteral("radius_x"))},
                         {QStringLiteral("radiusY"),
                          region.value(QStringLiteral("radius_y"))},
                     }},
                },
            }},
       }},
  };
}

QVariantMap evaluate_corpus_retouch_proof(
    const QImage& before,
    const QImage& after,
    const QJsonObject& evaluation_case,
    const QVariantMap& materialization) {
  QVariantMap report{
      {QStringLiteral("contract"),
       QStringLiteral("agent-real-photo-retouch-quality-v2")},
      {QStringLiteral("available"), false},
  };
  if (before.isNull() || after.isNull() || before.size() != after.size()) {
    return report;
  }
  const auto artifact = materialization.value(QStringLiteral("artifacts"))
                            .toList()
                            .value(0)
                            .toMap();
  const auto source_width = artifact.value(QStringLiteral("sourceWidth")).toInt();
  const auto source_height = artifact.value(QStringLiteral("sourceHeight")).toInt();
  if (source_width <= 0 || source_height <= 0) {
    return report;
  }
  const auto bounds_x = artifact.value(QStringLiteral("sourceBoundsX")).toInt();
  const auto bounds_y = artifact.value(QStringLiteral("sourceBoundsY")).toInt();
  const auto bounds_width = artifact.value(QStringLiteral("sourceBoundsWidth")).toInt();
  const auto bounds_height = artifact.value(QStringLiteral("sourceBoundsHeight")).toInt();
  const auto proof_bounds = QRect(
      static_cast<int>(std::floor(
          static_cast<double>(bounds_x) * before.width() / source_width)),
      static_cast<int>(std::floor(
          static_cast<double>(bounds_y) * before.height() / source_height)),
      std::max(
          1,
          static_cast<int>(std::ceil(
              static_cast<double>(bounds_width) * before.width() /
              source_width))),
      std::max(
          1,
          static_cast<int>(std::ceil(
              static_cast<double>(bounds_height) * before.height() /
              source_height))));

  auto changed_inside = 0;
  auto changed_outside = 0;
  for (auto y = 0; y < before.height(); ++y) {
    for (auto x = 0; x < before.width(); ++x) {
      const auto before_pixel = before.pixelColor(x, y);
      const auto after_pixel = after.pixelColor(x, y);
      const auto delta = std::abs(before_pixel.red() - after_pixel.red()) +
                         std::abs(before_pixel.green() - after_pixel.green()) +
                         std::abs(before_pixel.blue() - after_pixel.blue());
      if (delta <= 3) {
        continue;
      }
      if (proof_bounds.contains(x, y)) {
        ++changed_inside;
      } else {
        ++changed_outside;
      }
    }
  }

  const auto target_region =
      evaluation_case.value(QStringLiteral("retouch"))
          .toObject()
          .value(QStringLiteral("region"))
          .toObject();
  const auto target_center_x =
      target_region.value(QStringLiteral("center_x")).toDouble() *
      before.width();
  const auto target_center_y =
      target_region.value(QStringLiteral("center_y")).toDouble() *
      before.height();
  const auto target_radius_x =
      target_region.value(QStringLiteral("radius_x")).toDouble() *
      before.width();
  const auto target_radius_y =
      target_region.value(QStringLiteral("radius_y")).toDouble() *
      before.height();
  const auto target_bounds = QRect(
      static_cast<int>(std::floor(
          (target_region.value(QStringLiteral("center_x")).toDouble() -
           target_region.value(QStringLiteral("radius_x")).toDouble()) *
          before.width())),
      static_cast<int>(std::floor(
          (target_region.value(QStringLiteral("center_y")).toDouble() -
           target_region.value(QStringLiteral("radius_y")).toDouble()) *
          before.height())),
      std::max(
          1,
          static_cast<int>(std::ceil(
              target_region.value(QStringLiteral("radius_x")).toDouble() *
              2.0 * before.width()))),
      std::max(
          1,
          static_cast<int>(std::ceil(
              target_region.value(QStringLiteral("radius_y")).toDouble() *
              2.0 * before.height()))));
  const auto target_delta = protected_region_mean_rgb_delta(
      before,
      after,
      QJsonObject{
          {QStringLiteral("x"),
           static_cast<double>(target_bounds.x()) / before.width()},
          {QStringLiteral("y"),
           static_cast<double>(target_bounds.y()) / before.height()},
          {QStringLiteral("width"),
           static_cast<double>(target_bounds.width()) / before.width()},
          {QStringLiteral("height"),
           static_cast<double>(target_bounds.height()) / before.height()},
      });

  constexpr auto kMaximumSurroundingChangedFraction = 0.75;
  constexpr auto kMaximumSurroundingMeanRgbDelta = 0.004;
  constexpr auto kMaximumTexturedSurroundingMeanRgbDelta = 8.0 / 255.0;
  constexpr auto kMinimumSurroundingGradientRatio = 0.65;
  constexpr auto kMaximumSurroundingGradientRatio = 1.35;
  constexpr auto kMinimumReconstructionGradientRatio = 0.5;
  constexpr auto kMaximumReconstructionGradientRatio = 1.6;
  const auto ellipse_distance_squared = [&](const int x, const int y) {
    const auto normalized_x =
        (static_cast<double>(x) + 0.5 - target_center_x) /
        target_radius_x;
    const auto normalized_y =
        (static_cast<double>(y) + 0.5 - target_center_y) /
        target_radius_y;
    return normalized_x * normalized_x + normalized_y * normalized_y;
  };
  const auto rgb_delta = [](const QColor& lhs, const QColor& rhs) {
    return std::abs(lhs.red() - rhs.red()) +
           std::abs(lhs.green() - rhs.green()) +
           std::abs(lhs.blue() - rhs.blue());
  };
  const auto image_bounds = QRect(0, 0, before.width(), before.height());
  const auto measured_bounds = proof_bounds.intersected(image_bounds);
  auto surrounding_pixels = 0;
  auto surrounding_changed_pixels = 0;
  auto surrounding_rgb_delta_sum = 0.0;
  auto surrounding_before_gradient = 0.0;
  auto surrounding_after_gradient = 0.0;
  auto surrounding_gradient_edges = 0;
  for (auto y = measured_bounds.top(); y <= measured_bounds.bottom(); ++y) {
    for (auto x = measured_bounds.left(); x <= measured_bounds.right(); ++x) {
      const auto distance_squared = ellipse_distance_squared(x, y);
      if (distance_squared <= 1.0 || distance_squared > 2.25) {
        continue;
      }
      const auto delta = rgb_delta(
          before.pixelColor(x, y),
          after.pixelColor(x, y));
      ++surrounding_pixels;
      surrounding_rgb_delta_sum += delta;
      if (delta > 3) {
        ++surrounding_changed_pixels;
      }
      const std::array<QPoint, 2> neighbors{
          QPoint(x + 1, y),
          QPoint(x, y + 1),
      };
      for (const auto& neighbor : neighbors) {
        if (!image_bounds.contains(neighbor) ||
            !measured_bounds.contains(neighbor)) {
          continue;
        }
        const auto neighbor_distance_squared =
            ellipse_distance_squared(neighbor.x(), neighbor.y());
        if (neighbor_distance_squared <= 1.0 ||
            neighbor_distance_squared > 2.25) {
          continue;
        }
        surrounding_before_gradient += rgb_delta(
            before.pixelColor(x, y),
            before.pixelColor(neighbor));
        surrounding_after_gradient += rgb_delta(
            after.pixelColor(x, y),
            after.pixelColor(neighbor));
        ++surrounding_gradient_edges;
      }
    }
  }
  const auto surrounding_changed_fraction =
      surrounding_pixels > 0
          ? static_cast<double>(surrounding_changed_pixels) /
                surrounding_pixels
          : 1.0;
  const auto surrounding_mean_rgb_delta =
      surrounding_pixels > 0
          ? surrounding_rgb_delta_sum /
                (static_cast<double>(surrounding_pixels) * 3.0 * 255.0)
          : 1.0;
  const auto surrounding_gradient_ratio =
      surrounding_gradient_edges <= 0
          ? 0.0
          : surrounding_before_gradient <= 0.0
                ? (surrounding_after_gradient <= 0.0
                       ? 1.0
                       : surrounding_after_gradient)
                : surrounding_after_gradient / surrounding_before_gradient;
  const auto reconstruction_bounds = QRect(
      static_cast<int>(std::floor(target_center_x - target_radius_x * 2.0)),
      static_cast<int>(std::floor(target_center_y - target_radius_y * 2.0)),
      std::max(1, static_cast<int>(std::ceil(target_radius_x * 4.0))),
      std::max(1, static_cast<int>(std::ceil(target_radius_y * 4.0))))
                                         .intersected(image_bounds);
  const auto region_gradient = [&](const double minimum_distance_squared,
                                   const double maximum_distance_squared) {
    auto gradient_sum = 0.0;
    auto gradient_edges = 0;
    for (auto y = reconstruction_bounds.top();
         y <= reconstruction_bounds.bottom();
         ++y) {
      for (auto x = reconstruction_bounds.left();
           x <= reconstruction_bounds.right();
           ++x) {
        const auto distance_squared = ellipse_distance_squared(x, y);
        if (distance_squared <= minimum_distance_squared ||
            distance_squared > maximum_distance_squared) {
          continue;
        }
        const std::array<QPoint, 2> neighbors{
            QPoint(x + 1, y),
            QPoint(x, y + 1),
        };
        for (const auto& neighbor : neighbors) {
          if (!reconstruction_bounds.contains(neighbor)) {
            continue;
          }
          const auto neighbor_distance_squared =
              ellipse_distance_squared(neighbor.x(), neighbor.y());
          if (neighbor_distance_squared <= minimum_distance_squared ||
              neighbor_distance_squared > maximum_distance_squared) {
            continue;
          }
          gradient_sum += rgb_delta(
              after.pixelColor(x, y),
              after.pixelColor(neighbor));
          ++gradient_edges;
        }
      }
    }
    return std::pair{gradient_sum, gradient_edges};
  };
  const auto [target_gradient_sum, target_gradient_edges] =
      region_gradient(-1.0, 1.0);
  const auto [reference_gradient_sum, reference_gradient_edges] =
      region_gradient(2.25, 4.0);
  const auto target_gradient_mean =
      target_gradient_edges > 0
          ? target_gradient_sum / target_gradient_edges
          : 0.0;
  const auto reference_gradient_mean =
      reference_gradient_edges > 0
          ? reference_gradient_sum / reference_gradient_edges
          : 0.0;
  const auto reconstruction_gradient_ratio =
      target_gradient_edges <= 0 || reference_gradient_edges <= 0
          ? 0.0
          : reference_gradient_mean <= 0.0
                ? (target_gradient_mean <= 0.0 ? 1.0 : target_gradient_mean)
                : target_gradient_mean / reference_gradient_mean;
  const auto texture_exemplar =
      artifact.value(QStringLiteral("textureExemplar")).toBool();
  const auto maximum_surrounding_mean_rgb_delta =
      texture_exemplar ? kMaximumTexturedSurroundingMeanRgbDelta
                       : kMaximumSurroundingMeanRgbDelta;
  const auto background_continuity =
      surrounding_pixels > 0 && surrounding_gradient_edges > 0 &&
      target_gradient_edges > 0 && reference_gradient_edges > 0 &&
      surrounding_changed_fraction <=
          kMaximumSurroundingChangedFraction &&
      surrounding_mean_rgb_delta <= maximum_surrounding_mean_rgb_delta &&
      surrounding_gradient_ratio >= kMinimumSurroundingGradientRatio &&
      surrounding_gradient_ratio <= kMaximumSurroundingGradientRatio &&
      reconstruction_gradient_ratio >=
          kMinimumReconstructionGradientRatio &&
      reconstruction_gradient_ratio <=
          kMaximumReconstructionGradientRatio;
  const auto materialized =
      materialization.value(QStringLiteral("ok")).toBool() &&
      materialization.value(QStringLiteral("materializedCount")).toInt() == 1;
  const auto full_resolution =
      materialization.value(QStringLiteral("fullResolution")).toBool();
  QVariantList checks{
      QVariantMap{
          {QStringLiteral("name"), QStringLiteral("artifactMaterialized")},
          {QStringLiteral("passed"), materialized},
      },
      QVariantMap{
          {QStringLiteral("name"), QStringLiteral("fullResolutionArtifact")},
          {QStringLiteral("passed"), full_resolution},
      },
      QVariantMap{
          {QStringLiteral("name"), QStringLiteral("targetChanged")},
          {QStringLiteral("passed"),
           changed_inside > 0 && target_delta >= 0.002},
      },
      QVariantMap{
          {QStringLiteral("name"), QStringLiteral("outsidePreserved")},
          {QStringLiteral("passed"), changed_outside == 0},
      },
      QVariantMap{
          {QStringLiteral("name"), QStringLiteral("backgroundContinuity")},
          {QStringLiteral("passed"), background_continuity},
      },
  };
  report.insert(QStringLiteral("available"), true);
  report.insert(QStringLiteral("ok"),
                materialized && full_resolution && changed_inside > 0 &&
                    target_delta >= 0.002 && changed_outside == 0 &&
                    background_continuity);
  report.insert(QStringLiteral("checks"), checks);
  report.insert(QStringLiteral("changedInsideBounds"), changed_inside);
  report.insert(QStringLiteral("changedOutsideBounds"), changed_outside);
  report.insert(QStringLiteral("targetMeanRgbDelta"), target_delta);
  report.insert(QStringLiteral("surroundingPixelCount"), surrounding_pixels);
  report.insert(
      QStringLiteral("surroundingChangedPixels"),
      surrounding_changed_pixels);
  report.insert(
      QStringLiteral("surroundingChangedFraction"),
      surrounding_changed_fraction);
  report.insert(
      QStringLiteral("surroundingMeanRgbDelta"),
      surrounding_mean_rgb_delta);
  report.insert(
      QStringLiteral("surroundingGradientEnergyRatio"),
      surrounding_gradient_ratio);
  report.insert(
      QStringLiteral("reconstructionGradientEnergyRatio"),
      reconstruction_gradient_ratio);
  report.insert(
      QStringLiteral("maximumSurroundingChangedFraction"),
      kMaximumSurroundingChangedFraction);
  report.insert(
      QStringLiteral("maximumSurroundingMeanRgbDelta"),
      maximum_surrounding_mean_rgb_delta);
  report.insert(QStringLiteral("textureExemplar"), texture_exemplar);
  report.insert(
      QStringLiteral("minimumSurroundingGradientEnergyRatio"),
      kMinimumSurroundingGradientRatio);
  report.insert(
      QStringLiteral("maximumSurroundingGradientEnergyRatio"),
      kMaximumSurroundingGradientRatio);
  report.insert(
      QStringLiteral("minimumReconstructionGradientEnergyRatio"),
      kMinimumReconstructionGradientRatio);
  report.insert(
      QStringLiteral("maximumReconstructionGradientEnergyRatio"),
      kMaximumReconstructionGradientRatio);
  report.insert(QStringLiteral("proofBoundsX"), proof_bounds.x());
  report.insert(QStringLiteral("proofBoundsY"), proof_bounds.y());
  report.insert(QStringLiteral("proofBoundsWidth"), proof_bounds.width());
  report.insert(QStringLiteral("proofBoundsHeight"), proof_bounds.height());
  return report;
}

int run_real_photo_corpus(
    const QString& manifest_path,
    const QString& report_path,
    bool require_human_ratings,
    const QString& case_filter) {
#if defined(PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE)
  qputenv(
      "PHOTO_EDITOR_AI_MODEL_DIR",
      QByteArray(PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE));
#endif
  const QFileInfo manifest_info(manifest_path);
  const auto manifest = read_json_object(manifest_info.absoluteFilePath());
  if (manifest.isEmpty() || !manifest.value(QStringLiteral("assets")).isArray()) {
    return fail("real-photo corpus manifest should contain an assets array");
  }

  const QFileInfo report_info(report_path);
  if (!QDir().mkpath(report_info.absolutePath())) {
    return fail("real-photo corpus report directory should be writable");
  }
  const auto artifact_root = QDir(report_info.absolutePath()).filePath(
      report_info.completeBaseName() + QStringLiteral("-artifacts"));
  if (!QDir().mkpath(artifact_root)) {
    return fail("real-photo corpus artifact directory should be writable");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", artifact_root.toUtf8());
  qputenv(
      "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR",
      QDir(artifact_root).filePath(QStringLiteral("observability")).toUtf8());
  if (!write_corpus_ollama_model_manifest(artifact_root)) {
    return fail("real-photo corpus retouch model manifest should be writable");
  }
  photo_editor::desktop::observability::initialize();

  QJsonArray case_reports;
  auto asset_count = 0;
  auto case_count = 0;
  auto machine_pass_count = 0;
  auto human_pass_count = 0;
  auto human_fail_count = 0;
  auto human_pending_count = 0;

  for (const auto& asset_value : manifest.value(QStringLiteral("assets")).toArray()) {
    if (!asset_value.isObject()) {
      continue;
    }
    ++asset_count;
    const auto asset = asset_value.toObject();
    const auto asset_id = asset.value(QStringLiteral("asset_id")).toString();
    const auto source_path = resolved_manifest_path(
        manifest_info, asset.value(QStringLiteral("path")).toString());
    const auto recipe_path = resolved_manifest_path(
        manifest_info, asset.value(QStringLiteral("recipe")).toString());
    QFile recipe_file(recipe_path);
    QString recipe_json = QStringLiteral("{}");
    if (recipe_file.open(QIODevice::ReadOnly)) {
      recipe_json = QString::fromUtf8(recipe_file.readAll());
    }
    const auto canonical_recipe = photo_editor::desktop::serialize_edit_recipe_json(
        photo_editor::desktop::parse_edit_recipe_json(recipe_json));

    for (const auto& case_value : asset.value(QStringLiteral("agent_cases")).toArray()) {
      if (!case_value.isObject()) {
        continue;
      }
      const auto evaluation_case = case_value.toObject();
      const auto case_id = evaluation_case.value(QStringLiteral("case_id")).toString();
      if (!case_filter.isEmpty() && case_id != case_filter) {
        continue;
      }
      ++case_count;
      const auto instructions =
          evaluation_case.value(QStringLiteral("instructions")).toString();
      QStringList failures;

      photo_editor::desktop::CatalogAssetModel catalog_model;
      catalog_model.replaceAssetsForTesting({
          make_asset(asset_id, source_path, canonical_recipe),
      });
      catalog_model.selectIndex(0);
      catalog_model.toggleSelectionIndex(0);
      const auto original_catalog_recipe =
          adjustment_patch_for_asset(catalog_model.assetAt(0));
      photo_editor::desktop::AgentEditController controller;
      controller.setCatalogModel(&catalog_model);
      const auto case_kind =
          evaluation_case.value(QStringLiteral("kind"))
              .toString(QStringLiteral("global_edit"));
      const auto retouch_case = case_kind == QStringLiteral("retouch");
      if (retouch_case) {
        controller.reloadModels();
        controller.setSelectedModelId(QStringLiteral("ollama-corpus-retouch"));
        const auto style = controller.createStyle(
            QStringLiteral("Corpus Retouch · %1").arg(case_id),
            QStringLiteral("Evaluate one explicitly bounded, approval-gated retouch."),
            QVariantMap{
                {QStringLiteral("retouch"), true},
            },
            {},
            {},
            {});
        if (!style.value(QStringLiteral("ok")).toBool()) {
          failures.append(QStringLiteral("Unable to create the corpus retouch style."));
        }
      } else {
        controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
      }

      QElapsedTimer case_timer;
      case_timer.start();
      if (retouch_case) {
        qputenv(
            "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
            fake_ollama_response(corpus_retouch_intent(evaluation_case)));
      }
      auto run_report = controller.runDraftForScope(
          QStringLiteral("current-photo"), instructions);
      const auto initial_needs_user_input =
          run_report.value(QStringLiteral("needsUserInput")).toBool();
      const auto initial_input_kind =
          run_report.value(QStringLiteral("inputKind")).toString();
      const auto initial_safe_stop_reason =
          run_report.value(QStringLiteral("safeStopReason")).toString();
      const auto initial_user_questions =
          run_report.value(QStringLiteral("userQuestions")).toList();
      const auto initial_selection_confirmation =
          run_report.value(QStringLiteral("selectionConfirmation")).toMap();
      const auto initial_planned_patch_present =
          !run_report.value(QStringLiteral("plannedRecipePatch")).toMap().isEmpty();
      if (retouch_case) {
        qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
        if (run_report.value(QStringLiteral("needsUserInput")).toBool() &&
            !run_report.value(QStringLiteral("plannedRecipePatch"))
                 .toMap()
                 .isEmpty()) {
          run_report = controller.answerPendingUserInput(QStringLiteral(
              "I reviewed the bounded target and approve this reversible corpus draft."));
        }
      }
      const auto run_duration_ms = case_timer.elapsed();
      const auto asset_reports =
          run_report.value(QStringLiteral("assetReports")).toList();
      const auto asset_report = asset_reports.isEmpty()
                                    ? QVariantMap{}
                                    : asset_reports.first().toMap();
      auto validation =
          asset_report.value(QStringLiteral("validationReport")).toMap();
      if (validation.isEmpty()) {
        validation = run_report.value(QStringLiteral("validationReport")).toMap();
      }
      const auto alignment =
          validation.value(QStringLiteral("pixelIntentAlignment")).toMap();
      const auto artifacts = validation.value(QStringLiteral("artifacts")).toMap();
      const auto retouch_materialization =
          asset_report.value(QStringLiteral("retouchMaterialization")).toMap();

      if (!run_report.value(QStringLiteral("ok")).toBool()) {
        failures.append(
            run_report.value(QStringLiteral("failureReason")).toString().isEmpty()
                ? QStringLiteral("The agent draft did not complete successfully.")
                : run_report.value(QStringLiteral("failureReason")).toString());
      }
      if (!retouch_case &&
          !alignment.value(QStringLiteral("available")).toBool()) {
        failures.append(QStringLiteral("No deterministic pixel-intent evidence was produced."));
      } else if (!retouch_case &&
                 !alignment.value(QStringLiteral("ok")).toBool()) {
        failures.append(alignment.value(QStringLiteral("issues")).toStringList());
      }

      QHash<QString, bool> observed_checks;
      if (!retouch_case) {
        for (const auto& check_value :
             alignment.value(QStringLiteral("checks")).toList()) {
          const auto check = check_value.toMap();
          observed_checks.insert(
              check.value(QStringLiteral("name")).toString(),
              check.value(QStringLiteral("passed")).toBool());
        }
      }

      const QImage before_image(
          artifacts.value(QStringLiteral("beforeProofPath")).toString());
      const QImage after_image(
          artifacts.value(QStringLiteral("afterProofPath")).toString());
      auto retouch_quality =
          retouch_case
              ? validation.value(QStringLiteral("retouchProofQuality")).toMap()
              : QVariantMap{};
      if (retouch_case) {
        const auto artifact_materialized =
            retouch_materialization.value(QStringLiteral("ok")).toBool() &&
            retouch_materialization.value(QStringLiteral("materializedCount"))
                    .toInt() == 1;
        const auto full_resolution =
            retouch_materialization.value(QStringLiteral("fullResolution"))
                .toBool();
        auto retouch_checks =
            retouch_quality.value(QStringLiteral("checks")).toList();
        retouch_checks.prepend(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("fullResolutionArtifact")},
            {QStringLiteral("passed"), full_resolution},
        });
        retouch_checks.prepend(QVariantMap{
            {QStringLiteral("name"), QStringLiteral("artifactMaterialized")},
            {QStringLiteral("passed"), artifact_materialized},
        });
        retouch_quality.insert(QStringLiteral("checks"), retouch_checks);
        retouch_quality.insert(QStringLiteral("productionGateEnforced"), true);
        retouch_quality.insert(
            QStringLiteral("ok"),
            retouch_quality.value(QStringLiteral("ok")).toBool() &&
                artifact_materialized && full_resolution);
        if (!retouch_quality.value(QStringLiteral("available")).toBool()) {
          failures.append(QStringLiteral(
              "No real-photo retouch pixel evidence was produced."));
        } else if (!retouch_quality.value(QStringLiteral("ok")).toBool()) {
          failures.append(QStringLiteral(
              "The real-photo retouch quality/locality gate failed."));
        }
        for (const auto& check_value :
             retouch_quality.value(QStringLiteral("checks")).toList()) {
          const auto check = check_value.toMap();
          observed_checks.insert(
              check.value(QStringLiteral("name")).toString(),
              check.value(QStringLiteral("passed")).toBool());
        }
      }
      for (const auto& expected_value :
           evaluation_case.value(QStringLiteral("expected_pixel_checks")).toArray()) {
        const auto expected = expected_value.toString();
        if (!observed_checks.contains(expected)) {
          failures.append(QStringLiteral("Expected pixel check was not produced: %1").arg(expected));
        } else if (!observed_checks.value(expected)) {
          failures.append(QStringLiteral("Expected pixel check failed: %1").arg(expected));
        }
      }
      QJsonArray protected_region_reports;
      for (const auto& region_value :
           evaluation_case.value(QStringLiteral("protected_regions")).toArray()) {
        const auto region = region_value.toObject();
        const auto measured_delta =
            protected_region_mean_rgb_delta(before_image, after_image, region);
        const auto maximum_delta =
            region.value(QStringLiteral("max_mean_rgb_delta")).toDouble();
        const auto passed = measured_delta >= 0.0 && measured_delta <= maximum_delta;
        if (!passed) {
          failures.append(
              QStringLiteral("Protected region changed beyond tolerance: %1")
                  .arg(region.value(QStringLiteral("label")).toString()));
        }
        auto region_report = region;
        region_report.insert(QStringLiteral("measuredMeanRgbDelta"), measured_delta);
        region_report.insert(QStringLiteral("passed"), passed);
        protected_region_reports.append(region_report);
      }

      const auto draft_active_after_run = controller.draftActive();
      const auto original_recipe_restored =
          adjustment_patch_for_asset(catalog_model.assetAt(0)) ==
          original_catalog_recipe;
      const auto production_retouch_rejection =
          retouch_case &&
          retouch_quality.value(QStringLiteral("productionGateEnforced"))
              .toBool() &&
          retouch_quality.value(QStringLiteral("required")).toBool() &&
          !retouch_quality.value(QStringLiteral("ok")).toBool();
      const auto proof_rejected =
          run_report.value(QStringLiteral("proofRejected")).toBool();
      const auto approval_consumed =
          run_report.value(QStringLiteral("approvalConsumed")).toBool();
      const auto awaiting_user_input_after_run = controller.awaitingUserInput();
      const auto proof_rejection =
          run_report.value(QStringLiteral("proofRejection")).toMap();
      if (production_retouch_rejection && draft_active_after_run) {
        failures.append(QStringLiteral(
            "Production retouch rejection left a publishable draft active."));
      }
      if (production_retouch_rejection && !original_recipe_restored) {
        failures.append(QStringLiteral(
            "Production retouch rejection did not restore the original recipe."));
      }
      if (production_retouch_rejection &&
          (!proof_rejected || !approval_consumed ||
           awaiting_user_input_after_run ||
           proof_rejection.value(QStringLiteral("primaryActionLabel")).toString() !=
               QStringLiteral("Point to repair"))) {
        failures.append(QStringLiteral(
            "Production retouch rejection did not consume approval and expose an adjusted-target recovery."));
      }

      const auto machine_pass = failures.isEmpty();
      if (machine_pass) {
        ++machine_pass_count;
      }

      const auto human_review =
          evaluation_case.value(QStringLiteral("human_review")).toObject();
      const auto human_status =
          human_review.value(QStringLiteral("status")).toString();
      if (human_status == QStringLiteral("pass")) {
        ++human_pass_count;
      } else if (human_status == QStringLiteral("fail")) {
        ++human_fail_count;
      } else {
        ++human_pending_count;
      }
      if (require_human_ratings && human_status != QStringLiteral("pass")) {
        failures.append(
            human_status == QStringLiteral("fail")
                ? QStringLiteral("Human review rejected this edit.")
                : QStringLiteral("Human review is still pending."));
      }

      case_reports.append(QJsonObject{
          {QStringLiteral("caseId"), case_id},
          {QStringLiteral("caseKind"), case_kind},
          {QStringLiteral("assetId"), asset_id},
          {QStringLiteral("instructions"), instructions},
          {QStringLiteral("sourcePath"), source_path},
          {QStringLiteral("expectedPixelChecks"),
           evaluation_case.value(QStringLiteral("expected_pixel_checks")).toArray()},
          {QStringLiteral("machinePass"), machine_pass},
          {QStringLiteral("gatePass"), failures.isEmpty()},
          {QStringLiteral("runDurationMs"), run_duration_ms},
          {QStringLiteral("controllerRunOk"),
           run_report.value(QStringLiteral("ok")).toBool()},
          {QStringLiteral("controllerFailureReason"),
           run_report.value(QStringLiteral("failureReason")).toString()},
          {QStringLiteral("controllerErrors"),
           QJsonArray::fromStringList(
               run_report.value(QStringLiteral("errors")).toStringList())},
          {QStringLiteral("inputKind"),
           run_report.value(QStringLiteral("inputKind")).toString()},
          {QStringLiteral("selectionConfirmation"),
           QJsonObject::fromVariantMap(
               run_report.value(QStringLiteral("selectionConfirmation")).toMap())},
          {QStringLiteral("initialNeedsUserInput"), initial_needs_user_input},
          {QStringLiteral("initialInputKind"), initial_input_kind},
          {QStringLiteral("initialSafeStopReason"), initial_safe_stop_reason},
          {QStringLiteral("initialUserQuestions"),
           QJsonArray::fromVariantList(initial_user_questions)},
          {QStringLiteral("initialSelectionConfirmation"),
           QJsonObject::fromVariantMap(initial_selection_confirmation)},
          {QStringLiteral("initialPlannedPatchPresent"),
           initial_planned_patch_present},
          {QStringLiteral("draftActiveAfterRun"), draft_active_after_run},
          {QStringLiteral("originalRecipeRestored"), original_recipe_restored},
          {QStringLiteral("productionRetouchRejection"),
           production_retouch_rejection},
          {QStringLiteral("proofRejected"), proof_rejected},
          {QStringLiteral("approvalConsumed"), approval_consumed},
          {QStringLiteral("awaitingUserInputAfterRun"),
           awaiting_user_input_after_run},
          {QStringLiteral("proofRejection"),
           QJsonObject::fromVariantMap(proof_rejection)},
          {QStringLiteral("failures"), QJsonArray::fromStringList(failures)},
          {QStringLiteral("pixelIntentAlignment"), QJsonObject::fromVariantMap(alignment)},
          {QStringLiteral("retouchQuality"),
           QJsonObject::fromVariantMap(retouch_quality)},
          {QStringLiteral("retouchMaterialization"),
           QJsonObject::fromVariantMap(retouch_materialization)},
          {QStringLiteral("protectedRegions"), protected_region_reports},
          {QStringLiteral("humanReview"), human_review},
          {QStringLiteral("artifacts"), QJsonObject::fromVariantMap(artifacts)},
      });
      if (controller.draftActive()) {
        controller.rollbackDraft(QStringLiteral("real-photo-corpus-next-case"));
      }
    }
  }

  const auto machine_fail_count = case_count - machine_pass_count;
  const auto human_gate_pass =
      !require_human_ratings ||
      (human_pending_count == 0 && human_fail_count == 0 &&
       human_pass_count == case_count);
  const auto gate_pass =
      case_count > 0 && machine_fail_count == 0 && human_gate_pass;
  const QJsonObject report{
      {QStringLiteral("contract"), QStringLiteral("agent-real-photo-corpus-report-v1")},
      {QStringLiteral("version"), 1},
      {QStringLiteral("createdAt"), QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs)},
      {QStringLiteral("manifestPath"), manifest_info.absoluteFilePath()},
      {QStringLiteral("artifactRoot"), artifact_root},
      {QStringLiteral("requireHumanRatings"), require_human_ratings},
      {QStringLiteral("ok"), gate_pass},
      {QStringLiteral("summary"),
       QJsonObject{
           {QStringLiteral("assetCount"), asset_count},
           {QStringLiteral("caseCount"), case_count},
           {QStringLiteral("machinePassCount"), machine_pass_count},
           {QStringLiteral("machineFailCount"), machine_fail_count},
           {QStringLiteral("humanPassCount"), human_pass_count},
           {QStringLiteral("humanFailCount"), human_fail_count},
           {QStringLiteral("humanPendingCount"), human_pending_count},
       }},
      {QStringLiteral("cases"), case_reports},
  };
  QFile report_file(report_info.absoluteFilePath());
  if (!report_file.open(QIODevice::WriteOnly | QIODevice::Truncate) ||
      report_file.write(QJsonDocument(report).toJson(QJsonDocument::Indented)) <= 0) {
    return fail("real-photo corpus report should be writable");
  }
  std::cout << "agent_real_photo_corpus_report="
            << report_info.absoluteFilePath().toStdString() << '\n'
            << "agent_real_photo_corpus_machine_pass="
            << machine_pass_count << '/' << case_count << '\n'
            << "agent_real_photo_corpus_human_pending="
            << human_pending_count << '\n';
  return gate_pass ? EXIT_SUCCESS : EXIT_FAILURE;
}

}  // namespace

int run_search_edit_workflow_contract();
int run_raw_locality_probe(const QStringList& arguments);

int main(int argc, char** argv) {
  QCoreApplication app(argc, argv);
  const auto arguments = app.arguments();
  if (arguments.contains(QStringLiteral("--managed-download-handoff"))) {
    using namespace photo_editor::desktop;
    QTemporaryDir root;
    if (!root.isValid()) return fail("managed download proof needs a temporary profile");
    qputenv("PHOTO_EDITOR_APP_DATA_DIR", root.path().toUtf8());
    qputenv("PHOTO_EDITOR_OLLAMA_BINARY", "/missing/cli-must-not-be-used");
    const auto store = QDir(root.path()).filePath("managed-models");
    if (!QDir().mkpath(store) || !QDir(root.path()).mkdir("agent-models")) return EXIT_FAILURE;
    QSaveFile manifest(QDir(root.path()).filePath("agent-models/models.json"));
    const QByteArray bytes(R"({"models":[{"id":"managed-vision","name":"Managed vision","backend":"ollama","runtime":"managed","model":"qwen3-vl:32b","vision":true}]})");
    if (!manifest.open(QIODevice::WriteOnly) || manifest.write(bytes) != bytes.size() || !manifest.commit()) return EXIT_FAILURE;
    publish_managed_agent_runtime({{"state", "ready"}, {"ready", true}, {"ownsProcess", true},
        {"endpoint", "http://127.0.0.1:12345"}, {"modelsPath", store}});
    AgentEditController controller;
    controller.reloadModels();
    controller.setSelectedModelId("managed-vision");
    const auto plan = controller.prepareSelectedModelInstall();
    if (!plan.value("ok").toBool()) {
      std::cerr << QJsonDocument::fromVariant(plan).toJson(QJsonDocument::Compact).constData() << '\n';
      return fail("managed handoff proof requires the production free-space gate to pass");
    }
    if (plan.value("transport") != "pinned-manifest-digest-blobs-v1" ||
        plan.value("program") != "native-pinned-model-downloader" || !plan.value("arguments").toStringList().isEmpty() ||
        controller.installSelectedModel(false).value("ok").toBool() || controller.modelInstallRunning())
      return fail("managed installation must require confirmation and bypass the mutable-tag CLI");
    QStringList gates;
    const auto connection = QObject::connect(&controller, &AgentEditController::managedModelInstallStateChanged,
        &app, [&](const QString& model, bool installing) {
          gates.append(model + (installing ? ":start" : ":stop"));
          if (installing) controller.cancelModelInstall(); // before worker/network admission
        });
    for (int attempt = 0; attempt < 2; ++attempt) {
      const auto result = controller.installSelectedModel(true);
      if (!result.value("started").toBool() || !result.value("launchPending").toBool()) return EXIT_FAILURE;
      QElapsedTimer deadline;
      deadline.start();
      while (controller.modelInstallRunning() && deadline.elapsed() < 5000) {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        QThread::msleep(1);
      }
      if (controller.modelInstallRunning() || !controller.modelInstallStatus().startsWith("Download paused") ||
          controller.modelInstallProgress().value("ollamaModel") != "qwen3-vl:32b" ||
          controller.modelInstallProgress().value("status") != "paused")
        return fail("managed install and explicit retry must honor pre-start pause and settle their state");
    }
    QObject::disconnect(connection);
    publish_managed_agent_runtime({});
    if (gates != QStringList{"qwen3-vl:32b:start", "qwen3-vl:32b:stop", "qwen3-vl:32b:start", "qwen3-vl:32b:stop"} ||
        !QDir(store).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty())
      return fail("managed download must balance readiness gates and make no pre-canceled store writes");
    std::cout << "Managed controller: confirmed native transport, no CLI, pre-start pause, explicit retry and readiness gates pass; no weights requested.\n";
    return EXIT_SUCCESS;
  }
  if (arguments.contains(QStringLiteral("--search-edit-contract"))) return run_search_edit_workflow_contract();
  if (arguments.contains(QStringLiteral("--raw-locality-probe"))) return run_raw_locality_probe(arguments);
  if (arguments.contains(QStringLiteral("--suspend-resilience-contract"))) {
    const auto ordinary_sample =
        photo_editor::desktop::agent_proof_worker_active_clock_sample(5, 5);
    const auto boundary_sample =
        photo_editor::desktop::agent_proof_worker_active_clock_sample(5000, 5);
    const auto discontinuous_sample =
        photo_editor::desktop::agent_proof_worker_active_clock_sample(60005, 5);
    if (ordinary_sample.value(QStringLiteral("contract")).toString() !=
            QStringLiteral("agent-proof-worker-active-clock-v1") ||
        ordinary_sample.value(QStringLiteral("deadlineClock")).toString() !=
            QStringLiteral("ui-pump-active-time") ||
        ordinary_sample.value(QStringLiteral("discontinuityDetected")).toBool() ||
        ordinary_sample.value(QStringLiteral("activeDurationMs")).toLongLong() != 5 ||
        ordinary_sample.value(QStringLiteral("suspendedDurationMs")).toLongLong() != 0 ||
        boundary_sample.value(QStringLiteral("discontinuityDetected")).toBool() ||
        boundary_sample.value(QStringLiteral("activeDurationMs")).toLongLong() != 5000 ||
        discontinuous_sample.value(QStringLiteral("discontinuityThresholdMs")).toLongLong() != 5000 ||
        !discontinuous_sample.value(QStringLiteral("discontinuityDetected")).toBool() ||
        discontinuous_sample.value(QStringLiteral("activeDurationMs")).toLongLong() != 5 ||
        discontinuous_sample.value(QStringLiteral("suspendedDurationMs")).toLongLong() != 60000) {
      return fail("proof deadlines should count active UI-pump time while excluding only explicit long clock discontinuities");
    }
    std::cout << "agent_suspend_resilience_contract=passed\n";
    return EXIT_SUCCESS;
  }
  if (arguments.size() >= 2 &&
      arguments.at(1) == QStringLiteral("--real-photo-corpus")) {
    if (arguments.size() < 4) {
      return fail("usage: agent_edit_controller_test --real-photo-corpus MANIFEST REPORT [--case-id ID] [--require-human-ratings]");
    }
    const auto case_id_option = arguments.indexOf(QStringLiteral("--case-id"));
    const auto case_filter =
        case_id_option >= 0 && case_id_option + 1 < arguments.size()
            ? arguments.at(case_id_option + 1).trimmed()
            : QString{};
    return run_real_photo_corpus(
        arguments.at(2),
        arguments.at(3),
        arguments.contains(QStringLiteral("--require-human-ratings")),
        case_filter);
  }
  const QVariantList large_batch_exceptions{
      QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("asset-017")},
          {QStringLiteral("status"), QStringLiteral("needs_review")},
          {QStringLiteral("category"), QStringLiteral("warning")},
      },
      QVariantMap{
          {QStringLiteral("assetId"), QStringLiteral("asset-411")},
          {QStringLiteral("status"), QStringLiteral("failed")},
          {QStringLiteral("category"), QStringLiteral("failed")},
      },
  };
  const auto large_batch_review =
      photo_editor::desktop::agent_exception_review_summary(
          500,
          500,
          498,
          1,
          1,
          large_batch_exceptions);
  if (large_batch_review.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-exception-review-v1") ||
      large_batch_review.value(QStringLiteral("totalCount")).toInt() != 500 ||
      large_batch_review.value(QStringLiteral("verifiedCount")).toInt() != 498 ||
      large_batch_review.value(QStringLiteral("hiddenVerifiedCount")).toInt() != 498 ||
      large_batch_review.value(QStringLiteral("exceptionCount")).toInt() != 2 ||
      large_batch_review.value(QStringLiteral("reviewWorkItemCount")).toInt() != 2 ||
      large_batch_review.value(QStringLiteral("queueAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-017"), QStringLiteral("asset-411")} ||
      large_batch_review.value(QStringLiteral("categoryCounts"))
              .toMap()
              .value(QStringLiteral("warning"))
              .toInt() != 1 ||
      large_batch_review.value(QStringLiteral("categoryCounts"))
              .toMap()
              .value(QStringLiteral("failed"))
              .toInt() != 1 ||
      large_batch_review.value(QStringLiteral("headline")).toString() !=
          QStringLiteral("498 verified · 2 need review") ||
      !large_batch_review.value(QStringLiteral("queueContainsOnlyExceptions")).toBool() ||
      !large_batch_review.value(QStringLiteral("workloadScalesWithExceptions")).toBool() ||
      large_batch_review.value(QStringLiteral("reviewWorkloadPolicy")).toString() !=
          QStringLiteral("one-batch-receipt-plus-exceptions-only") ||
      !large_batch_review.value(QStringLiteral("commitBlockedByExceptions")).toBool()) {
    return fail("a 500-photo batch should collapse 498 verified edits into one receipt and create only two review work items");
  }
  const auto all_clear_large_batch =
      photo_editor::desktop::agent_exception_review_summary(
          500,
          500,
          500,
          0,
          0,
          {});
  if (!all_clear_large_batch.value(QStringLiteral("allClear")).toBool() ||
      all_clear_large_batch.value(QStringLiteral("reviewWorkItemCount")).toInt() != 0 ||
      all_clear_large_batch.value(QStringLiteral("headline")).toString() !=
          QStringLiteral("500 verified · no exceptions")) {
    return fail("a clean 500-photo batch should require no per-photo review work");
  }
  const auto consistency_stats = [](
                                     double luma,
                                     double contrast,
                                     double warmth,
                                     double green_cast,
                                     double saturation,
                                     double skin_hue,
                                     double skin_luma) {
    return QVariantMap{
        {QStringLiteral("version"), QStringLiteral("agent-image-stats-v1")},
        {QStringLiteral("histogram"),
         QVariantMap{
             {QStringLiteral("averageLuma"), luma},
             {QStringLiteral("lumaStddev"), contrast},
         }},
        {QStringLiteral("rgb"),
         QVariantMap{
             {QStringLiteral("redBlueCast"), warmth},
             {QStringLiteral("greenCast"), green_cast},
         }},
        {QStringLiteral("vectorscope"),
         QVariantMap{{QStringLiteral("averageSaturation"), saturation}}},
        {QStringLiteral("skin"),
         QVariantMap{
             {QStringLiteral("candidatePercent"), 18.0},
             {QStringLiteral("averageHue"), skin_hue},
             {QStringLiteral("averageLuma"), skin_luma},
         }},
    };
  };
  const auto consistency_asset = [&](const QString& asset_id,
                                     double tone_response,
                                     double contrast_response,
                                     double warmth_response,
                                     double green_response,
                                     double saturation_response,
                                     double skin_hue_response,
                                     double skin_luma_response) {
    const auto suffix = asset_id.right(3).toInt();
    const auto before = consistency_stats(
        72.0 + suffix,
        29.0 + static_cast<double>(suffix) * 0.15,
        0.12,
        0.05,
        0.34,
        24.0,
        118.0 + suffix);
    const auto after = consistency_stats(
        72.0 + suffix + tone_response,
        29.0 + static_cast<double>(suffix) * 0.15 + contrast_response,
        0.12 + warmth_response,
        0.05 + green_response,
        0.34 + saturation_response,
        24.0 + skin_hue_response,
        118.0 + suffix + skin_luma_response);
    return QVariantMap{
        {QStringLiteral("assetId"), asset_id},
        {QStringLiteral("clusterId"), QStringLiteral("ceremony-window")},
        {QStringLiteral("clusterRepresentativeAssetId"),
         QStringLiteral("asset-001")},
        {QStringLiteral("validationReport"),
         QVariantMap{
             {QStringLiteral("beforeImageStats"), before},
             {QStringLiteral("imageStats"), after},
         }},
    };
  };
  QVariantList aligned_consistency_assets;
  for (auto index = 1; index <= 8; ++index) {
    const auto variation = static_cast<double>(index - 1);
    aligned_consistency_assets.append(consistency_asset(
        QStringLiteral("asset-%1").arg(index, 3, 10, QLatin1Char('0')),
        10.0 + variation * 0.20,
        4.0 + variation * 0.08,
        0.025 + variation * 0.001,
        0.012 + variation * 0.0005,
        0.040 + variation * 0.001,
        1.5 + variation * 0.10,
        6.0 + variation * 0.25));
  }
  const auto aligned_consistency =
      photo_editor::desktop::agent_professional_consistency_map(
          aligned_consistency_assets);
  auto reversed_consistency_assets = aligned_consistency_assets;
  std::reverse(
      reversed_consistency_assets.begin(), reversed_consistency_assets.end());
  const auto reordered_consistency =
      photo_editor::desktop::agent_professional_consistency_map(
          reversed_consistency_assets);
  if (aligned_consistency.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-professional-consistency-map-v1") ||
      !aligned_consistency.value(QStringLiteral("measurementComplete")).toBool() ||
      !aligned_consistency.value(QStringLiteral("professionalGateEligible")).toBool() ||
      !aligned_consistency.value(QStringLiteral("consistencyPassed")).toBool() ||
      aligned_consistency.value(QStringLiteral("readyForProfessionalUse")).toBool() ||
      aligned_consistency.value(QStringLiteral("professionalReadinessClaimed")).toBool() ||
      !aligned_consistency.value(QStringLiteral("humanCalibrationRequired")).toBool() ||
      aligned_consistency.value(QStringLiteral("consistentAssetCount")).toInt() != 8 ||
      aligned_consistency.value(QStringLiteral("driftCount")).toInt() != 0 ||
      aligned_consistency.value(QStringLiteral("checkedMetricCount")).toInt() != 49 ||
      aligned_consistency.value(QStringLiteral("groups")).toList().size() != 1 ||
      aligned_consistency.value(QStringLiteral("groups"))
              .toList()
              .first()
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("aligned") ||
      aligned_consistency.value(QStringLiteral("digestSha256")).toString().size() != 64 ||
      aligned_consistency.value(QStringLiteral("digestSha256")) !=
          reordered_consistency.value(QStringLiteral("digestSha256")) ||
      !aligned_consistency.value(QStringLiteral("reproducibility"))
           .toMap()
           .value(QStringLiteral("deterministic"))
           .toBool()) {
    return fail("professional consistency proof should deterministically verify coherent relative edit response inside a lighting group");
  }
  auto drifting_consistency_assets = aligned_consistency_assets;
  drifting_consistency_assets[7] = consistency_asset(
      QStringLiteral("asset-008"),
      27.0,
      14.0,
      0.18,
      0.11,
      0.19,
      13.0,
      28.0);
  const auto drifting_consistency =
      photo_editor::desktop::agent_professional_consistency_map(
          drifting_consistency_assets);
  if (drifting_consistency.value(QStringLiteral("status")).toString() !=
          QStringLiteral("attention") ||
      drifting_consistency.value(QStringLiteral("consistencyPassed")).toBool() ||
      drifting_consistency.value(QStringLiteral("readyForProfessionalUse")).toBool() ||
      drifting_consistency.value(QStringLiteral("driftCount")).toInt() != 1 ||
      drifting_consistency.value(QStringLiteral("driftAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-008")} ||
      drifting_consistency.value(QStringLiteral("consistentAssetCount")).toInt() != 7 ||
      drifting_consistency.value(QStringLiteral("digestSha256")) ==
          aligned_consistency.value(QStringLiteral("digestSha256"))) {
    return fail("professional consistency proof should isolate a visually divergent edit response and fail the delivery gate");
  }
  const auto drifting_asset_result =
      drifting_consistency.value(QStringLiteral("assetResults"))
          .toList()
          .last()
          .toMap();
  const auto skin_sensitive_guidance =
      drifting_asset_result.value(QStringLiteral("repairGuidance")).toMap();
  const auto drifting_routing =
      photo_editor::desktop::agent_consistency_exception_routing(
          drifting_consistency,
          drifting_consistency_assets,
          {});
  const auto drifting_issue =
      drifting_routing.value(QStringLiteral("reviewIssues"))
          .toList()
          .first()
          .toMap();
  if (skin_sensitive_guidance.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-consistency-drift-repair-v1") ||
      skin_sensitive_guidance.value(QStringLiteral("autoRepairSafe")).toBool() ||
      skin_sensitive_guidance.value(QStringLiteral("driftMetricKeys"))
          .toStringList()
          .size() < 2 ||
      skin_sensitive_guidance.value(QStringLiteral("recipeDelta"))
          .toMap()
          .contains(QStringLiteral("crop")) ||
      drifting_routing.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-consistency-exception-routing-v1") ||
      drifting_routing.value(QStringLiteral("addedIssueCount")).toInt() != 1 ||
      !drifting_routing.value(QStringLiteral("allDriftRouted")).toBool() ||
      drifting_issue.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("asset-008") ||
      drifting_issue.value(QStringLiteral("category")).toString() !=
          QStringLiteral("consistency_drift") ||
      drifting_issue.value(QStringLiteral("autoRepairAvailable")).toBool()) {
    return fail("consistency drift should become one photographer-facing exception and refuse automatic skin-sensitive repair");
  }

  auto safe_drift_assets = aligned_consistency_assets;
  safe_drift_assets[7] = consistency_asset(
      QStringLiteral("asset-008"),
      10.0,
      4.0,
      0.12,
      0.012,
      0.040,
      1.5,
      6.0);
  const auto safe_drift_consistency =
      photo_editor::desktop::agent_professional_consistency_map(
          safe_drift_assets);
  const auto safe_drift_routing =
      photo_editor::desktop::agent_consistency_exception_routing(
          safe_drift_consistency,
          safe_drift_assets,
          {});
  const auto safe_drift_guidance =
      safe_drift_routing.value(QStringLiteral("reviewIssues"))
          .toList()
          .first()
          .toMap()
          .value(QStringLiteral("repairGuidance"))
          .toMap();
  if (!safe_drift_guidance.value(QStringLiteral("autoRepairSafe")).toBool() ||
      safe_drift_guidance.value(QStringLiteral("actionLabel")).toString() !=
          QStringLiteral("Match group") ||
      safe_drift_guidance.value(QStringLiteral("recipeDelta"))
          .toMap()
          .value(QStringLiteral("color"))
          .toMap()
          .value(QStringLiteral("temperature"))
          .toDouble() >= 0.0 ||
      !safe_drift_guidance.value(QStringLiteral("preservedSummary"))
           .toString()
           .contains(QStringLiteral("retouch"))) {
    return fail("a bounded color-only consistency drift should publish a minimal app-owned repair that preserves unrelated edits");
  }
  const QVariantMap consistency_source_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.25},
           {QStringLiteral("contrast"), 5.0},
       }},
      {QStringLiteral("color"),
       QVariantMap{
           {QStringLiteral("temperature"), 4.0},
           {QStringLiteral("saturation"), 8.0},
       }},
      {QStringLiteral("crop"),
       QVariantMap{{QStringLiteral("cropZoom"), 1.12}}},
      {QStringLiteral("masks"),
       QVariantMap{{QStringLiteral("maskStack"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("photographer-mask")}}}}}},
      {QStringLiteral("retouch"),
       QVariantMap{{QStringLiteral("operations"), QVariantList{QVariantMap{{QStringLiteral("id"), QStringLiteral("photographer-retouch")}}}}}},
  };
  const auto repair_application =
      photo_editor::desktop::agent_consistency_repair_patch(
          consistency_source_patch,
          safe_drift_guidance);
  const auto repaired_consistency_patch =
      repair_application.value(QStringLiteral("recipePatch")).toMap();
  const auto unsafe_repair_application =
      photo_editor::desktop::agent_consistency_repair_patch(
          consistency_source_patch,
          skin_sensitive_guidance);
  if (repair_application.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-consistency-repair-application-v1") ||
      !repair_application.value(QStringLiteral("ok")).toBool() ||
      !repair_application.value(QStringLiteral("applied")).toBool() ||
      repaired_consistency_patch.value(QStringLiteral("color"))
              .toMap()
              .value(QStringLiteral("temperature"))
              .toDouble() >=
          consistency_source_patch.value(QStringLiteral("color"))
              .toMap()
              .value(QStringLiteral("temperature"))
              .toDouble() ||
      repaired_consistency_patch.value(QStringLiteral("globalTone")) !=
          consistency_source_patch.value(QStringLiteral("globalTone")) ||
      repaired_consistency_patch.value(QStringLiteral("crop")) !=
          consistency_source_patch.value(QStringLiteral("crop")) ||
      repaired_consistency_patch.value(QStringLiteral("masks")) !=
          consistency_source_patch.value(QStringLiteral("masks")) ||
      repaired_consistency_patch.value(QStringLiteral("retouch")) !=
          consistency_source_patch.value(QStringLiteral("retouch")) ||
      unsafe_repair_application.value(QStringLiteral("ok")).toBool() ||
      unsafe_repair_application.value(QStringLiteral("recipePatch")).toMap() !=
          consistency_source_patch) {
    return fail("consistency repair should apply only its bounded tone/color delta and leave photographer-authored crop, masks, and retouch byte-for-byte unchanged");
  }
  auto accepted_drift_assets = safe_drift_assets;
  auto accepted_drift_report = accepted_drift_assets.last().toMap();
  accepted_drift_report.insert(
      QStringLiteral("reviewIssueResolution"),
      QStringLiteral("accepted_anyway"));
  accepted_drift_assets[accepted_drift_assets.size() - 1] =
      accepted_drift_report;
  const auto accepted_drift_routing =
      photo_editor::desktop::agent_consistency_exception_routing(
          safe_drift_consistency,
          accepted_drift_assets,
          {});
  if (!accepted_drift_routing.value(QStringLiteral("reviewIssues"))
           .toList()
           .isEmpty() ||
      accepted_drift_routing.value(QStringLiteral("resolvedAssetIds"))
              .toStringList() !=
          QStringList{QStringLiteral("asset-008")} ||
      !accepted_drift_routing.value(QStringLiteral("allDriftRouted")).toBool()) {
    return fail("an explicitly accepted consistency difference should stay visible in the map without returning to the exception queue");
  }
  if (arguments.contains(QStringLiteral("--consistency-director-contract"))) {
    std::cout << "agent_consistency_director_contract=passed\n";
    return EXIT_SUCCESS;
  }
  QImage discontinuous_before(200, 100, QImage::Format_ARGB32);
  for (auto y = 0; y < discontinuous_before.height(); ++y) {
    for (auto x = 0; x < discontinuous_before.width(); ++x) {
      discontinuous_before.setPixelColor(
          x,
          y,
          QColor(104 + x / 20, 126 + y / 20, 164));
    }
  }
  auto discontinuous_after = discontinuous_before;
  for (auto y = 35; y < 65; ++y) {
    for (auto x = 70; x < 130; ++x) {
      const auto normalized_x =
          (static_cast<double>(x) + 0.5 - 100.0) / 20.0;
      const auto normalized_y =
          (static_cast<double>(y) + 0.5 - 50.0) / 10.0;
      const auto distance_squared =
          normalized_x * normalized_x + normalized_y * normalized_y;
      if (distance_squared > 2.25) {
        continue;
      }
      const auto before_pixel = discontinuous_before.pixelColor(x, y);
      const auto offset =
          distance_squared <= 1.0 ? 20 : ((x + y) % 2 == 0 ? 10 : -10);
      discontinuous_after.setPixelColor(
          x,
          y,
          QColor(
              std::clamp(before_pixel.red() + offset, 0, 255),
              std::clamp(before_pixel.green() + offset, 0, 255),
              std::clamp(before_pixel.blue() + offset, 0, 255)));
    }
  }
  const auto discontinuous_quality = evaluate_corpus_retouch_proof(
      discontinuous_before,
      discontinuous_after,
      QJsonObject{
          {QStringLiteral("retouch"),
           QJsonObject{
               {QStringLiteral("region"),
                QJsonObject{
                    {QStringLiteral("center_x"), 0.5},
                    {QStringLiteral("center_y"), 0.5},
                    {QStringLiteral("radius_x"), 0.1},
                    {QStringLiteral("radius_y"), 0.1},
                }},
           }},
      },
      QVariantMap{
          {QStringLiteral("ok"), true},
          {QStringLiteral("materializedCount"), 1},
          {QStringLiteral("fullResolution"), true},
          {QStringLiteral("artifacts"),
           QVariantList{
               QVariantMap{
                   {QStringLiteral("sourceWidth"), 200},
                   {QStringLiteral("sourceHeight"), 100},
                   {QStringLiteral("sourceBoundsX"), 70},
                   {QStringLiteral("sourceBoundsY"), 35},
                   {QStringLiteral("sourceBoundsWidth"), 60},
                   {QStringLiteral("sourceBoundsHeight"), 30},
               },
           }},
      });
  auto discontinuous_background_continuity = true;
  for (const auto& check_value :
       discontinuous_quality.value(QStringLiteral("checks")).toList()) {
    const auto check = check_value.toMap();
    if (check.value(QStringLiteral("name")).toString() ==
        QStringLiteral("backgroundContinuity")) {
      discontinuous_background_continuity =
          check.value(QStringLiteral("passed")).toBool();
    }
  }
  if (!discontinuous_quality.value(QStringLiteral("available")).toBool() ||
      discontinuous_quality.value(QStringLiteral("ok")).toBool() ||
      discontinuous_background_continuity ||
      discontinuous_quality
              .value(QStringLiteral("surroundingChangedFraction"))
              .toDouble() <= 0.75 ||
      discontinuous_quality
              .value(QStringLiteral("surroundingGradientEnergyRatio"))
              .toDouble() <= 1.35) {
    return fail("retouch proof should reject a changed, textured halo around the reviewed target");
  }
  auto crosshatched_after = discontinuous_before;
  for (auto y = 40; y < 60; ++y) {
    for (auto x = 80; x < 120; ++x) {
      const auto normalized_x =
          (static_cast<double>(x) + 0.5 - 100.0) / 20.0;
      const auto normalized_y =
          (static_cast<double>(y) + 0.5 - 50.0) / 10.0;
      if (normalized_x * normalized_x + normalized_y * normalized_y > 1.0) {
        continue;
      }
      const auto before_pixel = discontinuous_before.pixelColor(x, y);
      const auto offset = (x + y) % 2 == 0 ? 10 : -10;
      crosshatched_after.setPixelColor(
          x,
          y,
          QColor(
              std::clamp(before_pixel.red() + offset, 0, 255),
              std::clamp(before_pixel.green() + offset, 0, 255),
              std::clamp(before_pixel.blue() + offset, 0, 255)));
    }
  }
  const auto crosshatched_quality = evaluate_corpus_retouch_proof(
      discontinuous_before,
      crosshatched_after,
      QJsonObject{
          {QStringLiteral("retouch"),
           QJsonObject{
               {QStringLiteral("region"),
                QJsonObject{
                    {QStringLiteral("center_x"), 0.5},
                    {QStringLiteral("center_y"), 0.5},
                    {QStringLiteral("radius_x"), 0.1},
                    {QStringLiteral("radius_y"), 0.1},
                }},
           }},
      },
      QVariantMap{
          {QStringLiteral("ok"), true},
          {QStringLiteral("materializedCount"), 1},
          {QStringLiteral("fullResolution"), true},
          {QStringLiteral("artifacts"),
           QVariantList{
               QVariantMap{
                   {QStringLiteral("sourceWidth"), 200},
                   {QStringLiteral("sourceHeight"), 100},
                   {QStringLiteral("sourceBoundsX"), 70},
                   {QStringLiteral("sourceBoundsY"), 35},
                   {QStringLiteral("sourceBoundsWidth"), 60},
                   {QStringLiteral("sourceBoundsHeight"), 30},
               },
           }},
      });
  auto crosshatched_background_continuity = true;
  for (const auto& check_value :
       crosshatched_quality.value(QStringLiteral("checks")).toList()) {
    const auto check = check_value.toMap();
    if (check.value(QStringLiteral("name")).toString() ==
        QStringLiteral("backgroundContinuity")) {
      crosshatched_background_continuity =
          check.value(QStringLiteral("passed")).toBool();
    }
  }
  if (crosshatched_quality.value(QStringLiteral("ok")).toBool() ||
      crosshatched_background_continuity ||
      crosshatched_quality
              .value(QStringLiteral("surroundingChangedFraction"))
              .toDouble() > 0.01 ||
      crosshatched_quality
              .value(QStringLiteral("reconstructionGradientEnergyRatio"))
              .toDouble() <= 1.6) {
    return fail("retouch proof should reject crosshatch inside an otherwise local target");
  }
  const auto focused_cancel_render_contract =
      argc > 1 &&
      QString::fromLocal8Bit(argv[1]) ==
          QStringLiteral("--cancel-render-contract");
  QCoreApplication::setOrganizationName(QStringLiteral("PhotoEditorTests"));
  QCoreApplication::setApplicationName(QStringLiteral("AgentEditControllerTest"));

  QTemporaryDir runtime_root;
  if (!runtime_root.isValid()) {
    return fail("temporary runtime directory should be created");
  }
  qputenv("PHOTO_EDITOR_APP_DATA_DIR", runtime_root.path().toUtf8());
  qputenv(
      "PHOTO_EDITOR_OBSERVABILITY_RUN_DIR",
      QDir(runtime_root.path()).filePath(QStringLiteral("observability")).toUtf8());
  photo_editor::desktop::observability::initialize();
  const auto observability_log_path =
      photo_editor::desktop::observability::desktopLogFilePath();

  const auto source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source.png"));
  if (!write_fixture_image(source_path)) {
    return fail("fixture image should be written");
  }
  const auto second_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-2.png"));
  if (!write_fixture_image(second_source_path)) {
    return fail("second fixture image should be written");
  }
  const auto third_source_path = QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/source-3.png"));
  if (!write_fixture_image(third_source_path)) {
    return fail("third fixture image should be written");
  }
  const auto retouch_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/retouch-source.png"));
  if (!write_retouch_fixture_image(retouch_source_path)) {
    return fail("retouch fixture image should be written");
  }
  const auto semantic_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/semantic-source.png"));
  if (!write_semantic_fixture_image(semantic_source_path)) {
    return fail("semantic fixture image should be written");
  }
  const auto ambiguous_object_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/ambiguous-objects.png"));
  if (!write_ambiguous_object_fixture_image(ambiguous_object_source_path)) {
    return fail("ambiguous object fixture image should be written");
  }
  const auto original_source_bytes = read_file_bytes(source_path);
  const auto original_second_source_bytes = read_file_bytes(second_source_path);
  const auto original_third_source_bytes = read_file_bytes(third_source_path);
  const auto original_semantic_source_bytes = read_file_bytes(semantic_source_path);
  const auto original_ambiguous_object_source_bytes =
      read_file_bytes(ambiguous_object_source_path);
  if (original_source_bytes.isEmpty() ||
      original_second_source_bytes.isEmpty() ||
      original_third_source_bytes.isEmpty() ||
      original_semantic_source_bytes.isEmpty() ||
      original_ambiguous_object_source_bytes.isEmpty()) {
    return fail("fixture source image bytes should be readable before agent workflows");
  }

  photo_editor::desktop::EditRecipeState original_state;
  original_state.exposure = 0.0;
  original_state.contrast = 0.0;
  original_state.saturation = 0.0;
  original_state.output_color_space = QStringLiteral("Display P3");
  const auto original_recipe = photo_editor::desktop::serialize_edit_recipe_json(original_state);
  photo_editor::desktop::EditRecipeState second_original_state;
  second_original_state.exposure = 0.0;
  second_original_state.contrast = 0.0;
  second_original_state.saturation = 0.0;
  second_original_state.temperature = 12.0;
  const auto second_original_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(second_original_state);
  photo_editor::desktop::EditRecipeState third_original_state;
  third_original_state.exposure = 0.0;
  third_original_state.contrast = 0.0;
  third_original_state.saturation = 0.0;
  third_original_state.temperature = 2.0;
  const auto third_original_recipe =
      photo_editor::desktop::serialize_edit_recipe_json(third_original_state);

  photo_editor::desktop::CatalogAssetModel catalog_model;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  photo_editor::desktop::AgentEditController controller;
  photo_editor::desktop::ExportJobController export_jobs;
  controller.setCatalogModel(&catalog_model);
  controller.setExportJobController(&export_jobs);
  const auto accept_all_review_issues = [&controller]() {
    const auto issues =
        controller.lastRunReport()
            .value(QStringLiteral("batchSummary"))
            .toMap()
            .value(QStringLiteral("reviewIssues"))
            .toList();
    for (const auto& issue_value : issues) {
      const auto accepted = controller.acceptReviewIssue(
          issue_value.toMap().value(QStringLiteral("assetId")).toString());
      if (!accepted.value(QStringLiteral("ok")).toBool()) {
        return false;
      }
    }
    return true;
  };

  const auto single_photo_forecast =
      controller.batchReviewForecast(QStringLiteral("selected-assets"));
  if (!single_photo_forecast.value(QStringLiteral("ok")).toBool() ||
      single_photo_forecast.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-batch-review-forecast-v1") ||
      single_photo_forecast.value(QStringLiteral("totalCount")).toInt() != 1 ||
      single_photo_forecast.value(QStringLiteral("sampleCount")).toInt() != 1 ||
      single_photo_forecast.value(QStringLiteral("predictionAvailable")).toBool() ||
      single_photo_forecast.value(QStringLiteral("storesImageBytes")).toBool() ||
      single_photo_forecast.value(QStringLiteral("storesSourcePaths")).toBool()) {
    return fail("single-photo preflight should promise full proof without inventing a batch exception estimate");
  }
  catalog_model.toggleSelectionIndex(1);
  const auto calibrating_batch_forecast =
      controller.batchReviewForecast(QStringLiteral("selected-assets"));
  if (!calibrating_batch_forecast.value(QStringLiteral("ok")).toBool() ||
      calibrating_batch_forecast.value(QStringLiteral("totalCount")).toInt() != 2 ||
      calibrating_batch_forecast.value(QStringLiteral("predictionAvailable")).toBool() ||
      calibrating_batch_forecast.value(QStringLiteral("confidence")).toString() !=
          QStringLiteral("calibrating") ||
      !calibrating_batch_forecast.value(QStringLiteral("headline"))
           .toString()
           .contains(QStringLiteral("forecast calibrating"))) {
    return fail("a new project should disclose sample-first calibration instead of fabricating review counts");
  }
  catalog_model.toggleSelectionIndex(1);

  auto story_asset_1 =
      make_asset(QStringLiteral("story-1"), source_path, original_recipe);
  auto story_asset_2 =
      make_asset(QStringLiteral("story-2"), second_source_path, original_recipe);
  auto story_asset_3 =
      make_asset(QStringLiteral("story-3"), third_source_path, original_recipe);
  story_asset_3.rating = 5;
  story_asset_3.picked = true;
  auto story_asset_4 =
      make_asset(QStringLiteral("story-4"), source_path, second_original_recipe);
  story_asset_4.camera_label = QStringLiteral("Second Camera");
  auto story_asset_5 =
      make_asset(QStringLiteral("story-5"), second_source_path, second_original_recipe);
  story_asset_5.camera_label = QStringLiteral("Second Camera");
  catalog_model.replaceAssetsForTesting(
      {story_asset_1, story_asset_2, story_asset_3, story_asset_4,
       story_asset_5});
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  for (auto index = 0; index < 5; ++index) {
    catalog_model.toggleSelectionIndex(index);
  }
  const auto story_map =
      controller.assignmentStoryMapForScope(QStringLiteral("selected-assets"));
  const auto repeated_story_map =
      controller.assignmentStoryMapForScope(QStringLiteral("selected-assets"));
  QStringList story_roles;
  for (const auto& assignment_value :
       story_map.value(QStringLiteral("assignments")).toList()) {
    story_roles.append(
        assignment_value.toMap().value(QStringLiteral("role")).toString());
  }
  const auto story_forecast =
      controller.batchReviewForecast(QStringLiteral("selected-assets"));
  const auto hero_assignment =
      story_map.value(QStringLiteral("assignmentsByAssetId"))
          .toMap()
          .value(story_map.value(QStringLiteral("heroAssetId")).toString())
          .toMap();
  const auto hero_role_evidence =
      hero_assignment.value(QStringLiteral("roleEvidence")).toMap();
  const auto hero_role_evidence_signals =
      hero_role_evidence.value(QStringLiteral("signals")).toStringList();
  const auto hero_role_has_explicit_signal =
      hero_role_evidence_signals.contains(QStringLiteral("explicit-rating")) ||
      hero_role_evidence_signals.contains(QStringLiteral("pick-state")) ||
      hero_role_evidence_signals.contains(
          QStringLiteral("best-shot-suggestion"));
  if (!story_map.value(QStringLiteral("available")).toBool() ||
      story_map.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-assignment-story-map-v1") ||
      story_map.value(QStringLiteral("assetCount")).toInt() != 5 ||
      story_map.value(QStringLiteral("heroBasis")).toString() !=
          QStringLiteral("explicit-curation") ||
      !hero_assignment.value(QStringLiteral("explicitCurationEvidence")).toBool() ||
      story_map.value(QStringLiteral("roleEvidenceContract")).toString() !=
          QStringLiteral("agent-assignment-story-role-evidence-v1") ||
      hero_role_evidence.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-assignment-story-role-evidence-v1") ||
      hero_role_evidence.value(QStringLiteral("basis")).toString() !=
          QStringLiteral("explicit-curation") ||
      hero_role_evidence.value(QStringLiteral("semanticRecognitionUsed")).toBool() ||
      !hero_role_has_explicit_signal ||
      story_map.value(QStringLiteral("storySampleAssetIds")).toStringList().size() != 4 ||
      story_map.value(QStringLiteral("digestSha256")).toString().size() != 64 ||
      story_map.value(QStringLiteral("digestSha256")) !=
          repeated_story_map.value(QStringLiteral("digestSha256")) ||
      !story_roles.contains(QStringLiteral("hero")) ||
      !story_roles.contains(QStringLiteral("atmosphere")) ||
      !story_roles.contains(QStringLiteral("detail")) ||
      !story_roles.contains(QStringLiteral("transition")) ||
      !story_roles.contains(QStringLiteral("support")) ||
      story_map.value(QStringLiteral("semanticRecognitionUsed")).toBool() ||
      !story_map.value(QStringLiteral("noAutoCull")).toBool() ||
      !story_map.value(QStringLiteral("sourceOrderPreserved")).toBool() ||
      story_map.value(QStringLiteral("recipeVariationApplied")).toBool() ||
      story_map.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      story_map.value(QStringLiteral("planningInfluence")).toString() !=
          QStringLiteral("sample-review-coverage-only") ||
      story_forecast.value(QStringLiteral("sampleAssetIds")).toStringList().size() < 4 ||
      story_forecast.value(QStringLiteral("assignmentStoryMap"))
              .toMap()
              .value(QStringLiteral("digestSha256")) !=
          story_map.value(QStringLiteral("digestSha256"))) {
    std::cerr
        << "assignment_story_map="
        << QJsonDocument::fromVariant(story_map)
               .toJson(QJsonDocument::Compact)
               .constData()
        << '\n'
        << "assignment_story_forecast="
        << QJsonDocument::fromVariant(story_forecast)
               .toJson(QJsonDocument::Compact)
               .constData()
        << '\n';
    return fail("assignment story map should deterministically turn curation, sequence, and lighting boundaries into bounded sample-review coverage");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path,
                 second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  const auto inspiration = controller.suggestInspirationForPrompt(
      QStringLiteral("Warm editorial portrait with soft highlights and natural skin"));
  if (!inspiration.value(QStringLiteral("ok")).toBool() ||
      inspiration.value(QStringLiteral("suggestionEngine")).toString() !=
          QStringLiteral("local-prompt-context-ranker-v1") ||
      inspiration.value(QStringLiteral("styles")).toList().isEmpty() ||
      inspiration.value(QStringLiteral("photos")).toList().size() != 2) {
    return fail("prompt-first agent flow should suggest usable presets and catalog references before targets");
  }

  const auto model_manifest_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-models/models.json"));
  if (!QDir().mkpath(QFileInfo(model_manifest_path).dir().absolutePath())) {
    return fail("agent model manifest directory should be created");
  }
  QFile model_manifest_file(model_manifest_path);
  if (!model_manifest_file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("agent model manifest should be writable");
  }
  model_manifest_file.write(QJsonDocument(QJsonObject{
      {QStringLiteral("models"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("id"), QStringLiteral("ollama-qwen-test")},
               {QStringLiteral("name"), QStringLiteral("Qwen test via Ollama")},
               {QStringLiteral("backend"), QStringLiteral("ollama")},
               {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
               {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
               {QStringLiteral("vision"), true},
               {QStringLiteral("available"), true},
           },
       }},
  }).toJson(QJsonDocument::Compact));
  model_manifest_file.close();
  controller.reloadModels();
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto ollama_setup = controller.prepareSelectedModelInstall();
  if (!ollama_setup.value(QStringLiteral("ok")).toBool() ||
      !ollama_setup.value(QStringLiteral("installable")).toBool() ||
      ollama_setup.value(QStringLiteral("command")).toString() != QStringLiteral("ollama pull qwen2.5vl:7b") ||
      ollama_setup.value(QStringLiteral("arguments")).toStringList() !=
          QStringList{QStringLiteral("pull"), QStringLiteral("qwen2.5vl:7b")}) {
    return fail("Ollama model setup should expose a bounded local pull command");
  }
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  const auto controller_probe = controller.probeSelectedModel();
  const auto probed_ollama_model = model_by_id(controller.availableModels(), QStringLiteral("ollama-qwen-test"));
  if (!controller_probe.value(QStringLiteral("ok")).toBool() ||
      !controller_probe.value(QStringLiteral("available")).toBool() ||
      probed_ollama_model.value(QStringLiteral("status")).toString() != QStringLiteral("available") ||
      probed_ollama_model.value(QStringLiteral("modelFamily")).toString() != QStringLiteral("qwen2") ||
      probed_ollama_model.value(QStringLiteral("parameterSize")).toString() != QStringLiteral("7B") ||
      controller.modelInstallStatus() != QStringLiteral("Model available")) {
    return fail("controller model probe should update selected Ollama model runtime status");
  }
  QSaveFile stale_model_manifest(model_manifest_path);
  const auto stale_model_manifest_payload = QJsonDocument(QJsonObject{
      {QStringLiteral("models"), QJsonArray{QJsonObject{
          {QStringLiteral("id"), QStringLiteral("ollama-qwen-test")},
          {QStringLiteral("name"), QStringLiteral("Qwen test via Ollama")},
          {QStringLiteral("backend"), QStringLiteral("ollama")},
          {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("vision"), true},
          {QStringLiteral("status"), QStringLiteral("unavailable")},
          {QStringLiteral("available"), false},
      }}},
  }).toJson(QJsonDocument::Compact);
  if (!stale_model_manifest.open(QIODevice::WriteOnly) ||
      stale_model_manifest.write(stale_model_manifest_payload) !=
          stale_model_manifest_payload.size() ||
      !stale_model_manifest.commit()) {
    return fail("stale Ollama model status should be writable for readiness regression coverage");
  }
  controller.reloadModels();
  const auto stale_ollama_workflow = controller.aiEditWorkflowState();
  if (stale_ollama_workflow.value(QStringLiteral("setupRequired")).toBool() ||
      stale_ollama_workflow.value(QStringLiteral("primaryActionKey")).toString() !=
          QStringLiteral("check_local_model")) {
    return fail("a configured external Ollama model with stale availability should offer a connection check, not ask to set up or redownload it");
  }
  controller.probeSelectedModelAsync(true);
  QElapsedTimer async_model_probe_timeout;
  async_model_probe_timeout.start();
  while (controller.modelProbeRunning() && async_model_probe_timeout.elapsed() < 5000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
    QThread::msleep(1);
  }
  const auto refreshed_ollama_model = model_by_id(
      controller.availableModels(), QStringLiteral("ollama-qwen-test"));
  if (controller.modelProbeRunning() ||
      !refreshed_ollama_model.value(QStringLiteral("available")).toBool() ||
      controller.aiEditWorkflowState()
              .value(QStringLiteral("primaryActionKey"))
              .toString() != QStringLiteral("create_samples")) {
    return fail("background Ollama probing should recognize an already-installed model and clear the setup action");
  }
  QSaveFile restored_model_manifest(model_manifest_path);
  const auto restored_model_manifest_payload = QJsonDocument(QJsonObject{
      {QStringLiteral("models"), QJsonArray{QJsonObject{
          {QStringLiteral("id"), QStringLiteral("ollama-qwen-test")},
          {QStringLiteral("name"), QStringLiteral("Qwen test via Ollama")},
          {QStringLiteral("backend"), QStringLiteral("ollama")},
          {QStringLiteral("endpoint"), QStringLiteral("http://127.0.0.1:11434")},
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("vision"), true},
          {QStringLiteral("status"), QStringLiteral("available")},
          {QStringLiteral("available"), true},
      }}},
  }).toJson(QJsonDocument::Compact);
  if (!restored_model_manifest.open(QIODevice::WriteOnly) ||
      restored_model_manifest.write(restored_model_manifest_payload) !=
          restored_model_manifest_payload.size() ||
      !restored_model_manifest.commit()) {
    return fail("Ollama test model availability should be restored after stale-status coverage");
  }
  controller.reloadModels();
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto unconfirmed_install = controller.installSelectedModel(false);
  if (unconfirmed_install.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_install.value(QStringLiteral("confirmationRequired")).toBool()) {
    return fail("Ollama install should require confirmation before starting a process");
  }
  const auto fake_ollama_path = QDir(runtime_root.path()).filePath(QStringLiteral("fake-ollama.sh"));
  QFile fake_ollama(fake_ollama_path);
  if (!fake_ollama.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
    return fail("fake Ollama executable should be writable");
  }
  fake_ollama.write(
      "#!/bin/sh\n"
      "printf '{\"status\":\"pulling manifest\"}\\n'\n"
      "printf '{\"status\":\"downloading\",\"digest\":\"sha256:test-layer\",\"total\":100,\"completed\":25}\\n'\n"
      "printf '{\"status\":\"downloading\",\"digest\":\"sha256:test-layer\",\"total\":100,\"completed\":100}\\n'\n"
      "printf '{\"status\":\"success\"}\\n'\n"
      "printf 'pulled %s %s\\n' \"$1\" \"$2\"\n"
      "exit 0\n");
  fake_ollama.close();
  QFile::setPermissions(
      fake_ollama_path,
      QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", fake_ollama_path.toUtf8());
  const auto confirmed_install = controller.installSelectedModel(true);
  if (!confirmed_install.value(QStringLiteral("ok")).toBool() ||
      !confirmed_install.value(QStringLiteral("started")).toBool() ||
      !controller.modelInstallRunning()) {
    return fail("confirmed Ollama install should start the configured local process");
  }
  for (auto attempt = 0; attempt < 100 && controller.modelInstallRunning(); ++attempt) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(10);
  }
  if (controller.modelInstallRunning() ||
      controller.modelInstallStatus() != QStringLiteral("Installed") ||
      !controller.modelInstallOutput().contains(QStringLiteral("pulled pull qwen2.5vl:7b"))) {
    return fail("confirmed Ollama install should capture process completion and output");
  }
  const auto install_progress = controller.modelInstallProgress();
  if (install_progress.value(QStringLiteral("status")).toString() != QStringLiteral("success") ||
      install_progress.value(QStringLiteral("digest")).toString() != QStringLiteral("sha256:test-layer") ||
      install_progress.value(QStringLiteral("completedBytes")).toLongLong() != 100 ||
      install_progress.value(QStringLiteral("totalBytes")).toLongLong() != 100 ||
      install_progress.value(QStringLiteral("progressPercent")).toInt() != 100 ||
      install_progress.value(QStringLiteral("jsonLineCount")).toInt() < 4) {
    return fail("confirmed Ollama install should parse streaming pull progress JSON");
  }
  const auto slow_ollama_path = QDir(runtime_root.path()).filePath(QStringLiteral("slow-ollama.sh"));
  QFile slow_ollama(slow_ollama_path);
  if (!slow_ollama.open(QIODevice::WriteOnly) || slow_ollama.write("#!/bin/sh\nexec sleep 10\n") < 0)
    return fail("slow installer fixture should be writable");
  slow_ollama.close();
  QFile::setPermissions(slow_ollama_path, QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", slow_ollama_path.toUtf8());
  QElapsedTimer install_launch_timer;
  install_launch_timer.start();
  const auto cancelable_install = controller.installSelectedModel(true);
  if (!cancelable_install.value("launchPending").toBool() || install_launch_timer.elapsed() > 250)
    return fail("installer launch must return asynchronously");
  controller.cancelModelInstall();
  for (auto attempt = 0; attempt < 250 && controller.modelInstallRunning(); ++attempt) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(10);
  }
  if (controller.modelInstallRunning() || !controller.modelInstallStatus().startsWith("Download paused"))
    return fail("canceling even before process startup must stop only the owned installer");
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", fake_ollama_path.toUtf8());
  if (!controller.installSelectedModel(true).value("ok").toBool())
    return fail("a canceled download should permit an explicit retry");
  for (auto attempt = 0; attempt < 100 && controller.modelInstallRunning(); ++attempt) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
    QThread::msleep(10);
  }
  if (controller.modelInstallRunning() || controller.modelInstallStatus() != "Installed")
    return fail("retry after cancellation should complete the new owned installer");
  qunsetenv("PHOTO_EDITOR_OLLAMA_BINARY");
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  const auto invalid_style = controller.createStyle(
      QString(),
      QStringLiteral("Needs a name"),
      {},
      {},
      {},
      {});
  if (invalid_style.value(QStringLiteral("ok")).toBool()) {
    return fail("style creation should require a name and goal");
  }

  const auto invalid_described_style =
      controller.createStyleFromDescription(QStringLiteral("  "));
  if (invalid_described_style.value(QStringLiteral("ok")).toBool()) {
    return fail("plain-language style creation should require a useful description");
  }
  const auto described_style = controller.createStyleFromDescription(
      QStringLiteral(
          "Quiet cinematic color with natural skin, soft highlights, and cool dense shadows."));
  const auto described_style_id =
      described_style.value(QStringLiteral("styleId")).toString();
  QVariantMap described_style_data;
  for (const auto& style_value : controller.availableStyles()) {
    const auto style = style_value.toMap();
    if (style.value(QStringLiteral("id")).toString() == described_style_id) {
      described_style_data = style;
      break;
    }
  }
  if (!described_style.value(QStringLiteral("ok")).toBool() ||
      described_style.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("described-style") ||
      described_style.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-described-style-start-v1") ||
      !described_style.value(QStringLiteral("advancedFieldsGenerated")).toBool() ||
      !described_style.value(QStringLiteral("rememberedForProject")).toBool() ||
      described_style.value(QStringLiteral("generatedFieldGroups"))
              .toStringList()
              .size() != 4 ||
      described_style_id.isEmpty() ||
      controller.selectedStyleId() != described_style_id ||
      described_style_data.value(QStringLiteral("goal")).toString() !=
          QStringLiteral(
              "Quiet cinematic color with natural skin, soft highlights, and cool dense shadows.") ||
      !described_style_data.value(QStringLiteral("allowedTools"))
           .toMap()
           .value(QStringLiteral("masks"))
           .toBool() ||
      described_style_data.value(QStringLiteral("allowedTools"))
              .toMap()
              .value(QStringLiteral("retouch"))
              .toString() != QStringLiteral("minor_cleanup_only") ||
      !described_style_data.value(QStringLiteral("validation"))
           .toMap()
           .value(QStringLiteral("require_before_after_proof"))
           .toBool() ||
      !described_style_data.value(QStringLiteral("batchBehavior"))
           .toMap()
           .value(QStringLiteral("sample_first"))
           .toBool()) {
    return fail("plain-language style creation should generate a safe project-sticky style without exposing advanced fields");
  }

  const QVariantMap allowed_tools{
      {QStringLiteral("raw"), true},
      {QStringLiteral("global_color"), true},
      {QStringLiteral("curves"), true},
      {QStringLiteral("hsl"), true},
      {QStringLiteral("color_grading"), false},
      {QStringLiteral("detail"), true},
      {QStringLiteral("crop"), QStringLiteral("conservative")},
      {QStringLiteral("masks"), true},
      {QStringLiteral("retouch"), false},
      {QStringLiteral("layers_luts"), false},
      {QStringLiteral("export"), false},
  };
  const auto created_style = controller.createStyle(
      QStringLiteral("Moody Test Style"),
      QStringLiteral("Moody editorial contrast with protected highlights and restrained saturation."),
      allowed_tools,
      QVariantMap{
          {QStringLiteral("avoid_clipped_highlights"), true},
          {QStringLiteral("avoid_crushed_blacks"), true},
          {QStringLiteral("max_exposure_shift"), 0.9},
          {QStringLiteral("max_temperature_shift"), 1200},
          {QStringLiteral("max_saturation_shift"), 10},
      },
      QVariantMap{
          {QStringLiteral("highlight_clip_limit_percent"), 0.4},
          {QStringLiteral("shadow_clip_limit_percent"), 0.8},
          {QStringLiteral("require_before_after_proof"), true},
      },
      QVariantMap{
          {QStringLiteral("cluster_by_lighting"), true},
          {QStringLiteral("maintain_sequence_consistency"), true},
          {QStringLiteral("correct_outliers_individually"), true},
      });
  if (!created_style.value(QStringLiteral("ok")).toBool()) {
    std::cerr << created_style.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("style creation should persist a valid user style");
  }
  if (controller.selectedStyleId() != QStringLiteral("moody-test-style")) {
    return fail("newly created style should become the selected agent style");
  }
  const QVariantMap freedom_crop_patch{
      {QStringLiteral("crop"),
       QVariantMap{{QStringLiteral("cropX"), 0.04},
                   {QStringLiteral("cropY"), 0.04},
                   {QStringLiteral("cropWidth"), 0.92},
                   {QStringLiteral("cropHeight"), 0.92}}},
  };
  const QVariantMap freedom_grading_patch{
      {QStringLiteral("colorGrading"),
       QVariantMap{{QStringLiteral("gradingAmount"), 18.0}}},
  };
  const QVariantMap freedom_layer_effect_patch{
      {QStringLiteral("layers"),
       QVariantMap{{QStringLiteral("effectChanges"),
                    QVariantList{QVariantMap{
                        {QStringLiteral("layerId"), QStringLiteral("existing-layer")},
                        {QStringLiteral("effectId"), QStringLiteral("agent-effect-transform-test")},
                        {QStringLiteral("type"), QStringLiteral("gaussian_blur")},
                        {QStringLiteral("enabled"), true},
                        {QStringLiteral("amount"), 0.4},
                        {QStringLiteral("sigma"), 5.0},
                    }}}}},
  };
  const QVariantMap freedom_graphic_patch{
      {QStringLiteral("graphics"),
       QVariantMap{{QStringLiteral("textLayers"), QVariantList{QVariantMap{
           {QStringLiteral("id"), QStringLiteral("agent-graphic-transform-test")},
           {QStringLiteral("kind"), QStringLiteral("text")},
           {QStringLiteral("name"), QStringLiteral("Poster headline")},
           {QStringLiteral("text"), QStringLiteral("LOOKSY")},
           {QStringLiteral("enabled"), true},
           {QStringLiteral("opacity"), 1.0},
           {QStringLiteral("blendMode"), QStringLiteral("normal")},
           {QStringLiteral("x"), 96},
           {QStringLiteral("y"), 88},
           {QStringLiteral("pixelWidth"), 900},
           {QStringLiteral("pixelHeight"), 180},
           {QStringLiteral("canvasWidth"), 1200},
           {QStringLiteral("canvasHeight"), 1200},
           {QStringLiteral("textTypography"),
            photo_editor::desktop::encode_photo_text_style(
                photo_editor::desktop::PhotoTextStyle{}).toVariantMap()},
       }}}}},
  };
  if (controller.editingFreedom() != QStringLiteral("balanced") ||
      controller.editingFreedomPolicy()
              .value(QStringLiteral("contract"))
              .toString() != QStringLiteral("agent-editing-freedom-v1") ||
      !controller.validateRecipePatch(freedom_crop_patch)
           .value(QStringLiteral("ok")).toBool() ||
      controller.validateRecipePatch(freedom_layer_effect_patch)
          .value(QStringLiteral("ok")).toBool()) {
    return fail("balanced editing freedom should default to the project style's real tool permissions");
  }
  controller.setEditingFreedom(QStringLiteral("protect"));
  const auto protect_policy = controller.editingFreedomPolicy();
  const auto protected_crop = controller.validateRecipePatch(freedom_crop_patch);
  if (controller.editingFreedom() != QStringLiteral("protect") ||
      protect_policy.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Protect") ||
      !protect_policy.value(QStringLiteral("retouchApprovalRequired")).toBool() ||
      !protect_policy.value(QStringLiteral("permittedChangeGroups")).toStringList().contains(
          QStringLiteral("curves")) ||
      !protect_policy.value(QStringLiteral("protectedChangeGroups")).toStringList().contains(
          QStringLiteral("graphics")) ||
      protected_crop.value(QStringLiteral("ok")).toBool() ||
      controller.validateRecipePatch(freedom_layer_effect_patch)
          .value(QStringLiteral("ok")).toBool() ||
      controller.validateRecipePatch(freedom_graphic_patch)
          .value(QStringLiteral("ok")).toBool() ||
      !protected_crop.value(QStringLiteral("errors"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("does not allow"))) {
    return fail("Protect should enforce color-and-tone-only authority instead of merely changing a label");
  }
  controller.setEditingFreedom(QStringLiteral("transform"));
  const auto transform_policy = controller.editingFreedomPolicy();
  if (controller.editingFreedom() != QStringLiteral("transform") ||
      transform_policy.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Transform") ||
      !transform_policy.value(QStringLiteral("destructiveApprovalRequired")).toBool() ||
      !transform_policy.value(QStringLiteral("permittedChangeGroups")).toStringList().contains(
          QStringLiteral("graphics")) ||
      !transform_policy.value(QStringLiteral("permittedChangeGroups")).toStringList().contains(
          QStringLiteral("layers-luts")) ||
      !controller.validateRecipePatch(freedom_crop_patch)
           .value(QStringLiteral("ok"))
           .toBool() ||
      !controller.validateRecipePatch(freedom_grading_patch)
           .value(QStringLiteral("ok")).toBool() ||
      !controller.validateRecipePatch(freedom_layer_effect_patch)
           .value(QStringLiteral("ok")).toBool() ||
      !controller.validateRecipePatch(freedom_graphic_patch)
           .value(QStringLiteral("ok")).toBool()) {
    return fail("Transform should unlock the full bounded editing toolset while preserving approval gates");
  }
  controller.setEditingFreedom(QStringLiteral("balanced"));
  const auto sample_mcp_policy =
      controller.mcpToolPolicyForPhase(QStringLiteral("sample"));
  const auto batch_mcp_policy =
      controller.mcpToolPolicyForPhase(QStringLiteral("batch"));
  const auto sample_tool_names =
      sample_mcp_policy.value(QStringLiteral("toolNames")).toStringList();
  const auto batch_tool_names =
      batch_mcp_policy.value(QStringLiteral("toolNames")).toStringList();
  if (!sample_tool_names.contains(QStringLiteral("photo.apply_recipe_patch_draft")) ||
      !sample_tool_names.contains(QStringLiteral("photo.create_mask")) ||
      !sample_tool_names.contains(QStringLiteral("photo.apply_mask_adjustment")) ||
      !sample_tool_names.contains(QStringLiteral("photo.rollback_agent_draft")) ||
      sample_tool_names.contains(QStringLiteral("photo.commit_agent_draft")) ||
      sample_tool_names.contains(QStringLiteral("photo.export_batch")) ||
      !batch_tool_names.contains(QStringLiteral("photo.apply_bulk_draft")) ||
      !batch_tool_names.contains(QStringLiteral("photo.validate_batch"))) {
    return fail("editing phases should expose reversible editing tools while keeping commit and export in their explicit-confirmation phase");
  }
  if (focused_cancel_render_contract) {
    QImage bounded_source;
    if (!photo_editor::desktop::decode_source_image(
            semantic_source_path,
            &bounded_source,
            1600,
            original_state) ||
        bounded_source.size() != QSize(256, 192)) {
      return fail("a bounded proof decode must never enlarge a smaller source image");
    }
    catalog_model.replaceAssetsForTesting({
        make_asset(
            QStringLiteral("cancel-render-asset"),
            semantic_source_path,
            original_recipe),
    });
    catalog_model.selectIndex(0);
    catalog_model.clearSelection();
    catalog_model.toggleSelectionIndex(0);
    controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

    auto render_started_observed = false;
    auto cancellation_requested = false;
    const auto run_event_connection = QObject::connect(
        &controller,
        &photo_editor::desktop::AgentEditController::runEventsChanged,
        &controller,
        [&]() {
          const auto events = controller.runEvents();
          const auto latest_event = events.isEmpty()
                                        ? QVariantMap{}
                                        : events.last().toMap();
          if (render_started_observed ||
              latest_event.value(QStringLiteral("stage")).toString() !=
                  QStringLiteral("render") ||
              latest_event.value(QStringLiteral("status")).toString() !=
                  QStringLiteral("running")) {
            return;
          }
          render_started_observed = true;
          QTimer::singleShot(0, &controller, [&]() {
            cancellation_requested = true;
            controller.cancelRun(QStringLiteral("focused_render_cancel"));
          });
        });
    QElapsedTimer cancellation_timer;
    cancellation_timer.start();
    const auto canceled_report = controller.runDraftForScope(
        QStringLiteral("current-photo"),
        QStringLiteral("Make this photo slightly brighter."));
    QObject::disconnect(run_event_connection);
    const auto canceled_proof_worker =
        canceled_report.value(QStringLiteral("proofWorker")).toMap();
    if (!render_started_observed || !cancellation_requested ||
        canceled_report.value(QStringLiteral("ok")).toBool() ||
        canceled_report.value(QStringLiteral("cancellationReason")).toString() !=
            QStringLiteral("focused_render_cancel") ||
        !canceled_proof_worker
             .value(QStringLiteral("cancellationForwarded"))
             .toBool() ||
        !canceled_proof_worker
             .value(QStringLiteral("renderLoopCancellation"))
             .toBool() ||
        cancellation_timer.elapsed() > 5000 || controller.running() ||
        controller.draftActive() || controller.cancellationRequested() ||
        catalog_model.interactiveRenderingActive()) {
      std::cerr << "focused render cancellation report="
                << QJsonDocument::fromVariant(canceled_report)
                       .toJson(QJsonDocument::Compact)
                       .toStdString()
                << " elapsed_ms=" << cancellation_timer.elapsed() << '\n';
      return fail("proof cancellation should interrupt the active pixel render and release all run state");
    }
    return EXIT_SUCCESS;
  }
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  qputenv("PHOTO_EDITOR_OLLAMA_BINARY", fake_ollama_path.toUtf8());
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      QByteArrayLiteral(R"json({"model":"qwen2.5vl:7b","details":{"family":"qwen2","parameter_size":"7B","quantization_level":"Q4_K_M"}})json"));
  const auto local_ai_setup = controller.setupLocalAi(false);
  qunsetenv("PHOTO_EDITOR_OLLAMA_BINARY");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto setup_checks = local_ai_setup.value(QStringLiteral("checks")).toList();
  auto saw_model_response_check = false;
  auto saw_mcp_check = false;
  auto saw_proof_check = false;
  auto saw_model_proof_check = false;
  auto saw_optional_retouch_runtime_check = false;
  for (const auto& check_value : setup_checks) {
    const auto check = check_value.toMap();
    const auto key = check.value(QStringLiteral("key")).toString();
    if (key == QStringLiteral("model_can_respond") && check.value(QStringLiteral("ok")).toBool()) {
      saw_model_response_check = true;
    } else if (key == QStringLiteral("mcp_tools_reachable") &&
               check.value(QStringLiteral("ok")).toBool() &&
               !check.value(QStringLiteral("required")).toBool()) {
      saw_mcp_check = true;
    } else if (key == QStringLiteral("proof_rendering") &&
               check.value(QStringLiteral("ok")).toBool() &&
               !check.value(QStringLiteral("details")).toMap().value(QStringLiteral("skipped")).toBool()) {
      saw_proof_check = true;
    } else if (key == QStringLiteral("model_can_inspect_proof_image") &&
               check.value(QStringLiteral("ok")).toBool()) {
      saw_model_proof_check = true;
    } else if (key == QStringLiteral("professional_retouch_runtime") &&
               !check.value(QStringLiteral("ok")).toBool() &&
               !check.value(QStringLiteral("required")).toBool() &&
               check.value(QStringLiteral("details"))
                   .toMap()
                   .value(QStringLiteral("fallbackProtectedByProof"))
                   .toBool()) {
      saw_optional_retouch_runtime_check = true;
    }
  }
  const auto setup_proof_artifacts =
      local_ai_setup.value(QStringLiteral("proofReadiness")).toMap().value(QStringLiteral("artifacts")).toMap();
  const auto setup_retouch_runtime =
      local_ai_setup.value(QStringLiteral("retouchRuntime")).toMap();
  if (!local_ai_setup.value(QStringLiteral("ok")).toBool() ||
      !local_ai_setup.value(QStringLiteral("setupComplete")).toBool() ||
      local_ai_setup.value(QStringLiteral("mode")).toString() != QStringLiteral("local-ai-setup") ||
      local_ai_setup.value(QStringLiteral("primaryAction")).toString() != QStringLiteral("ready") ||
      controller.status() != QStringLiteral("Local AI ready") ||
      !controller.errorMessage().isEmpty() ||
      controller.lastRunReport().value(QStringLiteral("mode")).toString() != QStringLiteral("local-ai-setup") ||
      local_ai_setup.value(QStringLiteral("setupMemoryPacket")).toMap().value(QStringLiteral("phase")).toString() !=
          QStringLiteral("setup") ||
      local_ai_setup.value(QStringLiteral("setupMemoryPacket")).toMap().value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !saw_model_response_check ||
      !saw_mcp_check ||
      !saw_proof_check ||
      !saw_model_proof_check ||
      !saw_optional_retouch_runtime_check ||
      local_ai_setup.value(QStringLiteral("capabilityTier")).toString() !=
          QStringLiteral("core-editing-with-proof-gated-retouch-fallback") ||
      setup_retouch_runtime.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-retouch-runtime-readiness-v1") ||
      !setup_retouch_runtime.value(QStringLiteral("retouchSetupRequired")).toBool() ||
      !QFileInfo(setup_proof_artifacts.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(setup_proof_artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile()) {
    return fail("one local AI setup action should probe model readiness, MCP status, setup memory, proof rendering, and publish visible state");
  }
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  const auto style_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-styles/moody-test-style.json"));
  const auto persisted_style = read_json_object(style_path);
  if (persisted_style.value(QStringLiteral("name")).toString() != QStringLiteral("Moody Test Style") ||
      !persisted_style.value(QStringLiteral("allowed_tools")).toObject().value(QStringLiteral("masks")).toBool() ||
      persisted_style.value(QStringLiteral("constraints"))
              .toObject()
              .value(QStringLiteral("max_exposure_shift"))
              .toDouble() != 0.9) {
    return fail("created style should write user-controlled intent, permissions, and constraints");
  }
  const auto style_export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("style-exports"));
  const auto exported_style = controller.exportStyle(QStringLiteral("moody-test-style"), style_export_dir);
  const auto exported_style_path = exported_style.value(QStringLiteral("exportPath")).toString();
  if (!exported_style.value(QStringLiteral("ok")).toBool() ||
      exported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-export") ||
      exported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style") ||
      !QFileInfo(exported_style_path).isFile()) {
    return fail("style export should write a validated shareable style JSON file");
  }
  const auto imported_style = controller.importStyleJson(
      QString::fromUtf8(read_file_bytes(exported_style_path)),
      QStringLiteral("Imported Moody Test Style"));
  if (!imported_style.value(QStringLiteral("ok")).toBool() ||
      imported_style.value(QStringLiteral("mode")).toString() != QStringLiteral("style-import") ||
      imported_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("moody-test-style-2") ||
      imported_style.value(QStringLiteral("styleName")).toString() !=
          QStringLiteral("Imported Moody Test Style") ||
      !imported_style.value(QStringLiteral("renamedOnImport")).toBool() ||
      controller.selectedStyleId() != QStringLiteral("moody-test-style-2") ||
      !QFileInfo(imported_style.value(QStringLiteral("importPath")).toString()).isFile()) {
    return fail("style import should validate JSON, avoid id collisions, reload styles, and select the import");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  const auto edited_reference_id =
      catalog_model.createVirtualCopyForAsset(QStringLiteral("asset-1"), QStringLiteral("Edited reference"));
  if (edited_reference_id.isEmpty()) {
    return fail("test setup should create an edited reference virtual copy");
  }
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(2);
  const auto clean_reference_preflight = controller.styleReferencePreflight(
      catalog_model.selectedAssetIds());
  const auto mixed_reference_preflight = controller.styleReferencePreflight(
      QStringList{
          QStringLiteral("asset-1"),
          edited_reference_id,
          QStringLiteral("asset-2"),
      });
  const auto reference_preflight_json =
      QJsonDocument::fromVariant(mixed_reference_preflight)
          .toJson(QJsonDocument::Compact);
  if (clean_reference_preflight.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-style-reference-preflight-v1") ||
      !clean_reference_preflight.value(QStringLiteral("available")).toBool() ||
      clean_reference_preflight.value(QStringLiteral("editedReferenceCount")).toInt() != 1 ||
      clean_reference_preflight.value(QStringLiteral("pairedOriginalCount")).toInt() != 1 ||
      clean_reference_preflight.value(QStringLiteral("warningRequired")).toBool() ||
      !mixed_reference_preflight.value(QStringLiteral("warningRequired")).toBool() ||
      !mixed_reference_preflight.value(QStringLiteral("mixedEditedAndOriginal")).toBool() ||
      mixed_reference_preflight.value(QStringLiteral("unrelatedOriginalCount")).toInt() != 1 ||
      mixed_reference_preflight.value(QStringLiteral("unavailableSourceCount")).toInt() != 0 ||
      reference_preflight_json.contains(source_path.toUtf8()) ||
      reference_preflight_json.contains("data:image") ||
      reference_preflight_json.contains("base64")) {
    return fail("style reference preflight should distinguish a safe before/after pair from unrelated originals without storing image data or source paths");
  }
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto learnable_style_state = controller.aiEditWorkflowState();
  const auto learnable_style_action =
      learnable_style_state.value(QStringLiteral("primaryAction")).toMap();
  const auto learnable_style_candidate =
      learnable_style_state.value(QStringLiteral("styleLearning")).toMap();
  if (!learnable_style_state.value(QStringLiteral("ok")).toBool() ||
      learnable_style_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Needs style") ||
      learnable_style_action.value(QStringLiteral("key")).toString() !=
          QStringLiteral("learn_style") ||
      learnable_style_action.value(QStringLiteral("label")).toString() !=
          QStringLiteral("Learn This Style") ||
      !learnable_style_candidate.value(QStringLiteral("available")).toBool() ||
      learnable_style_candidate.value(QStringLiteral("editedReferenceCount")).toInt() != 1 ||
      learnable_style_candidate.value(QStringLiteral("usableRecipeCount")).toInt() != 2) {
    return fail("AI edit workflow should offer one Learn This Style action when edited references are selected");
  }
  const auto learned_style = controller.createLearnedStyleProfileFromSelection(
      QStringLiteral("Learned Wedding Style"),
      QStringLiteral("Match my warm clean wedding edits consistently."),
      QStringLiteral("Keep skin believable and protect dress highlights."));
  if (!learned_style.value(QStringLiteral("ok")).toBool() ||
      learned_style.value(QStringLiteral("mode")).toString() != QStringLiteral("learned-style-profile") ||
      learned_style.value(QStringLiteral("styleId")).toString() != QStringLiteral("learned-wedding-style") ||
      learned_style.value(QStringLiteral("sourceAssetCount")).toInt() != 2 ||
      learned_style.value(QStringLiteral("usableRecipeCount")).toInt() != 2 ||
      controller.selectedStyleId() != QStringLiteral("learned-wedding-style")) {
    std::cerr << learned_style.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("learned style creation should summarize selected edited assets and select the new style");
  }
  const auto selected_ids_before_reference_learning = catalog_model.selectedAssetIds();
  const auto missing_reference_style = controller.createLearnedStyleProfileFromAssets(
      QStringLiteral("Missing Reference Style"),
      QStringLiteral("Use an explicit inspiration reference."),
      QStringLiteral("Keep the edit target separate from the inspiration photo."),
      QStringList{QStringLiteral("missing-reference-id")});
  const auto missing_reference_preflight = controller.styleReferencePreflight(
      QStringList{QStringLiteral("missing-reference-id")});
  if (missing_reference_style.value(QStringLiteral("ok")).toBool() ||
      missing_reference_preflight.value(QStringLiteral("ok")).toBool() ||
      missing_reference_preflight.value(QStringLiteral("missingAssetCount")).toInt() != 1 ||
      !missing_reference_preflight.value(QStringLiteral("warningRequired")).toBool() ||
      catalog_model.selectedAssetIds() != selected_ids_before_reference_learning) {
    return fail("explicit reference learning should reject missing ids without changing the catalog edit selection");
  }
  const auto learned_style_ai_state = controller.aiEditWorkflowState();
  const auto learned_taste_card =
      learned_style_ai_state.value(QStringLiteral("tasteCard")).toMap();
  if (learned_style_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Ready") ||
      learned_style_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("create_samples") ||
      !learned_taste_card.value(QStringLiteral("available")).toBool() ||
      !learned_taste_card.value(QStringLiteral("learned")).toBool() ||
      learned_taste_card.value(QStringLiteral("referenceCount")).toInt() != 2 ||
      learned_taste_card.value(QStringLiteral("confidence")).toString().isEmpty() ||
      learned_taste_card.value(QStringLiteral("summary")).toString().isEmpty()) {
    return fail("AI edit workflow should show a compact learned-style taste card after learning from selected references");
  }
  const auto learned_style_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("agent-styles/learned-wedding-style.json"));
  const auto persisted_learned_style = read_json_object(learned_style_path);
  const auto learned_profile =
      persisted_learned_style.value(QStringLiteral("learned_profile")).toObject();
  const auto learned_persistence_ok =
      persisted_learned_style.value(QStringLiteral("reference_policy"))
              .toObject()
              .value(QStringLiteral("mode"))
              .toString() == QStringLiteral("learned_style_profile") &&
      persisted_learned_style.value(QStringLiteral("reference_policy"))
              .toObject()
              .value(QStringLiteral("reference_image_matching"))
              .toString() == QStringLiteral("rendered_proof_and_recipe") &&
      learned_profile.value(QStringLiteral("source_asset_count")).toInt() == 2 &&
      learned_profile.value(QStringLiteral("reference_examples")).toArray().size() == 2 &&
      learned_profile.value(QStringLiteral("before_after_pair_count")).toInt() == 1 &&
      learned_profile.value(QStringLiteral("before_after_pairs")).toArray().size() == 1 &&
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toObject().value(QStringLiteral("referenceFingerprintCount")).toInt() >= 1 &&
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toObject().value(QStringLiteral("method")).toString() ==
          QStringLiteral("rendered_reference_fingerprint_v2") &&
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toObject().contains(QStringLiteral("averageReferenceLuma")) &&
      learned_profile.value(QStringLiteral("visualFingerprintSummary")).toObject().contains(QStringLiteral("averageReferenceRedBlueCast")) &&
      !persisted_learned_style.value(QStringLiteral("tool_knowledge")).toObject().isEmpty() &&
      !persisted_learned_style.value(QStringLiteral("quality_model")).toObject().isEmpty() &&
      !persisted_learned_style.value(QStringLiteral("project_workflow")).toObject().isEmpty();
  if (!learned_persistence_ok) {
    return fail("learned style should persist reference metadata, tool knowledge, quality model, and project workflow");
  }
  const auto style_memory_path = learned_style.value(QStringLiteral("memoryNotePath")).toString();
  const auto style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  const auto tool_memory_path = learned_style.value(QStringLiteral("toolMemoryNotePath")).toString();
  const auto tool_memory_note = QString::fromUtf8(read_file_bytes(tool_memory_path));
  const auto learned_memory_events =
      learned_style.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  const auto learned_memory_event =
      learned_memory_events.isEmpty() ? QVariantMap{} : learned_memory_events.first().toMap();
  if (!QFileInfo(style_memory_path).isFile() ||
      !style_memory_note.contains(QStringLiteral("type: agent_style_memory")) ||
      !style_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !style_memory_note.contains(QStringLiteral("# Learned Wedding Style")) ||
      !style_memory_note.contains(QStringLiteral("## Reference Examples")) ||
      !style_memory_note.contains(QStringLiteral("## Related Memory")) ||
      !style_memory_note.contains(QStringLiteral("backlinks:")) ||
      !style_memory_note.contains(QStringLiteral("asset-1")) ||
      style_memory_note.contains(runtime_root.path()) ||
      style_memory_note.contains(QStringLiteral("data:image")) ||
      style_memory_note.contains(QStringLiteral("base64"))) {
    return fail("learned style creation should write an inspectable local Markdown memory note without image bytes or local source paths");
  }
  if (learned_memory_event.value(QStringLiteral("type")).toString() !=
          QStringLiteral("durable-style-memory") ||
      learned_memory_event.value(QStringLiteral("changeType")).toString() !=
          QStringLiteral("style_profile_created") ||
      !learned_memory_event.value(QStringLiteral("requiresUserVisibility")).toBool() ||
      learned_memory_event.value(QStringLiteral("notePaths"))
              .toMap()
              .value(QStringLiteral("styleMemoryNotePath"))
              .toString() != style_memory_path) {
    return fail("learned style creation should emit a user-visible durable memory event");
  }
  if (!QFileInfo(tool_memory_path).isFile() ||
      !tool_memory_note.contains(QStringLiteral("type: agent_tool_memory")) ||
      !tool_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !tool_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !tool_memory_note.contains(QStringLiteral("# Tool Rulebook - Learned Wedding Style")) ||
      !tool_memory_note.contains(QStringLiteral("globalTone")) ||
      !tool_memory_note.contains(QStringLiteral("Protect highlight detail")) ||
      tool_memory_note.contains(runtime_root.path()) ||
      tool_memory_note.contains(QStringLiteral("data:image")) ||
      tool_memory_note.contains(QStringLiteral("base64"))) {
    return fail("learned style creation should write an inspectable tool rulebook memory note without image bytes or local paths");
  }
  const auto initial_vault_status = controller.memoryVaultStatus();
  const auto initial_note_counts = initial_vault_status.value(QStringLiteral("noteCounts")).toMap();
  const auto initial_memory_root_path =
      initial_vault_status.value(QStringLiteral("rootPath")).toString();
  if (!initial_vault_status.value(QStringLiteral("ok")).toBool() ||
      !initial_vault_status.value(QStringLiteral("obsidianCompatible")).toBool() ||
      !initial_vault_status.value(QStringLiteral("localFirst")).toBool() ||
      initial_vault_status.value(QStringLiteral("storesImageBytes")).toBool() ||
      initial_vault_status.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      !initial_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      initial_vault_status.value(QStringLiteral("schemaMismatchCount")).toInt() != 0 ||
      initial_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      initial_vault_status.value(QStringLiteral("contextStrategy")).toString() !=
          QStringLiteral("retrieved-packets-not-raw-vault") ||
      initial_vault_status.value(QStringLiteral("vaultFingerprint")).toString().isEmpty() ||
      initial_vault_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      initial_vault_status.value(QStringLiteral("externalChangeCount")).toInt() != 0 ||
      initial_note_counts.value(QStringLiteral("styles")).toInt() != 1 ||
      initial_note_counts.value(QStringLiteral("tools")).toInt() != 1 ||
      initial_vault_status.value(QStringLiteral("activeStyleNotePath")).toString() !=
          style_memory_path) {
    return fail("memory vault status should expose an Obsidian-compatible local Markdown vault without image payloads");
  }
  const auto setup_memory_packet = controller.memoryPacketForPhase(QStringLiteral("setup"));
  if (!setup_memory_packet.value(QStringLiteral("ok")).toBool() ||
      setup_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("setup") ||
      !setup_memory_packet.value(QStringLiteral("includedMemoryKinds")).toStringList().isEmpty() ||
      setup_memory_packet.value(QStringLiteral("memoryBudget"))
              .toMap()
              .value(QStringLiteral("maxCards"))
              .toInt() != 0 ||
      setup_memory_packet.value(QStringLiteral("memoryBudget"))
          .toMap()
          .value(QStringLiteral("rawVaultIncluded"))
          .toBool()) {
    return fail("setup memory retrieval should avoid style memory and raw vault context");
  }
  const auto sample_memory_packet = controller.memoryPacketForPhase(QStringLiteral("sample"));
  if (!sample_memory_packet.value(QStringLiteral("ok")).toBool() ||
      sample_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("sample") ||
      sample_memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !sample_memory_packet.value(QStringLiteral("includedMemoryKinds")).toStringList().contains(QStringLiteral("style")) ||
      sample_memory_packet.value(QStringLiteral("retrievalPlan")).toList().isEmpty() ||
      !sample_memory_packet.value(QStringLiteral("relevancePolicy"))
           .toMap()
           .value(QStringLiteral("deterministicFirstPass"))
           .toBool() ||
      sample_memory_packet.value(QStringLiteral("relevancePolicy"))
              .toMap()
              .value(QStringLiteral("secondarySimilaritySearch"))
              .toMap()
              .value(QStringLiteral("role"))
              .toString() != QStringLiteral("secondary-ranking-only") ||
      sample_memory_packet.value(QStringLiteral("styleMemoryNotePath")).toString() != style_memory_path ||
      sample_memory_packet.value(QStringLiteral("styleCard"))
              .toMap()
              .value(QStringLiteral("styleName"))
              .toString() != QStringLiteral("Learned Wedding Style") ||
      sample_memory_packet.value(QStringLiteral("toolMemoryNotePath")).toString() != tool_memory_path ||
      sample_memory_packet.value(QStringLiteral("toolCard")).toMap().value(QStringLiteral("tools")).toList().isEmpty()) {
    return fail("agent memory retrieval should return compact style/tool packets instead of the full memory vault");
  }
  const auto project_plan = controller.planStyleProjectRun(QStringLiteral("selected-assets"));
  if (!project_plan.value(QStringLiteral("ok")).toBool() ||
      project_plan.value(QStringLiteral("mode")).toString() != QStringLiteral("style-project-plan") ||
      project_plan.value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_plan.value(QStringLiteral("clusterCount")).toInt() < 1 ||
      project_plan.value(QStringLiteral("sampleAssetIds")).toStringList().isEmpty() ||
      project_plan.value(QStringLiteral("beforeAfterPairCount")).toInt() != 1 ||
      project_plan.value(QStringLiteral("styleMatchScoring")).toString().isEmpty()) {
    return fail("style project planning should expose clusters, sample assets, before/after pairs, and style-match policy");
  }
  const auto project_memory_path = project_plan.value(QStringLiteral("projectMemoryNotePath")).toString();
  const auto project_memory_note = QString::fromUtf8(read_file_bytes(project_memory_path));
  const auto project_style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  if (!QFileInfo(project_memory_path).isFile() ||
      !project_memory_note.contains(QStringLiteral("type: agent_project_memory")) ||
      !project_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !project_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !project_memory_note.contains(QStringLiteral("Related style: [[learned-wedding-style]]")) ||
      !project_memory_note.contains(QStringLiteral("## Sample Assets")) ||
      !project_memory_note.contains(QStringLiteral("asset-1")) ||
      !project_style_memory_note.contains(QStringLiteral("[[learned-wedding-style-selected-assets]]")) ||
      project_memory_note.contains(runtime_root.path()) ||
      project_memory_note.contains(QStringLiteral("data:image")) ||
      project_memory_note.contains(QStringLiteral("base64"))) {
    return fail("project planning should write a local Obsidian-style project memory note without source paths or image bytes");
  }
  const auto feedback_report = controller.recordStyleFeedback(
      QStringLiteral("Samples should be a little warmer but keep dress highlights clean."),
      QVariantMap{{QStringLiteral("sampleApproved"), false}});
  if (!feedback_report.value(QStringLiteral("ok")).toBool() ||
      feedback_report.value(QStringLiteral("mode")).toString() != QStringLiteral("style-feedback") ||
      feedback_report.value(QStringLiteral("feedbackEventCount")).toInt() != 1) {
    return fail("style feedback should update the active learned profile");
  }
  const auto feedback_style = read_json_object(learned_style_path);
  if (feedback_style.value(QStringLiteral("learned_profile"))
          .toObject()
          .value(QStringLiteral("feedback_event_count"))
          .toInt() != 1 ||
      feedback_style.value(QStringLiteral("learned_profile"))
          .toObject()
          .value(QStringLiteral("latestFeedback"))
          .toString()
          .isEmpty()) {
    return fail("style feedback should persist into the learned profile file");
  }
  const auto updated_style_memory_note = QString::fromUtf8(read_file_bytes(style_memory_path));
  const auto feedback_memory_path =
      feedback_report.value(QStringLiteral("feedbackMemoryNotePath")).toString();
  const auto feedback_memory_note = QString::fromUtf8(read_file_bytes(feedback_memory_path));
  const auto feedback_note_link =
      QStringLiteral("[[%1]]").arg(QFileInfo(feedback_memory_path).completeBaseName());
  const auto feedback_memory_events =
      feedback_report.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  const auto feedback_memory_event =
      feedback_memory_events.isEmpty() ? QVariantMap{} : feedback_memory_events.first().toMap();
  if (!updated_style_memory_note.contains(QStringLiteral("## Feedback History")) ||
      !updated_style_memory_note.contains(QStringLiteral("Samples should be a little warmer")) ||
      !QFileInfo(feedback_memory_path).isFile() ||
      !feedback_memory_note.contains(QStringLiteral("type: agent_feedback_memory")) ||
      !feedback_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !feedback_memory_note.contains(QStringLiteral("links:\n  - \"[[learned-wedding-style]]\"")) ||
      !feedback_memory_note.contains(QStringLiteral("Samples should be a little warmer")) ||
      !updated_style_memory_note.contains(feedback_note_link) ||
      feedback_memory_note.contains(runtime_root.path()) ||
      feedback_memory_note.contains(QStringLiteral("data:image")) ||
      feedback_memory_note.contains(QStringLiteral("base64"))) {
    return fail("style feedback should write inspectable feedback memory and refresh the style memory note");
  }
  const auto feedback_run_history = controller.runHistory();
  const auto latest_history =
      feedback_run_history.isEmpty() ? QVariantMap{} : feedback_run_history.first().toMap();
  const auto latest_history_memory_events =
      latest_history.value(QStringLiteral("userVisibleMemoryEvents")).toList();
  if (feedback_memory_event.value(QStringLiteral("type")).toString() !=
          QStringLiteral("durable-style-memory") ||
      feedback_memory_event.value(QStringLiteral("changeType")).toString() !=
          QStringLiteral("style_feedback_saved") ||
      !feedback_memory_event.value(QStringLiteral("requiresUserVisibility")).toBool() ||
      feedback_report.value(QStringLiteral("styleChangeSummary")).toString() !=
          QStringLiteral("Style changed because: Samples should be a little warmer but keep dress highlights clean.") ||
      feedback_memory_event.value(QStringLiteral("styleChangeSummary")).toString() !=
          feedback_report.value(QStringLiteral("styleChangeSummary")).toString() ||
      feedback_memory_event.value(QStringLiteral("detail")).toString() !=
          feedback_report.value(QStringLiteral("styleChangeSummary")).toString() ||
      !feedback_memory_event.value(QStringLiteral("memoryWriteApproved")).toBool() ||
      feedback_memory_event.value(QStringLiteral("notePaths"))
              .toMap()
              .value(QStringLiteral("feedbackMemoryNotePath"))
              .toString() != feedback_memory_path ||
      latest_history_memory_events.isEmpty()) {
    return fail("style feedback should emit user-visible durable memory events in reports and run history");
  }
  const auto feedback_vault_status = controller.memoryVaultStatus();
  const auto feedback_note_counts = feedback_vault_status.value(QStringLiteral("noteCounts")).toMap();
  if (feedback_note_counts.value(QStringLiteral("styles")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("projects")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("tools")).toInt() != 1 ||
      feedback_note_counts.value(QStringLiteral("feedback")).toInt() != 1 ||
      !feedback_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      feedback_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      feedback_vault_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      feedback_vault_status.value(QStringLiteral("totalNoteCount")).toInt() < 4) {
    return fail("memory vault status should count style, project, tool, and feedback notes after the agent learns from corrections");
  }
  QFile external_feedback_edit(feedback_memory_path);
  if (!external_feedback_edit.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text) ||
      external_feedback_edit.write("\nExternal Obsidian note: keep ceremony skin tones consistent.\n") <= 0) {
    return fail("test should be able to simulate an external Obsidian memory edit");
  }
  external_feedback_edit.close();
  const auto external_memory_status = controller.memoryVaultStatus();
  const auto external_changed_files =
      external_memory_status.value(QStringLiteral("externalChangedFiles")).toList();
  auto saw_feedback_memory_change = false;
  for (const auto& changed_file_value : external_changed_files) {
    const auto changed_file = changed_file_value.toMap();
    if (changed_file.value(QStringLiteral("relativePath")).toString() ==
            QDir(initial_memory_root_path).relativeFilePath(feedback_memory_path) &&
        changed_file.value(QStringLiteral("changeType")).toString() == QStringLiteral("modified")) {
      saw_feedback_memory_change = true;
    }
  }
  if (!external_memory_status.value(QStringLiteral("externalChangeDetected")).toBool() ||
      external_memory_status.value(QStringLiteral("externalChangeCount")).toInt() < 1 ||
      !saw_feedback_memory_change ||
      external_memory_status.value(QStringLiteral("externalChangePolicy")).toString() !=
          QStringLiteral("warn-user-before-overwriting-obsidian-edits")) {
    return fail("memory vault status should detect Markdown notes changed outside the app while it is running");
  }
  const auto obsidian_open_dry_run = controller.openMemoryVaultInObsidian(true);
  if (!obsidian_open_dry_run.value(QStringLiteral("ok")).toBool() ||
      obsidian_open_dry_run.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("agent-memory-vault-open-obsidian") ||
      !obsidian_open_dry_run.value(QStringLiteral("dryRun")).toBool() ||
      !obsidian_open_dry_run.value(QStringLiteral("deepLinkReady")).toBool() ||
      obsidian_open_dry_run.value(QStringLiteral("fallback")).toString() !=
          QStringLiteral("folder-open") ||
      !obsidian_open_dry_run.value(QStringLiteral("obsidianUrl"))
           .toString()
           .startsWith(QStringLiteral("obsidian://open?path=")) ||
      obsidian_open_dry_run.value(QStringLiteral("rootPath")).toString() != initial_memory_root_path) {
    return fail("memory vault should expose a dry-run Obsidian deep link with folder-open fallback metadata");
  }
  const auto memory_export = controller.exportMemoryVault(
      QDir(runtime_root.path()).filePath(QStringLiteral("memory-exports")),
      true);
  const auto memory_export_path = memory_export.value(QStringLiteral("exportPath")).toString();
  const auto memory_export_manifest_path =
      memory_export.value(QStringLiteral("manifestPath")).toString();
  const auto memory_export_manifest =
      QJsonDocument::fromJson(read_file_bytes(memory_export_manifest_path)).object();
  const auto exported_style_note_path =
      QDir(memory_export_path).filePath(QStringLiteral("styles/learned-wedding-style.md"));
  const auto exported_style_note = QString::fromUtf8(read_file_bytes(exported_style_note_path));
  if (!memory_export.value(QStringLiteral("ok")).toBool() ||
      !memory_export.value(QStringLiteral("redacted")).toBool() ||
      memory_export.value(QStringLiteral("noteCount")).toInt() < 4 ||
      !QFileInfo(memory_export_manifest_path).isFile() ||
      !memory_export_manifest.value(QStringLiteral("redacted")).toBool() ||
      memory_export_manifest.value(QStringLiteral("memorySchemaVersion")).toInt() != 1 ||
      memory_export_manifest.value(QStringLiteral("sourceVaultIncluded")).toBool() ||
      !QFileInfo(exported_style_note_path).isFile() ||
      !exported_style_note.contains(QStringLiteral("schema_version: 1")) ||
      exported_style_note.contains(runtime_root.path()) ||
      exported_style_note.contains(QStringLiteral("data:image")) ||
      exported_style_note.contains(QStringLiteral("base64"))) {
    return fail("memory vault export should create a redacted inspectable Markdown copy with a manifest");
  }
  const auto unconfirmed_memory_reset = controller.resetMemoryVault(false);
  if (unconfirmed_memory_reset.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_memory_reset.value(QStringLiteral("requiresConfirmation")).toBool() ||
      !QFileInfo(style_memory_path).isFile()) {
    return fail("memory vault reset should require explicit confirmation and leave notes untouched when unconfirmed");
  }
  const auto revision_memory_packet = controller.memoryPacketForPhase(QStringLiteral("revision"));
  if (!revision_memory_packet.value(QStringLiteral("ok")).toBool() ||
      revision_memory_packet.value(QStringLiteral("phase")).toString() != QStringLiteral("revision") ||
      revision_memory_packet.value(QStringLiteral("styleCard"))
              .toMap()
              .value(QStringLiteral("latestFeedback"))
              .toString()
              .isEmpty()) {
    return fail("revision memory packet should include latest approved feedback in compact form");
  }
  const auto learned_validation = controller.validateBatchForScope(QStringLiteral("selected-assets"));
  const auto learned_validation_reports = learned_validation.value(QStringLiteral("assetReports")).toList();
  const auto first_learned_style_match =
      learned_validation_reports.isEmpty()
          ? QVariantMap{}
          : learned_validation_reports.first()
                .toMap()
                .value(QStringLiteral("validationReport"))
                .toMap()
                .value(QStringLiteral("styleMatch"))
                .toMap();
  if (!learned_validation.value(QStringLiteral("ok")).toBool() ||
      !first_learned_style_match.value(QStringLiteral("available")).toBool() ||
      first_learned_style_match.value(QStringLiteral("method")).toString() !=
          QStringLiteral("metadata_recipe_visual_fingerprint_v1") ||
      first_learned_style_match.value(QStringLiteral("scorePercent")).toInt() <= 0) {
    return fail("learned-style validation should include per-image style-match scoring");
  }
  catalog_model.clearVirtualCopiesForTesting();
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));

  const auto ready_ai_state = controller.aiEditWorkflowState();
  if (!ready_ai_state.value(QStringLiteral("ok")).toBool() ||
      ready_ai_state.value(QStringLiteral("mode")).toString() != QStringLiteral("ai-edit-workflow-state") ||
      ready_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready") ||
      ready_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("current-photo") ||
      ready_ai_state.value(QStringLiteral("assetCount")).toInt() != 1 ||
      ready_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("1 photo") ||
      ready_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("create_samples") ||
      ready_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply Edit") ||
      ready_ai_state.value(QStringLiteral("secondaryActions")).toList().size() < 5) {
    return fail("AI edit workflow state should expose one ready status, one direct apply primary action, and compact secondary actions");
  }
  catalog_model.toggleSelectionIndex(1);
  const auto multi_ai_state = controller.aiEditWorkflowState();
  if (multi_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      multi_ai_state.value(QStringLiteral("assetCount")).toInt() != 2 ||
      multi_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("2 selected photos")) {
    return fail("AI edit workflow state should infer selected-set scope for multiple selected photos");
  }
  catalog_model.clearSelection();
  catalog_model.createProject(QStringLiteral("Workflow Wedding"));
  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Workflow Wedding"));
  catalog_model.addAssetToProject(QStringLiteral("asset-2"), QStringLiteral("Workflow Wedding"));
  catalog_model.openProject(QStringLiteral("Workflow Wedding"));
  const auto project_ai_state = controller.aiEditWorkflowState();
  if (project_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_ai_state.value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_ai_state.value(QStringLiteral("scopeLabel")).toString() != QStringLiteral("2 project photos")) {
    return fail("AI edit workflow state should infer project scope when no assets are selected and a project is active");
  }
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.setSearchFilter(QStringLiteral("source-2"));
  const auto filtered_ai_state = controller.aiEditWorkflowState();
  if (filtered_ai_state.value(QStringLiteral("scope")).toString() != QStringLiteral("filtered-library") ||
      filtered_ai_state.value(QStringLiteral("assetCount")).toInt() != 1) {
    return fail("AI edit workflow state should infer filtered-library scope when no assets are selected and a filter is active");
  }
  catalog_model.setSearchFilter(QString());
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("missing-style"));
  const auto missing_style_state = controller.aiEditWorkflowState();
  if (missing_style_state.value(QStringLiteral("ok")).toBool() ||
      missing_style_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Needs style") ||
      missing_style_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("choose_style") ||
      missing_style_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("AI edit workflow state should reduce missing style setup to a single Choose Style action");
  }
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("missing-model"));
  const auto missing_model_state = controller.aiEditWorkflowState();
  if (missing_model_state.value(QStringLiteral("ok")).toBool() ||
      !missing_model_state.value(QStringLiteral("setupRequired")).toBool() ||
      missing_model_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Set up local AI") ||
      missing_model_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("set_up_local_ai") ||
      !missing_model_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("enabled")).toBool()) {
    return fail("AI edit workflow state should reduce local model readiness to one setup action");
  }
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));

  const QJsonObject needs_input_content{
      {QStringLiteral("decision"), QStringLiteral("needs_user_input")},
      {QStringLiteral("summary"), QStringLiteral("Need the target reception warmth before editing.")},
      {QStringLiteral("userQuestions"),
       QJsonArray{QStringLiteral("Should the reception photos stay warm editorial or neutral documentary?")}},
  };
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(needs_input_content));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto needs_input_report =
      controller.runDraftOnSelected(QStringLiteral("edit this wedding sample to my style"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (needs_input_report.value(QStringLiteral("ok")).toBool() ||
      needs_input_report.value(QStringLiteral("decision")).toString() != QStringLiteral("needs_user_input") ||
      !needs_input_report.value(QStringLiteral("needsUserInput")).toBool() ||
      !controller.awaitingUserInput() ||
      controller.pendingUserQuestions().isEmpty() ||
      controller.status() != QStringLiteral("Needs user input") ||
      controller.draftActive()) {
    return fail("model clarification should become pending controller input without applying a draft");
  }
  if (controller.pendingUserQuestions().first().toString() !=
      QStringLiteral("Should the reception photos stay warm editorial or neutral documentary?")) {
    return fail("pending model clarification should preserve the user-facing question");
  }
  QVariantMap canceled_planning_report;
  QString canceled_planning_request_id;
  auto canceled_planning_completion_count = 0;
  const auto canceled_planning_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString& request_id, const QVariantMap& report) {
        canceled_planning_request_id = request_id;
        canceled_planning_report = report;
        ++canceled_planning_completion_count;
      });
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
          fake_ollama_response(needs_input_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("300"));
  const auto canceled_planning_start = controller.startDraftOnSelected(
      QStringLiteral("edit this wedding sample to my style"));
  const auto expected_canceled_planning_request_id =
      canceled_planning_start.value(QStringLiteral("requestId")).toString();
  const auto planning_worker_started = wait_for_condition(
      [&]() {
        for (const auto& event_value : controller.runEvents()) {
          const auto event = event_value.toMap();
          if (event.value(QStringLiteral("stage")).toString() ==
                  QStringLiteral("plan-worker") &&
              event.value(QStringLiteral("status")).toString() ==
                  QStringLiteral("running")) {
            return true;
          }
        }
        return false;
      },
      1000);
  const auto canceled_planning_request =
      controller.cancelRun(QStringLiteral("cancel_model_planning_test"));
  const auto canceled_planning_completed = wait_for_condition(
      [&]() { return canceled_planning_completion_count == 1; },
      5000);
  QObject::disconnect(canceled_planning_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (!canceled_planning_start.value(QStringLiteral("accepted")).toBool() ||
      expected_canceled_planning_request_id.isEmpty() ||
      !planning_worker_started ||
      !canceled_planning_request.value(QStringLiteral("ok")).toBool() ||
      !canceled_planning_completed ||
      canceled_planning_completion_count != 1 ||
      canceled_planning_request_id !=
          expected_canceled_planning_request_id ||
      canceled_planning_report.value(QStringLiteral("ok")).toBool() ||
      canceled_planning_report
              .value(QStringLiteral("cancellationReason"))
              .toString() !=
          QStringLiteral("cancel_model_planning_test") ||
      canceled_planning_report.value(QStringLiteral("modelRunnerMode")).toString() !=
          QStringLiteral("request-owned-continuation") ||
      canceled_planning_report
              .value(QStringLiteral("planningContinuation"))
              .toMap()
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      canceled_planning_report.value(QStringLiteral("durationMs")).toLongLong() <
          300 ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      controller.cancellationRequested() || controller.draftActive()) {
    return fail("canceling an in-flight local-model continuation should finish exactly once, preserve request evidence, and leave no editable draft");
  }
  QVariantMap stale_context_report;
  auto stale_context_completion_count = 0;
  const auto stale_context_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        stale_context_report = report;
        ++stale_context_completion_count;
      });
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
          fake_ollama_response(needs_input_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("150"));
  const auto stale_context_start = controller.startDraftOnSelected(
      QStringLiteral("edit this wedding sample to my style"));
  const auto stale_context_worker_started = wait_for_condition(
      [&]() {
        return controller.draftRequestPending() &&
               !controller.activeDraftRequestId().isEmpty();
      },
      1000);
  catalog_model.toggleSelectionIndex(1);
  const auto stale_context_completed = wait_for_condition(
      [&]() { return stale_context_completion_count == 1; }, 5000);
  catalog_model.toggleSelectionIndex(1);
  QObject::disconnect(stale_context_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto stale_context_continuation =
      stale_context_report.value(QStringLiteral("planningContinuation"))
          .toMap();
  if (!stale_context_start.value(QStringLiteral("accepted")).toBool() ||
      !stale_context_worker_started || !stale_context_completed ||
      stale_context_completion_count != 1 ||
      stale_context_report.value(QStringLiteral("ok")).toBool() ||
      stale_context_report.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("cannot_complete_safely") ||
      stale_context_report.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("stale_request_context") ||
      stale_context_continuation
              .value(QStringLiteral("originContextGuard"))
              .toString() !=
          QStringLiteral("project-look-model-scope-and-assets") ||
      !stale_context_continuation
           .value(QStringLiteral("assetMembershipFrozen"))
           .toBool() ||
      stale_context_continuation
              .value(QStringLiteral("completedStages"))
              .toStringList() !=
          QStringList{QStringLiteral("primary-plan")} ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      controller.draftActive()) {
    return fail("request-owned planning should safe-stop exactly once when the target photo membership changes in flight");
  }
  QVariantMap async_planning_report;
  QString async_planning_request_id;
  auto async_planning_completion_count = 0;
  auto planning_heartbeat_fired_while_pending = false;
  const auto async_planning_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString& request_id, const QVariantMap& report) {
        async_planning_request_id = request_id;
        async_planning_report = report;
        ++async_planning_completion_count;
      });
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
          fake_ollama_response(needs_input_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("150"));
  QTimer planning_heartbeat;
  planning_heartbeat.setSingleShot(true);
  QObject::connect(
      &planning_heartbeat,
      &QTimer::timeout,
      &controller,
      [&]() {
        planning_heartbeat_fired_while_pending =
            controller.running() && controller.draftRequestPending() &&
            async_planning_completion_count == 0;
      });
  planning_heartbeat.start(20);
  QElapsedTimer async_planning_start_timer;
  async_planning_start_timer.start();
  const auto async_planning_start = controller.startDraftOnSelected(
      QStringLiteral("edit this wedding sample to my style"));
  const auto async_planning_start_duration_ms =
      async_planning_start_timer.elapsed();
  const auto expected_async_planning_request_id =
      async_planning_start.value(QStringLiteral("requestId")).toString();
  const auto async_planning_completed = wait_for_condition(
      [&]() { return async_planning_completion_count == 1; },
      5000);
  QObject::disconnect(async_planning_connection);
  planning_heartbeat.stop();
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto async_planning_contract =
      async_planning_report.value(QStringLiteral("asyncDraft")).toMap();
  const auto planning_continuation =
      async_planning_report
          .value(QStringLiteral("planningContinuation"))
          .toMap();
  const auto async_planning_feedback =
      async_planning_report.value(QStringLiteral("interactionFeedback")).toMap();
  const auto async_planning_manifest = read_json_object(
      async_planning_report.value(QStringLiteral("manifestPath")).toString());
  auto saw_planning_worker_running = false;
  auto saw_planning_worker_complete = false;
  auto saw_local_planning_heartbeat_contract = false;
  for (const auto& event_value :
       async_planning_report.value(QStringLiteral("runEvents")).toList()) {
    const auto event = event_value.toMap();
    if (event.value(QStringLiteral("stage")).toString() !=
        QStringLiteral("plan-worker")) {
      continue;
    }
    saw_planning_worker_running =
        saw_planning_worker_running ||
        event.value(QStringLiteral("status")).toString() ==
            QStringLiteral("running");
    saw_planning_worker_complete =
        saw_planning_worker_complete ||
        event.value(QStringLiteral("status")).toString() ==
            QStringLiteral("complete");
    const auto evidence = event.value(QStringLiteral("evidence")).toMap();
    saw_local_planning_heartbeat_contract =
        saw_local_planning_heartbeat_contract ||
        (event.value(QStringLiteral("status")).toString() ==
             QStringLiteral("running") &&
         event.value(QStringLiteral("headline")).toString() ==
             QStringLiteral("I'm reviewing this photo locally.") &&
         evidence.value(QStringLiteral("localOnly")).toBool() &&
         evidence.value(QStringLiteral("cancelSafe")).toBool() &&
         !evidence.value(QStringLiteral("pixelsChanged")).toBool());
  }
  if (!async_planning_start.value(QStringLiteral("accepted")).toBool() ||
      async_planning_start.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-async-draft-run-v2") ||
      expected_async_planning_request_id.isEmpty() ||
      async_planning_start_duration_ms > 50 ||
      !async_planning_completed ||
      !planning_heartbeat_fired_while_pending ||
      !saw_planning_worker_running || !saw_planning_worker_complete ||
      !saw_local_planning_heartbeat_contract ||
      async_planning_request_id != expected_async_planning_request_id ||
      async_planning_report.value(QStringLiteral("ok")).toBool() ||
      async_planning_report.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("needs_user_input") ||
      async_planning_report.value(QStringLiteral("modelRunnerMode")).toString() !=
          QStringLiteral("request-owned-continuation") ||
      async_planning_report.value(QStringLiteral("uiEventPumpCount")).toInt() != 0 ||
      async_planning_report.value(QStringLiteral("durationMs")).toLongLong() <
          150 ||
      !async_planning_feedback
           .value(QStringLiteral("workerPlanningResponsive"))
           .toBool() ||
      async_planning_feedback.value(QStringLiteral("planningExecution")).toString() !=
          QStringLiteral("qfuture-watcher") ||
      async_planning_manifest
              .value(QStringLiteral("planningContinuation"))
              .toObject()
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      async_planning_manifest.value(QStringLiteral("durationMs")).toInteger() <
          150 ||
      planning_continuation.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      planning_continuation.value(QStringLiteral("execution")).toString() !=
          QStringLiteral("qfuture-watcher") ||
      !planning_continuation.value(QStringLiteral("requestOwned")).toBool() ||
      planning_continuation.value(QStringLiteral("controllerCallPending")).toBool() ||
      planning_continuation.value(QStringLiteral("catalogAccessOnWorker")).toBool() ||
      planning_continuation.value(QStringLiteral("controllerAccessOnWorker")).toBool() ||
      !planning_continuation.value(QStringLiteral("staleRequestRejected")).toBool() ||
      planning_continuation
          .value(QStringLiteral("explicitEventPumpLoop"))
          .toBool() ||
      planning_continuation
              .value(QStringLiteral("completedStages"))
              .toStringList() !=
          QStringList{QStringLiteral("primary-plan")} ||
      async_planning_contract.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-async-draft-run-v2") ||
      async_planning_contract
              .value(QStringLiteral("planningContinuationContract"))
              .toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      async_planning_contract.value(QStringLiteral("planningExecution")).toString() !=
          QStringLiteral("qfuture-watcher") ||
      async_planning_contract
              .value(QStringLiteral("requestOwnedModelStages"))
              .toStringList() !=
          QStringList{
              QStringLiteral("primary-plan"),
              QStringLiteral("model-tool-follow-up"),
              QStringLiteral("batch-cluster-plan"),
              QStringLiteral("validation-repair"),
          } ||
      async_planning_contract
              .value(QStringLiteral("modelStageReplayPolicy"))
              .toString() !=
          QStringLiteral("logical-stage-once-per-request") ||
      async_planning_contract
              .value(QStringLiteral("proofCritiqueReplayPolicy"))
              .toString() !=
          QStringLiteral("cached-per-asset-iteration") ||
      async_planning_contract
          .value(QStringLiteral("modelPlannerControllerCallPending"))
          .toBool() ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty()) {
    std::cerr << "async planning report="
              << QJsonDocument::fromVariant(async_planning_report)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << " start_ms=" << async_planning_start_duration_ms
              << " heartbeat=" << planning_heartbeat_fired_while_pending
              << '\n';
    return fail("request-owned local-model planning should keep the UI event loop responsive and resume exactly once without controller access on the worker");
  }
  const auto pending_ai_state = controller.aiEditWorkflowState();
  if (pending_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Waiting for your answer") ||
      !pending_ai_state.value(QStringLiteral("reviewRequired")).toBool() ||
      !pending_ai_state.value(QStringLiteral("awaitingUserInput")).toBool() ||
      pending_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("answer_question")) {
    return fail("AI edit workflow state should make model clarification a review state");
  }

  const QJsonObject answered_patch_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Warm reception edit with protected highlights.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.goal")},
               {QStringLiteral("reason"), QStringLiteral("The user answered that the reception should stay warm.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Keep the reception warm while protecting highlights.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("warm")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("editorial")},
           {QStringLiteral("referenceStrength"), 1.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.9},
           {QStringLiteral("globalChangePolicy"), QStringLiteral("allow")},
           {QStringLiteral("visualFindings"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("skin_tone")},
                    {QStringLiteral("target"), QStringLiteral("face")},
                    {QStringLiteral("severity"), QStringLiteral("medium")},
                    {QStringLiteral("observation"),
                     QStringLiteral("The face is slightly too warm relative to the dress highlights.")},
                    {QStringLiteral("confidence"), 0.91},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.46},
                         {QStringLiteral("centerY"), 0.34},
                         {QStringLiteral("radiusX"), 0.14},
                         {QStringLiteral("radiusY"), 0.18},
                     }},
                },
            }},
           {QStringLiteral("localizedActions"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("target"), QStringLiteral("face")},
                    {QStringLiteral("maskShape"), QStringLiteral("radial")},
                    {QStringLiteral("reason"),
                     QStringLiteral("Cool the face slightly without neutralizing the reception.")},
                    {QStringLiteral("confidence"), 0.89},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.46},
                         {QStringLiteral("centerY"), 0.34},
                         {QStringLiteral("radiusX"), 0.14},
                         {QStringLiteral("radiusY"), 0.18},
                         {QStringLiteral("feather"), 0.76},
                     }},
                    {QStringLiteral("adjustments"),
                     QJsonObject{
                         {QStringLiteral("temperature"), -8.0},
                         {QStringLiteral("highlights"), -10.0},
                     }},
                },
            }},
       }},
  };
  const auto answer_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-clarification-answer-request.json"));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(answered_patch_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", answer_request_path.toUtf8());
  const auto answered_report =
      controller.answerPendingUserInput(QStringLiteral("Keep reception warm but protect skin and dress highlights."));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  const auto answer_request_bytes = read_file_bytes(answer_request_path);
  const auto answered_edit_intent =
      answered_report.value(QStringLiteral("editIntent")).toMap();
  const auto answered_mask_patch =
      answered_report.value(QStringLiteral("plannedRecipePatch"))
          .toMap()
          .value(QStringLiteral("masks"))
          .toMap()
          .value(QStringLiteral("appendMaskStack"))
          .toList();
  if (!answered_report.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput() ||
      !controller.pendingUserQuestions().isEmpty() ||
      !controller.draftActive() ||
      answered_edit_intent.value(QStringLiteral("visualFindings")).toList().size() != 1 ||
      answered_edit_intent.value(QStringLiteral("localizedActions")).toList().size() != 1 ||
      answered_mask_patch.size() != 1 ||
      answered_mask_patch.first().toMap().value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial") ||
      answered_mask_patch.first().toMap().value(QStringLiteral("temperature")).toDouble() != -8.0 ||
      !answer_request_bytes.contains("User answer to agent question") ||
      !answer_request_bytes.contains("Keep reception warm but protect skin and dress highlights")) {
    std::cerr << "localized clarification errors="
              << answered_report.value(QStringLiteral("errors"))
                     .toStringList()
                     .join(QStringLiteral("|"))
                     .toStdString()
              << " failure="
              << answered_report.value(QStringLiteral("failureReason"))
                     .toString()
                     .toStdString()
              << " findings="
              << answered_edit_intent.value(QStringLiteral("visualFindings"))
                     .toList()
                     .size()
              << " actions="
              << answered_edit_intent.value(QStringLiteral("localizedActions"))
                     .toList()
                     .size()
              << " masks=" << answered_mask_patch.size()
              << " validation_warnings="
              << answered_report.value(QStringLiteral("assetReports"))
                     .toList()
                     .value(0)
                     .toMap()
                     .value(QStringLiteral("validationReport"))
                     .toMap()
                     .value(QStringLiteral("warnings"))
                     .toStringList()
                     .join(QStringLiteral("|"))
                     .toStdString()
              << '\n';
    return fail("answering a pending model clarification should resume planning with the answer in context");
  }
  const auto draft_ai_state = controller.aiEditWorkflowState();
  if (draft_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready to commit") ||
      !draft_ai_state.value(QStringLiteral("draftActive")).toBool() ||
      !draft_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("confirmationRequired")).toBool() ||
      draft_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("commit")) {
    return fail("AI edit workflow state should surface a single commit action for active drafts");
  }
  const auto answered_rollback = controller.rollbackDraft();
  if (!answered_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("draft created after a model clarification answer should remain rollbackable");
  }

  auto ambiguous_object_content = answered_patch_content;
  ambiguous_object_content.insert(
      QStringLiteral("summary"),
      QStringLiteral("Lift the requested jacket while preserving the rest."));
  auto ambiguous_object_intent =
      ambiguous_object_content.value(QStringLiteral("editIntent")).toObject();
  ambiguous_object_intent.insert(
      QStringLiteral("goalSummary"),
      QStringLiteral("Lift only the requested jacket."));
  ambiguous_object_intent.insert(
      QStringLiteral("globalChangePolicy"), QStringLiteral("preserve"));
  ambiguous_object_intent.insert(
      QStringLiteral("visualFindings"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("kind"), QStringLiteral("exposure")},
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("second jacket")},
              {QStringLiteral("severity"), QStringLiteral("medium")},
              {QStringLiteral("observation"),
               QStringLiteral("Two similar jackets are visible, so the requested instance needs confirmation.")},
              {QStringLiteral("confidence"), 0.88},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.74},
                   {QStringLiteral("centerY"), 0.50},
                   {QStringLiteral("radiusX"), 0.13},
                   {QStringLiteral("radiusY"), 0.24},
               }},
          },
      });
  ambiguous_object_intent.insert(
      QStringLiteral("localizedActions"),
      QJsonArray{
          QJsonObject{
              {QStringLiteral("target"), QStringLiteral("object")},
              {QStringLiteral("targetLabel"), QStringLiteral("second jacket")},
              {QStringLiteral("maskShape"), QStringLiteral("radial")},
              {QStringLiteral("selectionMode"), QStringLiteral("guided")},
              {QStringLiteral("selectionCandidates"),
               QJsonArray{
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Left jacket")},
                       {QStringLiteral("confidence"), 0.78},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.26},
                            {QStringLiteral("centerY"), 0.50},
                            {QStringLiteral("radiusX"), 0.13},
                            {QStringLiteral("radiusY"), 0.24},
                            {QStringLiteral("feather"), 0.68},
                        }},
                   },
                   QJsonObject{
                       {QStringLiteral("label"), QStringLiteral("Right jacket")},
                       {QStringLiteral("confidence"), 0.76},
                       {QStringLiteral("region"),
                        QJsonObject{
                            {QStringLiteral("centerX"), 0.74},
                            {QStringLiteral("centerY"), 0.50},
                            {QStringLiteral("radiusX"), 0.13},
                            {QStringLiteral("radiusY"), 0.24},
                            {QStringLiteral("feather"), 0.68},
                        }},
                   },
               }},
              {QStringLiteral("reason"),
               QStringLiteral("Lift the requested jacket without changing the other one.")},
              {QStringLiteral("confidence"), 0.86},
              {QStringLiteral("region"),
               QJsonObject{
                   {QStringLiteral("centerX"), 0.74},
                   {QStringLiteral("centerY"), 0.50},
                   {QStringLiteral("radiusX"), 0.13},
                   {QStringLiteral("radiusY"), 0.24},
                   {QStringLiteral("feather"), 0.68},
               }},
              {QStringLiteral("adjustments"),
               QJsonObject{{QStringLiteral("exposure"), 0.22}}},
          },
      });
  ambiguous_object_content.insert(
      QStringLiteral("editIntent"), ambiguous_object_intent);
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("ambiguous-object-asset"),
                 ambiguous_object_source_path,
                 original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(ambiguous_object_content));
  const auto ambiguity_report = controller.runDraftOnSelected(
      QStringLiteral("Lift only the second jacket and leave the other one unchanged."));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto selection_confirmation =
      ambiguity_report.value(QStringLiteral("selectionConfirmation")).toMap();
  const auto selection_candidates =
      selection_confirmation.value(QStringLiteral("candidates")).toList();
  if (ambiguity_report.value(QStringLiteral("ok")).toBool() ||
      ambiguity_report.value(QStringLiteral("inputKind")).toString() !=
          QStringLiteral("grounded_selection_confirmation") ||
      ambiguity_report.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("ambiguous_visible_instance") ||
      selection_confirmation.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-grounded-selection-confirmation-v1") ||
      !selection_confirmation.value(QStringLiteral("required")).toBool() ||
      selection_candidates.size() != 2 ||
      !controller.awaitingUserInput() || controller.draftActive() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0)).mask_stack.isEmpty()) {
    return fail("ambiguous visible instances should stop before mutation and expose stable candidates for explicit confirmation");
  }
  const auto ambiguous_object_workflow_state = controller.aiEditWorkflowState();
  if (ambiguous_object_workflow_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Choose object") ||
      ambiguous_object_workflow_state.value(QStringLiteral("primaryAction"))
              .toMap()
              .value(QStringLiteral("key"))
              .toString() != QStringLiteral("confirm_object") ||
      !ambiguous_object_workflow_state.value(QStringLiteral("reviewRequired")).toBool()) {
    return fail("AI workflow state should identify visible-instance confirmation as a distinct review action");
  }
  const auto text_bypass_confirmation = controller.answerPendingUserInput(
      QStringLiteral("Use whichever one you think is right."));
  if (text_bypass_confirmation.value(QStringLiteral("ok")).toBool() ||
      !controller.awaitingUserInput() || controller.draftActive()) {
    return fail("generic clarification text should not bypass explicit visible-instance confirmation");
  }
  const auto missing_candidate_confirmation =
      controller.confirmGroundedSelectionCandidate(
          QStringLiteral("candidate-that-does-not-exist"));
  if (missing_candidate_confirmation.value(QStringLiteral("ok")).toBool() ||
      !controller.awaitingUserInput() || controller.draftActive()) {
    return fail("an unknown candidate identity should fail closed without consuming the pending choice");
  }
  QString right_jacket_candidate_id;
  for (const auto& candidate_value : selection_candidates) {
    const auto candidate = candidate_value.toMap();
    if (candidate.value(QStringLiteral("label")).toString() ==
        QStringLiteral("Right jacket")) {
      right_jacket_candidate_id =
          candidate.value(QStringLiteral("id")).toString();
      break;
    }
  }
  const auto confirmed_object_report =
      controller.confirmGroundedSelectionCandidate(right_jacket_candidate_id);
  const auto confirmed_selection =
      confirmed_object_report.value(QStringLiteral("selectionConfirmation")).toMap();
  const auto confirmed_object_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto confirmed_object_mask = confirmed_object_state.mask_stack.isEmpty()
                                         ? QVariantMap{}
                                         : confirmed_object_state.mask_stack.last().toMap();
  const auto confirmed_object_alpha =
      confirmed_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto confirmed_object_width =
      confirmed_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto confirmed_object_height =
      confirmed_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto alpha_value_at = [&](double x, double y) {
    if (confirmed_object_width <= 0 || confirmed_object_height <= 0 ||
        confirmed_object_alpha.size() !=
            confirmed_object_width * confirmed_object_height) {
      return 0;
    }
    const auto sample_x = std::clamp(
        static_cast<int>(std::round(x * (confirmed_object_width - 1))),
        0,
        confirmed_object_width - 1);
    const auto sample_y = std::clamp(
        static_cast<int>(std::round(y * (confirmed_object_height - 1))),
        0,
        confirmed_object_height - 1);
    return static_cast<int>(static_cast<unsigned char>(
        confirmed_object_alpha.at(sample_y * confirmed_object_width + sample_x)));
  };
  const auto left_candidate_alpha = alpha_value_at(0.26, 0.50);
  const auto right_candidate_alpha = alpha_value_at(0.74, 0.50);
  const auto visual_referent_memory =
      confirmed_object_report.value(QStringLiteral("visualReferentMemory")).toMap();
  const auto remembered_visual_referent =
      visual_referent_memory.value(QStringLiteral("referent")).toMap();
  const auto remembered_visual_referent_id =
      remembered_visual_referent.value(QStringLiteral("id")).toString();
  const auto referent_memory_packet =
      controller.memoryPacketForPhase(QStringLiteral("sample"));
  const auto remembered_referents =
      referent_memory_packet.value(QStringLiteral("recentVisualReferents")).toList();
  if (right_jacket_candidate_id.isEmpty() ||
      !confirmed_object_report.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput() || !controller.draftActive() ||
      !confirmed_selection.value(QStringLiteral("confirmed")).toBool() ||
      confirmed_selection.value(QStringLiteral("confirmedCandidateId")).toString() !=
          right_jacket_candidate_id ||
      confirmed_selection.value(QStringLiteral("confirmedCandidateLabel")).toString() !=
          QStringLiteral("Right jacket") ||
      confirmed_selection.value(QStringLiteral("resumePolicy")).toString() !=
          QStringLiteral("apply-original-plan-with-confirmed-instance-no-replan") ||
      confirmed_object_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("object") ||
      confirmed_object_alpha.isEmpty() || right_candidate_alpha < 128 ||
      left_candidate_alpha >= 64 ||
      !visual_referent_memory.value(QStringLiteral("ok")).toBool() ||
      remembered_visual_referent.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-visual-referent-v1") ||
      remembered_visual_referent.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("ambiguous-object-asset") ||
      remembered_visual_referent.value(QStringLiteral("chosenLabel")).toString() !=
          QStringLiteral("Right jacket") ||
      remembered_visual_referent_id.isEmpty() ||
      remembered_referents.isEmpty() ||
      !referent_memory_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("visual-referent")) ||
      remembered_visual_referent.value(QStringLiteral("storesImageBytes")).toBool() ||
      remembered_visual_referent.value(QStringLiteral("storesSourcePaths")).toBool() ||
      !visual_referent_memory
           .value(QStringLiteral("transientSessionMatteCached"))
           .toBool() ||
      visual_referent_memory
          .value(QStringLiteral("transientCachePersisted"))
          .toBool() ||
      visual_referent_memory
          .value(QStringLiteral("transientCacheStoresSourcePaths"))
          .toBool() ||
      !visual_referent_memory
           .value(QStringLiteral("transientBeforeProofCached"))
           .toBool() ||
      std::abs(confirmed_object_mask.value(QStringLiteral("exposure")).toDouble() - 0.22) >
          0.001) {
    std::cerr << "confirmed object report="
              << "ok:" << confirmed_object_report.value(QStringLiteral("ok")).toBool()
              << " awaiting:" << controller.awaitingUserInput()
              << " draft:" << controller.draftActive()
              << " confirmation:"
              << QJsonDocument::fromVariant(confirmed_selection)
                     .toJson(QJsonDocument::Compact).toStdString()
              << " mask_kind:"
              << confirmed_object_mask.value(QStringLiteral("kind")).toString().toStdString()
              << " alpha:"
              << confirmed_object_mask.value(QStringLiteral("segmentationData")).toByteArray().size()
              << " center_x:"
              << confirmed_object_mask.value(QStringLiteral("centerX")).toDouble()
              << " left_alpha:" << left_candidate_alpha
              << " right_alpha:" << right_candidate_alpha
              << " exposure:"
              << confirmed_object_mask.value(QStringLiteral("exposure")).toDouble()
              << '\n';
    return fail("confirming a visible candidate should apply the original reversible plan to only that generated object mask without replanning");
  }
  const auto confirmed_object_rollback = controller.rollbackDraft();
  if (!confirmed_object_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0)).mask_stack.isEmpty()) {
    return fail("confirmed object-selection drafts should remain rollbackable");
  }

  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(ambiguous_object_content));
  QVariantList observed_progressive_previews;
  auto saw_progressive_recipe_on_canvas = false;
  auto progressive_commit_was_blocked = false;
  auto saw_transient_preview_contract = false;
  auto proof_worker_ui_timer_fired = false;
  auto proof_worker_priority_lease_observed = false;
  QTimer proof_worker_ui_timer;
  proof_worker_ui_timer.setSingleShot(true);
  proof_worker_ui_timer.setInterval(20);
  QObject::connect(
      &proof_worker_ui_timer,
      &QTimer::timeout,
      &controller,
      [&]() {
        proof_worker_ui_timer_fired =
            controller.running() &&
            controller.progressivePreview()
                    .value(QStringLiteral("state"))
                    .toString() == QStringLiteral("validating");
        proof_worker_priority_lease_observed =
            catalog_model.interactiveRenderingActive() &&
            catalog_model.interactiveRenderingPriorityDepth() >= 1;
      });
  const auto progressive_preview_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::progressivePreviewChanged,
      &controller,
      [&]() {
        const auto preview = controller.progressivePreview();
        observed_progressive_previews.append(preview);
        if (preview.value(QStringLiteral("state")).toString() !=
            QStringLiteral("validating")) {
          return;
        }
        const auto live_state =
            adjustment_state_for_asset(catalog_model.assetAt(0));
        saw_progressive_recipe_on_canvas =
            controller.running() && !live_state.mask_stack.isEmpty() &&
            std::abs(live_state.mask_stack.last()
                         .toMap()
                         .value(QStringLiteral("exposure"))
                         .toDouble() -
                     0.18) < 0.001;
        saw_transient_preview_contract =
            !preview.value(QStringLiteral("durableStateWritten")).toBool() &&
            preview.value(QStringLiteral("thumbnailRefreshDeferred")).toBool();
        const auto early_commit = controller.commitDraft(true);
        progressive_commit_was_blocked =
            !early_commit.value(QStringLiteral("ok")).toBool() &&
            early_commit.value(QStringLiteral("committedCount")).toInt() == 0;
        if (!proof_worker_ui_timer.isActive() &&
            !proof_worker_ui_timer_fired) {
          proof_worker_ui_timer.start();
        }
      });
  QVariantMap reused_visual_referent_report;
  QString completed_async_request_id;
  auto async_completion_count = 0;
  const auto async_completion_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString& request_id, const QVariantMap& report) {
        completed_async_request_id = request_id;
        reused_visual_referent_report = report;
        ++async_completion_count;
      });
  QElapsedTimer async_start_timer;
  async_start_timer.start();
  const auto async_start = controller.startDraftOnSelected(
      QStringLiteral("Make that Right jacket a little brighter and keep everything else unchanged."));
  const auto async_start_duration_ms = async_start_timer.elapsed();
  const auto async_request_id =
      async_start.value(QStringLiteral("requestId")).toString();
  const auto duplicate_async_start = controller.startDraftOnSelected(
      QStringLiteral("Make that Right jacket darker instead."));
  if (!async_start.value(QStringLiteral("ok")).toBool() ||
      !async_start.value(QStringLiteral("accepted")).toBool() ||
      async_start.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-async-draft-run-v2") ||
      async_request_id.isEmpty() || async_start_duration_ms > 50 ||
      !controller.running() || !controller.draftRequestPending() ||
      controller.activeDraftRequestId() != async_request_id ||
      controller.draftActive() ||
      duplicate_async_start.value(QStringLiteral("ok")).toBool() ||
      duplicate_async_start.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("run_in_progress")) {
    return fail("asynchronous draft start should return immediately with one request id, expose pending state, and reject duplicate starts");
  }
  if (!wait_for_condition(
          [&]() { return async_completion_count == 1; },
          30000)) {
    return fail("asynchronous same-object draft should complete through draftRunFinished without blocking the caller");
  }
  QObject::disconnect(async_completion_connection);
  QObject::disconnect(progressive_preview_connection);
  proof_worker_ui_timer.stop();
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto async_draft_contract =
      reused_visual_referent_report
          .value(QStringLiteral("asyncDraft"))
          .toMap();
  const auto visual_referent_resolution =
      reused_visual_referent_report
          .value(QStringLiteral("visualReferentResolution"))
          .toMap();
  const auto visual_referent_fast_path =
      reused_visual_referent_report
          .value(QStringLiteral("visualReferentFastPath"))
          .toMap();
  const auto reused_referent_prompt_audit =
      reused_visual_referent_report
          .value(QStringLiteral("modelPromptAudit"))
          .toMap();
  const auto reused_referent_semantic_selection =
      reused_visual_referent_report
          .value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("semanticSelection"))
          .toMap();
  const auto reused_referent_validation =
      reused_visual_referent_report
          .value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("validationReport"))
          .toMap();
  const auto reused_referent_performance =
      reused_visual_referent_report
          .value(QStringLiteral("performanceBudget"))
          .toMap();
  const auto progressive_preview =
      reused_visual_referent_report
          .value(QStringLiteral("progressivePreview"))
          .toMap();
  const auto proof_worker =
      reused_referent_validation
          .value(QStringLiteral("proofWorker"))
          .toMap();
  const auto aggregate_proof_worker =
      reused_visual_referent_report
          .value(QStringLiteral("proofWorker"))
          .toMap();
  const auto request_owned_worker_continuation =
      reused_visual_referent_report
          .value(QStringLiteral("workerContinuation"))
          .toMap();
  if (!reused_referent_validation
           .value(QStringLiteral("beforeProofReused"))
           .toBool()) {
    std::cerr
        << "async proof continuation diagnostics ok="
        << reused_visual_referent_report.value(QStringLiteral("ok")).toBool()
        << " failure="
        << reused_visual_referent_report
               .value(QStringLiteral("failureReason"))
               .toString()
               .toStdString()
        << " errors="
        << reused_visual_referent_report
               .value(QStringLiteral("errors"))
               .toStringList()
               .join(QStringLiteral(" | "))
               .toStdString()
        << " asset_reports="
        << reused_visual_referent_report
               .value(QStringLiteral("assetReports"))
               .toList()
               .size()
        << " selected_count=" << catalog_model.selectedAssetCount()
        << " worker_stages="
        << reused_visual_referent_report
               .value(QStringLiteral("workerContinuation"))
               .toMap()
               .value(QStringLiteral("completedStages"))
               .toStringList()
               .join(QStringLiteral(","))
               .toStdString()
        << " recent_referents="
        << QJsonDocument::fromVariant(
               controller.memoryPacketForPhase(QStringLiteral("sample"))
                   .value(QStringLiteral("recentVisualReferents")))
               .toJson(QJsonDocument::Compact)
               .toStdString()
        << '\n';
    return fail("a same-object fast follow-up should reuse the prior validated before proof when the source and exact base recipe are unchanged");
  }
  if (reused_referent_performance
          .value(QStringLiteral("durationMs"))
          .toLongLong() > 15000) {
    return fail("the complete same-object fast follow-up should remain under the 15 second CPU proof budget");
  }
  const auto reused_object_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto reused_object_mask = reused_object_state.mask_stack.isEmpty()
                                      ? QVariantMap{}
                                      : reused_object_state.mask_stack.last().toMap();
  const auto reused_object_alpha =
      reused_object_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto reused_object_width =
      reused_object_mask.value(QStringLiteral("segmentationWidth")).toInt();
  const auto reused_object_height =
      reused_object_mask.value(QStringLiteral("segmentationHeight")).toInt();
  const auto reused_alpha_value_at = [&](double x, double y) {
    if (reused_object_width <= 0 || reused_object_height <= 0 ||
        reused_object_alpha.size() !=
            reused_object_width * reused_object_height) {
      return 0;
    }
    const auto sample_x = std::clamp(
        static_cast<int>(std::round(x * (reused_object_width - 1))),
        0,
        reused_object_width - 1);
    const auto sample_y = std::clamp(
        static_cast<int>(std::round(y * (reused_object_height - 1))),
        0,
        reused_object_height - 1);
    return static_cast<int>(static_cast<unsigned char>(
        reused_object_alpha.at(
            sample_y * reused_object_width + sample_x)));
  };
  if (!reused_visual_referent_report.value(QStringLiteral("ok")).toBool() ||
      completed_async_request_id != async_request_id ||
      async_draft_contract.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-async-draft-run-v2") ||
      async_draft_contract
              .value(QStringLiteral("statePublicationContract"))
              .toString() !=
          QStringLiteral("agent-nonreentrant-state-publication-v1") ||
      async_draft_contract.value(QStringLiteral("eventPumpBoundary"))
              .toString() !=
          QStringLiteral("no-resident-draft-event-pump-waits") ||
      async_draft_contract
              .value(QStringLiteral("workerContinuationContract"))
              .toString() !=
          QStringLiteral("agent-request-owned-worker-continuation-v1") ||
      async_draft_contract.value(QStringLiteral("interactiveRenderPriority"))
              .toString() !=
          QStringLiteral("agent-proof-over-thumbnails-v1") ||
      async_draft_contract.value(QStringLiteral("requestId")).toString() !=
          async_request_id ||
      !async_draft_contract
           .value(QStringLiteral("controllerCallReturned"))
           .toBool() ||
      async_draft_contract.value(QStringLiteral("completionSignal")).toString() !=
          QStringLiteral("draftRunFinished") ||
      controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      controller.awaitingUserInput() || !controller.draftActive() ||
      progressive_preview.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-progressive-draft-preview-v1") ||
      !progressive_preview.value(QStringLiteral("eligible")).toBool() ||
      !progressive_preview.value(QStringLiteral("published")).toBool() ||
      progressive_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("validated") ||
      progressive_preview.value(QStringLiteral("publishCount")).toInt() != 1 ||
      progressive_preview.value(QStringLiteral("firstPublishMs")).toLongLong() < 0 ||
      progressive_preview.value(QStringLiteral("firstPublishMs")).toLongLong() > 250 ||
      progressive_preview.value(QStringLiteral("validationPending")).toBool() ||
      !progressive_preview
           .value(QStringLiteral("commitBlockedWhilePending"))
           .toBool() ||
      !progressive_preview.value(QStringLiteral("rollbackOnFailure")).toBool() ||
      progressive_preview.value(QStringLiteral("validationExecution")).toString() !=
          QStringLiteral("request-owned-proof-continuation") ||
      progressive_preview.value(QStringLiteral("explicitEventPumpLoop")).toBool() ||
      progressive_preview.value(QStringLiteral("uiEventPumpIntervalMs")).toInt() != 0 ||
      !progressive_preview.value(QStringLiteral("eventLoopHeartbeat")).toBool() ||
      progressive_preview.value(QStringLiteral("eventLoopHeartbeatIntervalMs")).toInt() != 5 ||
      !progressive_preview.value(QStringLiteral("durableStateWritten")).toBool() ||
      progressive_preview.value(QStringLiteral("thumbnailRefreshDeferred")).toBool() ||
      !saw_progressive_recipe_on_canvas ||
      !progressive_commit_was_blocked ||
      !saw_transient_preview_contract ||
      !proof_worker_ui_timer_fired ||
      !proof_worker_priority_lease_observed ||
      catalog_model.interactiveRenderingActive() ||
      proof_worker.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-proof-worker-v2") ||
      proof_worker.value(QStringLiteral("deadlineClock")).toString() !=
          QStringLiteral("request-owned-active-time") ||
      proof_worker.value(QStringLiteral("activeClockContract")).toString() !=
          QStringLiteral("agent-proof-worker-active-clock-v1") ||
      proof_worker.value(QStringLiteral("activeDurationMs")).toLongLong() < 0 ||
      !proof_worker.value(QStringLiteral("backgroundThread")).toBool() ||
      proof_worker.value(QStringLiteral("uiEventLoopServiced")).toBool() ||
      proof_worker.value(QStringLiteral("controllerCallPending")).toBool() ||
      request_owned_worker_continuation
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-request-owned-worker-continuation-v1") ||
      request_owned_worker_continuation
          .value(QStringLiteral("explicitEventPumpLoop"))
          .toBool() ||
      request_owned_worker_continuation
              .value(QStringLiteral("completedStages"))
              .toStringList() !=
          QStringList{
              QStringLiteral("retouch-materialization:ambiguous-object-asset:0"),
              QStringLiteral("proof-render:ambiguous-object-asset:0"),
          } ||
      request_owned_worker_continuation
              .value(QStringLiteral("stageDispatchCount"))
              .toInt() != 2 ||
      !aggregate_proof_worker
           .value(QStringLiteral("thumbnailWorkersQuiesced"))
           .toBool() ||
      aggregate_proof_worker
              .value(QStringLiteral("thumbnailPriorityWaitMs"))
              .toLongLong() >
          aggregate_proof_worker
              .value(QStringLiteral("thumbnailPriorityWaitBudgetMs"))
              .toLongLong() ||
      proof_worker.value(QStringLiteral("uiEventPumpIntervalMs")).toInt() != 0 ||
      proof_worker.value(QStringLiteral("uiEventPumpCount")).toInt() != 0 ||
      observed_progressive_previews.isEmpty() ||
      visual_referent_resolution
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-visual-referent-resolution-v1") ||
      !visual_referent_resolution.value(QStringLiteral("resolved")).toBool() ||
      visual_referent_resolution.value(QStringLiteral("referentId")).toString() !=
          remembered_visual_referent_id ||
      visual_referent_resolution.value(QStringLiteral("assetId")).toString() !=
          QStringLiteral("ambiguous-object-asset") ||
      visual_referent_resolution
          .value(QStringLiteral("crossAssetReuseAllowed"))
          .toBool() ||
      visual_referent_resolution
          .value(QStringLiteral("modelReplanRequired"))
          .toBool() ||
      !visual_referent_resolution.value(QStringLiteral("fastPathUsed")).toBool() ||
      !visual_referent_resolution.value(QStringLiteral("modelPlanSkipped")).toBool() ||
      !visual_referent_resolution.value(QStringLiteral("mcpDiscoverySkipped")).toBool() ||
      !visual_referent_resolution.value(QStringLiteral("planningWithinBudget")).toBool() ||
      visual_referent_resolution.value(QStringLiteral("planningDurationMs")).toLongLong() > 50 ||
      !visual_referent_resolution.value(QStringLiteral("transientMatteReused")).toBool() ||
      visual_referent_resolution.value(QStringLiteral("transientMatteReuseCount")).toInt() != 1 ||
      visual_referent_resolution.value(QStringLiteral("transientMattePersisted")).toBool() ||
      visual_referent_fast_path.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-visual-referent-fast-path-v1") ||
      !visual_referent_fast_path.value(QStringLiteral("used")).toBool() ||
      reused_referent_prompt_audit.value(QStringLiteral("plannerMode")).toString() !=
          QStringLiteral("confirmed-visual-referent-fast-path") ||
      reused_referent_prompt_audit.value(QStringLiteral("modelInvoked")).toBool() ||
      !reused_referent_prompt_audit.value(QStringLiteral("planningWithinBudget")).toBool() ||
      !reused_visual_referent_report
           .value(QStringLiteral("mcpDiscovery"))
           .toMap()
           .value(QStringLiteral("skipped"))
           .toBool() ||
      reused_referent_semantic_selection
              .value(QStringLiteral("cachedReuseCount"))
              .toInt() != 1 ||
      !reused_referent_semantic_selection
           .value(QStringLiteral("transientMatteReused"))
           .toBool() ||
      reused_referent_validation.value(QStringLiteral("proofMaxEdge")).toInt() !=
          512 ||
      reused_referent_validation
              .value(QStringLiteral("proofResolutionPolicy"))
              .toString() !=
          QStringLiteral("fast-confirmed-referent-validation-preview") ||
      reused_object_mask.value(QStringLiteral("agentVisualReferentId")).toString() !=
          remembered_visual_referent_id ||
      reused_object_mask.value(QStringLiteral("agentSelectionSource")).toString() !=
          QStringLiteral("confirmed-visual-referent") ||
      std::abs(reused_object_mask.value(QStringLiteral("exposure")).toDouble() - 0.18) >
          0.001 ||
      reused_alpha_value_at(0.74, 0.50) < 128 ||
      reused_alpha_value_at(0.26, 0.50) >= 64) {
    std::cerr
        << "reused referent report="
        << " ok:" << reused_visual_referent_report.value(QStringLiteral("ok")).toBool()
        << " awaiting:" << controller.awaitingUserInput()
        << " draft:" << controller.draftActive()
        << " errors:"
        << reused_visual_referent_report.value(QStringLiteral("errors"))
               .toStringList().join(QStringLiteral("|"))
               .toStdString()
        << " warnings:"
        << reused_visual_referent_report.value(QStringLiteral("warnings"))
               .toStringList().join(QStringLiteral("|"))
               .toStdString()
        << " resolution:"
        << QJsonDocument::fromVariant(visual_referent_resolution)
               .toJson(QJsonDocument::Compact).toStdString()
        << " remembered_id:" << remembered_visual_referent_id.toStdString()
        << " mask_referent:"
        << reused_object_mask.value(QStringLiteral("agentVisualReferentId"))
               .toString().toStdString()
        << " mask_source:"
        << reused_object_mask.value(QStringLiteral("agentSelectionSource"))
               .toString().toStdString()
        << " left_alpha:" << reused_alpha_value_at(0.26, 0.50)
        << " right_alpha:" << reused_alpha_value_at(0.74, 0.50)
        << " center_x:"
        << reused_object_mask.value(QStringLiteral("centerX")).toDouble()
        << " exposure:"
        << reused_object_mask.value(QStringLiteral("exposure")).toDouble()
        << " mcp_skipped:"
        << reused_visual_referent_report
               .value(QStringLiteral("mcpDiscovery"))
               .toMap()
               .value(QStringLiteral("skipped"))
               .toBool()
        << " semantic_selection:"
        << QJsonDocument::fromVariant(reused_referent_semantic_selection)
               .toJson(QJsonDocument::Compact).toStdString()
        << " performance:"
        << QJsonDocument::fromVariant(reused_referent_performance)
               .toJson(QJsonDocument::Compact).toStdString()
        << " fast_path:"
        << QJsonDocument::fromVariant(visual_referent_fast_path)
               .toJson(QJsonDocument::Compact).toStdString()
        << " prompt_audit:"
        << QJsonDocument::fromVariant(reused_referent_prompt_audit)
               .toJson(QJsonDocument::Compact).toStdString()
        << " proof_worker:"
        << QJsonDocument::fromVariant(proof_worker)
               .toJson(QJsonDocument::Compact).toStdString()
        << " aggregate_proof_worker:"
        << QJsonDocument::fromVariant(aggregate_proof_worker)
               .toJson(QJsonDocument::Compact).toStdString()
        << " worker_continuation:"
        << QJsonDocument::fromVariant(request_owned_worker_continuation)
               .toJson(QJsonDocument::Compact).toStdString()
        << " progressive:"
        << QJsonDocument::fromVariant(progressive_preview)
               .toJson(QJsonDocument::Compact).toStdString()
        << " timer_fired:" << proof_worker_ui_timer_fired
        << " priority_observed:" << proof_worker_priority_lease_observed
        << '\n';
    return fail("an explicit bounded same-object follow-up should reuse the exact confirmed region without model planning, MCP discovery, asking, or guessing again");
  }
  const auto reused_visual_referent_rollback = controller.rollbackDraft();
  if (!reused_visual_referent_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive()) {
    return fail("a follow-up edit using a remembered visual referent should remain rollbackable");
  }

  const auto recipe_before_progressive_cancel =
      adjustment_patch_for_asset(catalog_model.assetAt(0));
  auto progressive_cancel_requested = false;
  auto progressive_cancel_scheduled = false;
  QTimer progressive_cancel_timer;
  progressive_cancel_timer.setSingleShot(true);
  progressive_cancel_timer.setInterval(20);
  QObject::connect(
      &progressive_cancel_timer,
      &QTimer::timeout,
      &controller,
      [&]() {
        progressive_cancel_requested = true;
        controller.cancelRun(QStringLiteral("progressive_preview_test"));
      });
  const auto progressive_cancel_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::progressivePreviewChanged,
      &controller,
      [&]() {
        if (progressive_cancel_scheduled ||
            controller.progressivePreview()
                    .value(QStringLiteral("state"))
                    .toString() != QStringLiteral("validating")) {
          return;
        }
        progressive_cancel_scheduled = true;
        progressive_cancel_timer.start();
      });
  QVariantMap canceled_progressive_follow_up;
  QString canceled_async_request_id;
  auto canceled_async_completion_count = 0;
  const auto canceled_async_completion_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString& request_id, const QVariantMap& report) {
        canceled_async_request_id = request_id;
        canceled_progressive_follow_up = report;
        ++canceled_async_completion_count;
      });
  const auto canceled_async_start = controller.startDraftOnSelected(
      QStringLiteral("Make that Right jacket slightly warmer and keep everything else unchanged."));
  const auto expected_canceled_async_request_id =
      canceled_async_start.value(QStringLiteral("requestId")).toString();
  if (!canceled_async_start.value(QStringLiteral("accepted")).toBool() ||
      expected_canceled_async_request_id.isEmpty() ||
      !wait_for_condition(
          [&]() { return canceled_async_completion_count == 1; },
          30000)) {
    return fail("asynchronous progressive cancellation should finish through its request-scoped completion signal");
  }
  QObject::disconnect(canceled_async_completion_connection);
  QObject::disconnect(progressive_cancel_connection);
  progressive_cancel_timer.stop();
  const auto canceled_progressive_preview =
      canceled_progressive_follow_up
          .value(QStringLiteral("progressivePreview"))
          .toMap();
  const auto canceled_proof_worker =
      canceled_progressive_follow_up
          .value(QStringLiteral("proofWorker"))
          .toMap();
  if (!progressive_cancel_scheduled || !progressive_cancel_requested ||
      canceled_async_request_id != expected_canceled_async_request_id ||
      canceled_progressive_follow_up
              .value(QStringLiteral("asyncDraft"))
              .toMap()
              .value(QStringLiteral("contract"))
              .toString() != QStringLiteral("agent-async-draft-run-v2") ||
      canceled_progressive_follow_up
              .value(QStringLiteral("asyncDraft"))
              .toMap()
              .value(QStringLiteral("statePublicationContract"))
              .toString() !=
          QStringLiteral("agent-nonreentrant-state-publication-v1") ||
      canceled_progressive_follow_up
              .value(QStringLiteral("asyncDraft"))
              .toMap()
              .value(QStringLiteral("eventPumpBoundary"))
              .toString() !=
          QStringLiteral("no-resident-draft-event-pump-waits") ||
      canceled_progressive_follow_up
              .value(QStringLiteral("asyncDraft"))
              .toMap()
              .value(QStringLiteral("interactiveRenderPriority"))
              .toString() !=
          QStringLiteral("agent-proof-over-thumbnails-v1") ||
      canceled_progressive_follow_up.value(QStringLiteral("ok")).toBool() ||
      canceled_progressive_follow_up
              .value(QStringLiteral("cancellationReason"))
              .toString() != QStringLiteral("progressive_preview_test") ||
      canceled_progressive_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("restored-after-cancel") ||
      !canceled_progressive_preview.value(QStringLiteral("restored")).toBool() ||
      canceled_progressive_preview.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      canceled_progressive_follow_up.value(QStringLiteral("appliedCount")).toInt() != 1 ||
      canceled_progressive_follow_up.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      canceled_proof_worker.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-proof-worker-v2") ||
      !canceled_proof_worker
           .value(QStringLiteral("cancellationForwarded"))
           .toBool() ||
      !canceled_proof_worker
           .value(QStringLiteral("renderLoopCancellation"))
           .toBool() ||
      canceled_proof_worker.value(QStringLiteral("uiEventPumpCount")).toInt() != 0 ||
      canceled_progressive_preview.value(QStringLiteral("validationPending")).toBool() ||
      controller.draftActive() || controller.cancellationRequested() ||
      catalog_model.interactiveRenderingActive() ||
      adjustment_patch_for_asset(catalog_model.assetAt(0)) !=
          recipe_before_progressive_cancel) {
    return fail("canceling during a live provisional preview should restore the exact original recipe and leave nothing committable");
  }

  const auto recipe_before_pending_async_cancel =
      adjustment_patch_for_asset(catalog_model.assetAt(0));
  QVariantMap pending_async_cancel_report;
  auto pending_async_cancel_finished = false;
  const auto pending_async_cancel_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        pending_async_cancel_report = report;
        pending_async_cancel_finished = true;
      });
  const auto pending_async_start = controller.startDraftOnSelected(
      QStringLiteral("Make that Right jacket a touch brighter."));
  const auto pending_async_cancel =
      controller.cancelRun(QStringLiteral("cancel_before_execution_test"));
  if (!pending_async_start.value(QStringLiteral("accepted")).toBool() ||
      !pending_async_cancel.value(QStringLiteral("ok")).toBool() ||
      !wait_for_condition(
          [&]() { return pending_async_cancel_finished; },
          1000)) {
    return fail("an accepted asynchronous draft should remain cancelable before queued execution begins");
  }
  QObject::disconnect(pending_async_cancel_connection);
  if (pending_async_cancel_report.value(QStringLiteral("ok")).toBool() ||
      !pending_async_cancel_report
           .value(QStringLiteral("canceledBeforeExecution"))
           .toBool() ||
      pending_async_cancel_report
              .value(QStringLiteral("cancellationReason"))
              .toString() != QStringLiteral("cancel_before_execution_test") ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      adjustment_patch_for_asset(catalog_model.assetAt(0)) !=
          recipe_before_pending_async_cancel) {
    return fail("canceling a queued asynchronous draft should finish exactly once without mutating the photo");
  }

  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(ambiguous_object_content));
  const auto vague_pronoun_report = controller.runDraftOnSelected(
      QStringLiteral("Make it a little brighter."));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (vague_pronoun_report.value(QStringLiteral("ok")).toBool() ||
      vague_pronoun_report.contains(QStringLiteral("visualReferentResolution")) ||
      vague_pronoun_report.value(QStringLiteral("inputKind")).toString() !=
          QStringLiteral("grounded_selection_confirmation") ||
      !controller.awaitingUserInput() || controller.draftActive()) {
    return fail("a vague pronoun must not silently resolve to remembered visual geometry");
  }
  controller.dismissPendingAgentInput();

  catalog_model.selectIndex(1);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(1);
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(ambiguous_object_content));
  const auto cross_asset_reuse_report = controller.runDraftOnSelected(
      QStringLiteral("Make the same jacket a little brighter."));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (cross_asset_reuse_report.value(QStringLiteral("ok")).toBool() ||
      cross_asset_reuse_report.contains(QStringLiteral("visualReferentResolution")) ||
      cross_asset_reuse_report.value(QStringLiteral("inputKind")).toString() !=
          QStringLiteral("grounded_selection_confirmation") ||
      !controller.awaitingUserInput() || controller.draftActive()) {
    return fail("visual referents must never transfer their geometry to another asset");
  }
  controller.dismissPendingAgentInput();
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  const QVariantMap semantic_subject_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("appendMaskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-unrelated-background-proof")},
                    {QStringLiteral("name"), QStringLiteral("Agent · Background")},
                    {QStringLiteral("kind"), QStringLiteral("radial")},
                    {QStringLiteral("operation"), QStringLiteral("add")},
                    {QStringLiteral("enabled"), true},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("density"), 1.0},
                    {QStringLiteral("centerX"), 0.18},
                    {QStringLiteral("centerY"), 0.22},
                    {QStringLiteral("radiusX"), 0.12},
                    {QStringLiteral("radiusY"), 0.16},
                    {QStringLiteral("feather"), 0.65},
                    {QStringLiteral("exposure"), -0.16},
                },
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-semantic-subject-proof")},
                    {QStringLiteral("name"), QStringLiteral("Agent · Subject")},
                    {QStringLiteral("kind"), QStringLiteral("radial")},
                    {QStringLiteral("operation"), QStringLiteral("add")},
                    {QStringLiteral("enabled"), true},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("density"), 1.0},
                    {QStringLiteral("centerX"), 0.5},
                    {QStringLiteral("centerY"), 0.5},
                    {QStringLiteral("radiusX"), 0.25},
                    {QStringLiteral("radiusY"), 0.35},
                    {QStringLiteral("feather"), 0.72},
                    {QStringLiteral("exposure"), 0.24},
                    {QStringLiteral("agentSelectionMode"), QStringLiteral("semantic")},
                    {QStringLiteral("agentSemanticTarget"), QStringLiteral("subject")},
                },
            }},
       }},
  };
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("semantic-asset"), semantic_source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  const QVariantMap guided_object_patch{
      {QStringLiteral("masks"),
       QVariantMap{
           {QStringLiteral("appendMaskStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-guided-orange-jacket-proof")},
                    {QStringLiteral("name"), QStringLiteral("Agent · Orange jacket")},
                    {QStringLiteral("kind"), QStringLiteral("radial")},
                    {QStringLiteral("operation"), QStringLiteral("add")},
                    {QStringLiteral("enabled"), true},
                    {QStringLiteral("opacity"), 1.0},
                    {QStringLiteral("density"), 1.0},
                    {QStringLiteral("centerX"), 0.5},
                    {QStringLiteral("centerY"), 0.5},
                    {QStringLiteral("radiusX"), 0.25},
                    {QStringLiteral("radiusY"), 0.35},
                    {QStringLiteral("feather"), 0.72},
                    {QStringLiteral("exposure"), 0.20},
                    {QStringLiteral("agentSelectionMode"), QStringLiteral("guided")},
                    {QStringLiteral("agentSelectionLabel"), QStringLiteral("orange jacket")},
                },
            }},
       }},
  };
  const auto guided_object_draft =
      controller.applyDraftRecipePatchToSelected(guided_object_patch);
  const auto guided_object_report =
      guided_object_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("groundedSelection"))
          .toMap();
  const auto guided_object_selection =
      guided_object_report.value(QStringLiteral("selections"))
          .toList()
          .value(0)
          .toMap();
  const auto guided_object_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto guided_object_mask = guided_object_state.mask_stack.isEmpty()
                                      ? QVariantMap{}
                                      : guided_object_state.mask_stack.last().toMap();
  if (!guided_object_draft.value(QStringLiteral("ok")).toBool() ||
      guided_object_report.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-grounded-selection-materialization-v2") ||
      guided_object_report.value(QStringLiteral("requestedCount")).toInt() != 1 ||
      guided_object_report.value(QStringLiteral("semanticRequestedCount")).toInt() != 0 ||
      guided_object_report.value(QStringLiteral("guidedRequestedCount")).toInt() != 1 ||
      guided_object_report.value(QStringLiteral("generatedCount")).toInt() != 1 ||
      guided_object_report.value(QStringLiteral("guidedGeneratedCount")).toInt() != 1 ||
      guided_object_report.value(QStringLiteral("fallbackCount")).toInt() != 0 ||
      guided_object_selection.value(QStringLiteral("requestedMode")).toString() !=
          QStringLiteral("guided") ||
      guided_object_selection.value(QStringLiteral("targetLabel")).toString() !=
          QStringLiteral("orange jacket") ||
      guided_object_selection.value(QStringLiteral("status")).toString() !=
          QStringLiteral("evidence-guided") ||
      guided_object_mask.value(QStringLiteral("id")).toString() !=
          QStringLiteral("agent-guided-orange-jacket-proof") ||
      guided_object_mask.value(QStringLiteral("name")).toString() !=
          QStringLiteral("Agent · Orange jacket") ||
      guided_object_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("object") ||
      guided_object_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      std::abs(guided_object_mask.value(QStringLiteral("exposure")).toDouble() - 0.20) >
          0.001) {
    std::cerr << "guided object report="
              << QJsonDocument::fromVariant(guided_object_report)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    std::cerr << "guided object draft="
              << QJsonDocument::fromVariant(guided_object_draft)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("a labeled open-vocabulary object request should become an evidence-guided editable matte with stable identity");
  }
  const auto guided_object_rollback = controller.rollbackDraft();
  if (!guided_object_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0)).mask_stack.isEmpty()) {
    return fail("evidence-guided object drafts should remain rollbackable");
  }
  qputenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION", QByteArrayLiteral("1"));
  const auto semantic_subject_draft =
      controller.applyDraftRecipePatchToSelected(semantic_subject_patch);
  qunsetenv("PHOTO_EDITOR_TEST_FAKE_SEGMENTATION");
  const auto semantic_subject_report =
      semantic_subject_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("semanticSelection"))
          .toMap();
  const auto semantic_subject_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto semantic_subject_mask = semantic_subject_state.mask_stack.isEmpty()
                                         ? QVariantMap{}
                                         : semantic_subject_state.mask_stack.last().toMap();
  if (!semantic_subject_draft.value(QStringLiteral("ok")).toBool() ||
      !controller.draftActive() ||
      semantic_subject_state.mask_stack.size() != 2 ||
      semantic_subject_report.value(QStringLiteral("generatedCount")).toInt() != 1 ||
      semantic_subject_report.value(QStringLiteral("semanticGeneratedCount")).toInt() != 1 ||
      semantic_subject_report.value(QStringLiteral("guidedGeneratedCount")).toInt() != 0 ||
      semantic_subject_report.value(QStringLiteral("fallbackCount")).toInt() != 0 ||
      semantic_subject_mask.value(QStringLiteral("id")).toString() !=
          QStringLiteral("agent-semantic-subject-proof") ||
      semantic_subject_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("subject") ||
      semantic_subject_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      semantic_subject_mask.value(QStringLiteral("segmentationWidth")).toInt() != 8 ||
      semantic_subject_mask.value(QStringLiteral("segmentationHeight")).toInt() != 8 ||
      std::abs(semantic_subject_mask.value(QStringLiteral("exposure")).toDouble() - 0.24) >
          0.001) {
    return fail("agent draft application should materialize a grounded semantic subject mask while preserving stable identity and local adjustments");
  }
  const auto semantic_subject_alpha =
      semantic_subject_mask.value(QStringLiteral("segmentationData")).toByteArray();
  const auto recipe_before_intent_protection =
      adjustment_patch_for_asset(catalog_model.assetAt(0));
  const QVariantMap protected_subject_finding{
      {QStringLiteral("maskId"), QStringLiteral("agent-semantic-subject-proof")},
      {QStringLiteral("target"), QStringLiteral("subject")},
      {QStringLiteral("targetLabel"), QStringLiteral("Central subject")},
      {QStringLiteral("observation"),
       QStringLiteral("The subject lift is stronger than requested.")},
  };
  const auto protected_subject = controller.reviseDraftFinding(
      protected_subject_finding,
      QStringLiteral("Keep this change exactly."));
  const auto protected_boundary =
      protected_subject.value(QStringLiteral("intentBoundary")).toMap();
  const auto blocked_protected_correction = controller.reviseDraftFinding(
      protected_subject_finding,
      QStringLiteral("Less on this, but leave every other edit unchanged."));
  const auto blocked_protected_replan = controller.reviseDraft(
      QStringLiteral("Rebuild the draft with a softer overall treatment."));
  const auto recipe_after_blocked_protected_requests =
      adjustment_patch_for_asset(catalog_model.assetAt(0));
  if (!protected_subject.value(QStringLiteral("ok")).toBool() ||
      !protected_subject.value(QStringLiteral("protected")).toBool() ||
      protected_subject.value(QStringLiteral("source")).toString() !=
          QStringLiteral("finding-scoped-natural-language") ||
      protected_subject.value(QStringLiteral("draftRecipeChanged")).toBool() ||
      protected_boundary.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-draft-intent-boundary-v1") ||
      !protected_boundary.value(QStringLiteral("enforced")).toBool() ||
      protected_boundary.value(QStringLiteral("protectedCount")).toInt() != 1 ||
      !protected_boundary.value(QStringLiteral("targetedCorrectionBlocked")).toBool() ||
      !protected_boundary.value(QStringLiteral("fullReplanBlocked")).toBool() ||
      blocked_protected_correction.value(QStringLiteral("ok")).toBool() ||
      blocked_protected_correction.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("protected_intent_locked") ||
      blocked_protected_replan.value(QStringLiteral("ok")).toBool() ||
      blocked_protected_replan.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("protected_intent_locked") ||
      recipe_after_blocked_protected_requests != recipe_before_intent_protection ||
      !controller.draftActive()) {
    return fail("protected draft intents should block both targeted correction and full replanning without changing the active draft recipe");
  }
  const auto unlocked_subject = controller.reviseDraftFinding(
      protected_subject_finding,
      QStringLiteral("Unlock this change."));
  const auto boundary_events = controller.lastRunReport()
                                   .value(QStringLiteral("runEvents"))
                                   .toList();
  if (!unlocked_subject.value(QStringLiteral("ok")).toBool() ||
      unlocked_subject.value(QStringLiteral("protected")).toBool() ||
      unlocked_subject.value(QStringLiteral("source")).toString() !=
          QStringLiteral("finding-scoped-natural-language") ||
      !unlocked_subject.value(QStringLiteral("protectedDraftIntents")).toList().isEmpty() ||
      unlocked_subject
              .value(QStringLiteral("intentBoundary"))
              .toMap()
              .value(QStringLiteral("protectedCount"))
              .toInt() != 0 ||
      boundary_events.isEmpty() ||
      boundary_events.last().toMap().value(QStringLiteral("stage")).toString() !=
          QStringLiteral("boundary") ||
      !boundary_events.last()
           .toMap()
           .value(QStringLiteral("detail"))
           .toString()
           .contains(QStringLiteral("can be revised again")) ||
      adjustment_patch_for_asset(catalog_model.assetAt(0)) !=
          recipe_before_intent_protection ||
      !controller.draftActive()) {
    return fail("unlocking a protected draft intent should restore correction authority without changing the draft recipe");
  }
  const auto targeted_subject_correction = controller.reviseDraftFinding(
      protected_subject_finding,
      QStringLiteral("Less on this, but leave every other edit unchanged."));
  const auto targeted_subject_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  QVariantMap targeted_subject_mask;
  QVariantMap preserved_background_mask;
  for (const auto& mask_value : targeted_subject_state.mask_stack) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("agent-semantic-subject-proof")) {
      targeted_subject_mask = mask;
    } else if (mask.value(QStringLiteral("id")).toString() ==
               QStringLiteral("agent-unrelated-background-proof")) {
      preserved_background_mask = mask;
    }
  }
  const auto targeted_correction_report =
      targeted_subject_correction.value(QStringLiteral("targetedCorrection")).toMap();
  if (!targeted_subject_correction.value(QStringLiteral("ok")).toBool() ||
      targeted_subject_correction.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("targeted-draft-correction") ||
      targeted_subject_correction.value(QStringLiteral("runId")).toString() !=
          semantic_subject_draft.value(QStringLiteral("runId")).toString() ||
      targeted_subject_correction.value(QStringLiteral("fullReplan")).toBool() ||
      !targeted_subject_correction.value(QStringLiteral("preservedUnrelatedEdits")).toBool() ||
      !targeted_correction_report.value(QStringLiteral("applied")).toBool() ||
      !targeted_correction_report.value(QStringLiteral("generatedMattePreserved")).toBool() ||
      targeted_correction_report.value(QStringLiteral("maskId")).toString() !=
          QStringLiteral("agent-semantic-subject-proof") ||
      targeted_correction_report.value(QStringLiteral("originalMaskCount")).toInt() != 2 ||
      targeted_correction_report.value(QStringLiteral("correctedMaskCount")).toInt() != 2 ||
      !targeted_correction_report.value(QStringLiteral("changedFields"))
           .toStringList()
           .contains(QStringLiteral("opacity")) ||
      targeted_subject_state.mask_stack.size() != 2 ||
      targeted_subject_mask.value(QStringLiteral("segmentationData")).toByteArray() !=
          semantic_subject_alpha ||
      std::abs(targeted_subject_mask.value(QStringLiteral("opacity")).toDouble() - 0.6) >
          0.001 ||
      std::abs(targeted_subject_mask.value(QStringLiteral("exposure")).toDouble() - 0.24) >
          0.001 ||
      std::abs(preserved_background_mask.value(QStringLiteral("exposure")).toDouble() + 0.16) >
          0.001 ||
      !QFileInfo(targeted_subject_correction.value(QStringLiteral("afterProofPath")).toString())
           .isFile() ||
      !controller.draftActive()) {
    return fail("finding-targeted corrections should revise only the linked mask, preserve its generated matte and unrelated edits, render proof, and stay in the original rollback transaction");
  }
  const auto ambiguous_targeted_correction = controller.reviseDraftFinding(
      QVariantMap{
          {QStringLiteral("maskId"), QStringLiteral("agent-semantic-subject-proof")},
          {QStringLiteral("target"), QStringLiteral("subject")},
      },
      QStringLiteral("Make this feel mysterious."));
  const auto state_after_ambiguous_correction =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  QVariantMap mask_after_ambiguous_correction;
  for (const auto& mask_value : state_after_ambiguous_correction.mask_stack) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        QStringLiteral("agent-semantic-subject-proof")) {
      mask_after_ambiguous_correction = mask;
      break;
    }
  }
  if (ambiguous_targeted_correction.value(QStringLiteral("ok")).toBool() ||
      !ambiguous_targeted_correction.value(QStringLiteral("retryable")).toBool() ||
      !ambiguous_targeted_correction.value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("could not map")) ||
      std::abs(mask_after_ambiguous_correction.value(QStringLiteral("opacity")).toDouble() -
               0.6) > 0.001 ||
      !controller.lastRunReport()
           .value(QStringLiteral("targetedCorrection"))
           .toMap()
           .value(QStringLiteral("applied"))
           .toBool() ||
      !controller.draftActive()) {
    return fail("ambiguous targeted corrections should ask for clearer language without changing the draft or hiding the last successful proof");
  }
  const auto recipe_before_pointed_correction =
      adjustment_patch_for_asset(catalog_model.assetAt(0));
  const auto pointed_asset_id =
      catalog_model.assetAt(0).value(QStringLiteral("assetId")).toString();
  const auto outside_pointed_correction = controller.reviseDraftAtPoint(
      -0.05,
      0.31,
      QStringLiteral("Make only this area darker."),
      pointed_asset_id);
  const auto ambiguous_pointed_correction = controller.reviseDraftAtPoint(
      0.72,
      0.31,
      QStringLiteral("Make this feel mysterious."),
      pointed_asset_id);
  const auto foreign_asset_pointed_correction = controller.reviseDraftAtPoint(
      0.72,
      0.31,
      QStringLiteral("Make only this area darker."),
      QStringLiteral("asset-not-in-draft"));
  if (outside_pointed_correction.value(QStringLiteral("ok")).toBool() ||
      outside_pointed_correction.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("point_outside_image") ||
      ambiguous_pointed_correction.value(QStringLiteral("ok")).toBool() ||
      ambiguous_pointed_correction.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("ambiguous_point_correction") ||
      foreign_asset_pointed_correction.value(QStringLiteral("ok")).toBool() ||
      foreign_asset_pointed_correction.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("asset_outside_draft") ||
      adjustment_patch_for_asset(catalog_model.assetAt(0)) !=
          recipe_before_pointed_correction ||
      !controller.draftActive()) {
    return fail("point-grounded correction should fail closed for outside coordinates, ambiguous language, and photos outside the active draft");
  }
  const auto pointed_fallback_correction = controller.reviseDraftAtPoint(
      0.72,
      0.31,
      QStringLiteral("Make only this area darker."),
      pointed_asset_id);
  const auto pointed_fallback_report =
      pointed_fallback_correction.value(QStringLiteral("pointGroundedCorrection"))
          .toMap();
  const auto pointed_fallback_region =
      pointed_fallback_report.value(QStringLiteral("region")).toMap();
  const auto pointed_fallback_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  QVariantMap pointed_fallback_mask;
  for (const auto& mask_value : pointed_fallback_state.mask_stack) {
    const auto mask = mask_value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        pointed_fallback_report.value(QStringLiteral("maskId")).toString()) {
      pointed_fallback_mask = mask;
      break;
    }
  }
  if (!pointed_fallback_correction.value(QStringLiteral("ok")).toBool() ||
      pointed_fallback_correction.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-point-grounded-correction-v2") ||
      pointed_fallback_report.value(QStringLiteral("resolutionMode")).toString() !=
          QStringLiteral("radial-fallback") ||
      pointed_fallback_report.value(QStringLiteral("resolutionLabel")).toString() !=
          QStringLiteral("Feathered area fallback") ||
      !pointed_fallback_report.value(QStringLiteral("radialFallbackUsed")).toBool() ||
      pointed_fallback_report.value(QStringLiteral("generatedBoundary")).toBool() ||
      !pointed_fallback_report.value(QStringLiteral("preservedOutsidePointedRegion"))
           .toBool() ||
      !pointed_fallback_report
           .value(QStringLiteral("preservedOutsideResolvedTarget"))
           .toBool() ||
      pointed_fallback_state.mask_stack.size() != 3 ||
      pointed_fallback_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial") ||
      !pointed_fallback_mask
           .value(QStringLiteral("segmentationData"))
           .toByteArray()
           .isEmpty() ||
      std::abs(pointed_fallback_region.value(QStringLiteral("centerX")).toDouble() -
               0.72) > 0.0001 ||
      std::abs(pointed_fallback_region.value(QStringLiteral("centerY")).toDouble() -
               0.31) > 0.0001) {
    std::cerr << "point fallback report="
              << QJsonDocument::fromVariant(pointed_fallback_report)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("an unclear point seed should keep the visible feathered boundary and disclose that no object was claimed");
  }

  const auto pointed_snap_correction = controller.reviseDraftAtPoint(
      0.5,
      0.5,
      QStringLiteral("Make only this content darker."),
      pointed_asset_id);
  const auto pointed_snap_report =
      pointed_snap_correction.value(QStringLiteral("pointGroundedCorrection"))
          .toMap();
  const auto pointed_snap_region =
      pointed_snap_report.value(QStringLiteral("region")).toMap();
  const auto pointed_snap_evidence =
      pointed_snap_report.value(QStringLiteral("selectionEvidence")).toMap();
  const auto pointed_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  QVariantMap pointed_snap_mask;
  QVariantMap subject_after_pointed_correction;
  QVariantMap background_after_pointed_correction;
  for (const auto& mask_value : pointed_state.mask_stack) {
    const auto mask = mask_value.toMap();
    const auto id = mask.value(QStringLiteral("id")).toString();
    if (id == pointed_snap_report.value(QStringLiteral("maskId")).toString()) {
      pointed_snap_mask = mask;
    } else if (id == QStringLiteral("agent-semantic-subject-proof")) {
      subject_after_pointed_correction = mask;
    } else if (id == QStringLiteral("agent-unrelated-background-proof")) {
      background_after_pointed_correction = mask;
    }
  }
  if (!pointed_snap_correction.value(QStringLiteral("ok")).toBool() ||
      pointed_snap_correction.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-point-grounded-correction-v2") ||
      pointed_snap_correction.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("point-grounded-draft-correction") ||
      pointed_snap_correction.value(QStringLiteral("fullReplan")).toBool() ||
      !pointed_snap_correction
           .value(QStringLiteral("preservedUnrelatedEdits"))
           .toBool() ||
      pointed_snap_correction
          .value(QStringLiteral("preservedOutsidePointedRegion"))
          .toBool() ||
      !pointed_snap_correction
           .value(QStringLiteral("preservedOutsideResolvedTarget"))
           .toBool() ||
      !pointed_snap_report.value(QStringLiteral("applied")).toBool() ||
      pointed_snap_report.value(QStringLiteral("assetId")).toString() !=
          pointed_asset_id ||
      pointed_snap_report.value(QStringLiteral("maskId")).toString() !=
          pointed_snap_mask.value(QStringLiteral("id")).toString() ||
      pointed_snap_report.value(QStringLiteral("resolutionMode")).toString() !=
          QStringLiteral("content-snap") ||
      pointed_snap_report.value(QStringLiteral("resolutionLabel")).toString() !=
          QStringLiteral("Snapped to local content") ||
      pointed_snap_report.value(QStringLiteral("boundaryKind")).toString() !=
          QStringLiteral("generated-matte") ||
      !pointed_snap_report.value(QStringLiteral("generatedBoundary")).toBool() ||
      pointed_snap_report.value(QStringLiteral("radialFallbackUsed")).toBool() ||
      pointed_snap_report.value(QStringLiteral("originalMaskCount")).toInt() != 3 ||
      pointed_snap_report.value(QStringLiteral("correctedMaskCount")).toInt() != 4 ||
      !pointed_snap_report.value(QStringLiteral("changedFields"))
           .toStringList()
           .contains(QStringLiteral("exposure")) ||
      !pointed_snap_report.value(QStringLiteral("draftReversible")).toBool() ||
      pointed_snap_report.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      pointed_snap_evidence.value(QStringLiteral("status")).toString() !=
          QStringLiteral("point-content-snap") ||
      pointed_snap_evidence.value(QStringLiteral("evidenceContrast")).toDouble() <
          pointed_snap_evidence
              .value(QStringLiteral("evidenceContrastThreshold"))
              .toDouble() ||
      std::abs(pointed_snap_region.value(QStringLiteral("centerX")).toDouble() -
               0.5) > 0.0001 ||
      std::abs(pointed_snap_region.value(QStringLiteral("centerY")).toDouble() -
               0.5) > 0.0001 ||
      pointed_state.mask_stack.size() != 4 ||
      pointed_snap_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("object") ||
      pointed_snap_mask
          .value(QStringLiteral("segmentationData"))
          .toByteArray()
          .isEmpty() ||
      std::abs(pointed_snap_mask.value(QStringLiteral("feather")).toDouble()) >
          0.0001 ||
      std::abs(pointed_snap_mask.value(QStringLiteral("exposure")).toDouble() + 0.18) >
          0.0001 ||
      subject_after_pointed_correction.value(QStringLiteral("segmentationData"))
              .toByteArray() != semantic_subject_alpha ||
      std::abs(subject_after_pointed_correction.value(QStringLiteral("opacity")).toDouble() -
               0.6) > 0.001 ||
      std::abs(background_after_pointed_correction.value(QStringLiteral("exposure")).toDouble() +
               0.16) > 0.001 ||
      !QFileInfo(pointed_snap_correction.value(QStringLiteral("afterProofPath")).toString())
           .isFile() ||
      !controller.draftActive()) {
    std::cerr << "point content snap report="
              << QJsonDocument::fromVariant(pointed_snap_report)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("a confident point seed should snap to local content, expose its evidence, preserve every existing mask, and stay in the enclosing rollback transaction");
  }
  const auto semantic_subject_rollback = controller.rollbackDraft();
  if (!semantic_subject_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0)).mask_stack.isEmpty()) {
    return fail("materialized agent semantic masks should roll back with the enclosing draft transaction");
  }
  const auto semantic_guided_draft =
      controller.applyDraftRecipePatchToSelected(semantic_subject_patch);
  const auto semantic_guided_report =
      semantic_guided_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("semanticSelection"))
          .toMap();
  const auto semantic_guided_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto semantic_guided_mask = semantic_guided_state.mask_stack.isEmpty()
                                        ? QVariantMap{}
                                        : semantic_guided_state.mask_stack.last().toMap();
  const auto semantic_guided_selection =
      semantic_guided_report.value(QStringLiteral("selections"))
          .toList()
          .value(0)
          .toMap();
  if (!semantic_guided_draft.value(QStringLiteral("ok")).toBool() ||
      semantic_guided_report.value(QStringLiteral("generatedCount")).toInt() != 1 ||
      semantic_guided_report.value(QStringLiteral("semanticGeneratedCount")).toInt() != 0 ||
      semantic_guided_report.value(QStringLiteral("guidedGeneratedCount")).toInt() != 1 ||
      semantic_guided_report.value(QStringLiteral("fallbackCount")).toInt() != 0 ||
      semantic_guided_selection.value(QStringLiteral("status")).toString() !=
          QStringLiteral("evidence-guided") ||
      semantic_guided_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("subject") ||
      semantic_guided_mask.value(QStringLiteral("segmentationData")).toByteArray().isEmpty() ||
      semantic_guided_mask.value(QStringLiteral("id")).toString() !=
          QStringLiteral("agent-semantic-subject-proof")) {
    std::cerr << "guided semantic report="
              << QJsonDocument::fromVariant(semantic_guided_report)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("an unavailable semantic runtime should produce a truthful evidence-guided editable matte before falling back to a radial region");
  }
  const auto semantic_guided_rollback = controller.rollbackDraft();
  if (!semantic_guided_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive()) {
    return fail("evidence-guided semantic drafts should remain rollbackable");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  const auto semantic_fallback_draft =
      controller.applyDraftRecipePatchToSelected(semantic_subject_patch);
  const auto semantic_fallback_report =
      semantic_fallback_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("semanticSelection"))
          .toMap();
  const auto semantic_fallback_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  const auto semantic_fallback_strategy =
      semantic_fallback_draft.value(QStringLiteral("editStrategy")).toMap();
  const auto semantic_fallback_mask = semantic_fallback_state.mask_stack.isEmpty()
                                          ? QVariantMap{}
                                          : semantic_fallback_state.mask_stack.last().toMap();
  if (!semantic_fallback_draft.value(QStringLiteral("ok")).toBool() ||
      semantic_fallback_report.value(QStringLiteral("generatedCount")).toInt() != 0 ||
      semantic_fallback_report.value(QStringLiteral("fallbackCount")).toInt() != 1 ||
      semantic_fallback_mask.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("radial") ||
      semantic_fallback_mask.value(QStringLiteral("id")).toString() !=
          QStringLiteral("agent-semantic-subject-proof") ||
      semantic_fallback_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("local-mask") ||
      !semantic_fallback_strategy.value(QStringLiteral("executionAllowed")).toBool()) {
    return fail("semantic and evidence-guided failure should retain the bounded radial fallback without losing mask identity");
  }
  const auto semantic_fallback_rollback = controller.rollbackDraft();
  if (!semantic_fallback_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive()) {
    return fail("semantic radial fallback drafts should remain rollbackable");
  }

  auto agent_retouch_tools = allowed_tools;
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), retouch_source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  agent_retouch_tools.insert(QStringLiteral("retouch"), true);
  const auto agent_retouch_style = controller.createStyle(
      QStringLiteral("Agent Retouch Planning Style"),
      QStringLiteral("Natural cleanup with explicit retouch approval."),
      agent_retouch_tools,
      {},
      {},
      {});
  if (!agent_retouch_style.value(QStringLiteral("ok")).toBool()) {
    return fail("retouch planning test should create a style with explicit retouch permission");
  }
  const QJsonObject retouch_intent_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"),
       QStringLiteral("Remove the small sensor spot near the upper-right sky.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.retouch")},
               {QStringLiteral("reason"),
                QStringLiteral("The selected style explicitly permits reviewed retouching.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"),
            QStringLiteral("Remove only the small sensor spot near the upper-right sky.")},
           {QStringLiteral("colorMode"), QStringLiteral("preserve")},
           {QStringLiteral("contrast"), QStringLiteral("preserve")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("preserve")},
           {QStringLiteral("saturation"), QStringLiteral("preserve")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("preserve")},
           {QStringLiteral("mood"), QStringLiteral("natural")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"),
            QJsonArray{QStringLiteral("retouch")}},
           {QStringLiteral("requiresApproval"),
            QJsonArray{QStringLiteral("retouch")}},
           {QStringLiteral("confidence"), 0.93},
           {QStringLiteral("globalChangePolicy"), QStringLiteral("preserve")},
           {QStringLiteral("visualFindings"), QJsonArray{}},
           {QStringLiteral("localizedActions"), QJsonArray{}},
           {QStringLiteral("retouchActions"),
            QJsonArray{
                QJsonObject{
                    {QStringLiteral("kind"), QStringLiteral("distraction")},
                    {QStringLiteral("operation"), QStringLiteral("remove")},
                    {QStringLiteral("targetLabel"), QStringLiteral("sensor spot")},
                    {QStringLiteral("reason"),
                     QStringLiteral("A small isolated dark spot is visible against the clear sky.")},
                    {QStringLiteral("confidence"), 0.94},
                    {QStringLiteral("region"),
                     QJsonObject{
                         {QStringLiteral("centerX"), 0.78},
                         {QStringLiteral("centerY"), 0.21},
                         {QStringLiteral("radiusX"), 0.018},
                         {QStringLiteral("radiusY"), 0.02},
                     }},
                },
            }},
       }},
  };
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(retouch_intent_content));
  const auto retouch_plan_stop = controller.runDraftOnSelected(
      QStringLiteral("remove only the small sensor spot in the upper-right sky"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto retouch_plan_patch =
      retouch_plan_stop.value(QStringLiteral("plannedRecipePatch")).toMap();
  const auto planned_retouch_stack =
      retouch_plan_patch.value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("appendRetouchStack"))
          .toList();
  const auto planned_retouch = planned_retouch_stack.value(0).toMap();
  const auto planned_retouch_point =
      planned_retouch.value(QStringLiteral("brushPoints"))
          .toList()
          .value(0)
          .toMap();
  const auto planned_retouch_region =
      planned_retouch.value(QStringLiteral("agentRegion")).toMap();
  const auto retouch_plan_risk =
      retouch_plan_stop.value(QStringLiteral("riskAssessment")).toMap();
  const auto retouch_plan_strategy =
      retouch_plan_stop.value(QStringLiteral("editStrategy")).toMap();
  if (retouch_plan_stop.value(QStringLiteral("ok")).toBool() ||
      retouch_plan_stop.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("needs_user_input") ||
      !retouch_plan_stop.value(QStringLiteral("needsUserInput")).toBool() ||
      retouch_plan_stop.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("high_risk_or_low_confidence") ||
      planned_retouch_stack.size() != 1 ||
      planned_retouch.value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      planned_retouch.value(QStringLiteral("toolRole")).toString() ==
          QStringLiteral("retouch") ||
      std::abs(planned_retouch_point.value(QStringLiteral("x")).toDouble() -
               0.78) > 0.001 ||
      std::abs(planned_retouch_point.value(QStringLiteral("y")).toDouble() -
               0.21) > 0.001 ||
      std::abs(planned_retouch_region.value(QStringLiteral("radiusX")).toDouble() -
               0.018) > 0.001 ||
      std::abs(planned_retouch_region.value(QStringLiteral("radiusY")).toDouble() -
               0.02) > 0.001 ||
      !retouch_plan_risk.value(QStringLiteral("approvalTypes"))
           .toStringList()
           .contains(QStringLiteral("retouch")) ||
      retouch_plan_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("protected-retouch") ||
      !retouch_plan_strategy.value(QStringLiteral("approvalRequired")).toBool() ||
      controller.draftActive()) {
    std::cerr << "retouch plan stop="
              << QJsonDocument::fromVariant(retouch_plan_stop)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("a grounded retouch request should compile append-only geometry and stop for approval before drafting");
  }
  const auto approved_retouch_draft = controller.answerPendingUserInput(
      QStringLiteral(
          "I reviewed this request and approve continuing with a reversible draft."));
  const auto approved_retouch_asset_report =
      approved_retouch_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap();
  const auto approved_retouch_materialization =
      approved_retouch_asset_report
          .value(QStringLiteral("retouchMaterialization"))
          .toMap();
  const auto approved_retouch_validation =
      approved_retouch_asset_report.value(QStringLiteral("validationReport"))
          .toMap();
  const auto approved_retouch_quality =
      approved_retouch_validation.value(QStringLiteral("retouchProofQuality"))
          .toMap();
  const auto approved_retouch_rejection =
      approved_retouch_draft.value(QStringLiteral("proofRejection")).toMap();
  const auto approved_retouch_critique =
      approved_retouch_rejection.value(QStringLiteral("visualCritique")).toMap();
  const auto approved_retouch_repair_seed =
      approved_retouch_draft.value(QStringLiteral("proofRepair")).toMap();
  const auto proof_retouch =
      approved_retouch_validation.value(QStringLiteral("recipeDiff"))
          .toMap()
          .value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("appendRetouchStack"))
          .toMap()
          .value(QStringLiteral("after"))
          .toList()
          .value(0)
          .toMap();
  const auto approved_retouch_state =
      adjustment_state_for_asset(catalog_model.assetAt(0));
  QVariantMap applied_retouch;
  for (const auto& value : approved_retouch_state.mask_stack) {
    const auto mask = value.toMap();
    if (mask.value(QStringLiteral("id")).toString() ==
        planned_retouch.value(QStringLiteral("id")).toString()) {
      applied_retouch = mask;
      break;
    }
  }
  if (approved_retouch_draft.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput() || controller.draftActive() ||
      !approved_retouch_draft.value(QStringLiteral("proofRejected")).toBool() ||
      !approved_retouch_draft.value(QStringLiteral("approvalConsumed")).toBool() ||
      !approved_retouch_draft.value(QStringLiteral("retryable")).toBool() ||
      approved_retouch_draft.value(QStringLiteral("retryPolicy")).toString() !=
          QStringLiteral("replan-from-original-with-adjusted-target") ||
      approved_retouch_rejection.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-proof-rejection-presentation-v1") ||
      approved_retouch_rejection.value(QStringLiteral("kind")).toString() !=
          QStringLiteral("retouch-quality") ||
      approved_retouch_rejection.value(QStringLiteral("headline")).toString() !=
          QStringLiteral("I left your photo unchanged.") ||
      approved_retouch_rejection.value(QStringLiteral("primaryActionLabel"))
              .toString() != QStringLiteral("Point to repair") ||
      approved_retouch_rejection.value(QStringLiteral("primaryActionKey"))
              .toString() != QStringLiteral("point-to-repair") ||
      approved_retouch_critique.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-visual-critique-v1") ||
      approved_retouch_critique.value(QStringLiteral("verdict")).toString() !=
          QStringLiteral("reject") ||
      approved_retouch_critique.value(QStringLiteral("finding"))
          .toString()
          .isEmpty() ||
      approved_retouch_critique.value(QStringLiteral("evidence"))
          .toList()
          .size() != 4 ||
      approved_retouch_critique.value(QStringLiteral("repair"))
              .toMap()
              .value(QStringLiteral("kind"))
              .toString() != QStringLiteral("point-and-tighten") ||
      approved_retouch_repair_seed.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-proof-repair-seed-v1") ||
      !approved_retouch_repair_seed
           .value(QStringLiteral("usesOriginalRecipe"))
           .toBool() ||
      !approved_retouch_rejection
           .value(QStringLiteral("originalRecipePreserved"))
           .toBool() ||
      !approved_retouch_draft.value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("left your photo unchanged")) ||
      !applied_retouch.isEmpty() ||
      proof_retouch.value(QStringLiteral("id")).toString() !=
          planned_retouch.value(QStringLiteral("id")).toString() ||
      proof_retouch.value(QStringLiteral("toolRole")).toString() !=
          QStringLiteral("retouch") ||
      proof_retouch.value(QStringLiteral("retouchMode")).toString() !=
          QStringLiteral("remove") ||
      !planned_retouch.value(QStringLiteral("inpaintPatchData"))
           .toByteArray()
           .isEmpty() ||
      !planned_retouch.value(QStringLiteral("inpaintSourceMaskData"))
           .toByteArray()
           .isEmpty() ||
      approved_retouch_materialization.value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-approved-retouch-materialization-v1") ||
      approved_retouch_materialization.value(QStringLiteral("artifactOwner"))
              .toString() != QStringLiteral("application") ||
      approved_retouch_materialization.value(QStringLiteral("executionMode"))
              .toString() != QStringLiteral("background-worker") ||
      !approved_retouch_materialization.value(QStringLiteral("backgroundThread"))
           .toBool() ||
      approved_retouch_materialization.value(QStringLiteral("requestedCount"))
              .toInt() != 1 ||
      approved_retouch_materialization
              .value(QStringLiteral("materializedCount"))
              .toInt() != 1 ||
      !approved_retouch_materialization.value(QStringLiteral("failClosed"))
           .toBool() ||
      proof_retouch.value(QStringLiteral("inpaintPatchData"))
          .toByteArray()
          .isEmpty() ||
      proof_retouch.value(QStringLiteral("inpaintSourceMaskData"))
          .toByteArray()
          .isEmpty() ||
      proof_retouch.value(QStringLiteral("inpaintModelName"))
          .toString()
          .isEmpty() ||
      proof_retouch.value(QStringLiteral("inpaintQualityMode")).toString() !=
          QStringLiteral("final") ||
      proof_retouch.value(QStringLiteral("inpaintFinalRequired")).toBool() ||
      approved_retouch_materialization.value(QStringLiteral("artifacts"))
          .toList()
          .value(0)
          .toMap()
          .value(QStringLiteral("reviewedPlanHash"))
          .toString()
          .isEmpty() ||
      !approved_retouch_materialization.value(QStringLiteral("artifacts"))
           .toList()
           .value(0)
           .toMap()
           .value(QStringLiteral("reviewedRegionGeometryApplied"))
           .toBool() ||
      approved_retouch_validation.value(QStringLiteral("recipeValidation"))
              .toMap()
              .value(QStringLiteral("missingRetouchProvenanceCount"))
              .toInt() != 0 ||
      !approved_retouch_quality.value(QStringLiteral("required")).toBool() ||
      !approved_retouch_quality.value(QStringLiteral("available")).toBool() ||
      approved_retouch_quality.value(QStringLiteral("ok")).toBool() ||
      approved_retouch_quality.value(QStringLiteral("backgroundContinuity"))
          .toBool() ||
      approved_retouch_validation.value(QStringLiteral("ok")).toBool() ||
      !approved_retouch_validation.value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("Retouch proof rejected")) ||
      approved_retouch_validation.value(QStringLiteral("warnings"))
          .toStringList()
          .join(QStringLiteral(" "))
          .contains(QStringLiteral("retouch provenance risk"),
                    Qt::CaseInsensitive)) {
    return fail("approved retouch should materialize the reviewed plan, reject bad proof, and restore the original recipe");
  }
  const auto approved_retouch_artifacts =
      approved_retouch_validation.value(QStringLiteral("artifacts")).toMap();
  const QImage retouch_before(
      approved_retouch_artifacts.value(QStringLiteral("beforeProofPath"))
          .toString());
  const QImage retouch_after(
      approved_retouch_artifacts.value(QStringLiteral("afterProofPath"))
          .toString());
  if (retouch_before.isNull() || retouch_after.isNull() ||
      retouch_before.size() != retouch_after.size()) {
    return fail("approved retouch should publish readable before/after pixel proof");
  }
  auto changed_inside_retouch_region = 0;
  auto changed_outside_retouch_region = 0;
  for (auto y = 0; y < retouch_before.height(); ++y) {
    for (auto x = 0; x < retouch_before.width(); ++x) {
      const auto before_pixel = retouch_before.pixelColor(x, y);
      const auto after_pixel = retouch_after.pixelColor(x, y);
      const auto delta = std::abs(before_pixel.red() - after_pixel.red()) +
                         std::abs(before_pixel.green() - after_pixel.green()) +
                         std::abs(before_pixel.blue() - after_pixel.blue());
      if (delta <= 3) {
        continue;
      }
      const auto normalized_x =
          static_cast<double>(x) /
          static_cast<double>(std::max(1, retouch_before.width() - 1));
      const auto normalized_y =
          static_cast<double>(y) /
          static_cast<double>(std::max(1, retouch_before.height() - 1));
      if (std::abs(normalized_x - 0.78) <= 0.14 &&
          std::abs(normalized_y - 0.21) <= 0.16) {
        ++changed_inside_retouch_region;
      } else {
        ++changed_outside_retouch_region;
      }
    }
  }
  auto remaining_dark_target_pixels = 0;
  auto maximum_target_background_error = 0;
  for (auto y = 30; y <= 36; ++y) {
    for (auto x = 183; x <= 189; ++x) {
      const auto after_pixel = retouch_after.pixelColor(x, y);
      const auto left_background = retouch_after.pixelColor(181, y);
      const auto right_background = retouch_after.pixelColor(191, y);
      const QColor expected_background(
          (left_background.red() + right_background.red()) / 2,
          (left_background.green() + right_background.green()) / 2,
          (left_background.blue() + right_background.blue()) / 2);
      maximum_target_background_error = std::max(
          maximum_target_background_error,
          std::abs(after_pixel.red() - expected_background.red()) +
              std::abs(after_pixel.green() - expected_background.green()) +
              std::abs(after_pixel.blue() - expected_background.blue()));
      if (after_pixel.red() + after_pixel.green() + after_pixel.blue() + 60 <
          expected_background.red() + expected_background.green() +
              expected_background.blue()) {
        ++remaining_dark_target_pixels;
      }
    }
  }
  if (changed_inside_retouch_region <= 0 ||
      changed_outside_retouch_region != 0 ||
      remaining_dark_target_pixels != 0 ||
      maximum_target_background_error > 60) {
    std::cerr << "retouch changed inside=" << changed_inside_retouch_region
              << " outside=" << changed_outside_retouch_region
              << " dark target pixels=" << remaining_dark_target_pixels
              << " maximum background error="
              << maximum_target_background_error << '\n';
    return fail("approved retouch should cleanly remove the distraction only inside the reviewed bounded target");
  }
  const auto retouch_proof_directory =
      qEnvironmentVariable("PHOTO_EDITOR_AGENT_RETOUCH_PROOF_DIR").trimmed();
  if (!retouch_proof_directory.isEmpty()) {
    const auto retouch_patch = QImage::fromData(
        proof_retouch.value(QStringLiteral("inpaintPatchData")).toByteArray(),
        "PNG");
    if (!QDir().mkpath(retouch_proof_directory) ||
        !retouch_before.save(
            QDir(retouch_proof_directory)
                .filePath(QStringLiteral("before-retouch.png")),
            "PNG") ||
        !retouch_after.save(
            QDir(retouch_proof_directory)
                .filePath(QStringLiteral("after-retouch.png")),
            "PNG") ||
        retouch_patch.isNull() ||
        !retouch_patch.save(
            QDir(retouch_proof_directory)
                .filePath(QStringLiteral("retouch-patch.png")),
            "PNG")) {
      return fail("requested durable retouch proof images should be writable");
    }
    QSaveFile proof_manifest(
        QDir(retouch_proof_directory)
            .filePath(QStringLiteral("retouch-proof.json")));
    if (!proof_manifest.open(QIODevice::WriteOnly) ||
        proof_manifest.write(QJsonDocument::fromVariant(QVariantMap{
                                 {QStringLiteral("contract"),
                                  QStringLiteral("agent-retouch-pixel-proof-v1")},
                                 {QStringLiteral("changedInsideTarget"),
                                  changed_inside_retouch_region},
                                 {QStringLiteral("changedOutsideTarget"),
                                  changed_outside_retouch_region},
                                 {QStringLiteral("remainingDarkTargetPixels"),
                                  remaining_dark_target_pixels},
                                 {QStringLiteral("maximumTargetBackgroundError"),
                                  maximum_target_background_error},
                                 {QStringLiteral("materialization"),
                                  approved_retouch_materialization},
                             })
                                 .toJson(QJsonDocument::Indented)) < 0 ||
        !proof_manifest.commit()) {
      return fail("requested durable retouch proof manifest should be writable");
    }
  }
  if (controller.draftActive() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0)).mask_stack.isEmpty()) {
    return fail("a rejected semantic retouch must already be rolled back before publication");
  }

  const auto repaired_retouch_plan =
      controller.prepareRejectedRetouchRepairAtPoint(0.74, 0.24,
                                                     QStringLiteral("asset-1"));
  const auto repaired_retouch =
      repaired_retouch_plan.value(QStringLiteral("plannedRecipePatch"))
          .toMap()
          .value(QStringLiteral("retouch"))
          .toMap()
          .value(QStringLiteral("appendRetouchStack"))
          .toList()
          .value(0)
          .toMap();
  const auto repaired_region =
      repaired_retouch.value(QStringLiteral("agentRegion")).toMap();
  const auto repaired_point =
      repaired_retouch.value(QStringLiteral("brushPoints"))
          .toList()
          .value(0)
          .toMap();
  const auto repair_receipt =
      repaired_retouch_plan.value(QStringLiteral("proofRepair")).toMap();
  if (repaired_retouch_plan.value(QStringLiteral("ok")).toBool() ||
      !repaired_retouch_plan.value(QStringLiteral("needsUserInput")).toBool() ||
      repaired_retouch_plan.value(QStringLiteral("inputKind")).toString() !=
          QStringLiteral("pre_draft_plan_approval") ||
      repaired_retouch_plan.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("point_guided_retouch_repair") ||
      !controller.awaitingUserInput() || controller.draftActive() ||
      repair_receipt.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-proof-repair-plan-v1") ||
      repair_receipt.value(QStringLiteral("rebuildBase")).toString() !=
          QStringLiteral("untouched-original") ||
      !repair_receipt.value(QStringLiteral("freshApprovalRequired")).toBool() ||
      repair_receipt.value(QStringLiteral("oldArtifactReused")).toBool() ||
      std::abs(repaired_region.value(QStringLiteral("centerX")).toDouble() -
               0.74) > 0.001 ||
      std::abs(repaired_region.value(QStringLiteral("centerY")).toDouble() -
               0.24) > 0.001 ||
      std::abs(repaired_region.value(QStringLiteral("radiusX")).toDouble() -
               0.01476) > 0.0001 ||
      std::abs(repaired_region.value(QStringLiteral("radiusY")).toDouble() -
               0.0164) > 0.0001 ||
      std::abs(repaired_point.value(QStringLiteral("x")).toDouble() - 0.74) >
          0.001 ||
      std::abs(repaired_point.value(QStringLiteral("y")).toDouble() - 0.24) >
          0.001 ||
      !repaired_retouch.value(QStringLiteral("inpaintPatchData"))
           .toByteArray()
           .isEmpty() ||
      !repaired_retouch.value(QStringLiteral("segmentationData"))
           .toByteArray()
           .isEmpty() ||
      !adjustment_state_for_asset(catalog_model.assetAt(0))
           .mask_stack
           .isEmpty()) {
    std::cerr << "repaired retouch plan="
              << QJsonDocument::fromVariant(repaired_retouch_plan)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("a rejected cleanup should turn one new point into a tighter original-based plan that requires fresh approval");
  }
  const auto rebuilt_retouch_draft = controller.answerPendingUserInput(
      QStringLiteral(
          "I reviewed the tighter point-guided target and approve a new reversible draft."));
  const auto rebuilt_asset_report =
      rebuilt_retouch_draft.value(QStringLiteral("assetReports"))
          .toList()
          .value(0)
          .toMap();
  const auto rebuilt_materialization =
      rebuilt_asset_report.value(QStringLiteral("retouchMaterialization"))
          .toMap();
  const auto rebuilt_artifact =
      rebuilt_materialization.value(QStringLiteral("artifacts"))
          .toList()
          .value(0)
          .toMap();
  const auto original_failed_artifact =
      approved_retouch_materialization.value(QStringLiteral("artifacts"))
          .toList()
          .value(0)
          .toMap();
  const auto rebuilt_source_width =
      std::max(1, rebuilt_artifact.value(QStringLiteral("sourceWidth")).toInt());
  const auto rebuilt_source_height =
      std::max(1, rebuilt_artifact.value(QStringLiteral("sourceHeight")).toInt());
  const auto rebuilt_center_x =
      (rebuilt_artifact.value(QStringLiteral("sourceBoundsX")).toDouble() +
       0.5 * rebuilt_artifact.value(QStringLiteral("sourceBoundsWidth")).toDouble()) /
      static_cast<double>(rebuilt_source_width);
  const auto rebuilt_center_y =
      (rebuilt_artifact.value(QStringLiteral("sourceBoundsY")).toDouble() +
       0.5 * rebuilt_artifact.value(QStringLiteral("sourceBoundsHeight")).toDouble()) /
      static_cast<double>(rebuilt_source_height);
  if (controller.awaitingUserInput() ||
      rebuilt_materialization.value(QStringLiteral("requestedCount")).toInt() != 1 ||
      rebuilt_materialization.value(QStringLiteral("materializedCount")).toInt() != 1 ||
      rebuilt_artifact.value(QStringLiteral("reviewedPlanHash")).toString().isEmpty() ||
      rebuilt_artifact.value(QStringLiteral("reviewedPlanHash")).toString() ==
          original_failed_artifact.value(QStringLiteral("reviewedPlanHash")).toString() ||
      std::abs(rebuilt_center_x - 0.74) > 0.03 ||
      std::abs(rebuilt_center_y - 0.24) > 0.03 ||
      rebuilt_artifact.value(QStringLiteral("sourceBoundsWidth")).toInt() >=
          original_failed_artifact.value(QStringLiteral("sourceBoundsWidth")).toInt() ||
      rebuilt_artifact.value(QStringLiteral("sourceBoundsHeight")).toInt() >=
          original_failed_artifact.value(QStringLiteral("sourceBoundsHeight")).toInt() ||
      (!rebuilt_retouch_draft.value(QStringLiteral("ok")).toBool() &&
       !rebuilt_retouch_draft.value(QStringLiteral("proofRejected")).toBool())) {
    std::cerr << "rebuilt retouch draft="
              << QJsonDocument::fromVariant(rebuilt_retouch_draft)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("approving a point-guided repair should materialize a fresh, smaller artifact at the new target and proof-gate it again");
  }
  if (controller.draftActive()) {
    const auto rebuilt_rollback =
        controller.rollbackDraft(QStringLiteral("point_repair_test_complete"));
    if (!rebuilt_rollback.value(QStringLiteral("ok")).toBool()) {
      return fail("the approved point-guided repair draft should remain reversible");
    }
  }
  if (!adjustment_state_for_asset(catalog_model.assetAt(0))
           .mask_stack
           .isEmpty()) {
    return fail("the point-guided repair test should restore the original recipe after review");
  }

  auto uncertain_retouch_content = retouch_intent_content;
  auto uncertain_retouch_intent =
      uncertain_retouch_content.value(QStringLiteral("editIntent")).toObject();
  auto uncertain_retouch_actions =
      uncertain_retouch_intent.value(QStringLiteral("retouchActions")).toArray();
  auto uncertain_retouch_action = uncertain_retouch_actions.at(0).toObject();
  uncertain_retouch_action.insert(QStringLiteral("confidence"), 0.72);
  uncertain_retouch_actions[0] = uncertain_retouch_action;
  uncertain_retouch_intent.insert(
      QStringLiteral("retouchActions"), uncertain_retouch_actions);
  uncertain_retouch_content.insert(
      QStringLiteral("editIntent"), uncertain_retouch_intent);
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(uncertain_retouch_content));
  const auto uncertain_retouch_stop = controller.runDraftOnSelected(
      QStringLiteral("remove the possible spot in the upper-right sky"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (uncertain_retouch_stop.value(QStringLiteral("ok")).toBool() ||
      uncertain_retouch_stop.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("needs_user_input") ||
      uncertain_retouch_stop.value(QStringLiteral("modelPromptAudit"))
              .toMap()
              .value(QStringLiteral("retouchIntentRejected"))
              .toString() !=
          QStringLiteral("missing-bounded-high-confidence-evidence") ||
      uncertain_retouch_stop.contains(QStringLiteral("plannedRecipePatch")) ||
      controller.draftActive()) {
    return fail("uncertain retouch evidence should ask for a clearer target without compiling a patch");
  }
  const auto uncertain_retouch_dismiss = controller.dismissPendingAgentInput();
  if (!uncertain_retouch_dismiss.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput()) {
    return fail("dismissing an uncertain retouch clarification should clear pending input");
  }

  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(retouch_intent_content));
  const auto disallowed_retouch_stop = controller.runDraftOnSelected(
      QStringLiteral("remove only the small sensor spot in the upper-right sky"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (disallowed_retouch_stop.value(QStringLiteral("ok")).toBool() ||
      disallowed_retouch_stop.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("cannot_complete_safely") ||
      !disallowed_retouch_stop.value(QStringLiteral("cannotCompleteSafely")).toBool() ||
      !disallowed_retouch_stop.value(QStringLiteral("safeStopReason"))
           .toString()
           .contains(QStringLiteral("does not allow retouching")) ||
      controller.draftActive()) {
    return fail("a style that disallows retouch should stop the semantic cleanup plan without drafting");
  }
  const auto disallowed_retouch_dismiss = controller.dismissPendingAgentInput();
  if (!disallowed_retouch_dismiss.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput()) {
    return fail("dismissing a disallowed retouch stop should clear pending input");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  const QJsonObject high_risk_patch_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Tighten the composition with a strong editorial crop.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.rules.crop")},
               {QStringLiteral("reason"), QStringLiteral("The style allows composition cleanup with review.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Tighten the composition with a strong editorial crop.")},
           {QStringLiteral("colorMode"), QStringLiteral("preserve")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("preserve")},
           {QStringLiteral("crop"), QStringLiteral("tighter")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("editorial")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("crop")}},
           {QStringLiteral("requiresApproval"), QJsonArray{QStringLiteral("large_crop")}},
           {QStringLiteral("confidence"), 0.86},
       }},
  };
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(high_risk_patch_content));
  const auto high_risk_stop =
      controller.runDraftOnSelected(QStringLiteral("make the sample more editorial"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto high_risk_policy =
      high_risk_stop.value(QStringLiteral("preDraftStopPolicy")).toMap();
  const auto high_risk_assessment =
      high_risk_stop.value(QStringLiteral("riskAssessment")).toMap();
  const auto high_risk_strategy =
      high_risk_stop.value(QStringLiteral("editStrategy")).toMap();
  if (high_risk_stop.value(QStringLiteral("ok")).toBool() ||
      high_risk_stop.value(QStringLiteral("decision")).toString() != QStringLiteral("needs_user_input") ||
      high_risk_stop.value(QStringLiteral("safeStopReason")).toString() !=
          QStringLiteral("high_risk_or_low_confidence") ||
      !high_risk_stop.value(QStringLiteral("needsUserInput")).toBool() ||
      !high_risk_policy.value(QStringLiteral("stopRequired")).toBool() ||
      !high_risk_policy.value(QStringLiteral("triggers")).toStringList().contains(QStringLiteral("high_risk")) ||
      !high_risk_assessment.value(QStringLiteral("approvalTypes")).toStringList().contains(QStringLiteral("large_crop")) ||
      high_risk_strategy.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-edit-strategy-route-v1") ||
      high_risk_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("composition") ||
      !high_risk_strategy.value(QStringLiteral("appRouted")).toBool() ||
      !high_risk_strategy.value(QStringLiteral("approvalRequired")).toBool() ||
      !high_risk_strategy.value(QStringLiteral("sensitiveActionsPauseBeforeDraft")).toBool() ||
      !controller.awaitingUserInput() ||
      controller.pendingUserQuestions().isEmpty() ||
      controller.status() != QStringLiteral("Needs user input") ||
      controller.draftActive()) {
    return fail("model-planned high-risk edits should stop for user approval before creating a draft");
  }
  if (high_risk_stop.value(QStringLiteral("editingFreedom")).toString() !=
      QStringLiteral("balanced")) {
    return fail("a stopped high-risk plan should preserve the exact editing-freedom boundary it was reviewed under");
  }
  controller.setEditingFreedom(QStringLiteral("transform"));
  const auto stale_freedom_approval = controller.answerPendingUserInput(
      QStringLiteral("I approve this reversible draft."));
  if (stale_freedom_approval.value(QStringLiteral("ok")).toBool() ||
      !stale_freedom_approval.value(QStringLiteral("errors"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("editing freedom changed")) ||
      !controller.awaitingUserInput() || controller.draftActive()) {
    return fail("approval should never resume a plan under a different editing-freedom boundary");
  }
  controller.setEditingFreedom(QStringLiteral("balanced"));
  const auto high_risk_dismiss = controller.dismissPendingAgentInput();
  if (!high_risk_dismiss.value(QStringLiteral("ok")).toBool() || controller.awaitingUserInput()) {
    return fail("dismissing a high-risk pre-draft stop should clear pending input");
  }
  const auto recipe_before_destructive_wording =
      catalog_model.selectedAdjustmentPatchJson();
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(high_risk_patch_content));
  const auto destructive_intent_stop = controller.runDraftOnSelected(
      QStringLiteral(
          "overwrite the original and discard all existing edits with this crop"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto destructive_risk =
      destructive_intent_stop.value(QStringLiteral("riskAssessment")).toMap();
  QVariantMap destructive_risk_item;
  for (const auto& risk_value :
       destructive_risk.value(QStringLiteral("risks")).toList()) {
    const auto risk = risk_value.toMap();
    if (risk.value(QStringLiteral("type")).toString() ==
        QStringLiteral("destructiveIntent")) {
      destructive_risk_item = risk;
      break;
    }
  }
  if (destructive_intent_stop.value(QStringLiteral("ok")).toBool() ||
      destructive_intent_stop.value(QStringLiteral("decision")).toString() !=
          QStringLiteral("needs_user_input") ||
      destructive_intent_stop.value(QStringLiteral("safetyContract")).toString() !=
          QStringLiteral("agent-destructive-intent-seatbelt-v1") ||
      !destructive_risk.value(QStringLiteral("approvalTypes"))
           .toStringList()
           .contains(QStringLiteral("destructive_intent")) ||
      destructive_risk_item.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-destructive-intent-seatbelt-v1") ||
      !destructive_risk_item.value(QStringLiteral("sourcePreserved")).toBool() ||
      !destructive_risk_item.value(QStringLiteral("currentRecipePreserved")).toBool() ||
      !destructive_risk_item.value(QStringLiteral("draftOnly")).toBool() ||
      !destructive_intent_stop.value(QStringLiteral("userQuestions"))
           .toList()
           .value(0)
           .toString()
           .contains(QStringLiteral("preserve the source and current edit")) ||
      !controller.awaitingUserInput() || controller.draftActive() ||
      catalog_model.selectedAdjustmentPatchJson() !=
          recipe_before_destructive_wording) {
    return fail("destructive-looking wording should trigger an exact reversible-draft approval seatbelt before any recipe mutation");
  }
  const auto destructive_intent_dismiss =
      controller.dismissPendingAgentInput();
  if (!destructive_intent_dismiss.value(QStringLiteral("ok")).toBool() ||
      controller.awaitingUserInput() || controller.draftActive() ||
      catalog_model.selectedAdjustmentPatchJson() !=
          recipe_before_destructive_wording) {
    return fail("dismissing destructive-intent approval should preserve the untouched source recipe");
  }
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(high_risk_patch_content));
  const auto negated_destructive_wording = controller.runDraftOnSelected(
      QStringLiteral(
          "do not overwrite the original; only create a reversible editorial crop"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (negated_destructive_wording.value(QStringLiteral("riskAssessment"))
          .toMap()
          .value(QStringLiteral("approvalTypes"))
          .toStringList()
          .contains(QStringLiteral("destructive_intent"))) {
    return fail("explicit source-preservation wording must not be misclassified as destructive intent");
  }
  controller.dismissPendingAgentInput();
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  controller.setUserInstructions(QStringLiteral("bright warm editorial draft with soft highlights"));

  const auto recipe_before_manual_gate =
      catalog_model
          .selectedAdjustmentPatchJson();
  controller.setManualEditingActive(true);
  const auto paused_agent_run =
      controller.runDraftOnSelected(QStringLiteral("should wait for manual edits"));
  if (paused_agent_run.value(QStringLiteral("ok")).toBool() ||
      paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
              .toMap()
              .value(QStringLiteral("action"))
              .toString() != QStringLiteral("deferred") ||
      !paused_agent_run.value(QStringLiteral("backgroundWorkPolicy"))
           .toMap()
           .value(QStringLiteral("manualEditingActive"))
           .toBool() ||
      controller.draftActive() ||
      catalog_model
              .selectedAdjustmentPatchJson() !=
          recipe_before_manual_gate) {
    return fail("agent draft work should pause without mutating recipes during active manual editing");
  }
  controller.setManualEditingActive(false);

  const auto recipe_before_analysis =
      catalog_model
          .selectedAdjustmentPatchJson();
  auto reentrant_attempted = false;
  QVariantMap reentrant_report;
  const auto reentrant_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::runningChanged,
      &controller,
      [&controller, &reentrant_attempted, &reentrant_report]() {
        if (!controller.running() || reentrant_attempted) {
          return;
        }
        reentrant_attempted = true;
        reentrant_report = controller.runDraftForScope(
            QStringLiteral("current-photo"),
            QStringLiteral("This reentrant request must not start."));
      });
  const auto analysis = controller.analyzeSelected();
  QObject::disconnect(reentrant_connection);
  if (!analysis.value(QStringLiteral("ok")).toBool()) {
    std::cerr << analysis.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent analyze-only run should succeed");
  }
  if (!reentrant_attempted ||
      reentrant_report.value(QStringLiteral("ok")).toBool() ||
      reentrant_report.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("run_in_progress") ||
      controller.running()) {
    return fail("an active agent run should reject a reentrant draft without disturbing the first run");
  }
  if (analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analyze-only report should identify its run mode");
  }
  if (controller.draftActive()) {
    return fail("analyze-only run should not create an active draft");
  }
  if (catalog_model
          .selectedAdjustmentPatchJson() !=
      recipe_before_analysis) {
    return fail("analyze-only run should not mutate the selected recipe");
  }
  if (analysis.value(QStringLiteral("assetMetadata")).toMap().value(QStringLiteral("cameraLabel")).toString() !=
      QStringLiteral("Test Camera")) {
    return fail("analyze-only report should include selected asset metadata");
  }
  if (analysis.value(QStringLiteral("recipeSummary")).toMap().value(QStringLiteral("snapshotCount")).toInt() != 0) {
    return fail("analyze-only report should inspect the current recipe before edits");
  }
  if (analysis.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !analysis.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("analyze-only report should name edit goals and current-image risks");
  }
  if (!QFileInfo(analysis.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
      !QFileInfo(analysis.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("analyze-only run should persist current proof, report, and manifest artifacts");
  }
  if (!analysis.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("analyze-only report should validate the rendered current image");
  }
  if (analysis.value(QStringLiteral("canonicalPlanCacheKey")).toString().isEmpty()) {
    return fail("modern analyze-only proof rendering should publish its canonical plan identity");
  }
  const auto analysis_proof_source =
      analysis.value(QStringLiteral("proofRenderSource")).toMap();
  if (analysis_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      analysis_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !analysis_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      analysis_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("analyze-only report should use a bounded cached preview source for proof rendering");
  }
  if (analysis.value(QStringLiteral("durationMs")).toInt() < 0 ||
      analysis.value(QStringLiteral("startedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("finishedAtUtc")).toString().isEmpty() ||
      analysis.value(QStringLiteral("toolCalls")).toList().isEmpty() ||
      analysis.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !analysis.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analyze-only report should include timing, performance budget, proof gate, and tool call metadata");
  }
  auto run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("analyze") ||
      run_history.first().toMap().value(QStringLiteral("manifestPath")).toString().isEmpty() ||
      !run_history.first().toMap().contains(QStringLiteral("durationMs")) ||
      run_history.first().toMap().value(QStringLiteral("durationMs")).toInt() < 0 ||
      run_history.first()
              .toMap()
              .value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget")) {
    return fail("agent controller should expose analyze runs in user-facing run history");
  }
  const auto persisted_analysis = read_json_object(analysis.value(QStringLiteral("analysisReportPath")).toString());
  if (persisted_analysis.value(QStringLiteral("mode")).toString() != QStringLiteral("analyze")) {
    return fail("analysis artifact should persist the read-only report");
  }
  const auto analysis_manifest = read_json_object(analysis.value(QStringLiteral("manifestPath")).toString());
  if (analysis_manifest.value(QStringLiteral("style"))
              .toObject()
              .value(QStringLiteral("schemaVersion"))
              .toInt() != 1 ||
      analysis_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("analysis manifest should persist style schema and model backend metadata");
  }
  if (analysis_manifest.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      analysis_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      analysis_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !analysis_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("analysis manifest should persist timing, performance proof gate, and tool call metadata");
  }

  const auto export_dir = QDir(runtime_root.path()).filePath(QStringLiteral("exports"));
  if (!QDir().mkpath(export_dir)) {
    return fail("export destination should be created for agent preflight testing");
  }
  const auto recipe_before_preflight =
      catalog_model
          .selectedAdjustmentPatchJson();
  const auto preflight = controller.exportPreflightForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("shadowClipCount"), 0},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!preflight.value(QStringLiteral("ok")).toBool() ||
      preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      preflight.value(QStringLiteral("checkedCount")).toInt() != 1 ||
      preflight.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      preflight.value(QStringLiteral("warningCount")).toInt() < 2 ||
      preflight.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !QFileInfo(preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("agent export preflight should report and persist selected asset export readiness without creating a draft");
  }
  const auto persisted_preflight =
      read_json_object(preflight.value(QStringLiteral("preflightReportPath")).toString());
  if (persisted_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      persisted_preflight.value(QStringLiteral("assetReports")).toArray().size() != 1 ||
      persisted_preflight.value(QStringLiteral("toolCalls")).toArray().isEmpty() ||
      persisted_preflight.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      !persisted_preflight.value(QStringLiteral("performanceBudget"))
           .toObject()
           .value(QStringLiteral("withinBudget"))
           .toBool()) {
    return fail("agent export preflight artifact should persist the report and performance budget");
  }
  const auto preflight_asset_report = preflight.value(QStringLiteral("assetReports")).toList().first().toMap();
  const auto preflight_parity =
      preflight_asset_report.value(QStringLiteral("previewExportParity")).toMap();
  if (preflight_asset_report.value(QStringLiteral("assetId")).toString() != QStringLiteral("asset-1") ||
      preflight_asset_report.value(QStringLiteral("preset")).toString() != QStringLiteral("Web Proof") ||
      preflight_asset_report.value(QStringLiteral("destinationDirectory")).toString() != export_dir ||
      preflight_asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("ready") ||
      !preflight_parity.value(QStringLiteral("required")).toBool() ||
      preflight_parity.value(QStringLiteral("status")).toString() != QStringLiteral("required-at-export") ||
      !preflight_parity.value(QStringLiteral("highRiskGroups"))
           .toStringList()
           .contains(QStringLiteral("output-profile-proofing"))) {
    return fail("agent export preflight should include per-asset preset, destination, status, and parity proof requirements");
  }
  if (catalog_model
          .selectedAdjustmentPatchJson() !=
      recipe_before_preflight) {
    return fail("agent export preflight should not mutate the selected recipe");
  }

  const auto unconfirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      false,
      QVariantMap{{QStringLiteral("histogramSampleCount"), 100}});
  if (unconfirmed_export.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_export.value(QStringLiteral("queuedCount")).toInt() != 0 ||
      export_jobs.jobCount() != 0) {
    return fail("agent export should require explicit confirmation before queueing jobs");
  }
  auto confirmed_export = controller.queueExportForSelected(
      QStringLiteral("Web Proof"),
      export_dir,
      true,
      QVariantMap{
          {QStringLiteral("histogramSampleCount"), 100},
          {QStringLiteral("highlightClipCount"), 2},
      });
  if (!confirmed_export.value(QStringLiteral("admissionPending")).toBool() ||
      confirmed_export.value(QStringLiteral("durablyQueued")).toBool() ||
      confirmed_export.value(QStringLiteral("queuedCount")).toInt() != 0 ||
      confirmed_export.value(QStringLiteral("submittedCount")).toInt() != 1) {
    return fail("agent export submission must not claim durable queueing before the catalog receipt");
  }
  const auto export_submission_run_id = confirmed_export.value(QStringLiteral("runId"));
  QElapsedTimer export_admission_timer;
  export_admission_timer.start();
  while (controller.running() && export_admission_timer.elapsed() < 15000) {
    QCoreApplication::processEvents(QEventLoop::AllEvents, 25);
    QThread::msleep(1);
  }
  confirmed_export = controller.lastRunReport();
  if (controller.running() || confirmed_export.value(QStringLiteral("runId")) != export_submission_run_id ||
      confirmed_export.value(QStringLiteral("admissionPending")).toBool() ||
      !confirmed_export.value(QStringLiteral("durablyQueued")).toBool()) {
    return fail("agent export admission must finish with its own durable catalog receipt");
  }
  if (!confirmed_export.value(QStringLiteral("ok")).toBool() ||
      confirmed_export.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      confirmed_export.value(QStringLiteral("queuedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("expectedCount")).toInt() != 1 ||
      confirmed_export.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      export_jobs.jobCount() != 1 ||
      !QFileInfo(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString()).isFile() ||
      !QFileInfo(confirmed_export.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("confirmed agent export should queue and persist the selected asset through the export controller");
  }
  const auto persisted_export_batch =
      read_json_object(confirmed_export.value(QStringLiteral("exportBatchReportPath")).toString());
  if (persisted_export_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("export-batch") ||
      persisted_export_batch.value(QStringLiteral("toolCalls")).toArray().size() < 2 ||
      persisted_export_batch.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("confirmed agent export artifact should persist timing and tool call outcomes");
  }

  const auto revised_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 5.0},
               {QStringLiteral("highlights"), 100.0},
               {QStringLiteral("whites"), 100.0},
           }},
      });
  const auto revised_asset_reports = revised_draft.value(QStringLiteral("assetReports")).toList();
  const auto revised_asset_report =
      revised_asset_reports.isEmpty() ? QVariantMap{} : revised_asset_reports.first().toMap();
  const auto revised_edit_loop = revised_asset_report.value(QStringLiteral("editLoop")).toMap();
  if (!revised_draft.value(QStringLiteral("ok")).toBool() ||
      revised_draft.value(QStringLiteral("editLoopVersion")).toString() != QStringLiteral("agent-edit-loop-v1") ||
      revised_draft.value(QStringLiteral("editLoops")).toList().isEmpty() ||
      revised_edit_loop.value(QStringLiteral("version")).toString() != QStringLiteral("agent-edit-loop-v1") ||
      revised_edit_loop.value(QStringLiteral("attemptCount")).toInt() < 2 ||
      revised_edit_loop.value(QStringLiteral("maxAttempts")).toInt() != 3 ||
      revised_edit_loop.value(QStringLiteral("finalScore")).toDouble() <= 0.0 ||
      revised_edit_loop.value(QStringLiteral("attempts")).toList().first().toMap().value(QStringLiteral("patchHash")).toString().isEmpty() ||
      revised_asset_report.value(QStringLiteral("revisionCount")).toInt() < 1 ||
      revised_asset_report.value(QStringLiteral("qualityScore")).toDouble() <= 0.0 ||
      revised_asset_report.value(QStringLiteral("validationIterations")).toList().size() < 2 ||
      revised_draft.value(QStringLiteral("requestedRecipePatch")).toMap() ==
          revised_draft.value(QStringLiteral("recipePatch")).toMap() ||
      !controller.draftActive()) {
    return fail("agent draft should run a scored edit-verify loop and revise unsafe patches before applying");
  }
  const auto live_run_events = controller.runEvents();
  auto saw_render_event = false;
  auto saw_validation_event = false;
  auto saw_review_ready_event = false;
  auto saw_render_tool = false;
  auto saw_validation_tool = false;
  for (const auto& event_value : live_run_events) {
    const auto event = event_value.toMap();
    const auto stage = event.value(QStringLiteral("stage")).toString();
    const auto status = event.value(QStringLiteral("status")).toString();
    saw_render_event = saw_render_event || stage == QStringLiteral("render");
    saw_validation_event = saw_validation_event || stage == QStringLiteral("validate");
    saw_render_tool = saw_render_tool ||
                      (stage == QStringLiteral("render") &&
                       event.value(QStringLiteral("characterTool")).toString() ==
                           QStringLiteral("tone-key"));
    saw_validation_tool = saw_validation_tool ||
                          (stage == QStringLiteral("validate") &&
                           event.value(QStringLiteral("characterTool")).toString() ==
                               QStringLiteral("loupe"));
    saw_review_ready_event = saw_review_ready_event ||
                             (stage == QStringLiteral("review") && status == QStringLiteral("ready"));
  }
  if (!saw_render_event || !saw_validation_event || !saw_review_ready_event ||
      !saw_render_tool || !saw_validation_tool ||
      controller.lastRunReport().value(QStringLiteral("runEvents")).toList().isEmpty()) {
    return fail("agent drafts should publish live render, validation, review-ready, and character-tool events into the final report");
  }
  const auto revised_rollback = controller.rollbackDraft();
  if (!revised_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("revised validation-feedback draft should remain rollbackable");
  }

  const auto instructions_before_opposite_intent_test =
      controller.userInstructions();
  const auto recipe_before_opposite_intent_test =
      catalog_model.selectedAdjustmentPatchJson();
  controller.setUserInstructions(QStringLiteral("Make it brighter."));
  const auto opposite_intent_draft =
      controller.applyDraftRecipePatchToSelected(
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{{QStringLiteral("exposure"), -0.45}}},
          });
  controller.setUserInstructions(instructions_before_opposite_intent_test);
  const auto opposite_asset_reports =
      opposite_intent_draft.value(QStringLiteral("assetReports")).toList();
  const auto opposite_validation = opposite_asset_reports.isEmpty()
                                       ? QVariantMap{}
                                       : opposite_asset_reports.first()
                                             .toMap()
                                             .value(QStringLiteral("validationReport"))
                                             .toMap();
  const auto opposite_alignment =
      opposite_validation.value(QStringLiteral("pixelIntentAlignment")).toMap();
  const auto persisted_opposite_validation = read_json_object(
      opposite_validation.value(QStringLiteral("artifacts"))
          .toMap()
          .value(QStringLiteral("validationReportPath"))
          .toString());
  if (opposite_intent_draft.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      catalog_model.selectedAdjustmentPatchJson() !=
          recipe_before_opposite_intent_test ||
      !opposite_alignment.value(QStringLiteral("available")).toBool() ||
      opposite_alignment.value(QStringLiteral("ok")).toBool() ||
      !opposite_alignment.value(QStringLiteral("issues"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("requested a brighter image")) ||
      persisted_opposite_validation
              .value(QStringLiteral("pixelIntentAlignment"))
              .toObject()
              .value(QStringLiteral("version"))
              .toString() !=
          QStringLiteral("agent-pixel-intent-alignment-v1")) {
    return fail("controller should reject and restore a rendered draft that moves opposite explicit pixel intent");
  }

  const QJsonObject unsafe_plan_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Unsafe first pass for repair coverage")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("allowedTools.color")},
               {QStringLiteral("reason"), QStringLiteral("Color edits are allowed.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Create a very bright high-contrast first pass.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("strong")},
           {QStringLiteral("brightness"), QStringLiteral("brighter")},
           {QStringLiteral("palette"), QStringLiteral("warm")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("crisp")},
           {QStringLiteral("mood"), QStringLiteral("editorial")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.8},
       }},
  };
  const QJsonObject repaired_plan_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Reduced highlight risk after validation")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("validation.highlight_clip_limit_percent")},
               {QStringLiteral("reason"), QStringLiteral("Repair lowers highlight pressure.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Reduce highlight pressure while preserving a polished edit.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("neutral")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("clean")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.94},
       }},
  };
  const QJsonObject proof_critique_content{
      {QStringLiteral("pass"), false},
      {QStringLiteral("summary"), QStringLiteral("Rendered proof is too bright and needs highlight repair.")},
      {QStringLiteral("issues"),
       QJsonArray{
           QStringLiteral("High-severity highlight clipping in the rendered proof."),
       }},
      {QStringLiteral("repairHints"),
       QJsonArray{
           QStringLiteral("Lower exposure and pull highlights before accepting the draft."),
       }},
      {QStringLiteral("needsUserReview"), true},
      {QStringLiteral("tasteConfidence"), QStringLiteral("medium")},
  };
  const QJsonObject repaired_proof_critique_content{
      {QStringLiteral("pass"), true},
      {QStringLiteral("summary"), QStringLiteral("Repaired proof is balanced enough to accept.")},
      {QStringLiteral("issues"), QJsonArray{}},
      {QStringLiteral("repairHints"), QJsonArray{}},
      {QStringLiteral("needsUserReview"), false},
      {QStringLiteral("tasteConfidence"), QStringLiteral("high")},
  };

  catalog_model.setProjectFilter(QStringLiteral("Workflow Wedding"));
  catalog_model.selectAssetById(QStringLiteral("asset-1"), true);
  const auto two_speed_taste_before =
      controller.approvedProjectTaste()
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto two_speed_choice_count_before =
      two_speed_taste_before.value(QStringLiteral("choiceCount")).toInt();
  const auto two_speed_first_look_count_before =
      two_speed_taste_before.value(QStringLiteral("firstLookCount")).toInt();
  const auto two_speed_approval_count_before =
      controller.approvedProjectTaste()
          .value(QStringLiteral("approvalCount"))
          .toInt();
  const auto two_speed_cancel_original_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(unsafe_plan_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("500"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  QVariantMap two_speed_cancel_finished_report;
  auto two_speed_cancel_finished = false;
  const auto two_speed_cancel_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        two_speed_cancel_finished_report = report;
        two_speed_cancel_finished = true;
      });
  const auto two_speed_cancel_start = controller.startDraftOnSelected(
      QStringLiteral("Make the whole photo slightly warmer and brighter."));
  const auto two_speed_cancel_preview_published = wait_for_condition(
      [&]() {
        const auto preview = controller.progressivePreview();
        return preview.value(QStringLiteral("twoSpeedContract")).toString() ==
                   QStringLiteral("agent-two-speed-planning-preview-v1") &&
               preview.value(QStringLiteral("published")).toBool() &&
               (preview.value(QStringLiteral("state")).toString() ==
                    QStringLiteral("planning-refinement") ||
                preview.value(QStringLiteral("state")).toString() ==
                    QStringLiteral("fast-validated-deep-optional"));
      },
      2000);
  const auto two_speed_cancel_preview = controller.progressivePreview();
  const auto two_speed_cancel_live_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto two_speed_cancel_result =
      controller.cancelRun(QStringLiteral("two_speed_exact_restore_test"));
  const auto two_speed_cancel_terminal_preview =
      controller.progressivePreview();
  const auto two_speed_cancel_restored_immediately =
      catalog_model.selectedAdjustmentPatchJson() ==
      two_speed_cancel_original_recipe;
  const auto two_speed_cancel_completed = wait_for_condition(
      [&]() { return two_speed_cancel_finished; }, 2000);
  QObject::disconnect(two_speed_cancel_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  if (!two_speed_cancel_start.value(QStringLiteral("accepted")).toBool() ||
      !two_speed_cancel_preview_published ||
      two_speed_cancel_preview.value(QStringLiteral("firstPublishMs")).toLongLong() >
          two_speed_cancel_preview.value(QStringLiteral("firstPublishBudgetMs")).toLongLong() ||
      !two_speed_cancel_preview.value(QStringLiteral("firstPublishWithinBudget")).toBool() ||
      !two_speed_cancel_preview.value(QStringLiteral("commitBlockedWhilePending")).toBool() ||
      two_speed_cancel_preview.value(QStringLiteral("durableStateWritten")).toBool() ||
      two_speed_cancel_live_recipe == two_speed_cancel_original_recipe ||
      !two_speed_cancel_result.value(QStringLiteral("ok")).toBool() ||
      !two_speed_cancel_restored_immediately ||
      two_speed_cancel_terminal_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("restored-after-cancel") ||
      !two_speed_cancel_terminal_preview.value(QStringLiteral("restored")).toBool() ||
      two_speed_cancel_terminal_preview.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      two_speed_cancel_terminal_preview.value(QStringLiteral("validationPending")).toBool() ||
      !two_speed_cancel_completed ||
      two_speed_cancel_finished_report.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive()) {
    return fail("two-speed planning preview should publish within budget, remain non-committable, and restore the exact original recipe immediately on cancel");
  }

  const auto rejected_fast_original_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(unsafe_plan_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("500"));
  qputenv("PHOTO_EDITOR_TEST_FORCE_FAST_PROOF_FAILURE",
          QByteArrayLiteral("1"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  auto rejected_fast_finished = false;
  const auto rejected_fast_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap&) {
        rejected_fast_finished = true;
      });
  const auto rejected_fast_start = controller.startDraftOnSelected(
      QStringLiteral("Make the whole photo slightly warmer and brighter."));
  const auto rejected_fast_restored = wait_for_condition(
      [&]() {
        return controller.progressivePreview()
                   .value(QStringLiteral("state"))
                   .toString() ==
                   QStringLiteral("fast-validation-rejected") &&
               catalog_model.selectedAdjustmentPatchJson() ==
                   rejected_fast_original_recipe;
      },
      3000);
  const auto rejected_fast_preview = controller.progressivePreview();
  const auto rejected_fast_cancel = controller.cancelRun(
      QStringLiteral("fast_proof_rejection_test_complete"));
  const auto rejected_fast_completed = wait_for_condition(
      [&]() { return rejected_fast_finished; }, 2000);
  QObject::disconnect(rejected_fast_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_TEST_FORCE_FAST_PROOF_FAILURE");
  if (!rejected_fast_start.value(QStringLiteral("accepted")).toBool() ||
      !rejected_fast_restored ||
      rejected_fast_preview.value(QStringLiteral("published")).toBool() ||
      rejected_fast_preview
          .value(QStringLiteral("fastValidationPassed"))
          .toBool() ||
      !rejected_fast_preview
           .value(QStringLiteral("deepRefinementPending"))
           .toBool() ||
      rejected_fast_preview
          .value(QStringLiteral("keepFastDraftAvailable"))
          .toBool() ||
      !rejected_fast_preview.value(QStringLiteral("restored")).toBool() ||
      rejected_fast_preview.value(QStringLiteral("restoredCount")).toInt() !=
          1 ||
      !rejected_fast_preview
           .value(QStringLiteral("fastValidationReport"))
           .toMap()
           .value(QStringLiteral("testInjectedFailure"))
           .toBool() ||
      !rejected_fast_cancel.value(QStringLiteral("ok")).toBool() ||
      !rejected_fast_completed || controller.draftActive()) {
    return fail("an independently rejected fast proof should withdraw the preview, restore the exact original immediately, and leave only cancellable deep planning");
  }

  const auto kept_fast_original_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(unsafe_plan_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("2000"));
  qputenv("PHOTO_EDITOR_AGENT_HARDWARE_TIER",
          QByteArrayLiteral("performance"));
  qputenv("PHOTO_EDITOR_AGENT_DEEP_DEADLINE_MS", QByteArrayLiteral("5000"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  QVariantMap kept_fast_finished_report;
  auto kept_fast_completion_count = 0;
  const auto kept_fast_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        kept_fast_finished_report = report;
        ++kept_fast_completion_count;
      });
  const auto kept_fast_start = controller.startDraftOnSelected(
      QStringLiteral("Make the whole photo slightly warmer and brighter."));
  const auto kept_fast_ready = wait_for_condition(
      [&]() {
        const auto preview = controller.progressivePreview();
        return preview.value(QStringLiteral("fastValidationPassed")).toBool() &&
               preview.value(QStringLiteral("keepFastDraftAvailable")).toBool();
      },
      5000);
  const auto kept_fast_preview = controller.progressivePreview();
  const auto kept_fast_live_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto kept_fast_report = controller.keepValidatedFastDraft();
  const auto kept_fast_durable_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  QElapsedTimer kept_fast_late_callback_timer;
  kept_fast_late_callback_timer.start();
  const auto late_deep_callback_ignored = wait_for_condition(
      [&]() {
        return kept_fast_late_callback_timer.elapsed() >= 2200 &&
               !controller.running() && kept_fast_completion_count == 1;
      },
      3000);
  QObject::disconnect(kept_fast_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_AGENT_HARDWARE_TIER");
  qunsetenv("PHOTO_EDITOR_AGENT_DEEP_DEADLINE_MS");
  const auto kept_fast_validation =
      kept_fast_preview.value(QStringLiteral("fastValidationReport")).toMap();
  const auto kept_fast_deadline =
      kept_fast_preview.value(QStringLiteral("hardwareDeadline")).toMap();
  if (!kept_fast_start.value(QStringLiteral("accepted")).toBool() ||
      !kept_fast_ready ||
      kept_fast_validation.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-fast-draft-independent-proof-v1") ||
      !kept_fast_validation.value(QStringLiteral("ok")).toBool() ||
      kept_fast_deadline.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-fast-draft-hardware-deadline-v1") ||
      kept_fast_deadline.value(QStringLiteral("hardwareTier")).toString() !=
          QStringLiteral("performance") ||
      kept_fast_deadline.value(QStringLiteral("deepModelDeadlineMs")).toInt() !=
          5000 ||
      !kept_fast_deadline
           .value(QStringLiteral("keepRequiresIndependentProof"))
           .toBool() ||
      kept_fast_deadline
          .value(QStringLiteral("keepRequiresDeadline"))
          .toBool() ||
      !kept_fast_deadline
           .value(QStringLiteral("choiceAvailableAfterProof"))
           .toBool() ||
      kept_fast_deadline
          .value(QStringLiteral("automaticFastDraftSelection"))
          .toBool() ||
      kept_fast_preview.value(QStringLiteral("deepDeadlineReached")).toBool() ||
      kept_fast_live_recipe == kept_fast_original_recipe ||
      !kept_fast_report.value(QStringLiteral("ok")).toBool() ||
      kept_fast_report.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-validated-fast-draft-v1") ||
      kept_fast_report.value(QStringLiteral("fastChoiceContract")).toString() !=
          QStringLiteral("agent-proved-fast-choice-v1") ||
      kept_fast_report
          .value(QStringLiteral("progressivePreview"))
          .toMap()
          .value(QStringLiteral("deepDeadlineReachedAtChoice"))
          .toBool() ||
      !kept_fast_report.value(QStringLiteral("reviewRequired")).toBool() ||
      kept_fast_report.value(QStringLiteral("qualityGuarantee")).toBool() ||
      !kept_fast_report.value(QStringLiteral("deepPlanningCanceled")).toBool() ||
      kept_fast_report.value(QStringLiteral("committed")).toBool() ||
      kept_fast_durable_recipe != kept_fast_live_recipe ||
      kept_fast_completion_count != 1 || !late_deep_callback_ignored ||
      kept_fast_finished_report.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-validated-fast-draft-v1") ||
      controller.running() || !controller.draftActive()) {
    return fail("an independently proved fast result should become an explicit reversible draft before the hardware-tier deadline, cancel deep planning, and ignore its late callback");
  }
  const auto kept_fast_rollback = controller.rollbackDraft();
  if (!kept_fast_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      catalog_model.selectedAdjustmentPatchJson() !=
          kept_fast_original_recipe) {
    return fail("discarding a kept fast draft should restore the exact pre-preview recipe");
  }
  std::cout << "agent_validated_fast_proof_ms="
            << kept_fast_validation.value(QStringLiteral("durationMs"))
                   .toLongLong()
            << " hardware_tier="
            << kept_fast_deadline.value(QStringLiteral("hardwareTier"))
                   .toString()
                   .toStdString()
            << " deep_deadline_ms="
            << kept_fast_deadline
                   .value(QStringLiteral("deepModelDeadlineMs"))
                   .toInt()
            << " exact_rollback=1 late_deep_callback_ignored=1\n";

  const QJsonArray repair_sequence{
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(unsafe_plan_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(proof_critique_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(repaired_plan_content).toJson(QJsonDocument::Compact))},
           }},
      },
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"), QString::fromUtf8(QJsonDocument(repaired_proof_critique_content).toJson(QJsonDocument::Compact))},
           }},
      },
  };
  const auto repair_request_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("ollama-repair-requests.jsonl"));
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
      QJsonDocument(repair_sequence).toJson(QJsonDocument::Compact));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH", repair_request_path.toUtf8());
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("40"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const auto model_repair_original_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  QVariantMap model_repaired_draft;
  auto model_repair_completion_count = 0;
  const auto model_repair_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        model_repaired_draft = report;
        ++model_repair_completion_count;
      });
  QElapsedTimer model_repair_start_timer;
  model_repair_start_timer.start();
  const auto model_repair_start = controller.startDraftOnSelected(
      QStringLiteral("make a bright edit but obey validation feedback"));
  const auto model_repair_start_duration_ms = model_repair_start_timer.elapsed();
  const auto model_repair_fast_preview_published = wait_for_condition(
      [&]() {
        const auto preview = controller.progressivePreview();
        return preview.value(QStringLiteral("twoSpeedContract")).toString() ==
                   QStringLiteral("agent-two-speed-planning-preview-v1") &&
               preview.value(QStringLiteral("published")).toBool() &&
               (preview.value(QStringLiteral("state")).toString() ==
                    QStringLiteral("planning-refinement") ||
                preview.value(QStringLiteral("state")).toString() ==
                    QStringLiteral("fast-validated-deep-optional"));
      },
      2000);
  const auto model_repair_fast_preview = controller.progressivePreview();
  const auto model_repair_completed = wait_for_condition(
      [&]() { return model_repair_completion_count == 1; }, 10000);
  QObject::disconnect(model_repair_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_REQUEST_PATH");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  const auto model_repaired_asset_reports =
      model_repaired_draft.value(QStringLiteral("assetReports")).toList();
  const auto model_repaired_asset_report =
      model_repaired_asset_reports.isEmpty() ? QVariantMap{} : model_repaired_asset_reports.first().toMap();
  const auto model_repaired_repair_budget =
      model_repaired_asset_report.value(QStringLiteral("repairBudget")).toMap();
  auto saw_model_repair_tool_call = false;
  auto saw_model_proof_critique_tool_call = false;
  for (const auto& tool_call_value : model_repaired_draft.value(QStringLiteral("toolCalls")).toList()) {
    const auto name = tool_call_value.toMap().value(QStringLiteral("name")).toString();
    if (name == QStringLiteral("model_evaluate_rendered_proof")) {
      saw_model_proof_critique_tool_call = true;
    }
    if (name == QStringLiteral("model_repair_recipe_patch_from_validation") ||
        name == QStringLiteral("apply_model_repaired_recipe_patch")) {
      saw_model_repair_tool_call = true;
    }
  }
  const auto model_repaired_loop_attempts =
      model_repaired_asset_report.value(QStringLiteral("editLoop")).toMap().value(QStringLiteral("attempts")).toList();
  const auto model_repaired_first_attempt =
      model_repaired_loop_attempts.isEmpty() ? QVariantMap{} : model_repaired_loop_attempts.first().toMap();
  const auto model_repaired_second_attempt =
      model_repaired_loop_attempts.size() < 2 ? QVariantMap{} : model_repaired_loop_attempts.at(1).toMap();
  const auto model_repaired_critique =
      model_repaired_first_attempt.value(QStringLiteral("modelProofCritique")).toMap();
  const auto model_repaired_accepting_critique =
      model_repaired_second_attempt.value(QStringLiteral("modelProofCritique")).toMap();
  const auto model_repair_continuation =
      model_repaired_draft.value(QStringLiteral("planningContinuation")).toMap();
  const auto model_repair_final_preview =
      model_repaired_draft.value(QStringLiteral("progressivePreview")).toMap();
  const auto model_repair_completed_stages =
      model_repair_continuation.value(QStringLiteral("completedStages"))
          .toStringList();
  const auto first_model_repair_report =
      model_repaired_asset_report
          .value(QStringLiteral("modelRepairReports"))
          .toList()
          .value(0)
          .toMap();
  const auto repair_request_bytes = read_file_bytes(repair_request_path);
  const auto two_speed_draft_choice =
      model_repaired_draft
          .value(QStringLiteral("twoSpeedDraftChoice"))
          .toMap();
  QVariantMap first_look_version;
  QVariantMap deep_review_version;
  for (const auto& version_value :
       two_speed_draft_choice.value(QStringLiteral("versions")).toList()) {
    const auto version = version_value.toMap();
    if (version.value(QStringLiteral("key")).toString() ==
        QStringLiteral("first-look")) {
      first_look_version = version;
    } else if (version.value(QStringLiteral("key")).toString() ==
               QStringLiteral("deep-review")) {
      deep_review_version = version;
    }
  }
  if (!model_repair_start.value(QStringLiteral("accepted")).toBool() ||
      model_repair_start_duration_ms > 50 || !model_repair_completed ||
      !model_repair_fast_preview_published ||
      model_repair_fast_preview.value(QStringLiteral("firstPublishMs")).toLongLong() >
          model_repair_fast_preview.value(QStringLiteral("firstPublishBudgetMs")).toLongLong() ||
      !model_repair_fast_preview.value(QStringLiteral("commitBlockedWhilePending")).toBool() ||
      model_repair_fast_preview.value(QStringLiteral("durableStateWritten")).toBool() ||
      model_repair_completion_count != 1 ||
      !model_repaired_draft.value(QStringLiteral("ok")).toBool() ||
      model_repair_final_preview.value(QStringLiteral("twoSpeedContract")).toString() !=
          QStringLiteral("agent-two-speed-planning-preview-v1") ||
      model_repair_final_preview.value(QStringLiteral("publishCount")).toInt() < 2 ||
      model_repair_final_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("validated") ||
      model_repair_final_preview.value(QStringLiteral("validationPending")).toBool() ||
      model_repair_final_preview.value(QStringLiteral("deepRefinementPending")).toBool() ||
      !model_repair_final_preview.value(QStringLiteral("fastPreviewReplaced")).toBool() ||
      !model_repair_final_preview.value(QStringLiteral("deepValidationPassed")).toBool() ||
      model_repair_final_preview.value(QStringLiteral("finalModelId")).toString() !=
          QStringLiteral("ollama-qwen-test") ||
      model_repair_continuation.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      model_repair_continuation
          .value(QStringLiteral("explicitEventPumpLoop"))
          .toBool() ||
      model_repair_completed_stages.size() != 2 ||
      !model_repair_completed_stages.contains(
          QStringLiteral("primary-plan")) ||
      !model_repair_completed_stages.contains(
          QStringLiteral("validation-repair:asset-1:0")) ||
      !first_model_repair_report
           .value(QStringLiteral("requestOwnedContinuation"))
           .toBool() ||
      model_repaired_asset_report.value(QStringLiteral("modelRepairCount")).toInt() < 1 ||
      model_repaired_asset_report.value(QStringLiteral("revisionCount")).toInt() < 1 ||
      !model_repaired_repair_budget.value(QStringLiteral("hardLimit")).toBool() ||
      model_repaired_repair_budget.value(QStringLiteral("maxValidationRevisionIterations")).toInt() != 2 ||
      model_repaired_repair_budget.value(QStringLiteral("maxModelRepairTurns")).toInt() != 2 ||
      model_repaired_asset_report.value(QStringLiteral("editLoop")).toMap().value(QStringLiteral("attemptCount")).toInt() < 2 ||
      model_repaired_asset_report.value(QStringLiteral("modelRepairReports")).toList().isEmpty() ||
      model_repaired_draft.value(QStringLiteral("requestedRecipePatch")).toMap() ==
          model_repaired_draft.value(QStringLiteral("recipePatch")).toMap() ||
      !saw_model_proof_critique_tool_call ||
      !saw_model_repair_tool_call ||
      !model_repaired_critique.value(QStringLiteral("ok")).toBool() ||
      model_repaired_critique.value(QStringLiteral("pass")).toBool() ||
      !model_repaired_critique
           .value(QStringLiteral("requestOwnedEvidence"))
           .toBool() ||
      !model_repaired_critique
           .value(QStringLiteral("cachedReplayPrevented"))
           .toBool() ||
      model_repaired_critique.value(QStringLiteral("repairHints")).toList().isEmpty() ||
      !model_repaired_accepting_critique.value(QStringLiteral("ok")).toBool() ||
      !model_repaired_accepting_critique.value(QStringLiteral("pass")).toBool() ||
      two_speed_draft_choice.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-two-speed-draft-choice-v1") ||
      !two_speed_draft_choice.value(QStringLiteral("available")).toBool() ||
      two_speed_draft_choice.value(QStringLiteral("currentVersion")).toString() !=
          QStringLiteral("deep-review") ||
      !two_speed_draft_choice
           .value(QStringLiteral("originalRollbackRetained"))
           .toBool() ||
      !two_speed_draft_choice
           .value(QStringLiteral("commitStillRequiresConfirmation"))
           .toBool() ||
      two_speed_draft_choice
          .value(QStringLiteral("automaticSelectionAllowed"))
          .toBool() ||
      !first_look_version
           .value(QStringLiteral("independentlyProved"))
           .toBool() ||
      !deep_review_version
           .value(QStringLiteral("independentlyProved"))
           .toBool() ||
      first_look_version
          .value(QStringLiteral("adjustmentPatchJson"))
          .toString()
          .isEmpty() ||
      deep_review_version
          .value(QStringLiteral("adjustmentPatchJson"))
          .toString()
          .isEmpty() ||
      model_repaired_asset_report
          .value(QStringLiteral("finalAdjustmentPatchJson"))
          .toString() !=
          deep_review_version
              .value(QStringLiteral("adjustmentPatchJson"))
              .toString() ||
      !repair_request_bytes.contains("validation-repair") ||
      repair_request_bytes.contains("Current compiled patch JSON:") ||
      !repair_request_bytes.contains("Phase: revision.") ||
      !repair_request_bytes.contains("current-proof-validation") ||
      !repair_request_bytes.contains("imageStats") ||
      !repair_request_bytes.contains("vectorscope") ||
      !repair_request_bytes.contains("You are a local photo-editing proof evaluator") ||
      !repair_request_bytes.contains("Previous attempts JSON") ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      !controller.draftActive()) {
    std::cerr
        << "async model repair diagnostics stages="
        << QJsonDocument::fromVariant(model_repair_completed_stages)
               .toJson(QJsonDocument::Compact)
               .toStdString()
        << " repair_count="
        << model_repaired_asset_report
               .value(QStringLiteral("modelRepairCount"))
               .toInt()
        << " revision_count="
        << model_repaired_asset_report.value(QStringLiteral("revisionCount"))
               .toInt()
        << " first_critique_ok="
        << model_repaired_critique.value(QStringLiteral("ok")).toBool()
        << " first_critique_pass="
        << model_repaired_critique.value(QStringLiteral("pass")).toBool()
        << " critique_cached="
        << model_repaired_critique
               .value(QStringLiteral("cachedReplayPrevented"))
               .toBool()
        << " first_repair_ok="
        << first_model_repair_report.value(QStringLiteral("ok")).toBool()
        << " first_repair_errors="
        << first_model_repair_report.value(QStringLiteral("errors"))
               .toStringList().join(QStringLiteral("; ")).left(1000).toStdString()
        << " first_repair_request_owned="
        << first_model_repair_report
               .value(QStringLiteral("requestOwnedContinuation"))
               .toBool()
        << " request_lines=" << repair_request_bytes.count('\n')
        << " start_ms=" << model_repair_start_duration_ms
        << " completed=" << model_repair_completion_count
        << " start_accepted="
        << model_repair_start.value(QStringLiteral("accepted")).toBool()
        << " start_error="
        << model_repair_start.value(QStringLiteral("errorCode"))
               .toString()
               .toStdString()
        << " controller_status=" << controller.status().toStdString()
        << " controller_error=" << controller.errorMessage().toStdString()
        << " active_request="
        << controller.activeDraftRequestId().toStdString()
        << " progressive_state="
        << controller.progressivePreview()
               .value(QStringLiteral("state"))
               .toString()
               .toStdString()
        << '\n';
    return fail("model-planned drafts should use a request-owned scored repair turn, reuse prior critique evidence exactly once, and preserve validation history before procedural fallback");
  }
  const auto deep_review_live_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto first_look_choice =
      controller.chooseTwoSpeedDraftVersion(QStringLiteral("first-look"));
  const auto first_look_live_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto invalid_version_choice =
      controller.chooseTwoSpeedDraftVersion(QStringLiteral("automatic"));
  const auto invalid_choice_preserved_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto deep_review_choice =
      controller.chooseTwoSpeedDraftVersion(QStringLiteral("deep-review"));
  const auto restored_deep_review_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto active_two_speed_choice =
      controller.lastRunReport()
          .value(QStringLiteral("twoSpeedDraftChoice"))
          .toMap();
  if (!first_look_choice.value(QStringLiteral("ok")).toBool() ||
      first_look_choice.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-two-speed-draft-choice-v1") ||
      first_look_choice.value(QStringLiteral("currentVersion")).toString() !=
          QStringLiteral("first-look") ||
      !first_look_choice.value(QStringLiteral("switched")).toBool() ||
      first_look_choice.value(QStringLiteral("committed")).toBool() ||
      !first_look_choice
           .value(QStringLiteral("originalRollbackRetained"))
           .toBool() ||
      first_look_live_recipe !=
          first_look_version
              .value(QStringLiteral("adjustmentPatchJson"))
              .toString() ||
      invalid_version_choice.value(QStringLiteral("ok")).toBool() ||
      invalid_version_choice.value(QStringLiteral("errorCode")).toString() !=
          QStringLiteral("invalid_draft_version") ||
      invalid_choice_preserved_recipe != first_look_live_recipe ||
      !deep_review_choice.value(QStringLiteral("ok")).toBool() ||
      deep_review_choice.value(QStringLiteral("currentVersion")).toString() !=
          QStringLiteral("deep-review") ||
      deep_review_choice.value(QStringLiteral("switchCount")).toInt() != 2 ||
      restored_deep_review_recipe != deep_review_live_recipe ||
      active_two_speed_choice
          .value(QStringLiteral("currentVersion"))
          .toString() != QStringLiteral("deep-review") ||
      active_two_speed_choice.value(QStringLiteral("switchCount")).toInt() !=
          2 ||
      !controller.draftActive()) {
    return fail("a completed deep draft should let the photographer switch exactly between both proved versions while retaining one original-image rollback target and a separate commit");
  }
  std::cout << "agent_two_speed_first_preview_ms="
            << model_repair_fast_preview
                   .value(QStringLiteral("firstPublishMs"))
                   .toLongLong()
            << " budget_ms="
            << model_repair_fast_preview
                   .value(QStringLiteral("firstPublishBudgetMs"))
                   .toLongLong()
            << " publish_count="
            << model_repair_final_preview
                   .value(QStringLiteral("publishCount"))
                   .toInt()
            << " deep_validation_passed="
            << model_repair_final_preview
                   .value(QStringLiteral("deepValidationPassed"))
                   .toBool()
            << " proved_version_switches="
            << active_two_speed_choice
                   .value(QStringLiteral("switchCount"))
                   .toInt()
            << '\n';
  const auto precommit_two_speed_taste =
      controller.approvedProjectTaste()
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto precommit_assignment_reflection =
      controller.assignmentTasteReflection();
  if (precommit_two_speed_taste
          .value(QStringLiteral("choiceCount"))
          .toInt() != two_speed_choice_count_before ||
      precommit_assignment_reflection
          .value(QStringLiteral("available"))
          .toBool()) {
    return fail("switching between proved draft versions must not teach project taste before a confirmed commit");
  }
  const auto first_look_commit_choice =
      controller.chooseTwoSpeedDraftVersion(QStringLiteral("first-look"));
  const auto first_look_commit_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto first_look_commit = controller.commitDraft(true);
  const auto first_commit_taste = controller.approvedProjectTaste();
  const auto first_commit_two_speed_taste =
      first_commit_taste
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto first_commit_memory =
      first_look_commit.value(QStringLiteral("projectTasteMemory")).toMap();
  const auto first_commit_previous_patches =
      first_look_commit
          .value(QStringLiteral("previousAdjustmentPatchByAssetId"))
          .toMap();
  const auto first_commit_current_patches =
      first_look_commit
          .value(QStringLiteral("committedAdjustmentPatchByAssetId"))
          .toMap();
  const auto first_commit_assignment_reflection =
      controller.assignmentTasteReflection();
  if (!first_look_commit_choice.value(QStringLiteral("ok")).toBool() ||
      first_look_commit_recipe !=
          first_look_version
              .value(QStringLiteral("adjustmentPatchJson"))
              .toString() ||
      !first_look_commit.value(QStringLiteral("ok")).toBool() ||
      !first_look_commit.value(QStringLiteral("confirmed")).toBool() ||
      first_look_commit.value(QStringLiteral("draftActive")).toBool() ||
      first_commit_previous_patches.value(QStringLiteral("asset-1")).toString() !=
          model_repair_original_recipe ||
      first_commit_current_patches.value(QStringLiteral("asset-1")).toString() !=
          first_look_commit_recipe ||
      !first_commit_memory
           .value(QStringLiteral("twoSpeedChoiceRemembered"))
           .toBool() ||
      first_commit_two_speed_taste
          .value(QStringLiteral("contract"))
          .toString() != QStringLiteral("agent-two-speed-taste-signal-v1") ||
      first_commit_two_speed_taste
          .value(QStringLiteral("choiceCount"))
          .toInt() != two_speed_choice_count_before + 1 ||
      first_commit_two_speed_taste
          .value(QStringLiteral("firstLookCount"))
          .toInt() != two_speed_first_look_count_before + 1 ||
      first_commit_two_speed_taste
          .value(QStringLiteral("influencesPlanning"))
          .toBool() ||
      first_commit_two_speed_taste
          .value(QStringLiteral("automaticVersionSelectionAllowed"))
          .toBool() ||
      first_commit_two_speed_taste
          .value(QStringLiteral("automaticCommitAllowed"))
          .toBool() ||
      first_commit_assignment_reflection
          .value(QStringLiteral("available"))
          .toBool()) {
    return fail("a confirmed First Look commit should remember only compact project-local choice evidence, retain the exact pre-agent recipe receipt, and remain below the two-choice planning threshold");
  }

  auto repeated_choice_sequence = repair_sequence;
  repeated_choice_sequence.append(
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"),
                QString::fromUtf8(
                    QJsonDocument(repaired_plan_content)
                        .toJson(QJsonDocument::Compact))},
           }},
      });
  repeated_choice_sequence.append(
      QJsonObject{
          {QStringLiteral("model"), QStringLiteral("qwen2.5vl:7b")},
          {QStringLiteral("done"), true},
          {QStringLiteral("message"),
           QJsonObject{
               {QStringLiteral("role"), QStringLiteral("assistant")},
               {QStringLiteral("content"),
                QString::fromUtf8(
                    QJsonDocument(repaired_proof_critique_content)
                        .toJson(QJsonDocument::Compact))},
           }},
      });
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON",
      QJsonDocument(repeated_choice_sequence).toJson(QJsonDocument::Compact));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("40"));
  QVariantMap repeated_choice_draft;
  auto repeated_choice_completed = false;
  const auto repeated_choice_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        repeated_choice_draft = report;
        repeated_choice_completed = true;
      });
  const auto repeated_choice_start = controller.startDraftOnSelected(
      QStringLiteral("keep this edit restrained and natural"));
  const auto repeated_choice_finished = wait_for_condition(
      [&]() { return repeated_choice_completed; }, 10000);
  QObject::disconnect(repeated_choice_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_SEQUENCE_JSON");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  const auto repeated_draft_choice =
      repeated_choice_draft
          .value(QStringLiteral("twoSpeedDraftChoice"))
          .toMap();
  const auto repeated_first_choice =
      controller.chooseTwoSpeedDraftVersion(QStringLiteral("first-look"));
  const auto repeated_first_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto repeated_commit = controller.commitDraft(true);
  const auto learned_taste = controller.approvedProjectTaste();
  const auto learned_two_speed_taste =
      learned_taste
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto learned_memory_packet =
      controller.memoryPacketForPhase(QStringLiteral("sample"));
  const auto packet_two_speed_taste =
      learned_memory_packet
          .value(QStringLiteral("approvedProjectTasteCard"))
          .toMap()
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto learned_relevance_policy =
      learned_memory_packet
          .value(QStringLiteral("relevancePolicy"))
          .toMap();
  const auto learned_assignment_reflection =
      controller.assignmentTasteReflection();
  const auto learned_reflection_observations =
      learned_assignment_reflection
          .value(QStringLiteral("observations"))
          .toList();
  if (!repeated_choice_start.value(QStringLiteral("accepted")).toBool() ||
      !repeated_choice_finished ||
      !repeated_choice_draft.value(QStringLiteral("ok")).toBool() ||
      repeated_draft_choice
          .value(QStringLiteral("contract"))
          .toString() != QStringLiteral("agent-two-speed-draft-choice-v1") ||
      !repeated_first_choice.value(QStringLiteral("ok")).toBool() ||
      repeated_first_recipe.isEmpty() ||
      !repeated_commit.value(QStringLiteral("ok")).toBool() ||
      learned_two_speed_taste
          .value(QStringLiteral("choiceCount"))
          .toInt() != two_speed_choice_count_before + 2 ||
      learned_two_speed_taste
          .value(QStringLiteral("firstLookCount"))
          .toInt() != two_speed_first_look_count_before + 2 ||
      !learned_two_speed_taste
           .value(QStringLiteral("enoughEvidence"))
           .toBool() ||
      learned_two_speed_taste
          .value(QStringLiteral("preferredVersion"))
          .toString() != QStringLiteral("first-look") ||
      !learned_two_speed_taste
           .value(QStringLiteral("influencesPlanning"))
           .toBool() ||
      packet_two_speed_taste != learned_two_speed_taste ||
      learned_relevance_policy
          .value(QStringLiteral("twoSpeedTasteContract"))
          .toString() != QStringLiteral("agent-two-speed-taste-signal-v1") ||
      learned_relevance_policy
          .value(QStringLiteral("twoSpeedTasteAuthority"))
          .toString() !=
          QStringLiteral("project-local-qualitative-current-request-wins") ||
      learned_relevance_policy
          .value(QStringLiteral("twoSpeedAutomaticVersionSelectionAllowed"))
          .toBool() ||
      learned_relevance_policy
          .value(QStringLiteral("twoSpeedAutomaticCommitAllowed"))
          .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("contract"))
          .toString() !=
          QStringLiteral("agent-assignment-taste-reflection-v1") ||
      !learned_assignment_reflection
           .value(QStringLiteral("available"))
           .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("sourceChoiceCount"))
          .toInt() != two_speed_choice_count_before + 2 ||
      learned_assignment_reflection
          .value(QStringLiteral("sourcePreferredVersion"))
          .toString() != QStringLiteral("first-look") ||
      learned_assignment_reflection
          .value(QStringLiteral("proposedNorthStar"))
          .toString()
          .isEmpty() ||
      learned_reflection_observations.size() < 2 ||
      learned_assignment_reflection
          .value(QStringLiteral("durableMemoryWritten"))
          .toBool() ||
      !learned_assignment_reflection
           .value(QStringLiteral("requiresExplicitNorthStarSave"))
           .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("automaticMemoryWriteAllowed"))
          .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("automaticRecipeInfluenceAllowed"))
          .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("storesImageBytes"))
          .toBool() ||
      learned_assignment_reflection
          .value(QStringLiteral("storesSourcePaths"))
          .toBool() ||
      learned_memory_packet.contains(
          QStringLiteral("assignmentTasteReflection"))) {
    return fail("two confirmed First Look choices should become transparent project-local qualitative guidance in future planning without automatic version or commit authority");
  }

  const auto forget_two_speed_request =
      controller.forgetTwoSpeedTastePreference(false);
  const auto taste_after_forget_request =
      controller.approvedProjectTaste();
  const auto forget_two_speed_confirmed =
      controller.forgetTwoSpeedTastePreference(true);
  const auto taste_after_two_speed_forget =
      controller.approvedProjectTaste();
  const auto forgotten_two_speed_taste =
      taste_after_two_speed_forget
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap();
  const auto forgotten_assignment_reflection =
      controller.assignmentTasteReflection();
  if (forget_two_speed_request.value(QStringLiteral("ok")).toBool() ||
      !forget_two_speed_request
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      taste_after_forget_request
          .value(QStringLiteral("twoSpeedPreference"))
          .toMap()
          .value(QStringLiteral("choiceCount"))
          .toInt() != two_speed_choice_count_before + 2 ||
      !forget_two_speed_confirmed.value(QStringLiteral("ok")).toBool() ||
      !forget_two_speed_confirmed.value(QStringLiteral("forgotten")).toBool() ||
      !forget_two_speed_confirmed
           .value(QStringLiteral("approvedEditEvidenceRetained"))
           .toBool() ||
      forget_two_speed_confirmed
          .value(QStringLiteral("removedChoiceCount"))
          .toInt() != two_speed_choice_count_before + 2 ||
      forgotten_two_speed_taste
          .value(QStringLiteral("available"))
          .toBool() ||
      forgotten_assignment_reflection
          .value(QStringLiteral("available"))
          .toBool() ||
      taste_after_two_speed_forget
          .value(QStringLiteral("approvalCount"))
          .toInt() != two_speed_approval_count_before + 2) {
    return fail("forgetting proved-version taste should require confirmation, remove only the aggregate choice signal, and retain approved looks and existing edits");
  }
  if (catalog_model.applyAdjustmentPatchJsonToAssetIds(
          {QStringLiteral("asset-1")}, model_repair_original_recipe) != 1) {
    return fail("two-speed taste test cleanup should restore the original test recipe");
  }
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectAssetById(QStringLiteral("asset-1"), true);
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));

  const auto representative_proof = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.20},
               {QStringLiteral("contrast"), 4.0},
           }},
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropX"), 0.10},
               {QStringLiteral("cropY"), 0.10},
               {QStringLiteral("cropWidth"), 0.80},
               {QStringLiteral("cropHeight"), 0.80},
               {QStringLiteral("cropZoom"), 1.02},
           }},
          {QStringLiteral("masks"),
           QVariantMap{
               {QStringLiteral("maskStack"),
                QVariantList{
                    QVariantMap{
                        {QStringLiteral("id"), QStringLiteral("representative-brush")},
                        {QStringLiteral("kind"), QStringLiteral("brush")},
                        {QStringLiteral("name"), QStringLiteral("Representative Brush")},
                        {QStringLiteral("operation"), QStringLiteral("add")},
                        {QStringLiteral("exposure"), 0.35},
                        {QStringLiteral("feather"), 0.08},
                        {QStringLiteral("brushPoints"),
                         QVariantList{
                             QVariantMap{
                                 {QStringLiteral("x"), 0.50},
                                 {QStringLiteral("y"), 0.48},
                                 {QStringLiteral("size"), 0.12},
                                 {QStringLiteral("flow"), 0.75},
                             },
                         }},
                    },
                }},
           }},
      });
  const auto representative_validation =
      representative_proof.value(QStringLiteral("validationReport")).toMap();
  const auto representative_artifacts =
      representative_validation.value(QStringLiteral("artifacts")).toMap();
  const auto representative_diff = representative_proof.value(QStringLiteral("recipeDiff")).toMap();
  const auto representative_mask_local_proof =
      representative_proof.value(QStringLiteral("maskLocalProof")).toMap();
  const auto representative_batch_summary =
      representative_proof.value(QStringLiteral("batchSummary")).toMap();
  if (!representative_proof.value(QStringLiteral("ok")).toBool() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(representative_artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      representative_validation.value(QStringLiteral("proofWidth")).toInt() <= 0 ||
      representative_validation.value(QStringLiteral("proofHeight")).toInt() <= 0 ||
      !representative_diff.contains(QStringLiteral("globalTone")) ||
      !representative_diff.contains(QStringLiteral("crop")) ||
      !representative_diff.contains(QStringLiteral("masks")) ||
      !representative_mask_local_proof.value(QStringLiteral("required")).toBool() ||
      !representative_mask_local_proof.value(QStringLiteral("proofAvailable")).toBool() ||
      representative_mask_local_proof.value(QStringLiteral("uiProofSurface")).toString() !=
          QStringLiteral("agent-dialog-before-after-proof") ||
      representative_mask_local_proof.value(QStringLiteral("maskStackCount")).toInt() != 1 ||
      representative_mask_local_proof.value(QStringLiteral("localAdjustmentFieldCount")).toInt() <= 0 ||
      representative_batch_summary.value(QStringLiteral("maskLocalProofReadyCount")).toInt() != 1 ||
      !controller.draftActive()) {
    return fail("representative color, crop, and mask draft should render proof artifacts and grouped diffs");
  }
  const auto representative_risk =
      representative_proof.value(QStringLiteral("riskAssessment")).toMap();
  const auto representative_approval_types =
      representative_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto representative_manifest =
      read_json_object(representative_proof.value(QStringLiteral("manifestPath")).toString());
  const auto representative_manifest_risk =
      representative_manifest.value(QStringLiteral("riskAssessment")).toObject();
  if (!representative_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !representative_approval_types.contains(QStringLiteral("large_crop")) ||
      representative_risk.value(QStringLiteral("approvalRequests")).toList().isEmpty() ||
      !json_array_contains_string(
          representative_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("large_crop"))) {
    return fail("large crop drafts should carry explicit approval requirements in reports and manifests");
  }
  const QImage representative_after(
      representative_artifacts.value(QStringLiteral("afterProofPath")).toString());
  if (representative_after.isNull()) {
    return fail("representative draft after-proof image should be readable");
  }
  const auto representative_rollback = controller.rollbackDraft();
  if (!representative_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("representative proof draft should remain rollbackable");
  }

  auto retouch_allowed_tools = allowed_tools;
  retouch_allowed_tools.insert(QStringLiteral("retouch"), true);
  const auto retouch_style = controller.createStyle(
      QStringLiteral("Retouch Approval Style"),
      QStringLiteral("Retouch testing style with explicit cleanup approval."),
      retouch_allowed_tools,
      {},
      {},
      {});
  if (!retouch_style.value(QStringLiteral("ok")).toBool()) {
    return fail("retouch approval test should create a style with retouch permission");
  }
  const QVariantMap retouch_patch{
      {QStringLiteral("retouch"),
       QVariantMap{
           {QStringLiteral("retouchStack"),
            QVariantList{
                QVariantMap{
                    {QStringLiteral("id"), QStringLiteral("agent-heal-approval")},
                    {QStringLiteral("kind"), QStringLiteral("brush")},
                    {QStringLiteral("name"), QStringLiteral("Agent Heal Approval")},
                    {QStringLiteral("retouchMode"), QStringLiteral("heal")},
                    {QStringLiteral("removeStrength"), 68.0},
                    {QStringLiteral("healTextureBlend"), 0.32},
                    {QStringLiteral("healColorBlend"), 0.74},
                    {QStringLiteral("brushSize"), 0.06},
                    {QStringLiteral("brushFlow"), 0.78},
                    {QStringLiteral("committed"), false},
                    {QStringLiteral("brushPoints"),
                     QVariantList{
                         QVariantMap{
                             {QStringLiteral("x"), 0.36},
                             {QStringLiteral("y"), 0.44},
                             {QStringLiteral("size"), 0.06},
                             {QStringLiteral("flow"), 0.78},
                         },
                     }},
                },
            }},
       }},
  };
  const auto retouch_draft = controller.applyDraftRecipePatchToSelected(retouch_patch);
  const auto retouch_risk = retouch_draft.value(QStringLiteral("riskAssessment")).toMap();
  const auto retouch_approval_types =
      retouch_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto retouch_manifest =
      read_json_object(retouch_draft.value(QStringLiteral("manifestPath")).toString());
  const auto retouch_manifest_risk =
      retouch_manifest.value(QStringLiteral("riskAssessment")).toObject();
  const auto retouch_strategy =
      retouch_draft.value(QStringLiteral("editStrategy")).toMap();
  const auto retouch_manifest_strategy =
      retouch_manifest.value(QStringLiteral("editStrategy")).toObject();
  if (!retouch_draft.value(QStringLiteral("ok")).toBool() ||
      !retouch_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !retouch_approval_types.contains(QStringLiteral("retouch")) ||
      !json_array_contains_string(
          retouch_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("retouch")) ||
      retouch_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("protected-retouch") ||
      !retouch_strategy.value(QStringLiteral("approvalRequired")).toBool() ||
      !retouch_strategy.value(QStringLiteral("executionAllowed")).toBool() ||
      retouch_manifest_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("protected-retouch") ||
      !controller.draftActive()) {
    return fail("retouch drafts should carry explicit approval requirements in reports and manifests");
  }
  const auto retouch_rollback = controller.rollbackDraft();
  if (!retouch_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("retouch approval draft should remain rollbackable");
  }

  catalog_model.createProject(QStringLiteral("Wedding Scope Approval"));
  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Wedding Scope Approval"));
  catalog_model.openProject(QStringLiteral("Wedding Scope Approval"));
  const auto outside_project_scope_draft =
      controller.applyDraftRecipePatchForScope(
          QStringLiteral("filtered-library"),
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{
                   {QStringLiteral("exposure"), 0.05},
               }},
          });
  const auto outside_project_scope_risk =
      outside_project_scope_draft.value(QStringLiteral("riskAssessment")).toMap();
  const auto outside_project_scope_approval_types =
      outside_project_scope_risk.value(QStringLiteral("approvalTypes")).toStringList();
  const auto outside_project_scope_manifest =
      read_json_object(outside_project_scope_draft.value(QStringLiteral("manifestPath")).toString());
  const auto outside_project_scope_manifest_risk =
      outside_project_scope_manifest.value(QStringLiteral("riskAssessment")).toObject();
  const auto outside_project_scope_strategy =
      outside_project_scope_draft.value(QStringLiteral("editStrategy")).toMap();
  if (!outside_project_scope_draft.value(QStringLiteral("ok")).toBool() ||
      outside_project_scope_draft.value(QStringLiteral("scope")).toString() !=
          QStringLiteral("filtered-library") ||
      !outside_project_scope_risk.value(QStringLiteral("approvalRequired")).toBool() ||
      !outside_project_scope_approval_types.contains(QStringLiteral("outside_project_scope")) ||
      !outside_project_scope_risk.value(QStringLiteral("scopeContext"))
           .toMap()
           .value(QStringLiteral("scopeEscapesActiveProject"))
           .toBool() ||
      outside_project_scope_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("global-color") ||
      !outside_project_scope_strategy.value(QStringLiteral("preserved"))
           .toStringList()
           .contains(QStringLiteral("Local masks")) ||
      !outside_project_scope_strategy.value(QStringLiteral("executionAllowed")).toBool() ||
      !json_array_contains_string(
          outside_project_scope_manifest_risk.value(QStringLiteral("approvalTypes")).toArray(),
          QStringLiteral("outside_project_scope"))) {
    return fail("draft operations outside the active project scope should require explicit approval in reports and manifests");
  }
  const auto outside_project_scope_rollback = controller.rollbackDraft();
  if (!outside_project_scope_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("outside-project-scope approval draft should remain rollbackable");
  }
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));

  const auto report = controller.runDraftOnSelected(QString());
  if (!report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent draft should succeed");
  }
  if (!controller.draftActive()) {
    return fail("successful agent run should leave a draft active");
  }
  if (report.value(QStringLiteral("runId")).toString().isEmpty()) {
    return fail("draft report should include a run id");
  }
  const auto last_draft_report = controller.lastRunReport();
  const auto run_memory_path = last_draft_report.value(QStringLiteral("runMemoryNotePath")).toString();
  const auto run_memory_note = QString::fromUtf8(read_file_bytes(run_memory_path));
  const auto issue_memory_path = last_draft_report.value(QStringLiteral("issueMemoryNotePath")).toString();
  const auto issue_memory_note = QString::fromUtf8(read_file_bytes(issue_memory_path));
  if (last_draft_report.value(QStringLiteral("runId")).toString() !=
          report.value(QStringLiteral("runId")).toString() ||
      !QFileInfo(run_memory_path).isFile() ||
      !run_memory_note.contains(QStringLiteral("type: agent_run_memory")) ||
      !run_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !run_memory_note.contains(QStringLiteral("links:\n  - \"[[%1]]\"")
                                    .arg(report.value(QStringLiteral("styleId")).toString())) ||
      !run_memory_note.contains(report.value(QStringLiteral("runId")).toString()) ||
      !run_memory_note.contains(QStringLiteral("Related style: [[%1]]")
                                    .arg(report.value(QStringLiteral("styleId")).toString())) ||
      !run_memory_note.contains(QStringLiteral("## Safe Proof Links")) ||
      !run_memory_note.contains(QStringLiteral("Generated proofs are linked as relative app-owned artifacts")) ||
      !run_memory_note.contains(QStringLiteral("agent-runs/")) ||
      !run_memory_note.contains(QStringLiteral("../agent-runs/")) ||
      !run_memory_note.contains(QStringLiteral("after.png")) ||
      !run_memory_note.contains(QStringLiteral("safeArtifactReferences")) ||
      !run_memory_note.contains(QStringLiteral("mcp.tools/list")) ||
      run_memory_note.contains(runtime_root.path()) ||
      run_memory_note.contains(QStringLiteral("data:image")) ||
      run_memory_note.contains(QStringLiteral("base64"))) {
    return fail("successful drafts should persist a compact local run memory note without source paths or image bytes");
  }
  if (!QFileInfo(issue_memory_path).isFile() ||
      !issue_memory_note.contains(QStringLiteral("type: agent_issue_memory")) ||
      !issue_memory_note.contains(QStringLiteral("schema_version: 1")) ||
      !issue_memory_note.contains(report.value(QStringLiteral("runId")).toString()) ||
      !issue_memory_note.contains(QStringLiteral("[[%1]]")
                                      .arg(report.value(QStringLiteral("runId")).toString())) ||
      !issue_memory_note.contains(QStringLiteral("## Repair Strategy")) ||
      !issue_memory_note.contains(QStringLiteral("render_proof_and_validate")) ||
      issue_memory_note.contains(runtime_root.path()) ||
      issue_memory_note.contains(QStringLiteral("data:image")) ||
      issue_memory_note.contains(QStringLiteral("base64"))) {
    return fail("runs with validation warnings should persist issue memory with repair strategy and no source paths or image bytes");
  }
  const auto draft_style_memory_path =
      controller.memoryVaultStatus().value(QStringLiteral("activeStyleNotePath")).toString();
  const auto draft_style_memory_note = QString::fromUtf8(read_file_bytes(draft_style_memory_path));
  if (!draft_style_memory_note.contains(QStringLiteral("[[%1]]")
                                            .arg(QFileInfo(run_memory_path).completeBaseName())) ||
      !draft_style_memory_note.contains(QStringLiteral("[[%1]]")
                                            .arg(QFileInfo(issue_memory_path).completeBaseName()))) {
    return fail("style memory should backlink to run and issue notes that trained or changed the style");
  }
  const auto draft_memory_packet = controller.memoryPacketForPhase(QStringLiteral("batch"));
  if (draft_memory_packet.value(QStringLiteral("rawVaultIncluded")).toBool() ||
      !draft_memory_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("approved-sample-recipe")) ||
      !draft_memory_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("cluster-summary")) ||
      draft_memory_packet.value(QStringLiteral("memoryBudget"))
              .toMap()
              .value(QStringLiteral("approxTokenBudget"))
              .toInt() < 2000 ||
      !draft_memory_packet.value(QStringLiteral("approvedSampleRecipeCard"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      draft_memory_packet.value(QStringLiteral("styleConstraintsCard"))
              .toMap()
              .value(QStringLiteral("allowedTools"))
              .toMap()
              .isEmpty() ||
      draft_memory_packet.value(QStringLiteral("lastRunCard"))
              .toMap()
              .value(QStringLiteral("runId"))
              .toString() != report.value(QStringLiteral("runId")).toString() ||
      !draft_memory_packet.value(QStringLiteral("latestIssueCard"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool()) {
    return fail("batch memory retrieval should include compact run and issue cards without dumping the memory vault");
  }
  if (report.value(QStringLiteral("durationMs")).toInt() < 0 ||
      !report.contains(QStringLiteral("queueWaitMs")) ||
      report.value(QStringLiteral("queueWaitMs")).toInt() < 0 ||
      report.value(QStringLiteral("toolCalls")).toList().size() < 3 ||
      report.value(QStringLiteral("performanceBudget"))
              .toMap()
              .value(QStringLiteral("budgetMs"))
              .toInt() <= 0 ||
      !report.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("passed"))
           .toBool()) {
    return fail("draft report should include run timing, queue wait timing, performance gate, and structured tool call outcomes");
  }
  const auto interaction_feedback =
      report.value(QStringLiteral("interactionFeedback")).toMap();
  if (!interaction_feedback.value(QStringLiteral("passed")).toBool() ||
      interaction_feedback.value(QStringLiteral("firstFeedbackMs"), -1).toInt() < 0 ||
      interaction_feedback.value(QStringLiteral("firstFeedbackMs")).toInt() >
          interaction_feedback.value(QStringLiteral("firstFeedbackTargetMs")).toInt() ||
      interaction_feedback.value(QStringLiteral("liveEventCount")).toInt() < 3 ||
      !interaction_feedback.value(QStringLiteral("publishedStages"))
           .toStringList()
           .contains(QStringLiteral("observe")) ||
      !interaction_feedback.value(QStringLiteral("cancellationSupported")).toBool() ||
      !report.value(QStringLiteral("performanceProofGate"))
           .toMap()
           .value(QStringLiteral("interactionFeedbackPassed"))
           .toBool()) {
    return fail("draft report should prove immediate live feedback and cancellable responsive planning");
  }
  const auto report_tool_calls = report.value(QStringLiteral("toolCalls")).toList();
  const auto report_mcp_discovery = report.value(QStringLiteral("mcpDiscovery")).toMap();
  const auto report_mcp_observation = report.value(QStringLiteral("mcpObservation")).toMap();
  auto saw_mcp_discovery_tool_call = false;
  for (const auto& tool_call_value : report_tool_calls) {
    if (tool_call_value.toMap().value(QStringLiteral("name")).toString() ==
        QStringLiteral("mcp.tools/list")) {
      saw_mcp_discovery_tool_call = true;
      break;
    }
  }
  if (!saw_mcp_discovery_tool_call ||
      report.value(QStringLiteral("mcpAttached")).toBool() ||
      report.value(QStringLiteral("mcpToolCount")).toInt() != 0 ||
      report_mcp_discovery.value(QStringLiteral("mode")).toString() != QStringLiteral("mcp-readiness") ||
      report_mcp_discovery.value(QStringLiteral("ready")).toBool() ||
      report_mcp_discovery.value(QStringLiteral("attemptCount")).toInt() != 1 ||
      report_mcp_discovery.value(QStringLiteral("recoveryPolicy")).toString() !=
          QStringLiteral("retry-discovery-before-agent-planning") ||
      report_mcp_discovery.value(QStringLiteral("status")).toString() != QStringLiteral("Unavailable") ||
      report_mcp_observation.value(QStringLiteral("status")).toString() != QStringLiteral("unavailable") ||
      !report_mcp_observation.value(QStringLiteral("toolReports")).toList().isEmpty()) {
    return fail("draft report should audit MCP discovery/observation and degrade cleanly without a running endpoint");
  }
  if (!report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
    return fail("draft report should include a passing validation report");
  }
  const auto proof_render_policy =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderPolicy"))
          .toMap();
  const auto draft_proof_source =
      report.value(QStringLiteral("validationReport"))
          .toMap()
          .value(QStringLiteral("proofRenderSource"))
          .toMap();
  const auto proof_render_checkpoints =
      proof_render_policy.value(QStringLiteral("checkpoints")).toStringList();
  if (!proof_render_policy.value(QStringLiteral("cancellable")).toBool() ||
      !proof_render_policy.value(QStringLiteral("timeoutAware")).toBool() ||
      !proof_render_checkpoints.contains(QStringLiteral("before-decode")) ||
      !proof_render_checkpoints.contains(QStringLiteral("before-validation"))) {
    return fail("draft validation should expose cancellable proof-render checkpoints");
  }
  if (draft_proof_source.value(QStringLiteral("sourceResolutionPolicy")).toString() !=
          QStringLiteral("bounded-preview") ||
      draft_proof_source.value(QStringLiteral("sourceDecodeMaxEdge")).toInt() <= 0 ||
      !draft_proof_source.value(QStringLiteral("usesDecodedImageCache")).toBool() ||
      draft_proof_source.value(QStringLiteral("fullResolutionRequired")).toBool()) {
    return fail("draft validation should use a bounded cached preview source for proof rendering");
  }
  const auto preview_proof_policy = report.value(QStringLiteral("previewProofPolicy")).toMap();
  const auto preview_asset_reports = report.value(QStringLiteral("assetReports")).toList();
  const auto preview_first_asset_report =
      preview_asset_reports.isEmpty() ? QVariantMap{} : preview_asset_reports.first().toMap();
  if (!preview_proof_policy.value(QStringLiteral("draftPreviewsDuringPlanning")).toBool() ||
      preview_proof_policy.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_proof_policy.value(QStringLiteral("settledPreviewsForFinalProof")).toBool() ||
      preview_proof_policy.value(QStringLiteral("settledPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesDraftPreviewsForPlanning")).toBool() ||
      preview_first_asset_report.value(QStringLiteral("draftPreviewCount")).toInt() <= 0 ||
      !preview_first_asset_report.value(QStringLiteral("usesSettledPreviewForFinalProof")).toBool() ||
      !preview_first_asset_report.value(QStringLiteral("settledPreviewReady")).toBool()) {
    return fail("draft report should prove iterative draft previews and settled final proof previews");
  }
  if (report.value(QStringLiteral("planSummary")).toString().isEmpty()) {
    return fail("draft report should include model plan summary");
  }
  if (report.value(QStringLiteral("editGoals")).toList().isEmpty() ||
      !report.value(QStringLiteral("riskAssessment")).toMap().contains(QStringLiteral("riskCount"))) {
    return fail("draft report should include explicit edit goals and risk assessment");
  }
  if (!report.value(QStringLiteral("reviewRequired")).toBool() ||
      report.value(QStringLiteral("qualityGuarantee")).toBool() ||
      report.value(QStringLiteral("reviewRequiredReason")).toString().isEmpty()) {
    return fail("draft report should require user review without promising perfect taste");
  }
  if (report.value(QStringLiteral("modelBackend")).toString() != QStringLiteral("local") ||
      !QFileInfo(report.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("draft report should include model backend and persist a run manifest");
  }
  if (report.value(QStringLiteral("modelRunnerMode")).toString() != QStringLiteral("worker-thread") ||
      report.value(QStringLiteral("uiEventPumpCount")).toInt() < 0) {
    return fail("draft report should show that model planning ran on the worker runner");
  }
  const auto report_prompt_audit = report.value(QStringLiteral("modelPromptAudit")).toMap();
  if (report_prompt_audit.value(QStringLiteral("strategyPhase")).toString() != QStringLiteral("sample") ||
      !report_prompt_audit.value(QStringLiteral("deterministicPlanning")).toBool() ||
      report_prompt_audit.value(QStringLiteral("temperature")).toDouble() != 0.0 ||
      report_prompt_audit.value(QStringLiteral("memoryPacketPhase")).toString() != QStringLiteral("sample") ||
      report_prompt_audit.value(QStringLiteral("rawMemoryVaultIncluded")).toBool() ||
      !report_prompt_audit.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("style")) ||
      !report_prompt_audit.value(QStringLiteral("strategyPrompt"))
           .toString()
           .contains(QStringLiteral("Phase: sample"))) {
    return fail("draft planning should use a deterministic phase-specific strategy prompt with compact memory context");
  }
  if (controller.recipeBaseMode() != QStringLiteral("current") ||
      report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields")) {
    return fail("default agent draft should preserve unspecified fields from the current recipe");
  }
  const auto style_citations = report.value(QStringLiteral("styleConstraintCitations")).toList();
  if (style_citations.isEmpty() ||
      !has_citation_field(style_citations, QStringLiteral("constraints.max_exposure_shift")) ||
      !has_citation_field(
          style_citations,
          QStringLiteral("validation.highlight_clip_limit_percent"))) {
    return fail("draft report should include model style constraint citations");
  }
  const auto artifact_directory = report.value(QStringLiteral("artifactDirectory")).toString();
  const auto before_proof_path = report.value(QStringLiteral("beforeProofPath")).toString();
  const auto after_proof_path = report.value(QStringLiteral("afterProofPath")).toString();
  const auto recipe_diff_path = report.value(QStringLiteral("recipeDiffPath")).toString();
  const auto validation_report_path = report.value(QStringLiteral("validationReportPath")).toString();
  if (!QFileInfo::exists(artifact_directory) ||
      !QFileInfo(before_proof_path).isFile() ||
      !QFileInfo(after_proof_path).isFile() ||
      !QFileInfo(recipe_diff_path).isFile() ||
      !QFileInfo(validation_report_path).isFile()) {
    return fail("agent draft should persist before/after proof, recipe diff, and validation artifacts");
  }
  if (report.value(QStringLiteral("beforeCanonicalPlanCacheKey")).toString().isEmpty() ||
      report.value(QStringLiteral("afterCanonicalPlanCacheKey")).toString().isEmpty()) {
    return fail("modern agent draft proofs should publish canonical before/after plan identities");
  }
  const auto recipe_diff = report.value(QStringLiteral("recipeDiff")).toMap();
  const auto exposure_diff = recipe_diff.value(QStringLiteral("globalTone"))
                                 .toMap()
                                 .value(QStringLiteral("exposure"))
                                 .toMap();
  if (exposure_diff.isEmpty() ||
      exposure_diff.value(QStringLiteral("before")).toDouble() != original_state.exposure ||
      exposure_diff.value(QStringLiteral("after")).toDouble() <= original_state.exposure) {
    return fail("draft report should include a grouped before/after recipe diff");
  }
  const auto persisted_diff = read_json_object(recipe_diff_path);
  if (!persisted_diff.contains(QStringLiteral("globalTone"))) {
    return fail("recipe diff artifact should contain grouped edit changes");
  }
  const auto persisted_validation = read_json_object(validation_report_path);
  if (!persisted_validation.value(QStringLiteral("ok")).toBool()) {
    return fail("validation artifact should contain a passing report");
  }
  const auto draft_manifest = read_json_object(report.value(QStringLiteral("manifestPath")).toString());
  const auto draft_prompt_audit = draft_manifest.value(QStringLiteral("modelPromptAudit")).toObject();
  if (draft_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      draft_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty() ||
      draft_manifest.value(QStringLiteral("editGoals")).toArray().isEmpty() ||
      draft_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      !draft_prompt_audit.value(QStringLiteral("userPrompt")).toString().contains(controller.selectedStyleId()) ||
      draft_prompt_audit.value(QStringLiteral("userPromptCharCount")).toInt() <= 0 ||
      !draft_manifest.value(QStringLiteral("riskAssessment")).toObject().contains(QStringLiteral("riskCount")) ||
      draft_manifest.value(QStringLiteral("toolCalls")).toArray().size() < 3 ||
      draft_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("queueWaitMs")).toInt(-1) < 0 ||
      draft_manifest.value(QStringLiteral("performanceBudget"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("within-budget") ||
      !draft_manifest.value(QStringLiteral("performanceProofGate"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool() ||
      !draft_manifest.value(QStringLiteral("interactionFeedback"))
           .toObject()
           .value(QStringLiteral("passed"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("runEvents")).toArray().isEmpty() ||
      draft_manifest.value(QStringLiteral("modelRunnerMode")).toString() !=
          QStringLiteral("worker-thread") ||
      !draft_manifest.value(QStringLiteral("reviewRequired")).toBool() ||
      draft_manifest.value(QStringLiteral("qualityGuarantee")).toBool() ||
      draft_manifest.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("current") ||
      draft_manifest.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("preserve-unspecified-current-recipe-fields") ||
      draft_manifest.value(QStringLiteral("recipeStorageMode")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("draftPreviewsDuringPlanning"))
           .toBool() ||
      !draft_manifest.value(QStringLiteral("previewProofPolicy"))
           .toObject()
           .value(QStringLiteral("settledPreviewsForFinalProof"))
           .toBool() ||
      draft_manifest.value(QStringLiteral("recipeDiff")).toObject().isEmpty() ||
      draft_manifest.value(QStringLiteral("model"))
              .toObject()
              .value(QStringLiteral("backend"))
              .toString() != QStringLiteral("local")) {
    return fail("draft manifest should persist mode, model metadata, prompt audit, style citations, preview proof policy, and performance proof gate");
  }
  const auto shareable_manifest_path = report.value(QStringLiteral("shareableManifestPath")).toString();
  const auto shareable_manifest_bytes = read_file_bytes(shareable_manifest_path);
  const auto shareable_manifest = QJsonDocument::fromJson(shareable_manifest_bytes).object();
  const auto shareable_prompt_audit =
      shareable_manifest.value(QStringLiteral("modelPromptAudit")).toObject();
  if (shareable_manifest_path.isEmpty() ||
      !QFileInfo(shareable_manifest_path).isFile() ||
      !shareable_manifest.value(QStringLiteral("redacted")).toBool() ||
      shareable_manifest.value(QStringLiteral("mode")).toString() != QStringLiteral("draft") ||
      shareable_manifest.value(QStringLiteral("styleConstraintCitations")).toArray().isEmpty() ||
      !shareable_prompt_audit.value(QStringLiteral("promptTextRedacted")).toBool() ||
      shareable_prompt_audit.value(QStringLiteral("userPrompt")).toString() !=
          QStringLiteral("<redacted-prompt>")) {
    return fail("draft run should persist a redacted shareable manifest with useful run metadata");
  }
  if (shareable_manifest_bytes.contains(runtime_root.path().toUtf8()) ||
      shareable_manifest_bytes.contains(source_path.toUtf8()) ||
      shareable_manifest_bytes.contains(before_proof_path.toUtf8()) ||
      !shareable_manifest_bytes.contains("<app-data>")) {
    return fail("shareable draft manifest should redact absolute app-data and source paths");
  }

  const auto drafted_state =
      adjustment_state_from_patch(
          catalog_model
              .selectedAdjustmentPatchJson());
  if (drafted_state.exposure <= original_state.exposure) {
    return fail("agent draft should update exposure");
  }
  if (drafted_state.temperature <= original_state.temperature) {
    return fail("agent draft should update white balance temperature");
  }
  if (drafted_state.output_color_space != original_state.output_color_space) {
    return fail("current-base agent draft should preserve manual recipe fields outside the patch");
  }
  if (drafted_state.recipe_snapshots.empty()) {
    return fail("agent draft should snapshot the original recipe before applying changes");
  }
  const auto agent_snapshot = drafted_state.recipe_snapshots.back().toMap();
  if (!agent_snapshot.value(QStringLiteral("agentGenerated")).toBool() ||
      agent_snapshot.value(QStringLiteral("agentRunId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentModelId")).toString() != report.value(QStringLiteral("modelId")).toString() ||
      agent_snapshot.value(QStringLiteral("agentStyleId")).toString() != controller.selectedStyleId() ||
      agent_snapshot.value(QStringLiteral("agentCreatedAtUtc")).toString().isEmpty()) {
    return fail("agent draft snapshot should include run, model, style, and timestamp provenance");
  }
  if (report.value(QStringLiteral("recipeStorageMode")).toString() != QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentChangeStorage")).toString() !=
          QStringLiteral("recipe-diff-first") ||
      agent_snapshot.value(QStringLiteral("agentRecipePatch")).toMap().isEmpty() ||
      agent_snapshot.value(QStringLiteral("agentRecipeDiff")).toMap().isEmpty()) {
    return fail("agent draft should store change provenance as recipe diffs before full snapshots");
  }

  const auto user_revision = controller.runDraftOnSelected(QStringLiteral("revise the draft warmer"));
  if (!user_revision.value(QStringLiteral("ok")).toBool() ||
      user_revision.value(QStringLiteral("runId")).toString() != report.value(QStringLiteral("runId")).toString() ||
      !QFileInfo(user_revision.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      !controller.draftActive()) {
    return fail("user-requested draft revision should reuse the active draft transaction");
  }
  const auto revised_drafted_state =
      adjustment_state_from_patch(
          catalog_model
              .selectedAdjustmentPatchJson());
  if (revised_drafted_state.recipe_snapshots.size() < drafted_state.recipe_snapshots.size()) {
    return fail("user-requested draft revision should preserve agent draft provenance");
  }

  const auto rollback = controller.rollbackDraft();
  if (!rollback.value(QStringLiteral("ok")).toBool() ||
      rollback.value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("rollback should succeed");
  }
  run_history = controller.runHistory();
  if (run_history.isEmpty() ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("rollback")) {
    return fail("agent run history should record draft rollbacks as the latest run event");
  }
  if (controller.draftActive()) {
    return fail("rollback should clear the active draft");
  }

  const auto restored_state =
      adjustment_state_from_patch(
          catalog_model
              .selectedAdjustmentPatchJson());
  if (restored_state.exposure != original_state.exposure ||
      restored_state.temperature != original_state.temperature ||
      restored_state.saturation != original_state.saturation) {
    return fail("rollback should restore the original recipe values");
  }

  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  controller.setUserInstructions(
      QStringLiteral("Keep the portrait warm, luminous, and natural."));
  catalog_model.setProjectFilter(QStringLiteral("Workflow Wedding"));
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  catalog_model.selectAssetById(QStringLiteral("asset-1"), true);
  QVariantMap direction_calibration_import;
#ifdef PHOTO_EDITOR_PHOTOGRAPHER_CALIBRATION_FIXTURE_PATH
  direction_calibration_import =
      controller.importPhotographerCalibrationProfileFile(
          QUrl::fromLocalFile(QString::fromUtf8(
              PHOTO_EDITOR_PHOTOGRAPHER_CALIBRATION_FIXTURE_PATH))
              .toString());
#endif
  const auto comparison_taste_count_before =
      controller.approvedProjectTaste()
          .value(QStringLiteral("approvalCount")).toInt();
  const auto direction_a_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.24},
               {QStringLiteral("contrast"), 4.0},
           }},
          {QStringLiteral("color"),
           QVariantMap{
               {QStringLiteral("temperature"), 10.0},
           }},
      });
  const auto direction_a_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto photographer_taste_check =
      controller.photographerTasteCheck();
  QVariantMap direction_comparison_completion;
  auto direction_comparison_completion_count = 0;
  const auto direction_comparison_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& completed) {
        direction_comparison_completion = completed;
        ++direction_comparison_completion_count;
      });
  const auto direction_comparison_start =
      controller.compareDraftWithPhotographerCalibration();
  const auto direction_comparison_finished = wait_for_condition(
      [&]() { return direction_comparison_completion_count == 1; },
      30000);
  QObject::disconnect(direction_comparison_connection);
  const auto direction_comparison =
      controller.draftDirectionComparison();
  const auto direction_cards =
      direction_comparison.value(QStringLiteral("directions")).toList();
  const auto direction_b_recipe =
      catalog_model.selectedAdjustmentPatchJson();
  const auto comparison_commit_block = controller.commitDraft(true);
  if (!direction_a_draft.value(QStringLiteral("ok")).toBool() ||
      !direction_calibration_import.value(QStringLiteral("ok")).toBool() ||
      photographer_taste_check.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-taste-check-v1") ||
      !photographer_taste_check.value(QStringLiteral("recommended")).toBool() ||
      photographer_taste_check
              .value(QStringLiteral("primaryConcernGroup"))
              .toString() != QStringLiteral("warmth") ||
      photographer_taste_check
          .value(QStringLiteral("automaticComparisonStarted"))
          .toBool() ||
      photographer_taste_check.value(QStringLiteral("durableMemoryWritten"))
          .toBool() ||
      direction_a_recipe.isEmpty() ||
      !direction_comparison_start.value(QStringLiteral("accepted")).toBool() ||
      direction_comparison_start.value(QStringLiteral("mode")).toString() !=
          QStringLiteral("photographer-taste-check-compare") ||
      !direction_comparison_start
           .value(QStringLiteral("automaticComparisonStarted"))
           .toBool() ||
      !direction_comparison_finished ||
      direction_comparison_completion_count != 1 ||
      !direction_comparison_completion.value(QStringLiteral("ok")).toBool() ||
      direction_comparison.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-draft-direction-comparison-v1") ||
      direction_comparison.value(QStringLiteral("state")).toString() !=
          QStringLiteral("ready") ||
      direction_comparison
              .value(QStringLiteral("comparisonOrigin"))
              .toString() != QStringLiteral("photographer-calibration") ||
      direction_comparison
              .value(QStringLiteral("photographerTasteCheck"))
              .toMap()
              .value(QStringLiteral("profileDigestSha256"))
              .toString() !=
          photographer_taste_check
              .value(QStringLiteral("profileDigestSha256"))
              .toString() ||
      !direction_comparison.value(QStringLiteral("choiceRequired")).toBool() ||
      !direction_comparison.value(QStringLiteral("reversible")).toBool() ||
      !direction_comparison.value(QStringLiteral("actualRenderedProofs")).toBool() ||
      direction_comparison.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      direction_cards.size() != 2 ||
      !QFileInfo(direction_cards.at(0).toMap()
                     .value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      !QFileInfo(direction_cards.at(1).toMap()
                     .value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      direction_b_recipe.isEmpty() || direction_b_recipe == direction_a_recipe ||
      controller.approvedProjectTaste()
              .value(QStringLiteral("approvalCount")).toInt() !=
          comparison_taste_count_before ||
      comparison_commit_block.value(QStringLiteral("ok")).toBool() ||
      !comparison_commit_block
           .value(QStringLiteral("directionChoiceRequired")).toBool()) {
    std::cerr << "direction_comparison_completion="
              << QJsonDocument::fromVariant(direction_comparison_completion)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << " comparison="
              << QJsonDocument::fromVariant(direction_comparison)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << '\n';
    return fail("direction comparison should render two real reversible outcomes, defer memory, and block commit until the user chooses");
  }
  const auto chose_direction_a =
      controller.chooseDraftDirection(QStringLiteral("a"));
  const auto chosen_comparison = controller.draftDirectionComparison();
  if (!chose_direction_a.value(QStringLiteral("ok")).toBool() ||
      chose_direction_a.value(QStringLiteral("chosenKey")).toString() !=
          QStringLiteral("a") ||
      chosen_comparison.value(QStringLiteral("state")).toString() !=
          QStringLiteral("chosen") ||
      chosen_comparison.value(QStringLiteral("choiceRequired")).toBool() ||
      chosen_comparison.value(QStringLiteral("chosenKey")).toString() !=
          QStringLiteral("a") ||
      catalog_model.selectedAdjustmentPatchJson() != direction_a_recipe ||
      !controller.draftActive()) {
    return fail("choosing Direction A should restore its exact recipe while keeping the draft reversible");
  }
  if (!accept_all_review_issues()) {
    return fail("the chosen comparison direction should let the user explicitly clear its exception before commit");
  }
  const auto direction_choice_commit = controller.commitDraft(true);
  const auto direction_choice_exemplars =
      controller.approvedProjectTaste()
          .value(QStringLiteral("exemplars")).toList();
  if (!direction_choice_commit.value(QStringLiteral("ok")).toBool() ||
      direction_choice_commit.value(QStringLiteral("draftReport")).toMap()
              .value(QStringLiteral("directionChoice")).toMap()
              .value(QStringLiteral("key")).toString() !=
          QStringLiteral("a") ||
      direction_choice_exemplars.isEmpty() ||
      direction_choice_exemplars.first().toMap()
              .value(QStringLiteral("directionChoice")).toMap()
              .value(QStringLiteral("source")).toString() !=
          QStringLiteral("explicit-comparison-choice-confirmed-by-commit") ||
      !direction_choice_exemplars.first().toMap()
           .value(QStringLiteral("directionChoice"))
           .toMap()
           .value(QStringLiteral("tasteTeachingSignal"))
           .toBool() ||
      direction_choice_exemplars.first().toMap()
              .value(QStringLiteral("directionChoice"))
              .toMap()
              .value(QStringLiteral("comparisonOrigin"))
              .toString() != QStringLiteral("photographer-calibration") ||
      direction_choice_exemplars.first().toMap()
              .value(QStringLiteral("directionChoice"))
              .toMap()
              .value(QStringLiteral("photographerCalibrationDigest"))
              .toString() !=
          photographer_taste_check
              .value(QStringLiteral("profileDigestSha256"))
              .toString() ||
      controller.approvedProjectTaste()
              .value(QStringLiteral("approvalCount")).toInt() !=
          comparison_taste_count_before + 1 ||
      !controller.draftDirectionComparison().isEmpty()) {
    return fail("the explicit direction choice should become project taste only after confirmed commit");
  }
  const auto forgot_direction_calibration =
      controller.forgetPhotographerCalibrationProfile(true);
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectAssetById(QStringLiteral("asset-1"), true);
  if (!forgot_direction_calibration.value(QStringLiteral("ok")).toBool()) {
    return fail("calibrated direction teaching should cleanly return the shared fixture project to its prior state");
  }
  if (catalog_model.applyAdjustmentPatchJsonToAssetIds(
          {QStringLiteral("asset-1")},
          photo_editor::desktop::canonical_asset_bound_adjustment_patch_json(
              original_recipe)) != 1) {
    return fail("direction comparison test should restore the fixture recipe for later tests");
  }

  const auto displayed_commit_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{
               {QStringLiteral("exposure"), 0.18},
               {QStringLiteral("contrast"), 3.0},
           }},
      });
  const auto displayed_commit_validation =
      displayed_commit_draft.value(QStringLiteral("validationReport")).toMap();
  const auto displayed_commit_after_path = displayed_commit_validation
                                               .value(QStringLiteral("artifacts"))
                                               .toMap()
                                               .value(QStringLiteral("afterProofPath"))
                                               .toString();
  const auto displayed_commit_recipe =
      catalog_model
          .selectedAdjustmentPatchJson();
  if (!accept_all_review_issues()) {
    return fail("the displayed draft should let the user explicitly clear its exceptions before commit");
  }
  const auto displayed_commit = controller.commitDraft(true);
  const auto applied_commit_proof = controller.renderProofForSelected();
  const auto applied_commit_path = applied_commit_proof.value(QStringLiteral("currentProofPath")).toString();
  const QImage displayed_commit_after(displayed_commit_after_path);
  const QImage applied_commit_after(applied_commit_path);
  if (!displayed_commit_draft.value(QStringLiteral("ok")).toBool() ||
      displayed_commit_after.isNull() ||
      !displayed_commit.value(QStringLiteral("ok")).toBool() ||
      displayed_commit.value(QStringLiteral("committedCount")).toInt() != 1 ||
      catalog_model
              .selectedAdjustmentPatchJson() !=
          displayed_commit_recipe ||
      !applied_commit_proof.value(QStringLiteral("ok")).toBool() ||
      applied_commit_after.isNull() ||
      displayed_commit_after != applied_commit_after) {
    return fail("the recipe and rendered After proof shown before commit should be exactly the result applied after commit");
  }
  if (catalog_model
          .applyAdjustmentPatchJsonToAssetIds(
              {QStringLiteral("asset-1")},
              photo_editor::desktop::
                  canonical_asset_bound_adjustment_patch_json(
                      original_recipe)) != 1) {
    return fail("displayed-versus-applied proof should restore the fixture recipe for later tests");
  }

  controller.setRecipeBaseMode(QStringLiteral("neutral"));
  const auto neutral_report = controller.runDraftOnSelected(QStringLiteral("clean neutral draft"));
  if (!neutral_report.value(QStringLiteral("ok")).toBool() ||
      neutral_report.value(QStringLiteral("recipeBaseMode")).toString() != QStringLiteral("neutral") ||
      neutral_report.value(QStringLiteral("manualEditPolicy")).toString() !=
          QStringLiteral("replace-current-edits-with-neutral-base")) {
    return fail("neutral-base agent draft should report that it replaces current edits with a neutral base");
  }
  const auto neutral_drafted_state =
      adjustment_state_from_patch(
          catalog_model
              .selectedAdjustmentPatchJson());
  const photo_editor::desktop::EditRecipeState neutral_defaults;
  if (neutral_drafted_state.output_color_space != neutral_defaults.output_color_space) {
    return fail("neutral-base agent draft should start from neutral recipe fields outside the patch");
  }
  const auto neutral_snapshot = neutral_drafted_state.recipe_snapshots.back().toMap();
  const auto neutral_snapshot_patch =
      photo_editor::desktop::parse_adjustment_patch(
          QJsonObject::fromVariantMap(
              neutral_snapshot.value(
                  QStringLiteral("adjustmentPatch")).toMap()));
  if (!neutral_snapshot_patch.ok()) {
    return fail("neutral agent snapshot should contain an asset-bound adjustment patch");
  }
  const auto neutral_snapshot_state = *neutral_snapshot_patch.state;
  if (neutral_snapshot_state.output_color_space != original_state.output_color_space) {
    return fail("neutral-base agent draft should still snapshot the original current recipe for rollback");
  }
  const auto neutral_rollback = controller.rollbackDraft();
  if (!neutral_rollback.value(QStringLiteral("ok")).toBool()) {
    return fail("neutral-base draft rollback should succeed");
  }
  controller.setRecipeBaseMode(QStringLiteral("current"));

  catalog_model.toggleSelectionIndex(1);
  const auto recipe_before_batch_validation =
      catalog_model
          .selectedAdjustmentPatchJson();
  const auto batch_validation = controller.validateBatchForSelected();
  if (!batch_validation.value(QStringLiteral("ok")).toBool() ||
      batch_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      batch_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      batch_validation.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(batch_validation.value(QStringLiteral("batchValidationReportPath")).toString()).isFile() ||
      !QFileInfo(batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("selected-set batch validation should persist per-asset reports without creating a draft");
  }
  for (const auto& asset_report_value : batch_validation.value(QStringLiteral("assetReports")).toList()) {
    const auto asset_report = asset_report_value.toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("validated") ||
        !QFileInfo(asset_report.value(QStringLiteral("currentProofPath")).toString()).isFile() ||
        !QFileInfo(asset_report.value(QStringLiteral("analysisReportPath")).toString()).isFile() ||
        !asset_report.value(QStringLiteral("validationReport")).toMap().value(QStringLiteral("ok")).toBool()) {
      return fail("selected-set batch validation should persist each asset proof and validation report");
    }
  }
  if (catalog_model
          .selectedAdjustmentPatchJson() !=
      recipe_before_batch_validation) {
    return fail("selected-set batch validation should not mutate recipes");
  }

  const auto disallowed_global_crop = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("crop"),
           QVariantMap{
               {QStringLiteral("cropZoom"), 1.1},
           }},
      });
  if (disallowed_global_crop.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      !disallowed_global_crop.value(QStringLiteral("errors"))
           .toStringList()
           .join(QStringLiteral("; "))
           .contains(QStringLiteral("cannot apply crop edits globally"))) {
    return fail("selected-set drafts should reject global crop edits unless style batch behavior allows them");
  }

  const auto missing_second_source_path =
      QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/missing-source-2.png"));
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), missing_second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const QVariantMap resumable_patch{
      {QStringLiteral("globalTone"),
       QVariantMap{
           {QStringLiteral("exposure"), 0.18},
           {QStringLiteral("contrast"), 4.0},
       }},
  };
  const auto resumable_failure = controller.applyDraftRecipePatchToSelected(resumable_patch);
  const auto resume_plan = resumable_failure.value(QStringLiteral("resumePlan")).toMap();
  if (resumable_failure.value(QStringLiteral("ok")).toBool() ||
      !resume_plan.value(QStringLiteral("available")).toBool() ||
      !resume_plan.value(QStringLiteral("cleanAssetIds")).toStringList().contains(QStringLiteral("asset-1")) ||
      resume_plan.value(QStringLiteral("cleanAssetReports")).toList().isEmpty() ||
      controller.draftActive() ||
      adjustment_state_for_asset(
          catalog_model.assetAt(0)).exposure !=
          original_state.exposure) {
    return fail("failed batch draft should persist a resume plan after the last clean asset without applying recipes");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const auto resumed_batch = controller.resumeLastFailedBatchDraft();
  const auto resumed_asset_reports = resumed_batch.value(QStringLiteral("assetReports")).toList();
  auto reused_clean_validation = false;
  for (const auto& report_value : resumed_asset_reports) {
    const auto asset_report = report_value.toMap();
    if (asset_report.value(QStringLiteral("assetId")).toString() == QStringLiteral("asset-1") &&
        asset_report.value(QStringLiteral("validationSkippedByResume")).toBool()) {
      reused_clean_validation = true;
      break;
    }
  }
  const auto resumed_first_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto resumed_second_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(1));
  const auto resumed_manifest =
      read_json_object(resumed_batch.value(QStringLiteral("manifestPath")).toString());
  if (!resumed_batch.value(QStringLiteral("ok")).toBool() ||
      !resumed_batch.value(QStringLiteral("resumed")).toBool() ||
      resumed_batch.value(QStringLiteral("resumedFromRunId")).toString() !=
          resumable_failure.value(QStringLiteral("runId")).toString() ||
      resumed_asset_reports.size() != 2 ||
      !reused_clean_validation ||
      resumed_first_state.exposure <= original_state.exposure ||
      resumed_second_state.exposure <= second_original_state.exposure ||
      !resumed_manifest.value(QStringLiteral("resumed")).toBool() ||
      resumed_manifest.value(QStringLiteral("resumedFromRunId")).toString() !=
          resumable_failure.value(QStringLiteral("runId")).toString() ||
      !controller.draftActive()) {
    return fail("resume should continue a failed batch from the last clean asset and draft the full selection");
  }
  const auto resumed_rollback = controller.rollbackDraft();
  if (!resumed_rollback.value(QStringLiteral("ok")).toBool() || controller.draftActive()) {
    return fail("resumed batch draft should remain rollbackable");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();

  const QJsonObject async_batch_plan_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"),
       QStringLiteral("Clean consistent edit for this lighting group.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.goal")},
               {QStringLiteral("reason"),
                QStringLiteral("The selected look calls for restrained color and natural contrast.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"),
            QStringLiteral("Keep the set coherent with restrained color and natural contrast.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("neutral")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("clean")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"),
            QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.92},
       }},
  };
  QVariantMap async_batch_report;
  auto async_batch_completion_count = 0;
  const auto async_batch_connection = QObject::connect(
      &controller,
      &photo_editor::desktop::AgentEditController::draftRunFinished,
      &controller,
      [&](const QString&, const QVariantMap& report) {
        async_batch_report = report;
        ++async_batch_completion_count;
      });
  qputenv(
      "PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON",
      fake_ollama_response(async_batch_plan_content));
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS", QByteArrayLiteral("40"));
  QElapsedTimer async_batch_start_timer;
  async_batch_start_timer.start();
  const auto async_batch_start = controller.startDraftOnSelected(
      QStringLiteral("apply the style consistently"));
  const auto async_batch_start_duration_ms = async_batch_start_timer.elapsed();
  const auto async_batch_completed = wait_for_condition(
      [&]() { return async_batch_completion_count == 1; }, 10000);
  QObject::disconnect(async_batch_connection);
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_DELAY_MS");
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  const auto async_batch_continuation =
      async_batch_report.value(QStringLiteral("planningContinuation")).toMap();
  const auto async_batch_completed_stages =
      async_batch_continuation.value(QStringLiteral("completedStages"))
          .toStringList();
  auto all_cluster_plans_request_owned = true;
  for (const auto& cluster_plan_value :
       async_batch_report.value(QStringLiteral("clusterPlanning")).toList()) {
    all_cluster_plans_request_owned =
        all_cluster_plans_request_owned &&
        cluster_plan_value.toMap()
            .value(QStringLiteral("requestOwnedContinuation"))
            .toBool();
  }
  auto saw_cluster_stage_running = false;
  auto saw_cluster_stage_complete = false;
  for (const auto& event_value :
       async_batch_report.value(QStringLiteral("runEvents")).toList()) {
    const auto event = event_value.toMap();
    if (!event.value(QStringLiteral("stage"))
             .toString()
             .startsWith(QStringLiteral("plan-worker-batch-cluster-plan:"))) {
      continue;
    }
    saw_cluster_stage_running =
        saw_cluster_stage_running ||
        event.value(QStringLiteral("status")).toString() ==
            QStringLiteral("running");
    saw_cluster_stage_complete =
        saw_cluster_stage_complete ||
        event.value(QStringLiteral("status")).toString() ==
            QStringLiteral("complete");
  }
  if (!async_batch_start.value(QStringLiteral("accepted")).toBool() ||
      async_batch_start_duration_ms > 50 || !async_batch_completed ||
      async_batch_completion_count != 1 ||
      !async_batch_report.value(QStringLiteral("ok")).toBool() ||
      async_batch_continuation.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-request-owned-planning-continuation-v2") ||
      !async_batch_continuation
           .value(QStringLiteral("requestOwned"))
           .toBool() ||
      async_batch_continuation
          .value(QStringLiteral("explicitEventPumpLoop"))
          .toBool() ||
      async_batch_completed_stages.size() != 3 ||
      !async_batch_completed_stages.contains(QStringLiteral("primary-plan")) ||
      async_batch_report.value(QStringLiteral("clusterPlanning"))
              .toList()
              .size() != 2 ||
      !all_cluster_plans_request_owned || !saw_cluster_stage_running ||
      !saw_cluster_stage_complete ||
      async_batch_report
              .value(QStringLiteral("planningUiEventPumpCount"))
              .toInt() != 0 ||
      controller.running() || controller.draftRequestPending() ||
      !controller.activeDraftRequestId().isEmpty() ||
      !controller.draftActive()) {
    std::cerr << "async batch accepted:"
              << async_batch_start.value(QStringLiteral("accepted")).toBool()
              << " start_ms:" << async_batch_start_duration_ms
              << " completed:" << async_batch_completed
              << " completion_count:" << async_batch_completion_count
              << " ok:"
              << async_batch_report.value(QStringLiteral("ok")).toBool()
              << " continuation:"
              << QJsonDocument::fromVariant(async_batch_continuation)
                     .toJson(QJsonDocument::Compact)
                     .toStdString()
              << " cluster_count:"
              << async_batch_report.value(QStringLiteral("clusterPlanning"))
                     .toList()
                     .size()
              << " clusters_owned:" << all_cluster_plans_request_owned
              << " cluster_running:" << saw_cluster_stage_running
              << " cluster_complete:" << saw_cluster_stage_complete
              << " planning_pumps:"
              << async_batch_report
                     .value(QStringLiteral("planningUiEventPumpCount"))
                     .toInt()
              << " running:" << controller.running()
              << " pending:" << controller.draftRequestPending()
              << " active_request:"
              << controller.activeDraftRequestId().toStdString()
              << " draft:" << controller.draftActive() << '\n';
    return fail("primary and per-lighting-group model plans should run as one request-owned continuation chain without explicit event-pump waits or duplicate completion");
  }
  const auto async_batch_rollback = controller.rollbackDraft();
  if (!async_batch_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive()) {
    return fail("request-owned multi-stage batch planning should remain exactly rollbackable");
  }

  const auto batch_report = controller.runDraftOnSelected(QStringLiteral("apply the style consistently"));
  if (!batch_report.value(QStringLiteral("ok")).toBool()) {
    std::cerr << batch_report.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("; ")).toStdString()
              << '\n';
    return fail("agent selected-set draft should succeed");
  }
  if (batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      batch_report.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      batch_report.value(QStringLiteral("assetReports")).toList().size() != 2) {
    return fail("selected-set draft should report batch scope and per-asset results");
  }
  const auto batch_summary = batch_report.value(QStringLiteral("batchSummary")).toMap();
  const auto batch_status_counts = batch_summary.value(QStringLiteral("statusCounts")).toMap();
  const auto batch_review_summary = batch_summary.value(QStringLiteral("reviewSummary")).toMap();
  const auto batch_consistency_map = batch_summary.value(QStringLiteral("consistencyMap")).toMap();
  const auto batch_transaction = batch_report.value(QStringLiteral("batchTransaction")).toMap();
  const auto batch_clusters = batch_report.value(QStringLiteral("batchClusters")).toList();
  const auto batch_cluster_plans = batch_report.value(QStringLiteral("clusterPlanning")).toList();
  const auto batch_edit_strategy =
      batch_report.value(QStringLiteral("editStrategy")).toMap();
  const auto sample_review = batch_report.value(QStringLiteral("sampleReview")).toMap();
  const auto sample_review_samples = sample_review.value(QStringLiteral("samples")).toList();
  const auto first_sample =
      sample_review_samples.isEmpty() ? QVariantMap{} : sample_review_samples.first().toMap();
  const auto sample_review_actions = sample_review.value(QStringLiteral("reviewActions")).toList();
  if (batch_summary.value(QStringLiteral("assetCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("clusterCount")).toInt() != 2 ||
      batch_status_counts.value(QStringLiteral("drafted")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("proofCount")).toInt() != 2 ||
      batch_summary.value(QStringLiteral("recipeDiffCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("failedCount")).toInt() != 0 ||
      batch_review_summary.value(QStringLiteral("passedCount")).toInt() +
              batch_review_summary.value(QStringLiteral("needsReviewCount")).toInt() != 2 ||
      batch_review_summary.value(QStringLiteral("issueCount")).toInt() !=
          batch_summary.value(QStringLiteral("reviewIssues")).toList().size() ||
      batch_review_summary.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-exception-review-v1") ||
      !batch_review_summary.value(QStringLiteral("queueContainsOnlyExceptions")).toBool() ||
      !batch_review_summary.value(QStringLiteral("workloadScalesWithExceptions")).toBool() ||
      batch_review_summary.value(QStringLiteral("showOnlyIssuesFirst")).toBool() !=
          !batch_summary.value(QStringLiteral("reviewIssues")).toList().isEmpty() ||
      batch_clusters.size() != 2 ||
      batch_cluster_plans.size() != 2 ||
      batch_edit_strategy.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-edit-strategy-route-v1") ||
      batch_edit_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("clustered-batch") ||
      batch_edit_strategy.value(QStringLiteral("clusterCount")).toInt() != 2 ||
      !batch_edit_strategy.value(QStringLiteral("appRouted")).toBool() ||
      !batch_edit_strategy.value(QStringLiteral("executionAllowed")).toBool() ||
      batch_report.value(QStringLiteral("representativeAssetIds")).toStringList().size() != 2 ||
      batch_summary.value(QStringLiteral("readyForCommit")).toBool() ||
      !batch_summary.value(QStringLiteral("confirmationRequired")).toBool() ||
      batch_transaction.value(QStringLiteral("transactionId")).toString() !=
          batch_report.value(QStringLiteral("runId")).toString() ||
      batch_transaction.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
      !batch_transaction.value(QStringLiteral("rollbackAvailable")).toBool() ||
      batch_transaction.value(QStringLiteral("assetCount")).toInt() != 2) {
    return fail("selected-set draft should expose a pre-commit batch summary");
  }
  if (batch_consistency_map.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-professional-consistency-map-v1") ||
      !batch_consistency_map.value(QStringLiteral("available")).toBool() ||
      batch_consistency_map.value(QStringLiteral("basis")).toString() !=
          QStringLiteral("within-lighting-group-relative-render-response") ||
      batch_consistency_map.value(QStringLiteral("status")).toString() !=
          QStringLiteral("calibrating") ||
      batch_consistency_map.value(QStringLiteral("measurementComplete")).toBool() ||
      batch_consistency_map.value(QStringLiteral("readyForProfessionalUse")).toBool() ||
      batch_consistency_map.value(QStringLiteral("digestSha256")).toString().size() != 64) {
    return fail("a live batch should publish deterministic consistency evidence without claiming professional readiness when each lighting group has only one sample");
  }
  if (!sample_review.value(QStringLiteral("available")).toBool() ||
      !sample_review.value(QStringLiteral("sampleFirst")).toBool() ||
      !sample_review.value(QStringLiteral("showOnlySamplesFirst")).toBool() ||
      sample_review.value(QStringLiteral("sampleAssetIds")).toStringList().size() != 2 ||
      sample_review.value(QStringLiteral("visibleAssetIds")).toStringList() !=
          sample_review.value(QStringLiteral("sampleAssetIds")).toStringList() ||
      sample_review_samples.size() != 2 ||
      first_sample.value(QStringLiteral("badge")).toString().isEmpty() ||
      first_sample.value(QStringLiteral("explanation")).toString().isEmpty() ||
      !QFileInfo(first_sample.value(QStringLiteral("beforeProofPath")).toString()).isFile() ||
      !QFileInfo(first_sample.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
      sample_review_actions.size() < 4 ||
      sample_review_actions.at(0).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Looks good") ||
      sample_review_actions.at(1).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Make changes") ||
      sample_review_actions.at(2).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Reject") ||
      sample_review_actions.at(3).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply to All")) {
    return fail("selected-set draft should expose a sample-first review packet with proofs, badges, explanations, and review actions");
  }
  QFile batch_manifest_file(batch_report.value(QStringLiteral("manifestPath")).toString());
  if (!batch_manifest_file.open(QIODevice::ReadOnly)) {
    return fail("selected-set draft manifest should be readable");
  }
  const auto batch_manifest_document =
      QJsonDocument::fromJson(batch_manifest_file.readAll()).object();
  const auto batch_prompt_audit =
      batch_manifest_document.value(QStringLiteral("modelPromptAudit")).toObject();
  const auto batch_manifest_consistency =
      batch_manifest_document.value(QStringLiteral("batchSummary"))
          .toObject()
          .value(QStringLiteral("consistencyMap"))
          .toObject();
  const auto batch_cluster_planning =
      batch_manifest_document.value(QStringLiteral("clusterPlanning")).toArray();
  const auto first_cluster_prompt_audit =
      batch_cluster_planning.isEmpty()
          ? QJsonObject{}
          : batch_cluster_planning.first().toObject().value(QStringLiteral("modelPromptAudit")).toObject();
  if (batch_manifest_document.value(QStringLiteral("batchSummary"))
          .toObject()
          .value(QStringLiteral("assetCount"))
          .toInt() != 2 ||
      batch_manifest_document.value(QStringLiteral("batchClusters")).toArray().size() != 2 ||
      batch_manifest_consistency.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-professional-consistency-map-v1") ||
      batch_manifest_consistency.value(QStringLiteral("digestSha256")).toString().size() != 64 ||
      batch_manifest_document.value(QStringLiteral("editStrategy"))
              .toObject()
              .value(QStringLiteral("primaryPath"))
              .toString() != QStringLiteral("clustered-batch") ||
      batch_cluster_planning.size() != 2 ||
      batch_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      first_cluster_prompt_audit.value(QStringLiteral("backend")).toString() != QStringLiteral("local") ||
      batch_manifest_document.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("drafted") ||
      batch_manifest_document.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1) {
    return fail("selected-set draft manifest should persist the pre-commit batch summary, prompt audits, and transaction");
  }
  const auto batch_first_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto batch_second_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(1));
  if (batch_first_state.exposure <= original_state.exposure ||
      batch_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set draft should update every selected recipe");
  }
  const auto batch_asset_reports = batch_report.value(QStringLiteral("assetReports")).toList();
  for (const auto& asset_report_value : batch_asset_reports) {
    const auto asset_report = asset_report_value.toMap();
    const auto validation_report = asset_report.value(QStringLiteral("validationReport")).toMap();
    const auto artifacts = validation_report.value(QStringLiteral("artifacts")).toMap();
    if (asset_report.value(QStringLiteral("status")).toString() != QStringLiteral("drafted") ||
        asset_report.value(QStringLiteral("clusterId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterRepresentativeAssetId")).toString().isEmpty() ||
        asset_report.value(QStringLiteral("clusterSyncGroups")).toStringList().isEmpty() ||
        !QFileInfo(artifacts.value(QStringLiteral("afterProofPath")).toString()).isFile() ||
        validation_report.value(QStringLiteral("recipeDiff")).toMap().isEmpty()) {
      return fail("selected-set draft should persist per-asset cluster metadata, proof artifacts, and diffs");
    }
  }
  const auto initial_review_issues = batch_report.value(QStringLiteral("batchSummary"))
                                         .toMap()
                                         .value(QStringLiteral("reviewIssues"))
                                         .toList();
  if (initial_review_issues.isEmpty()) {
    return fail("selected-set draft should expose issue-first review rows when validation warnings are present");
  }
  const auto first_review_issue = initial_review_issues.first().toMap();
  if (first_review_issue.value(QStringLiteral("thumbnailPath")).toString().isEmpty() ||
      first_review_issue.value(QStringLiteral("reason")).toString().isEmpty() ||
      first_review_issue.value(QStringLiteral("suggestedAction")).toString().isEmpty() ||
      !first_review_issue.contains(QStringLiteral("warningCount")) ||
      !first_review_issue.contains(QStringLiteral("errorCount")) ||
      !first_review_issue.contains(QStringLiteral("styleMatchScorePercent"))) {
    return fail("review issue rows should include proof thumbnail, reason, suggested action, and expandable detail fields");
  }
  const auto initial_review_summary = batch_report.value(QStringLiteral("batchSummary"))
                                          .toMap()
                                          .value(QStringLiteral("reviewSummary"))
                                          .toMap();
  if (initial_review_issues.size() < 2 ||
      !initial_review_summary.value(QStringLiteral("fixAllOutliersAvailable")).toBool()) {
    return fail("selected-set draft should offer safe bulk fixing when multiple review issues have no failures");
  }
  const auto unresolved_batch_commit = controller.commitDraft(true);
  if (unresolved_batch_commit.value(QStringLiteral("ok")).toBool() ||
      !unresolved_batch_commit.value(QStringLiteral("reviewRequired")).toBool() ||
      !unresolved_batch_commit.value(QStringLiteral("commitBlockedByExceptions")).toBool() ||
      unresolved_batch_commit.value(QStringLiteral("reviewIssues")).toList().size() !=
          initial_review_issues.size() ||
      !controller.draftActive()) {
    return fail("a multi-photo batch should remain reversible and refuse commit while exception rows are unresolved");
  }
  const auto fixed_all_issues = controller.fixAllReviewIssues();
  const auto fixed_all_summary =
      fixed_all_issues.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  if (!fixed_all_issues.value(QStringLiteral("ok")).toBool() ||
      fixed_all_issues.value(QStringLiteral("mode")).toString() != QStringLiteral("review-issue-fix-all") ||
      fixed_all_issues.value(QStringLiteral("fixedCount")).toInt() != initial_review_issues.size() ||
      fixed_all_issues.value(QStringLiteral("repairReports")).toList().size() != initial_review_issues.size() ||
      fixed_all_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("fixing all review issues should repair every safe issue and keep the batch draft active");
  }
  const auto review_issue_asset_id =
      initial_review_issues.first().toMap().value(QStringLiteral("assetId")).toString();
  const auto fixed_issue = controller.fixReviewIssue(review_issue_asset_id);
  const auto fixed_issue_summary =
      fixed_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  const auto fixed_issue_strategy =
      fixed_issue.value(QStringLiteral("editStrategy")).toMap();
  if (!fixed_issue.value(QStringLiteral("ok")).toBool() ||
      fixed_issue.value(QStringLiteral("mode")).toString() != QStringLiteral("review-issue-fix") ||
      fixed_issue.value(QStringLiteral("assetId")).toString() != review_issue_asset_id ||
      fixed_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("fixed") ||
      fixed_issue.value(QStringLiteral("repairedRecipePatch")).toMap().isEmpty() ||
      fixed_issue_strategy.value(QStringLiteral("primaryPath")).toString() !=
          QStringLiteral("outlier-repair") ||
      fixed_issue_strategy.value(QStringLiteral("assetCount")).toInt() != 1 ||
      !fixed_issue_strategy.value(QStringLiteral("executionAllowed")).toBool() ||
      fixed_issue_summary.value(QStringLiteral("editedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("fixing a review issue should re-draft only that photo and keep the batch review active");
  }
  const auto accepted_issue = controller.acceptReviewIssue(review_issue_asset_id);
  const auto accepted_issue_summary =
      accepted_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  if (!accepted_issue.value(QStringLiteral("ok")).toBool() ||
      accepted_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("accepted_anyway") ||
      accepted_issue_summary.value(QStringLiteral("issueCount")).toInt() >= initial_review_issues.size() ||
      !controller.draftActive()) {
    return fail("accepting a review issue should resolve it without changing the active draft");
  }
  const auto rolled_back_issue = controller.rollbackReviewIssue(review_issue_asset_id);
  const auto rolled_back_issue_summary =
      rolled_back_issue.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("reviewSummary")).toMap();
  const auto rolled_back_issue_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  if (!rolled_back_issue.value(QStringLiteral("ok")).toBool() ||
      rolled_back_issue.value(QStringLiteral("resolution")).toString() != QStringLiteral("rolled_back") ||
      rolled_back_issue.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      rolled_back_issue_summary.value(QStringLiteral("editedCount")).toInt() != 1 ||
      rolled_back_issue_summary.value(QStringLiteral("issueCount")).toInt() >= initial_review_issues.size() ||
      rolled_back_issue_state.exposure != original_state.exposure ||
      !controller.draftActive()) {
    return fail("rolling back one review issue should restore that photo and keep the remaining batch draft active");
  }
  const auto batch_rollback = controller.rollbackDraft();
  if (!batch_rollback.value(QStringLiteral("ok")).toBool() ||
      batch_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("rolled_back") ||
      batch_rollback.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("assetCount"))
              .toInt() != 1) {
    return fail("selected-set rollback should restore the remaining active draft recipes and report the transaction");
  }
  const auto restored_first_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto restored_second_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(1));
  if (restored_first_state.exposure != original_state.exposure ||
      restored_second_state.exposure != second_original_state.exposure) {
    return fail("selected-set rollback should restore each asset's original recipe");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
      make_asset(QStringLiteral("asset-3"), third_source_path, third_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  const auto sample_first_batch =
      controller.runDraftOnSelected(QStringLiteral("create wedding samples before finishing the full set"));
  const auto sample_first_review = sample_first_batch.value(QStringLiteral("sampleReview")).toMap();
  if (!sample_first_batch.value(QStringLiteral("ok")).toBool() ||
      !sample_first_batch.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      sample_first_batch.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      sample_first_batch.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      sample_first_batch.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      !sample_first_review.value(QStringLiteral("applyToAllAvailable")).toBool() ||
      sample_first_review.value(QStringLiteral("sampleAssetIds")).toStringList().size() != 2 ||
      sample_first_review.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      sample_first_review.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      sample_first_batch.value(QStringLiteral("plannedPatchesByAssetId")).toMap().size() != 3 ||
      !controller.draftActive()) {
    return fail("larger selected-set draft should create representative samples before applying the full batch");
  }
  const auto sample_waiting_ai_state = controller.aiEditWorkflowState();
  if (sample_waiting_ai_state.value(QStringLiteral("statusLine")).toString() !=
          QStringLiteral("Waiting for your review") ||
      !sample_waiting_ai_state.value(QStringLiteral("reviewRequired")).toBool() ||
      !sample_waiting_ai_state.value(QStringLiteral("sampleReviewWaiting")).toBool() ||
      sample_waiting_ai_state.value(QStringLiteral("sampleRemainingCount")).toInt() != 1 ||
      !sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("confirmationRequired")).toBool() ||
      sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("apply_to_all") ||
      sample_waiting_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("label")).toString() !=
          QStringLiteral("Apply to All")) {
    return fail("AI edit workflow state should make a sample-only draft wait for Apply to All review");
  }
  const auto sample_first_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto sample_second_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(1));
  const auto sample_third_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(2));
  if (sample_first_state.exposure <= original_state.exposure ||
      sample_second_state.exposure <= second_original_state.exposure ||
      sample_third_state.exposure != third_original_state.exposure) {
    return fail("sample-first batch should draft only representative sample recipes before approval");
  }
  const auto sample_source_run_id = sample_first_batch.value(QStringLiteral("runId")).toString();
  const auto revised_sample_batch =
      controller.reviseSampleReview(QStringLiteral("Make the samples warmer and keep dress highlights softer."));
  const auto revised_sample_ai_state = controller.aiEditWorkflowState();
  const auto revised_third_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(2));
  const auto correction_transfer_preview =
      revised_sample_batch.value(QStringLiteral("correctionTransferPreview"))
          .toMap();
  if (!revised_sample_batch.value(QStringLiteral("ok")).toBool() ||
      !revised_sample_batch.value(QStringLiteral("sampleRevision")).toBool() ||
      revised_sample_batch.value(QStringLiteral("revisedFromRunId")).toString() != sample_source_run_id ||
      revised_sample_batch.value(QStringLiteral("sampleRevisionFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !revised_sample_batch.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      !revised_sample_batch.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      revised_sample_batch.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      revised_third_state.exposure != third_original_state.exposure ||
      revised_sample_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("apply_to_all") ||
      !controller.draftActive()) {
    return fail("sample review feedback should immediately revise samples without touching remaining photos or durable style memory");
  }
  if (correction_transfer_preview.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-correction-transfer-preview-v1") ||
      !correction_transfer_preview.value(QStringLiteral("available")).toBool() ||
      correction_transfer_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("waiting-for-approval") ||
      correction_transfer_preview.value(QStringLiteral("feedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      correction_transfer_preview.value(QStringLiteral("sampleCount")).toInt() != 2 ||
      correction_transfer_preview.value(QStringLiteral("remainingCount")).toInt() != 1 ||
      correction_transfer_preview.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      !correction_transfer_preview.value(QStringLiteral("remainingUntouched")).toBool() ||
      !correction_transfer_preview.value(QStringLiteral("approvalRequired")).toBool() ||
      correction_transfer_preview.value(QStringLiteral("approvalAction")).toString() !=
          QStringLiteral("apply_to_all") ||
      !correction_transfer_preview.value(QStringLiteral("durableMemoryDeferred")).toBool() ||
      correction_transfer_preview.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      !correction_transfer_preview.value(QStringLiteral("projectLocal")).toBool() ||
      correction_transfer_preview.value(QStringLiteral("transferPolicy")).toString() !=
          QStringLiteral("cluster-aware-after-sample-approval") ||
      correction_transfer_preview.value(QStringLiteral("headline")).toString() !=
          QStringLiteral("Your correction is ready for 1 more photo")) {
    return fail("sample correction should publish an exact approval-gated transfer preview from the revised batch plan");
  }
  const auto unconfirmed_apply_all = controller.applySampleReviewToAll();
  const auto unconfirmed_transfer_preview =
      unconfirmed_apply_all.value(QStringLiteral("correctionTransferPreview"))
          .toMap();
  if (unconfirmed_apply_all.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_apply_all.value(QStringLiteral("confirmationRequired")).toBool() ||
      !unconfirmed_apply_all.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      unconfirmed_apply_all.value(QStringLiteral("sampleRevisionFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !unconfirmed_apply_all.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      unconfirmed_transfer_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("waiting-for-approval") ||
      !unconfirmed_transfer_preview.value(QStringLiteral("remainingUntouched")).toBool() ||
      unconfirmed_apply_all.value(QStringLiteral("remainingAssetIds")).toStringList() !=
          QStringList{QStringLiteral("asset-3")} ||
      !controller.draftActive()) {
    return fail("sample review Apply to All should require confirmation while preserving draft context");
  }
  const auto confirmed_apply_all = controller.applySampleReviewToAll(true);
  const auto approved_feedback_memory = confirmed_apply_all.value(QStringLiteral("approvedFeedbackMemory")).toMap();
  const auto approved_feedback_style =
      read_json_object(approved_feedback_memory.value(QStringLiteral("stylePath")).toString());
  const auto approved_transfer_preview =
      confirmed_apply_all.value(QStringLiteral("correctionTransferPreview"))
          .toMap();
  const auto approved_feedback_note =
      QString::fromUtf8(read_file_bytes(confirmed_apply_all.value(QStringLiteral("approvedFeedbackMemoryNotePath")).toString()));
  const auto confirmed_third_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(2));
  if (!confirmed_apply_all.value(QStringLiteral("ok")).toBool() ||
      !confirmed_apply_all.value(QStringLiteral("sampleOnlyContinuation")).toBool() ||
      !confirmed_apply_all.value(QStringLiteral("sampleFeedbackApproved")).toBool() ||
      confirmed_apply_all.value(QStringLiteral("feedbackMemoryDeferred")).toBool() ||
      confirmed_apply_all.value(QStringLiteral("approvedFeedback")).toString() !=
          QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      confirmed_apply_all.value(QStringLiteral("styleChangeSummary")).toString() !=
          QStringLiteral("Style changed because: Make the samples warmer and keep dress highlights softer.") ||
      approved_feedback_memory.value(QStringLiteral("mode")).toString() != QStringLiteral("style-feedback") ||
      approved_feedback_memory.value(QStringLiteral("styleChangeSummary")).toString() !=
          confirmed_apply_all.value(QStringLiteral("styleChangeSummary")).toString() ||
      approved_feedback_style.value(QStringLiteral("learned_profile"))
              .toObject()
              .value(QStringLiteral("latestFeedback"))
              .toString() != QStringLiteral("Make the samples warmer and keep dress highlights softer.") ||
      !approved_feedback_note.contains(QStringLiteral("Make the samples warmer and keep dress highlights softer.")) ||
      confirmed_apply_all.value(QStringLiteral("userVisibleMemoryEvents")).toList().isEmpty() ||
      approved_transfer_preview.value(QStringLiteral("state")).toString() !=
          QStringLiteral("approved-for-batch") ||
      !approved_transfer_preview.value(QStringLiteral("approved")).toBool() ||
      approved_transfer_preview.value(QStringLiteral("remainingUntouched")).toBool() ||
      approved_transfer_preview.value(QStringLiteral("durableMemoryDeferred")).toBool() ||
      !approved_transfer_preview.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      approved_transfer_preview.value(QStringLiteral("approvalReceipt")).toString() !=
          QStringLiteral("Applied to the remaining photos and remembered for this project.") ||
      confirmed_apply_all.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      confirmed_apply_all.value(QStringLiteral("remainingCount")).toInt() != 0 ||
      confirmed_apply_all.value(QStringLiteral("fullTargetCount")).toInt() != 3 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("editedCount"))
              .toInt() != 1 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("failedCount"))
              .toInt() != 0 ||
      confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewSummary"))
              .toMap()
              .value(QStringLiteral("issueCount"))
              .toInt() !=
          confirmed_apply_all.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("reviewIssues"))
              .toList()
              .size() ||
      confirmed_third_state.exposure <= third_original_state.exposure ||
      !controller.draftActive()) {
    return fail("confirmed sample review Apply to All should draft the remaining selected photos");
  }
  const auto post_apply_all_ai_state = controller.aiEditWorkflowState();
  if (post_apply_all_ai_state.value(QStringLiteral("statusLine")).toString() != QStringLiteral("Ready to commit") ||
      post_apply_all_ai_state.value(QStringLiteral("sampleReviewWaiting")).toBool() ||
      post_apply_all_ai_state.value(QStringLiteral("primaryAction")).toMap().value(QStringLiteral("key")).toString() !=
          QStringLiteral("commit")) {
    return fail("AI edit workflow state should return to commit after Apply to All drafts remaining photos");
  }
  const auto sample_first_rollback = controller.rollbackDraft();
  if (!sample_first_rollback.value(QStringLiteral("ok")).toBool() ||
      sample_first_rollback.value(QStringLiteral("restoredCount")).toInt() != 3 ||
      controller.draftActive()) {
    return fail("sample-first batch rollback should restore both sample and apply-to-all drafts");
  }
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.selectAll();

  catalog_model.addAssetToProject(QStringLiteral("asset-1"), QStringLiteral("Agent Shoot"));
  catalog_model.addAssetToProject(QStringLiteral("asset-2"), QStringLiteral("Agent Shoot"));
  catalog_model.openProject(QStringLiteral("Agent Shoot"));
  const auto project_validation = controller.validateBatchForScope(QStringLiteral("project"));
  if (!project_validation.value(QStringLiteral("ok")).toBool() ||
      project_validation.value(QStringLiteral("mode")).toString() != QStringLiteral("validate-batch") ||
      project_validation.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_validation.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      project_validation.value(QStringLiteral("batchSummary")).toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      project_validation.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      controller.draftActive()) {
    return fail("project scope validation should cover the current album/project without creating a draft");
  }

  const auto project_preflight =
      controller.exportPreflightForScope(QStringLiteral("project"), QStringLiteral("Web Proof"), export_dir, {});
  if (!project_preflight.value(QStringLiteral("ok")).toBool() ||
      project_preflight.value(QStringLiteral("mode")).toString() != QStringLiteral("export-preflight") ||
      project_preflight.value(QStringLiteral("scope")).toString() != QStringLiteral("project") ||
      project_preflight.value(QStringLiteral("checkedCount")).toInt() != 2 ||
      project_preflight.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      !QFileInfo(project_preflight.value(QStringLiteral("preflightReportPath")).toString()).isFile() ||
      !QFileInfo(project_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive()) {
    return fail("project scope export preflight should inspect every project asset without creating a draft");
  }

  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.setSearchFilter(QStringLiteral("source-2"));
  const auto filtered_report = controller.runDraftForScope(
      QStringLiteral("filtered-library"),
      QStringLiteral("apply a filtered one-photo draft"));
  if (!filtered_report.value(QStringLiteral("ok")).toBool() ||
      filtered_report.value(QStringLiteral("scope")).toString() != QStringLiteral("filtered-library") ||
      filtered_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      filtered_report.value(QStringLiteral("assetIds")).toStringList() != QStringList{QStringLiteral("asset-2")} ||
      filtered_report.value(QStringLiteral("assetReports")).toList().size() != 1 ||
      !controller.draftActive()) {
    return fail("filtered-library scope draft should apply only the currently visible filtered asset");
  }
  const auto filtered_first_state =
      adjustment_state_for_asset(
          catalog_model
              .assetsForProject(
                  QStringLiteral("all"))
              .at(0)
              .toMap());
  const auto filtered_visible_state =
      adjustment_state_for_asset(
          catalog_model.visibleAssets()
              .first()
              .toMap());
  if (filtered_first_state.exposure != original_state.exposure ||
      filtered_visible_state.exposure <= second_original_state.exposure) {
    return fail("filtered-library draft should leave hidden assets unchanged and update the visible target");
  }
  const auto filtered_rollback = controller.rollbackDraft();
  if (!filtered_rollback.value(QStringLiteral("ok")).toBool() ||
      filtered_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      controller.draftActive()) {
    return fail("filtered-library rollback should restore the one scoped draft");
  }
  catalog_model.setSearchFilter(QString());
  catalog_model.setProjectFilter(QStringLiteral("all"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto second_batch_report = controller.runDraftOnSelected(QStringLiteral("commit the style consistently"));
  if (!second_batch_report.value(QStringLiteral("ok")).toBool() ||
      second_batch_report.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      second_batch_report.value(QStringLiteral("draftedCount")).toInt() != 2 ||
      !controller.draftActive()) {
    return fail("selected-set draft should be ready for commit verification");
  }
  const auto unconfirmed_commit_batch = controller.commitDraft();
  if (unconfirmed_commit_batch.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("committedCount")).toInt() != 0 ||
      !unconfirmed_commit_batch.value(QStringLiteral("confirmationRequired")).toBool() ||
      !unconfirmed_commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      unconfirmed_commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      unconfirmed_commit_batch.value(QStringLiteral("assetReports")).toList().size() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("drafted"))
              .toInt() != 2 ||
      unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("awaiting_commit_confirmation") ||
      !unconfirmed_commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      !controller.draftActive()) {
    return fail("selected-set commit should require explicit confirmation while preserving the batch summary");
  }
  if (!accept_all_review_issues()) {
    return fail("the selected-set draft should let the user explicitly clear its exceptions before commit");
  }
  const auto commit_batch = controller.commitDraft(true);
  if (!commit_batch.value(QStringLiteral("ok")).toBool() ||
      commit_batch.value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      commit_batch.value(QStringLiteral("scope")).toString() != QStringLiteral("selected-assets") ||
      commit_batch.value(QStringLiteral("committedCount")).toInt() != 2 ||
      !commit_batch.value(QStringLiteral("confirmed")).toBool() ||
      commit_batch.value(QStringLiteral("assetIds")).toStringList().size() != 2 ||
      commit_batch.value(QStringLiteral("batchConcurrencyLimit")).toInt() != 1 ||
      commit_batch.value(QStringLiteral("batchSummary"))
              .toMap()
              .value(QStringLiteral("statusCounts"))
              .toMap()
              .value(QStringLiteral("committed"))
              .toInt() != 2 ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("committed") ||
      commit_batch.value(QStringLiteral("batchTransaction"))
              .toMap()
              .value(QStringLiteral("rollbackAvailable"))
              .toBool() ||
      !commit_batch.value(QStringLiteral("approvedProjectTaste")).toBool() ||
      !commit_batch.value(QStringLiteral("projectTasteMemory"))
           .toMap()
           .value(QStringLiteral("ok"))
           .toBool() ||
      commit_batch.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive()) {
    return fail("selected-set commit should report and clear a two-asset draft transaction");
  }
  const auto committed_project_taste = controller.approvedProjectTaste();
  const auto committed_taste_packet = controller.memoryPacketForPhase(QStringLiteral("sample"));
  const auto committed_taste_json =
      QJsonDocument::fromVariant(committed_project_taste).toJson(QJsonDocument::Compact);
  if (!committed_project_taste.value(QStringLiteral("available")).toBool() ||
      committed_project_taste.value(QStringLiteral("approvalCount")).toInt() < 1 ||
      committed_project_taste.value(QStringLiteral("exemplars")).toList().isEmpty() ||
      committed_project_taste.value(QStringLiteral("storesImageBytes")).toBool() ||
      committed_project_taste.value(QStringLiteral("storesSourcePaths")).toBool() ||
      !committed_taste_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("project-taste")) ||
      !committed_taste_packet.value(QStringLiteral("approvedProjectTasteCard"))
           .toMap()
           .value(QStringLiteral("available"))
           .toBool() ||
      committed_taste_json.contains("data:image") ||
      committed_taste_json.contains("base64") ||
      committed_taste_json.contains(runtime_root.path().toUtf8())) {
    return fail("confirmed edits should become compact project taste without image bytes or source paths");
  }
  const auto committed_first_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto committed_second_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(1));
  if (committed_first_state.exposure <= original_state.exposure ||
      committed_second_state.exposure <= second_original_state.exposure) {
    return fail("selected-set commit should preserve drafted recipe changes");
  }
  run_history = controller.runHistory();
  const auto history_has_batch_review_evidence = std::any_of(
      run_history.cbegin(),
      run_history.cend(),
      [](const QVariant& value) {
        return value.toMap()
                   .value(QStringLiteral("batchReviewEvidence"))
                   .toMap()
                   .value(QStringLiteral("contract"))
                   .toString() ==
               QStringLiteral("agent-batch-review-evidence-v1");
      });
  if (run_history.size() < 5 ||
      run_history.first().toMap().value(QStringLiteral("mode")).toString() != QStringLiteral("commit") ||
      run_history.first().toMap().value(QStringLiteral("assetCount")).toInt() != 2 ||
      !history_has_batch_review_evidence ||
      run_history.size() > 20) {
    return fail("agent run history should retain recent user-facing summaries and compact batch review evidence newest-first");
  }
  const auto calibrated_batch_forecast =
      controller.batchReviewForecast(QStringLiteral("selected-assets"));
  const auto calibrated_forecast_json =
      QJsonDocument::fromVariant(calibrated_batch_forecast)
          .toJson(QJsonDocument::Compact);
  if (!calibrated_batch_forecast.value(QStringLiteral("ok")).toBool() ||
      !calibrated_batch_forecast.value(QStringLiteral("predictionAvailable")).toBool() ||
      calibrated_batch_forecast.value(QStringLiteral("priorRunCount")).toInt() < 1 ||
      calibrated_batch_forecast.value(QStringLiteral("priorPhotoCount")).toInt() < 1 ||
      calibrated_batch_forecast.value(QStringLiteral("estimatedReviewCount")).toInt() <
          calibrated_batch_forecast.value(QStringLiteral("knownReviewCount")).toInt() ||
      calibrated_batch_forecast.value(QStringLiteral("lowerReviewCount")).toInt() >
          calibrated_batch_forecast.value(QStringLiteral("estimatedReviewCount")).toInt() ||
      calibrated_batch_forecast.value(QStringLiteral("upperReviewCount")).toInt() <
          calibrated_batch_forecast.value(QStringLiteral("estimatedReviewCount")).toInt() ||
      !calibrated_batch_forecast.value(QStringLiteral("projectOnlyEvidence")).toBool() ||
      calibrated_forecast_json.contains(runtime_root.path().toUtf8()) ||
      calibrated_forecast_json.contains("data:image") ||
      calibrated_forecast_json.contains("base64")) {
    return fail("batch preflight should learn a bounded review estimate from compact project-only outcomes without storing paths or image data");
  }
  const auto empty_commit = controller.commitDraft(true);
  if (empty_commit.value(QStringLiteral("ok")).toBool() ||
      empty_commit.value(QStringLiteral("committedCount")).toInt() != 0 ||
      controller.draftActive()) {
    return fail("commit without an active draft should fail without mutating state");
  }

  const auto source_recipe_before_virtual_copy =
      adjustment_patch_for_asset(
          catalog_model.assetAt(0));
  const auto virtual_copy_id =
      catalog_model.createVirtualCopyForAsset(QStringLiteral("asset-1"), QStringLiteral("Agent Variant"));
  if (virtual_copy_id.isEmpty()) {
    return fail("agent virtual-copy independence test should be able to create a variant");
  }
  auto virtual_copy_row = -1;
  for (auto row = 0; row < catalog_model.assetCount(); ++row) {
    if (catalog_model.assetAt(row).value(QStringLiteral("assetId")).toString() == virtual_copy_id) {
      virtual_copy_row = row;
      break;
    }
  }
  if (virtual_copy_row < 0) {
    return fail("agent virtual-copy variant should be visible in the catalog");
  }
  catalog_model.selectIndex(virtual_copy_row);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(virtual_copy_row);
  const auto virtual_copy_report =
      controller.runDraftForScope(QStringLiteral("selected-assets"), QStringLiteral("edit the virtual copy only"));
  const auto virtual_copy_asset_reports = virtual_copy_report.value(QStringLiteral("assetReports")).toList();
  const auto virtual_copy_batch_summary = virtual_copy_report.value(QStringLiteral("batchSummary")).toMap();
  const auto variant_families = virtual_copy_batch_summary.value(QStringLiteral("variantFamilies")).toList();
  if (!virtual_copy_report.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_report.value(QStringLiteral("draftedCount")).toInt() != 1 ||
      virtual_copy_asset_reports.size() != 1 ||
      !virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopy")).toBool() ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      virtual_copy_asset_reports.first().toMap().value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      virtual_copy_batch_summary.value(QStringLiteral("virtualCopyPolicy")).toString() !=
          QStringLiteral("target-asset-id-only") ||
      variant_families.size() != 1 ||
      variant_families.first().toMap().value(QStringLiteral("sourceAssetId")).toString() !=
          QStringLiteral("asset-1") ||
      variant_families.first().toMap().value(QStringLiteral("virtualCopyAssetIds")).toStringList() !=
          QStringList{virtual_copy_id} ||
      adjustment_patch_for_asset(
          catalog_model.assetAt(0)) !=
          source_recipe_before_virtual_copy ||
      !controller.draftActive()) {
    return fail("agent virtual-copy draft should report variant metadata and leave the source recipe untouched");
  }
  if (adjustment_patch_for_asset(
          catalog_model.assetAt(
              virtual_copy_row)) ==
      source_recipe_before_virtual_copy) {
    return fail("agent virtual-copy draft should edit the variant recipe");
  }
  const auto virtual_copy_rollback = controller.rollbackDraft();
  if (!virtual_copy_rollback.value(QStringLiteral("ok")).toBool() ||
      virtual_copy_rollback.value(QStringLiteral("restoredCount")).toInt() != 1 ||
      adjustment_patch_for_asset(
          catalog_model.assetAt(0)) !=
          source_recipe_before_virtual_copy ||
      controller.draftActive()) {
    return fail("agent virtual-copy rollback should restore only the drafted variant");
  }
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  catalog_model.toggleSelectionIndex(1);

  const auto first_recipe_before_cancel =
      adjustment_patch_for_asset(
          catalog_model.assetAt(0));
  const auto second_recipe_before_cancel =
      adjustment_patch_for_asset(
          catalog_model.assetAt(1));
  QTimer::singleShot(0, [&controller]() {
    controller.cancelRun(QStringLiteral("user_stop"));
  });
  const auto user_canceled_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (user_canceled_draft.value(QStringLiteral("ok")).toBool() ||
      user_canceled_draft.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_draft.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_draft.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_draft.value(QStringLiteral("appliedCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("restoredCount")).toInt() != 0 ||
      user_canceled_draft.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive() ||
      controller.cancellationRequested() ||
      adjustment_patch_for_asset(
          catalog_model.assetAt(0)) !=
          first_recipe_before_cancel ||
      adjustment_patch_for_asset(
          catalog_model.assetAt(1)) !=
          second_recipe_before_cancel ||
      !QFileInfo(user_canceled_draft.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("user-canceled agent draft should stop and report rollback of any partial draft writes");
  }
  const auto user_canceled_manifest =
      read_json_object(user_canceled_draft.value(QStringLiteral("manifestPath")).toString());
  if (user_canceled_manifest.value(QStringLiteral("cancellationReason")).toString() !=
          QStringLiteral("user_stop") ||
      !user_canceled_manifest.value(QStringLiteral("rollbackOnCancel")).toBool() ||
      user_canceled_manifest.value(QStringLiteral("partialDraftRollback")).toString() !=
          QStringLiteral("nothing_applied") ||
      user_canceled_manifest.value(QStringLiteral("batchTransaction"))
              .toObject()
              .value(QStringLiteral("status"))
              .toString() != QStringLiteral("canceled_rolled_back")) {
    return fail("user-canceled draft manifest should persist rollback-on-cancel transaction metadata");
  }

  controller.setRunTimeoutMs(0);
  const auto timed_out_draft = controller.applyDraftRecipePatchToSelected(
      QVariantMap{
          {QStringLiteral("globalTone"),
           QVariantMap{{QStringLiteral("exposure"), 0.2}}},
      });
  if (timed_out_draft.value(QStringLiteral("ok")).toBool() ||
      timed_out_draft.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_draft.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(timed_out_draft.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.draftActive() ||
      controller.cancellationRequested()) {
    return fail("agent draft timeout should record cancellation metadata without leaving an active draft");
  }
  const auto timed_out_manifest =
      read_json_object(timed_out_draft.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out draft manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_batch_validation = controller.validateBatchForSelected();
  if (timed_out_batch_validation.value(QStringLiteral("ok")).toBool() ||
      timed_out_batch_validation.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_validation.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("batch validation timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_batch_manifest =
      read_json_object(timed_out_batch_validation.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_batch_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_batch_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_batch_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out batch validation manifest should persist cancellation reason, failure reason, and timing");
  }
  const auto timed_out_preflight =
      controller.exportPreflightForSelected(QString(), QString(), {});
  if (timed_out_preflight.value(QStringLiteral("ok")).toBool() ||
      timed_out_preflight.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight.value(QStringLiteral("checkedCount"), -1).toInt() != 0 ||
      !QFileInfo(timed_out_preflight.value(QStringLiteral("manifestPath")).toString()).isFile() ||
      controller.cancellationRequested()) {
    return fail("export preflight timeout should stop before processing assets and persist cancellation metadata");
  }
  const auto timed_out_preflight_manifest =
      read_json_object(timed_out_preflight.value(QStringLiteral("manifestPath")).toString());
  if (timed_out_preflight_manifest.value(QStringLiteral("cancellationReason")).toString() != QStringLiteral("timeout") ||
      timed_out_preflight_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      timed_out_preflight_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("timed-out export preflight manifest should persist cancellation reason, failure reason, and timing");
  }
  controller.setRunTimeoutMs(300000);

  catalog_model.replaceAssetsForTesting({
      make_asset(
          QStringLiteral("asset-missing"),
          QDir(runtime_root.path()).filePath(QStringLiteral("fixtures/missing.png")),
          original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.toggleSelectionIndex(0);
  const auto missing_source_validation = controller.validateBatchForSelected();
  if (missing_source_validation.value(QStringLiteral("ok")).toBool() ||
      missing_source_validation.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      !QFileInfo(missing_source_validation.value(QStringLiteral("manifestPath")).toString()).isFile()) {
    return fail("missing source batch validation should fail with persisted failure metadata");
  }
  const auto missing_source_manifest =
      read_json_object(missing_source_validation.value(QStringLiteral("manifestPath")).toString());
  if (missing_source_manifest.value(QStringLiteral("failureReason")).toString().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("errors")).toArray().isEmpty() ||
      missing_source_manifest.value(QStringLiteral("durationMs")).toInt(-1) < 0) {
    return fail("failed batch validation manifest should record failure reason, errors, and timing");
  }

  if (observability_log_path.isEmpty() ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.run_started")) < 4 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.tool_call")) < 4 ||
      observability_event_count(
          observability_log_path,
          QStringLiteral("agent.edit.proof_artifacts_written")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.validation_failed")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.draft_applied")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.commit")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.rollback")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_preflight")) < 1 ||
      observability_event_count(observability_log_path, QStringLiteral("agent.edit.export_queued")) < 1) {
    return fail("agent controller should write observability events for runs, tools, validation, proof, draft, commit, rollback, and export");
  }

  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path, second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);
  controller.setSelectedStyleId(QStringLiteral("moody-test-style-2"));
  controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
  const QJsonObject direct_apply_patch_content{
      {QStringLiteral("decision"), QStringLiteral("edit_intent")},
      {QStringLiteral("summary"), QStringLiteral("Clean balanced edit for the current photo.")},
      {QStringLiteral("styleConstraintCitations"),
       QJsonArray{
           QJsonObject{
               {QStringLiteral("field"), QStringLiteral("style.goal")},
               {QStringLiteral("reason"), QStringLiteral("The selected style asks for a polished natural edit.")},
           },
       }},
      {QStringLiteral("editIntent"),
       QJsonObject{
           {QStringLiteral("goalSummary"), QStringLiteral("Finish the photo with a clean balanced edit.")},
           {QStringLiteral("colorMode"), QStringLiteral("color")},
           {QStringLiteral("contrast"), QStringLiteral("natural")},
           {QStringLiteral("brightness"), QStringLiteral("preserve")},
           {QStringLiteral("palette"), QStringLiteral("neutral")},
           {QStringLiteral("crop"), QStringLiteral("preserve")},
           {QStringLiteral("detail"), QStringLiteral("natural")},
           {QStringLiteral("mood"), QStringLiteral("clean")},
           {QStringLiteral("referenceStrength"), 0.0},
           {QStringLiteral("requestedActions"), QJsonArray{QStringLiteral("tone"), QStringLiteral("color")}},
           {QStringLiteral("requiresApproval"), QJsonArray{}},
           {QStringLiteral("confidence"), 0.9},
       }},
  };
  qputenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON", fake_ollama_response(direct_apply_patch_content));
  const auto direct_apply_report =
      controller.applyEditForScope(QStringLiteral("current-photo"), QStringLiteral("make it look finished"));
  qunsetenv("PHOTO_EDITOR_FAKE_OLLAMA_RESPONSE_JSON");
  if (!direct_apply_report.value(QStringLiteral("ok")).toBool() ||
      direct_apply_report.value(QStringLiteral("mode")).toString() != QStringLiteral("apply-edit") ||
      !direct_apply_report.value(QStringLiteral("committed")).toBool() ||
      direct_apply_report.value(QStringLiteral("committedCount")).toInt() != 1 ||
      !direct_apply_report.value(QStringLiteral("commitReport"))
           .toMap()
           .value(QStringLiteral("previewRefreshRequested"))
           .toBool() ||
      direct_apply_report.value(QStringLiteral("commitReport"))
              .toMap()
              .value(QStringLiteral("selectedCommittedAssetId"))
              .toString() != catalog_model.selectedAssetId() ||
      direct_apply_report.value(QStringLiteral("commitReport"))
              .toMap()
              .value(QStringLiteral("previousAdjustmentPatchByAssetId"))
              .toMap()
              .contains(catalog_model.selectedAssetId()) == false ||
      direct_apply_report.value(QStringLiteral("commitReport"))
              .toMap()
              .value(QStringLiteral("committedAdjustmentPatchByAssetId"))
              .toMap()
              .value(catalog_model.selectedAssetId())
              .toString()
              .isEmpty() ||
      direct_apply_report.value(QStringLiteral("draftActive")).toBool() ||
      controller.draftActive() ||
      controller.status() != QStringLiteral("Applied")) {
    return fail("primary agent apply should plan, apply, and commit a normal single-photo edit without leaving a hidden draft");
  }

  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-empty-recipe"), source_path, QString()),
  });
  catalog_model.selectIndex(0);
  const auto empty_recipe_draft = controller.runDraftForScope(
      QStringLiteral("current-photo"),
      QStringLiteral("Make it super contrasty black and white and give it a better crop."));
  const auto drafted_empty_recipe =
      adjustment_patch_for_asset(
          catalog_model.assetAt(0));
  const auto drafted_empty_state =
      adjustment_state_for_asset(
          catalog_model.assetAt(0));
  const auto empty_recipe_asset_reports =
      empty_recipe_draft.value(QStringLiteral("assetReports")).toList();
  const auto empty_recipe_intent_match = empty_recipe_asset_reports.isEmpty()
                                             ? QVariantMap{}
                                             : empty_recipe_asset_reports.first()
                                                   .toMap()
                                                   .value(QStringLiteral("validationReport"))
                                                   .toMap()
                                                   .value(QStringLiteral("intentMatch"))
                                                   .toMap();
  if (!empty_recipe_draft.value(QStringLiteral("ok")).toBool() ||
      empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("colorMode")).toString() !=
          QStringLiteral("monochrome") ||
      empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("contrast")).toString() !=
          QStringLiteral("strong") ||
      empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("crop")).toString() ==
          QStringLiteral("preserve") ||
      !controller.draftActive() ||
      drafted_empty_recipe.isEmpty() ||
      drafted_empty_state.saturation > -95.0 ||
      drafted_empty_state.contrast < 35.0 ||
      drafted_empty_state.crop_rect_width >= 0.98 ||
      drafted_empty_state.crop_rect_height >= 0.98 ||
      !empty_recipe_intent_match.value(QStringLiteral("ok")).toBool() ||
      empty_recipe_intent_match.value(QStringLiteral("passedCount")).toInt() != 3) {
    std::cerr
        << "empty_recipe_draft ok="
        << empty_recipe_draft.value(QStringLiteral("ok")).toBool()
        << " color_mode="
        << empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("colorMode")).toString().toStdString()
        << " contrast_intent="
        << empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("contrast")).toString().toStdString()
        << " crop_intent="
        << empty_recipe_draft.value(QStringLiteral("editIntent")).toMap().value(QStringLiteral("crop")).toString().toStdString()
        << " draft_active=" << controller.draftActive()
        << " patch_empty=" << drafted_empty_recipe.isEmpty()
        << " saturation=" << drafted_empty_state.saturation
        << " contrast=" << drafted_empty_state.contrast
        << " crop=" << drafted_empty_state.crop_rect_width
        << "x" << drafted_empty_state.crop_rect_height
        << " intent_ok=" << empty_recipe_intent_match.value(QStringLiteral("ok")).toBool()
        << " passed=" << empty_recipe_intent_match.value(QStringLiteral("passedCount")).toInt()
        << " errors="
        << empty_recipe_draft.value(QStringLiteral("errors")).toStringList().join(QStringLiteral("|")).toStdString()
        << '\n';
    return fail("empty-recipe drafts should visibly satisfy monochrome, contrast, and crop instructions");
  }
  const auto empty_recipe_rollback = controller.rollbackDraft();
  if (!empty_recipe_rollback.value(QStringLiteral("ok")).toBool() ||
      controller.draftActive() ||
      adjustment_patch_for_asset(
          catalog_model.assetAt(0))
          .isEmpty()) {
    return fail("empty-recipe drafts should roll back to a valid base recipe");
  }

  photo_editor::desktop::CatalogAssetModel continuity_catalog;
  continuity_catalog.replaceAssetsForTesting({
      make_asset(QStringLiteral("continuity-alpha"), source_path, original_recipe),
      make_asset(QStringLiteral("continuity-beta"), second_source_path, second_original_recipe),
  });
  continuity_catalog.createProject(QStringLiteral("Project Alpha"));
  continuity_catalog.addAssetToProject(
      QStringLiteral("continuity-alpha"),
      QStringLiteral("Project Alpha"));
  continuity_catalog.createProject(QStringLiteral("Project Beta"));
  continuity_catalog.addAssetToProject(
      QStringLiteral("continuity-beta"),
      QStringLiteral("Project Beta"));
  QString alpha_run_id;
  QString beta_run_id;
  {
    photo_editor::desktop::AgentEditController continuity_controller;
    continuity_controller.setCatalogModel(&continuity_catalog);

    continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
    continuity_catalog.clearSelection();
    continuity_catalog.selectIndex(0);
    continuity_catalog.toggleSelectionIndex(0);
    continuity_controller.setSelectedStyleId(QStringLiteral("moody-test-style"));
    continuity_controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
    continuity_controller.setUserInstructions(
        QStringLiteral("Use the restrained approved contrast for Project Alpha."));
    if (continuity_controller.approvedProjectTaste()
            .value(QStringLiteral("available"))
            .toBool()) {
      return fail("a new project should not inherit approved taste from another context");
    }
    const auto alpha_draft = continuity_controller.applyDraftRecipePatchToSelected(
        QVariantMap{
            {QStringLiteral("globalTone"),
             QVariantMap{{QStringLiteral("exposure"), 0.18}}},
        });
    const auto alpha_revised = continuity_controller.reviseDraft(
        QStringLiteral("Keep skin less warm, protect highlights, and leave the crop looser."));
    continuity_catalog.setProjectFilter(QStringLiteral("Project Beta"));
    const auto alpha_commit = continuity_controller.commitDraft(true);
    alpha_run_id = alpha_commit.value(QStringLiteral("runId")).toString();
    const auto beta_taste_after_alpha_commit =
        continuity_controller.approvedProjectTaste();
    const auto beta_history_after_alpha_commit = continuity_controller.runHistory();
    continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
    const auto alpha_taste = continuity_controller.approvedProjectTaste();
    if (!alpha_draft.value(QStringLiteral("ok")).toBool() ||
        !alpha_revised.value(QStringLiteral("ok")).toBool() ||
        alpha_revised.value(QStringLiteral("pendingProjectCorrection"))
            .toMap()
            .value(QStringLiteral("feedback"))
            .toString() !=
            QStringLiteral("Keep skin less warm, protect highlights, and leave the crop looser.") ||
        !alpha_commit.value(QStringLiteral("ok")).toBool() ||
        alpha_run_id.isEmpty() ||
        continuity_controller.runHistory().isEmpty() ||
        alpha_commit.value(QStringLiteral("continuityProjectKey")).toString() !=
            QStringLiteral("Project Alpha") ||
        !beta_history_after_alpha_commit.isEmpty() ||
        beta_taste_after_alpha_commit.value(QStringLiteral("available")).toBool() ||
        !alpha_taste.value(QStringLiteral("available")).toBool() ||
        alpha_taste.value(QStringLiteral("projectKey")).toString() !=
            QStringLiteral("Project Alpha") ||
        alpha_taste.value(QStringLiteral("approvalCount")).toInt() != 1 ||
        alpha_taste.value(QStringLiteral("correctionCount")).toInt() != 1 ||
        alpha_taste.value(QStringLiteral("corrections"))
                .toList()
                .first()
                .toMap()
                .value(QStringLiteral("feedback"))
                .toString() !=
            QStringLiteral("Keep skin less warm, protect highlights, and leave the crop looser.") ||
        alpha_taste.value(QStringLiteral("exemplars")).toList()
            .first()
            .toMap()
            .value(QStringLiteral("runId"))
            .toString() != alpha_run_id) {
      std::cerr << "alpha continuity errors="
                << alpha_draft.value(QStringLiteral("errors"))
                       .toStringList()
                       .join(QStringLiteral("|"))
                       .toStdString()
                << " asset_count=" << continuity_catalog.assetCount()
                << " selected_count=" << continuity_catalog.selectedAssetCount()
                << " run_id=" << alpha_run_id.toStdString() << '\n';
      return fail("project continuity setup should record the Alpha project run");
    }

    continuity_catalog.setProjectFilter(QStringLiteral("Project Beta"));
    if (!continuity_controller.runHistory().isEmpty() ||
        continuity_controller.approvedProjectTaste()
            .value(QStringLiteral("available"))
            .toBool()) {
      return fail("a project without continuity should not inherit another project's history or approved taste");
    }
    continuity_catalog.clearSelection();
    continuity_catalog.selectIndex(0);
    continuity_catalog.toggleSelectionIndex(0);
    continuity_controller.setSelectedStyleId(QStringLiteral("warm-editorial-wedding"));
    continuity_controller.setSelectedModelId(QStringLiteral("ollama-qwen-test"));
    continuity_controller.setUserInstructions(
        QStringLiteral("Use the approved warm highlight treatment for Project Beta."));
    const auto beta_rejected_draft =
        continuity_controller.applyDraftRecipePatchToSelected(
            QVariantMap{
                {QStringLiteral("globalTone"),
                 QVariantMap{{QStringLiteral("exposure"), 0.22}}},
            });
    const auto beta_discard =
        continuity_controller.rollbackDraft(QStringLiteral("user_discarded"));
    const auto beta_taste_after_discard =
        continuity_controller.approvedProjectTaste();
    const auto beta_draft = continuity_controller.applyDraftRecipePatchToSelected(
        QVariantMap{
            {QStringLiteral("globalTone"),
             QVariantMap{{QStringLiteral("exposure"), -0.08}}},
        });
    const auto beta_commit = continuity_controller.commitDraft(true);
    beta_run_id = beta_commit.value(QStringLiteral("runId")).toString();
    const auto beta_taste = continuity_controller.approvedProjectTaste();
    if (!beta_rejected_draft.value(QStringLiteral("ok")).toBool() ||
        !beta_discard.value(QStringLiteral("ok")).toBool() ||
        !beta_discard.value(QStringLiteral("rejectedProjectDraftRemembered")).toBool() ||
        beta_taste_after_discard.value(QStringLiteral("approvalCount")).toInt() != 0 ||
        beta_taste_after_discard.value(QStringLiteral("rejectionCount")).toInt() != 1 ||
        beta_taste_after_discard.value(QStringLiteral("rejectedDrafts"))
            .toList()
            .first()
            .toMap()
            .value(QStringLiteral("inferredReason"))
            .toBool() ||
        !beta_draft.value(QStringLiteral("ok")).toBool() ||
        !beta_commit.value(QStringLiteral("ok")).toBool() ||
        beta_run_id.isEmpty() ||
        beta_taste.value(QStringLiteral("projectKey")).toString() !=
            QStringLiteral("Project Beta") ||
        beta_taste.value(QStringLiteral("approvalCount")).toInt() != 1 ||
        beta_taste.value(QStringLiteral("correctionCount")).toInt() != 0 ||
        beta_taste.value(QStringLiteral("rejectionCount")).toInt() != 1) {
      std::cerr << "beta continuity errors="
                << beta_draft.value(QStringLiteral("errors"))
                       .toStringList()
                       .join(QStringLiteral("|"))
                       .toStdString()
                << " asset_count=" << continuity_catalog.assetCount()
                << " selected_count=" << continuity_catalog.selectedAssetCount()
                << " run_id=" << beta_run_id.toStdString()
                << " project="
                << beta_taste.value(QStringLiteral("projectKey")).toString().toStdString()
                << " approvals="
                << beta_taste.value(QStringLiteral("approvalCount")).toInt()
                << " corrections="
                << beta_taste.value(QStringLiteral("correctionCount")).toInt()
                << " rejections="
                << beta_taste.value(QStringLiteral("rejectionCount")).toInt()
                << " discard_ok="
                << beta_discard.value(QStringLiteral("ok")).toBool()
                << " discard_remembered="
                << beta_discard.value(QStringLiteral("rejectedProjectDraftRemembered")).toBool()
                << " discard_approvals="
                << beta_taste_after_discard.value(QStringLiteral("approvalCount")).toInt()
                << " discard_rejections="
                << beta_taste_after_discard.value(QStringLiteral("rejectionCount")).toInt()
                << '\n';
      return fail("project continuity setup should record the Beta project run");
    }

    continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
    const auto restored_alpha_history = continuity_controller.runHistory();
    const auto restored_alpha_taste = continuity_controller.approvedProjectTaste();
    if (continuity_controller.selectedStyleId() != QStringLiteral("moody-test-style") ||
        continuity_controller.selectedModelId() != QStringLiteral("rulebook-deterministic") ||
        restored_alpha_history.isEmpty() ||
        restored_alpha_history.first().toMap().value(QStringLiteral("runId")).toString() != alpha_run_id ||
        restored_alpha_taste.value(QStringLiteral("approvalCount")).toInt() != 1 ||
        restored_alpha_taste.value(QStringLiteral("correctionCount")).toInt() != 1 ||
        restored_alpha_taste.value(QStringLiteral("rejectionCount")).toInt() != 0 ||
        restored_alpha_taste.value(QStringLiteral("exemplars"))
                .toList()
                .first()
                .toMap()
                .value(QStringLiteral("runId"))
                .toString() != alpha_run_id) {
      return fail("switching projects should restore Alpha style, model, run history, and approved taste");
    }
  }

  {
    photo_editor::desktop::AgentEditController relaunched_controller;
    relaunched_controller.setCatalogModel(&continuity_catalog);
    const auto relaunched_alpha_history = relaunched_controller.runHistory();
    const auto relaunched_alpha_taste = relaunched_controller.approvedProjectTaste();
    if (relaunched_controller.selectedStyleId() != QStringLiteral("moody-test-style") ||
        relaunched_controller.selectedModelId() != QStringLiteral("rulebook-deterministic") ||
        relaunched_alpha_history.isEmpty() ||
        relaunched_alpha_history.first().toMap().value(QStringLiteral("runId")).toString() != alpha_run_id ||
        relaunched_alpha_taste.value(QStringLiteral("approvalCount")).toInt() != 1 ||
        relaunched_alpha_taste.value(QStringLiteral("correctionCount")).toInt() != 1 ||
        relaunched_alpha_taste.value(QStringLiteral("rejectionCount")).toInt() != 0) {
      return fail("agent history and approved taste should survive controller relaunch for the active project");
    }

    continuity_catalog.setProjectFilter(QStringLiteral("Project Beta"));
    const auto relaunched_beta_history = relaunched_controller.runHistory();
    const auto relaunched_beta_taste = relaunched_controller.approvedProjectTaste();
    if (relaunched_controller.selectedStyleId() != QStringLiteral("warm-editorial-wedding") ||
        relaunched_controller.selectedModelId() != QStringLiteral("ollama-qwen-test") ||
        relaunched_beta_history.isEmpty() ||
        relaunched_beta_history.first().toMap().value(QStringLiteral("runId")).toString() != beta_run_id ||
        relaunched_beta_taste.value(QStringLiteral("approvalCount")).toInt() != 1 ||
        relaunched_beta_taste.value(QStringLiteral("correctionCount")).toInt() != 0 ||
        relaunched_beta_taste.value(QStringLiteral("rejectionCount")).toInt() != 1 ||
        relaunched_beta_taste.value(QStringLiteral("exemplars"))
                .toList()
                .first()
                .toMap()
                .value(QStringLiteral("runId"))
                .toString() != beta_run_id) {
      return fail("agent continuity should keep each project's style, model, history, and approved taste isolated");
    }
  }

  continuity_catalog.createProject(QStringLiteral("Project Taste Controls"));
  continuity_catalog.addAssetToProject(
      QStringLiteral("continuity-alpha"),
      QStringLiteral("Project Taste Controls"));
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  continuity_catalog.clearSelection();
  continuity_catalog.selectIndex(0);
  continuity_catalog.toggleSelectionIndex(0);
  photo_editor::desktop::AgentEditController taste_controls_controller;
  taste_controls_controller.setCatalogModel(&continuity_catalog);
  taste_controls_controller.setSelectedStyleId(
      QStringLiteral("moody-test-style"));
  taste_controls_controller.setSelectedModelId(
      QStringLiteral("rulebook-deterministic"));
  taste_controls_controller.setUserInstructions(
      QStringLiteral("Use a restrained project-local contrast treatment."));
  const auto taste_controls_draft =
      taste_controls_controller.applyDraftRecipePatchToSelected(
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{{QStringLiteral("contrast"), 6.0}}},
          });
  const auto taste_controls_commit =
      taste_controls_controller.commitDraft(true);
  const auto north_star_before =
      taste_controls_controller.projectNorthStarStatus();
  const auto invalid_north_star =
      taste_controls_controller.saveProjectNorthStar(
          QStringLiteral("Too short"));
  const auto north_star_active_draft =
      taste_controls_controller.applyDraftRecipePatchToSelected(
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{{QStringLiteral("exposure"), 0.04}}},
          });
  const auto north_star_draft_recipe = adjustment_patch_for_asset(
      continuity_catalog.assetAt(0));
  const auto north_star_brief = QStringLiteral(
      "Keep the ceremony intimate and documentary; protect skin, highlights, and unposed moments across the final delivery.");
  const auto saved_north_star =
      taste_controls_controller.saveProjectNorthStar(north_star_brief);
  const auto north_star_after_save =
      taste_controls_controller.projectNorthStarStatus();
  const auto north_star_taste =
      taste_controls_controller.approvedProjectTaste();
  const auto north_star_packet =
      taste_controls_controller.memoryPacketForPhase(QStringLiteral("sample"));
  if (north_star_before.value(QStringLiteral("available")).toBool() ||
      invalid_north_star.value(QStringLiteral("ok")).toBool() ||
      !north_star_active_draft.value(QStringLiteral("ok")).toBool() ||
      !saved_north_star.value(QStringLiteral("ok")).toBool() ||
      saved_north_star.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-project-north-star-save-v1") ||
      !saved_north_star.value(QStringLiteral("activeDraftUnaffected")).toBool() ||
      !taste_controls_controller.draftActive() ||
      adjustment_patch_for_asset(continuity_catalog.assetAt(0)) !=
          north_star_draft_recipe ||
      !north_star_after_save.value(QStringLiteral("available")).toBool() ||
      north_star_after_save.value(QStringLiteral("brief")).toString() !=
          north_star_brief ||
      north_star_after_save.value(QStringLiteral("northStar"))
              .toMap()
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-project-north-star-memory-v1") ||
      north_star_after_save.value(QStringLiteral("northStar"))
              .toMap()
              .value(QStringLiteral("digestSha256"))
              .toString()
              .size() != 64 ||
      !north_star_taste.value(QStringLiteral("projectNorthStarAvailable"))
           .toBool() ||
      north_star_taste.value(QStringLiteral("projectNorthStar"))
              .toMap()
              .value(QStringLiteral("brief"))
              .toString() != north_star_brief ||
      !north_star_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("project-brief")) ||
      north_star_packet.value(QStringLiteral("projectNorthStar"))
              .toMap()
              .value(QStringLiteral("brief"))
              .toString() != north_star_brief ||
      north_star_packet.value(QStringLiteral("relevancePolicy"))
              .toMap()
              .value(QStringLiteral("projectNorthStarPrecedence"))
              .toString() !=
          QStringLiteral(
              "current-request-then-style-and-safety-then-north-star") ||
      north_star_packet.value(QStringLiteral("relevancePolicy"))
          .toMap()
          .value(QStringLiteral("projectNorthStarAutomaticRecipeInfluence"))
          .toBool()) {
    return fail("an explicit Project North Star should be project-local, prompt-visible, precedence-bounded, and unable to mutate an active draft");
  }
  const auto north_star_save_rollback =
      taste_controls_controller.rollbackDraft(
          QStringLiteral("north-star-save-contract"));
  const auto north_star_future_draft =
      taste_controls_controller.runDraftForScope(
          QStringLiteral("current-photo"),
          QStringLiteral(
              "Keep the photo natural and protect highlights while following the assignment direction."));
  const auto north_star_draft_receipt =
      north_star_future_draft
          .value(QStringLiteral("projectNorthStarReceipt"))
          .toMap();
  const auto north_star_receipt_rollback =
      taste_controls_controller.rollbackDraft(
          QStringLiteral("north-star-receipt-contract"));
  if (!north_star_future_draft.value(QStringLiteral("ok")).toBool() ||
      north_star_draft_receipt.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-project-north-star-draft-receipt-v1") ||
      north_star_draft_receipt.value(QStringLiteral("brief")).toString() !=
          north_star_brief ||
      !north_star_draft_receipt
           .value(QStringLiteral("planningConsidered"))
           .toBool() ||
      !north_star_draft_receipt
           .value(QStringLiteral("qualitativePlanningInfluence"))
           .toBool() ||
      !north_star_draft_receipt.value(QStringLiteral("proofValidated"))
           .toBool() ||
      north_star_draft_receipt
              .value(QStringLiteral("proofValidatedAssetCount"))
              .toInt() != 1 ||
      north_star_draft_receipt
          .value(QStringLiteral("proofCritiqueConsidered"))
          .toBool() ||
      north_star_draft_receipt.value(QStringLiteral("proofReviewMode"))
              .toString() !=
          QStringLiteral("deterministic-validation-only") ||
      north_star_draft_receipt.value(QStringLiteral("alignmentClaimed"))
          .toBool() ||
      !north_star_draft_receipt
           .value(QStringLiteral("requiresPhotographerJudgment"))
           .toBool() ||
      north_star_draft_receipt
          .value(QStringLiteral("directNumericRecipeAuthority"))
          .toBool() ||
      !north_star_draft_receipt
           .value(QStringLiteral("currentRequestWins"))
           .toBool() ||
      !north_star_receipt_rollback.value(QStringLiteral("ok")).toBool()) {
    return fail("a future draft should visibly prove the exact North Star was considered, distinguish deterministic proof from model critique, and leave final fit to the photographer");
  }
  continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
  const auto other_project_north_star =
      taste_controls_controller.projectNorthStarStatus();
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  photo_editor::desktop::AgentEditController north_star_relaunch;
  north_star_relaunch.setCatalogModel(&continuity_catalog);
  const auto relaunched_north_star =
      north_star_relaunch.projectNorthStarStatus();
  continuity_catalog.clearSelection();
  continuity_catalog.selectIndex(0);
  continuity_catalog.toggleSelectionIndex(0);
  const auto north_star_forget_draft =
      taste_controls_controller.applyDraftRecipePatchToSelected(
          QVariantMap{
              {QStringLiteral("globalTone"),
               QVariantMap{{QStringLiteral("exposure"), -0.03}}},
          });
  const auto north_star_recipe_before_forget = adjustment_patch_for_asset(
      continuity_catalog.assetAt(0));
  const auto unconfirmed_north_star_forget =
      taste_controls_controller.forgetProjectNorthStar(false);
  const auto north_star_after_unconfirmed_forget =
      taste_controls_controller.projectNorthStarStatus();
  const auto confirmed_north_star_forget =
      taste_controls_controller.forgetProjectNorthStar(true);
  const auto north_star_after_confirmed_forget =
      taste_controls_controller.projectNorthStarStatus();
  const auto north_star_packet_after_forget =
      taste_controls_controller.memoryPacketForPhase(QStringLiteral("sample"));
  if (!north_star_save_rollback.value(QStringLiteral("ok")).toBool() ||
      other_project_north_star.value(QStringLiteral("available")).toBool() ||
      !relaunched_north_star.value(QStringLiteral("available")).toBool() ||
      relaunched_north_star.value(QStringLiteral("brief")).toString() !=
          north_star_brief ||
      !north_star_forget_draft.value(QStringLiteral("ok")).toBool() ||
      unconfirmed_north_star_forget.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_north_star_forget
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      !north_star_after_unconfirmed_forget
           .value(QStringLiteral("available"))
           .toBool() ||
      !confirmed_north_star_forget.value(QStringLiteral("ok")).toBool() ||
      !confirmed_north_star_forget.value(QStringLiteral("forgotten")).toBool() ||
      !confirmed_north_star_forget
           .value(QStringLiteral("activeDraftUnaffected"))
           .toBool() ||
      north_star_after_confirmed_forget
          .value(QStringLiteral("available"))
          .toBool() ||
      north_star_packet_after_forget.value(QStringLiteral("includedMemoryKinds"))
          .toStringList()
          .contains(QStringLiteral("project-brief")) ||
      !north_star_packet_after_forget.value(QStringLiteral("projectNorthStar"))
           .toMap()
           .isEmpty() ||
      !taste_controls_controller.draftActive() ||
      adjustment_patch_for_asset(continuity_catalog.assetAt(0)) !=
          north_star_recipe_before_forget) {
    return fail("Project North Star memory should stay isolated, persist across relaunch, require confirmed forgetting, and leave the active draft unchanged");
  }
  const auto north_star_forget_rollback =
      taste_controls_controller.rollbackDraft(
          QStringLiteral("north-star-forget-contract"));
  if (!north_star_forget_rollback.value(QStringLiteral("ok")).toBool()) {
    return fail("Project North Star verification should leave the continuity fixture clean for later taste controls");
  }
  const auto taste_before_forget =
      taste_controls_controller.approvedProjectTaste();
  QVariantMap approved_look_evidence;
  for (const auto& evidence_value :
       taste_before_forget.value(QStringLiteral("evidence")).toList()) {
    const auto evidence = evidence_value.toMap();
    if (evidence.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("approved-look")) {
      approved_look_evidence = evidence;
      break;
    }
  }
  const auto committed_taste_recipe = adjustment_patch_for_asset(
      continuity_catalog.assetAt(0));
  const auto invalid_taste_rename =
      taste_controls_controller.renameProjectTaste(QStringLiteral(" "));
  const auto taste_rename = taste_controls_controller.renameProjectTaste(
      QStringLiteral("  Restrained Ceremony Story  "));
  const auto taste_after_rename =
      taste_controls_controller.approvedProjectTaste();
  const auto unconfirmed_taste_forget =
      taste_controls_controller.forgetProjectTasteEvidence(
          QStringLiteral("approved-look"),
          approved_look_evidence.value(QStringLiteral("id")).toString(),
          false);
  const auto taste_after_unconfirmed_forget =
      taste_controls_controller.approvedProjectTaste();
  const auto confirmed_taste_forget =
      taste_controls_controller.forgetProjectTasteEvidence(
          QStringLiteral("approved-look"),
          approved_look_evidence.value(QStringLiteral("id")).toString(),
          true);
  const auto taste_after_confirmed_forget =
      taste_controls_controller.approvedProjectTaste();
  const auto recipe_after_taste_forget = adjustment_patch_for_asset(
      continuity_catalog.assetAt(0));
  continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
  const auto alpha_taste_after_other_project_forget =
      taste_controls_controller.approvedProjectTaste();
  if (!taste_controls_draft.value(QStringLiteral("ok")).toBool() ||
      !taste_controls_commit.value(QStringLiteral("ok")).toBool() ||
      taste_before_forget.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-project-taste-mirror-v2") ||
      !taste_before_forget.value(QStringLiteral("editable")).toBool() ||
      !taste_before_forget.value(QStringLiteral("renamable")).toBool() ||
      !taste_before_forget
           .value(QStringLiteral("visualExemplarsByCatalogId"))
           .toBool() ||
      taste_before_forget.value(QStringLiteral("visualExemplarCount")).toInt() != 1 ||
      !taste_before_forget
           .value(QStringLiteral("forgetRequiresConfirmation"))
           .toBool() ||
      taste_before_forget.value(QStringLiteral("evidenceCount")).toInt() != 1 ||
      approved_look_evidence.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-project-taste-evidence-v1") ||
      approved_look_evidence.value(QStringLiteral("id")).toString().isEmpty() ||
      approved_look_evidence.value(QStringLiteral("assetIds"))
              .toStringList() !=
          QStringList{QStringLiteral("continuity-alpha")} ||
      invalid_taste_rename.value(QStringLiteral("ok")).toBool() ||
      !taste_rename.value(QStringLiteral("ok")).toBool() ||
      taste_rename.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-project-taste-rename-v1") ||
      !taste_rename.value(QStringLiteral("activeDraftUnaffected")).toBool() ||
      taste_after_rename.value(QStringLiteral("name")).toString() !=
          QStringLiteral("Restrained Ceremony Story") ||
      taste_after_rename.value(QStringLiteral("approvalCount")).toInt() != 1 ||
      unconfirmed_taste_forget.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_taste_forget
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      taste_after_unconfirmed_forget.value(QStringLiteral("approvalCount")).toInt() != 1 ||
      !confirmed_taste_forget.value(QStringLiteral("ok")).toBool() ||
      !confirmed_taste_forget.value(QStringLiteral("forgotten")).toBool() ||
      !confirmed_taste_forget
           .value(QStringLiteral("activeDraftUnaffected"))
           .toBool() ||
      confirmed_taste_forget.value(QStringLiteral("memoryEffect")).toString() !=
          QStringLiteral("exclude-from-future-project-planning") ||
      confirmed_taste_forget.value(QStringLiteral("removedCount")).toInt() != 1 ||
      taste_after_confirmed_forget.value(QStringLiteral("available")).toBool() ||
      taste_after_confirmed_forget.value(QStringLiteral("evidenceCount")).toInt() != 0 ||
      committed_taste_recipe != recipe_after_taste_forget ||
      alpha_taste_after_other_project_forget.value(QStringLiteral("approvalCount")).toInt() != 1 ||
      alpha_taste_after_other_project_forget.value(QStringLiteral("correctionCount")).toInt() != 1) {
    return fail("taste mirror evidence should be project-local, inspectable, confirmation-gated, and forgettable without changing any existing edit");
  }

  QTemporaryDir calibration_review_root;
  if (!calibration_review_root.isValid() ||
      !QDir().mkpath(
          QDir(calibration_review_root.path()).filePath(QStringLiteral("proofs")))) {
    return fail("calibration review packet fixture should create its local proof directory");
  }
  const auto before_review_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("proofs/case-before.bin"));
  const auto after_review_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("proofs/case-after.bin"));
  const auto write_review_file = [](const QString& path, const QByteArray& bytes) {
    QFile file(path);
    return file.open(QIODevice::WriteOnly) &&
           file.write(bytes) == bytes.size();
  };
  const auto bytes_sha256 = [](const QByteArray& bytes) {
    return QString::fromLatin1(
        QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
  };
  const QByteArray before_review_bytes("blind-before-proof");
  const QByteArray after_review_bytes("blind-after-proof");
  if (!write_review_file(before_review_path, before_review_bytes) ||
      !write_review_file(after_review_path, after_review_bytes)) {
    return fail("calibration review packet fixture should write both proofs");
  }
  const QVariantMap review_case{
      {QStringLiteral("caseId"), QStringLiteral("portrait-natural")},
      {QStringLiteral("caseKind"), QStringLiteral("global_edit")},
      {QStringLiteral("beforeProof"), QStringLiteral("proofs/case-before.bin")},
      {QStringLiteral("afterProof"), QStringLiteral("proofs/case-after.bin")},
      {QStringLiteral("beforeProofSha256"),
       bytes_sha256(before_review_bytes)},
      {QStringLiteral("afterProofSha256"),
       bytes_sha256(after_review_bytes)},
  };
  const auto calibration_source_manifest_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("source-manifest.json"));
  const auto calibration_machine_report_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("source-machine-report.json"));
  const auto calibration_source_manifest_bytes =
      QJsonDocument::fromVariant(QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-calibration-manifest-test-v1")},
          {QStringLiteral("assets"),
           QVariantList{QVariantMap{
               {QStringLiteral("asset_id"),
                QStringLiteral("calibration-portrait")},
               {QStringLiteral("agent_cases"),
                QVariantList{QVariantMap{
                    {QStringLiteral("case_id"),
                     QStringLiteral("portrait-natural")},
                    {QStringLiteral("kind"),
                     QStringLiteral("global_edit")},
                    {QStringLiteral("human_review"),
                     QVariantMap{{QStringLiteral("status"),
                                  QStringLiteral("pending")}}},
                }}},
           }}},
      }).toJson(QJsonDocument::Indented);
  const auto calibration_machine_report_bytes =
      QJsonDocument::fromVariant(QVariantMap{
          {QStringLiteral("contract"),
           QStringLiteral("agent-calibration-machine-report-test-v1")},
          {QStringLiteral("cases"),
           QVariantList{QVariantMap{
               {QStringLiteral("caseId"),
                QStringLiteral("portrait-natural")},
               {QStringLiteral("machinePass"), true},
               {QStringLiteral("pixelIntentAlignment"),
                QVariantMap{
                    {QStringLiteral("available"), true},
                    {QStringLiteral("checks"),
                     QVariantList{QVariantMap{
                         {QStringLiteral("name"),
                          QStringLiteral("brighter")},
                         {QStringLiteral("delta"), 0.12},
                         {QStringLiteral("threshold"), 0.08},
                         {QStringLiteral("passed"), true},
                     }}},
                }},
               {QStringLiteral("retouchQuality"),
                QVariantMap{{QStringLiteral("available"), false}}},
           }}},
      }).toJson(QJsonDocument::Indented);
  if (!write_review_file(calibration_source_manifest_path,
                         calibration_source_manifest_bytes) ||
      !write_review_file(calibration_machine_report_path,
                         calibration_machine_report_bytes)) {
    return fail("calibration finalization fixture should write its immutable source evidence");
  }
  QVariantMap calibration_review_contract{
      {QStringLiteral("contract"), QStringLiteral("agent-human-review-packet-v2")},
      {QStringLiteral("version"), 2},
      {QStringLiteral("manifestSha256"),
       bytes_sha256(calibration_source_manifest_bytes)},
      {QStringLiteral("machineReportSha256"),
       bytes_sha256(calibration_machine_report_bytes)},
      {QStringLiteral("manifestCaseCount"), 1},
      {QStringLiteral("reportCaseCount"), 1},
      {QStringLiteral("humanDecisionRequired"), true},
      {QStringLiteral("machineResultCannotSetHumanRating"), true},
      {QStringLiteral("blindReviewRequired"), true},
      {QStringLiteral("machineEvidenceEmbeddedInReviewPage"), false},
      {QStringLiteral("sourcePathsEmbeddedInReviewPage"), false},
      {QStringLiteral("reviewExperienceContract"),
       QStringLiteral("agent-blind-light-table-v1")},
      {QStringLiteral("localDraftAutosave"), true},
      {QStringLiteral("localDraftContainsProofPixels"), false},
      {QStringLiteral("directBeforeAfterComparison"), true},
      {QStringLiteral("sourceManifest"),
       calibration_source_manifest_path},
      {QStringLiteral("sourceMachineReport"),
       calibration_machine_report_path},
      {QStringLiteral("cases"), QVariantList{review_case}},
  };
  const auto calibration_review_digest = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument::fromVariant(calibration_review_contract)
              .toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
  calibration_review_contract.insert(
      QStringLiteral("packetDigestSha256"), calibration_review_digest);
  const auto review_contract_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("review-contract.json"));
  const auto review_page_path =
      QDir(calibration_review_root.path()).filePath(QStringLiteral("index.html"));
  const auto review_contract_bytes =
      QJsonDocument::fromVariant(calibration_review_contract)
          .toJson(QJsonDocument::Indented);
  const auto review_page_bytes =
      QStringLiteral(
          "<!doctype html><title>Blind review</title>"
          "<div>agent-blind-light-table-v1</div><div>%1</div>")
          .arg(calibration_review_digest)
          .toUtf8();
  if (!write_review_file(review_contract_path, review_contract_bytes) ||
      !write_review_file(review_page_path, review_page_bytes)) {
    return fail("calibration review packet fixture should write its signed contract and page");
  }
  const auto calibration_review_ready =
      taste_controls_controller.openPhotographerCalibrationReviewPacketFile(
          QUrl::fromLocalFile(review_page_path).toString(), true);
  if (!calibration_review_ready.value(QStringLiteral("ok")).toBool() ||
      !calibration_review_ready.value(QStringLiteral("validated")).toBool() ||
      !calibration_review_ready.value(QStringLiteral("openReady")).toBool() ||
      calibration_review_ready.value(QStringLiteral("opened")).toBool() ||
      calibration_review_ready.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-calibration-review-launch-v1") ||
      calibration_review_ready.value(QStringLiteral("caseCount")).toInt() != 1 ||
      calibration_review_ready.value(QStringLiteral("proofHashesVerified")).toInt() != 2 ||
      !calibration_review_ready
           .value(QStringLiteral("requiresPhotographerJudgment"))
           .toBool() ||
      calibration_review_ready
          .value(QStringLiteral("machineCanCompleteRatings"))
          .toBool() ||
      calibration_review_ready
          .value(QStringLiteral("durableMemoryWritten"))
          .toBool()) {
    return fail("a signed blind-review packet should verify every proof before an explicit local handoff");
  }
  const auto headless_clipboard_finalize =
      taste_controls_controller.finalizePhotographerCalibrationReviewClipboard(
          QUrl::fromLocalFile(review_page_path).toString());
  if (headless_clipboard_finalize.value(QStringLiteral("ok")).toBool() ||
      headless_clipboard_finalize
          .value(QStringLiteral("machineCanCompleteRatings"))
          .toBool() ||
      headless_clipboard_finalize
          .value(QStringLiteral("ratingsFileRequired"))
          .toBool() ||
      headless_clipboard_finalize
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral(
              "agent-photographer-calibration-clipboard-handoff-v1") ||
      !headless_clipboard_finalize
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("clipboard is unavailable"))) {
    return fail("clipboard calibration finalization should fail closed outside a photographer-facing GUI session");
  }
  const auto calibration_ratings_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("photographer-ratings.json"));
  QVariantMap calibration_ratings{
      {QStringLiteral("contract"),
       QStringLiteral("agent-human-review-ratings-v2")},
      {QStringLiteral("blindReview"), true},
      {QStringLiteral("packetDigestSha256"), calibration_review_digest},
      {QStringLiteral("manifestSha256"),
       bytes_sha256(calibration_source_manifest_bytes)},
      {QStringLiteral("machineReportSha256"),
       bytes_sha256(calibration_machine_report_bytes)},
      {QStringLiteral("reviewer"), QStringLiteral("Fixture Photographer")},
      {QStringLiteral("reviewedAt"),
       QStringLiteral("2026-08-26T14:15:16.123-06:00")},
      {QStringLiteral("cases"),
       QVariantMap{
           {QStringLiteral("portrait-natural"),
            QVariantMap{
                {QStringLiteral("status"), QStringLiteral("pass")},
                {QStringLiteral("rating"), 5},
                {QStringLiteral("dimensions"),
                 QVariantMap{
                     {QStringLiteral("intentMatch"), 5},
                     {QStringLiteral("naturalness"), 4},
                     {QStringLiteral("artifactFreedom"), 5},
                     {QStringLiteral("protectedFidelity"), 5},
                 }},
                {QStringLiteral("issueTags"), QVariantList{}},
                {QStringLiteral("notes"),
                 QStringLiteral("Natural color and protected highlights are ready to deliver.")},
            }},
       }},
  };
  const auto calibration_ratings_bytes =
      QJsonDocument::fromVariant(calibration_ratings)
          .toJson(QJsonDocument::Indented);
  if (!write_review_file(calibration_ratings_path,
                         calibration_ratings_bytes)) {
    return fail("calibration finalization fixture should write exported photographer ratings");
  }
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  const auto calibration_finalize =
      taste_controls_controller.finalizePhotographerCalibrationReviewFiles(
          QUrl::fromLocalFile(calibration_ratings_path).toString(),
          QUrl::fromLocalFile(review_page_path).toString());
  const auto native_calibration_card =
      taste_controls_controller.photographerCalibrationProfile();
  const auto native_calibration_coach =
      taste_controls_controller.photographerCalibrationCoach();
  const auto reviewed_manifest_path =
      calibration_finalize.value(QStringLiteral("reviewedManifestPath"))
          .toString();
  const auto native_profile_path =
      calibration_finalize.value(QStringLiteral("profilePath")).toString();
  const auto reviewed_manifest_document = QJsonDocument::fromJson(
      read_file_bytes(reviewed_manifest_path));
  const auto reviewed_human =
      reviewed_manifest_document.object()
          .value(QStringLiteral("assets"))
          .toArray()
          .at(0)
          .toObject()
          .value(QStringLiteral("agent_cases"))
          .toArray()
          .at(0)
          .toObject()
          .value(QStringLiteral("human_review"))
          .toObject();
  const auto native_profile_document =
      QJsonDocument::fromJson(read_file_bytes(native_profile_path));
  const auto native_profile_retouch =
      native_profile_document.object()
          .value(QStringLiteral("cases"))
          .toArray()
          .at(0)
          .toObject()
          .value(QStringLiteral("retouchQuality"));
  const auto repeated_calibration_finalize =
      taste_controls_controller.finalizePhotographerCalibrationReviewFiles(
          calibration_ratings_path, review_contract_path);
  if (!calibration_finalize.value(QStringLiteral("ok")).toBool() ||
      calibration_finalize.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-calibration-native-finalize-v1") ||
      !calibration_finalize.value(QStringLiteral("profileImported")).toBool() ||
      calibration_finalize.value(QStringLiteral("caseCount")).toInt() != 1 ||
      calibration_finalize.value(QStringLiteral("humanPassCount")).toInt() != 1 ||
      calibration_finalize.value(QStringLiteral("humanFailCount")).toInt() != 0 ||
      calibration_finalize.value(QStringLiteral("thresholdReviewRequired")).toBool() ||
      calibration_finalize.value(QStringLiteral("sourceManifestOverwritten")).toBool() ||
      calibration_finalize
          .value(QStringLiteral("automaticThresholdMutationAllowed"))
          .toBool() ||
      calibration_finalize
          .value(QStringLiteral("professionalReadinessClaimed"))
          .toBool() ||
      calibration_source_manifest_bytes !=
          read_file_bytes(calibration_source_manifest_path) ||
      calibration_machine_report_bytes !=
          read_file_bytes(calibration_machine_report_path) ||
      !repeated_calibration_finalize.value(QStringLiteral("ok")).toBool() ||
      repeated_calibration_finalize
              .value(QStringLiteral("reviewedManifestPath"))
              .toString() != reviewed_manifest_path ||
      repeated_calibration_finalize.value(QStringLiteral("profilePath"))
              .toString() != native_profile_path ||
      reviewed_human.value(QStringLiteral("status")).toString() !=
          QStringLiteral("pass") ||
      reviewed_human.value(QStringLiteral("reviewer")).toString() !=
          QStringLiteral("Fixture Photographer") ||
      !native_profile_document.isObject() ||
      native_profile_document.object()
              .value(QStringLiteral("contract"))
              .toString() !=
          QStringLiteral("agent-photographer-calibration-profile-v1") ||
      !native_profile_retouch.isNull() ||
      !native_calibration_card.value(QStringLiteral("available")).toBool() ||
      native_calibration_card.value(QStringLiteral("caseCount")).toInt() != 1 ||
      native_calibration_coach.value(QStringLiteral("available")).toBool() ||
      native_calibration_coach.value(QStringLiteral("lessonCount")).toInt() != 0) {
    return fail("native calibration finalization should validate signed ratings, preserve the source, write new artifacts, and activate only the compact project-local profile");
  }
  auto coercible_calibration_ratings = calibration_ratings;
  auto coercible_cases =
      coercible_calibration_ratings.value(QStringLiteral("cases")).toMap();
  auto coercible_case =
      coercible_cases.value(QStringLiteral("portrait-natural")).toMap();
  coercible_case.insert(QStringLiteral("rating"), QStringLiteral("5"));
  coercible_cases.insert(QStringLiteral("portrait-natural"), coercible_case);
  coercible_calibration_ratings.insert(QStringLiteral("cases"), coercible_cases);
  const auto coercible_ratings_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("coercible-photographer-ratings.json"));
  if (!write_review_file(
          coercible_ratings_path,
          QJsonDocument::fromVariant(coercible_calibration_ratings)
              .toJson(QJsonDocument::Indented)) ||
      taste_controls_controller.finalizePhotographerCalibrationReviewFiles(
          coercible_ratings_path, review_contract_path)
          .value(QStringLiteral("ok"))
          .toBool()) {
    return fail("native calibration finalization should reject string-coerced ratings");
  }
  auto timezone_less_ratings = calibration_ratings;
  timezone_less_ratings.insert(
      QStringLiteral("reviewedAt"),
      QStringLiteral("2026-08-26T14:15:16.123"));
  const auto timezone_less_ratings_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("timezone-less-photographer-ratings.json"));
  if (!write_review_file(
          timezone_less_ratings_path,
          QJsonDocument::fromVariant(timezone_less_ratings)
              .toJson(QJsonDocument::Indented)) ||
      taste_controls_controller.finalizePhotographerCalibrationReviewFiles(
          timezone_less_ratings_path, review_contract_path)
          .value(QStringLiteral("ok"))
          .toBool()) {
    return fail("native calibration finalization should require a timezone-aware signed review timestamp");
  }
  if (!write_review_file(after_review_path,
                         after_review_bytes + QByteArray("-tampered"))) {
    return fail("calibration review tamper fixture should update one proof");
  }
  const auto calibration_review_tampered =
      taste_controls_controller.openPhotographerCalibrationReviewPacketFile(
          review_contract_path, true);
  if (calibration_review_tampered.value(QStringLiteral("ok")).toBool() ||
      !calibration_review_tampered
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("altered"))) {
    return fail("calibration review launch should fail closed when any copied proof changes");
  }
  const auto external_calibration_review =
      qEnvironmentVariable("PHOTO_EDITOR_CALIBRATION_REVIEW_PACKET_PATH");
  if (!external_calibration_review.trimmed().isEmpty()) {
    const auto external_review_ready =
        taste_controls_controller.openPhotographerCalibrationReviewPacketFile(
            external_calibration_review, true);
    if (!external_review_ready.value(QStringLiteral("ok")).toBool() ||
        !external_review_ready.value(QStringLiteral("validated")).toBool() ||
        external_review_ready.value(QStringLiteral("caseCount")).toInt() != 9 ||
        external_review_ready
                .value(QStringLiteral("proofHashesVerified"))
                .toInt() != 18 ||
        !external_review_ready
             .value(QStringLiteral("machineEvidenceHidden"))
             .toBool() ||
        !external_review_ready
             .value(QStringLiteral("sourcePathsHidden"))
             .toBool()) {
      return fail("the Python-built nine-case calibration packet should verify byte-for-byte in the desktop controller");
    }
  }

#ifdef PHOTO_EDITOR_PHOTOGRAPHER_CALIBRATION_FIXTURE_PATH
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  const auto calibration_fixture_path =
      QString::fromUtf8(PHOTO_EDITOR_PHOTOGRAPHER_CALIBRATION_FIXTURE_PATH);
  const auto calibration_fixture = read_file_bytes(calibration_fixture_path);
  auto tampered_calibration_document =
      QJsonDocument::fromJson(calibration_fixture);
  auto tampered_calibration_object = tampered_calibration_document.object();
  tampered_calibration_object.insert(
      QStringLiteral("reviewer"), QStringLiteral("Tampered Photographer"));
  const auto tampered_calibration_import =
      taste_controls_controller.importPhotographerCalibrationProfile(
          QString::fromUtf8(
              QJsonDocument(tampered_calibration_object)
                  .toJson(QJsonDocument::Compact)));
  auto internally_inconsistent_calibration =
      QJsonDocument::fromJson(calibration_fixture).object();
  auto inconsistent_outcomes =
      internally_inconsistent_calibration
          .value(QStringLiteral("outcomes"))
          .toObject();
  inconsistent_outcomes.insert(QStringLiteral("agreementPass"), 2);
  inconsistent_outcomes.insert(QStringLiteral("falseConfidence"), 0);
  internally_inconsistent_calibration.insert(
      QStringLiteral("outcomes"), inconsistent_outcomes);
  internally_inconsistent_calibration.remove(
      QStringLiteral("profileDigestSha256"));
  const auto inconsistent_digest = QString::fromLatin1(
      QCryptographicHash::hash(
          QJsonDocument(internally_inconsistent_calibration)
              .toJson(QJsonDocument::Compact),
          QCryptographicHash::Sha256)
          .toHex());
  internally_inconsistent_calibration.insert(
      QStringLiteral("profileDigestSha256"), inconsistent_digest);
  const auto internally_inconsistent_import =
      taste_controls_controller.importPhotographerCalibrationProfile(
          QString::fromUtf8(
              QJsonDocument(internally_inconsistent_calibration)
                  .toJson(QJsonDocument::Compact)));
  const auto calibration_import =
      taste_controls_controller.importPhotographerCalibrationProfileFile(
          QUrl::fromLocalFile(calibration_fixture_path).toString());
  const auto calibration_card =
      taste_controls_controller.photographerCalibrationProfile();
  const auto calibration_coach =
      taste_controls_controller.photographerCalibrationCoach();
  const auto calibration_lesson =
      calibration_coach.value(QStringLiteral("primaryLesson")).toMap();
  const auto calibrated_taste =
      taste_controls_controller.approvedProjectTaste();
  QVariantMap calibration_evidence;
  for (const auto& value :
       calibrated_taste.value(QStringLiteral("evidence")).toList()) {
    const auto evidence = value.toMap();
    if (evidence.value(QStringLiteral("kind")).toString() ==
        QStringLiteral("photographer-calibration")) {
      calibration_evidence = evidence;
      break;
    }
  }
  const auto calibrated_packet =
      taste_controls_controller.memoryPacketForPhase(
          QStringLiteral("sample"));
  const auto calibrated_workflow =
      taste_controls_controller.aiEditWorkflowState(
          QStringLiteral("selected-assets"));
  const auto taste_passport_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("photographer-taste-passport.json"));
  const auto taste_passport_export =
      taste_controls_controller.exportPhotographerTastePassportFile(
          QUrl::fromLocalFile(taste_passport_path).toString());
  const auto taste_passport_bytes = read_file_bytes(taste_passport_path);
  auto taste_passport_document =
      QJsonDocument::fromJson(taste_passport_bytes);
  const auto taste_passport_object = taste_passport_document.object();
  auto tampered_taste_passport_object = taste_passport_object;
  auto tampered_portable_profile =
      tampered_taste_passport_object
          .value(QStringLiteral("calibrationProfile"))
          .toObject();
  tampered_portable_profile.insert(
      QStringLiteral("reviewer"), QStringLiteral("Tampered Passport"));
  tampered_taste_passport_object.insert(
      QStringLiteral("calibrationProfile"), tampered_portable_profile);
  const auto tampered_taste_passport_path =
      QDir(calibration_review_root.path())
          .filePath(QStringLiteral("tampered-photographer-taste-passport.json"));
  const auto tampered_taste_passport_written = write_review_file(
      tampered_taste_passport_path,
      QJsonDocument(tampered_taste_passport_object)
          .toJson(QJsonDocument::Indented));
  continuity_catalog.setProjectFilter(
      QStringLiteral("Taste Passport Destination"));
  const auto taste_passport_preview =
      taste_controls_controller.inspectPhotographerTastePassportFile(
          QUrl::fromLocalFile(taste_passport_path).toString());
  const auto taste_passport_import =
      taste_controls_controller.importPhotographerTastePassportFile(
          QUrl::fromLocalFile(taste_passport_path).toString(),
          taste_passport_preview
              .value(QStringLiteral("passportDigestSha256"))
              .toString());
  const auto taste_passport_card =
      taste_controls_controller.photographerCalibrationProfile();
  photo_editor::desktop::AgentEditController taste_passport_relaunch;
  taste_passport_relaunch.setCatalogModel(&continuity_catalog);
  const auto relaunched_taste_passport_card =
      taste_passport_relaunch.photographerCalibrationProfile();
  const auto tampered_taste_passport_import =
      taste_controls_controller.importPhotographerTastePassportFile(
          tampered_taste_passport_path);
  const auto changed_after_preview_import =
      taste_controls_controller.importPhotographerTastePassportFile(
          taste_passport_path,
          QStringLiteral(
              "dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd"));
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  const auto source_calibration_after_passport_transfer =
      taste_controls_controller.photographerCalibrationProfile();
  const auto serialized_calibration =
      QJsonDocument::fromVariant(calibration_card)
          .toJson(QJsonDocument::Compact);
  const auto serialized_calibration_coach =
      QJsonDocument::fromVariant(calibration_coach)
          .toJson(QJsonDocument::Compact);
  continuity_catalog.setProjectFilter(QStringLiteral("Project Alpha"));
  const auto alpha_calibration =
      taste_controls_controller.photographerCalibrationProfile();
  const auto alpha_calibration_coach =
      taste_controls_controller.photographerCalibrationCoach();
  continuity_catalog.setProjectFilter(QStringLiteral("Project Taste Controls"));
  photo_editor::desktop::AgentEditController calibration_relaunch;
  calibration_relaunch.setCatalogModel(&continuity_catalog);
  const auto relaunched_calibration =
      calibration_relaunch.photographerCalibrationProfile();
  const auto relaunched_calibration_coach =
      calibration_relaunch.photographerCalibrationCoach();
  const auto calibrated_recipe_before_forget =
      adjustment_patch_for_asset(continuity_catalog.assetAt(0));
  const auto unconfirmed_calibration_forget =
      taste_controls_controller.forgetProjectTasteEvidence(
          QStringLiteral("photographer-calibration"),
          calibration_evidence.value(QStringLiteral("id")).toString(),
          false);
  const auto calibration_after_unconfirmed_forget =
      taste_controls_controller.photographerCalibrationProfile();
  const auto confirmed_calibration_forget =
      taste_controls_controller.forgetProjectTasteEvidence(
          QStringLiteral("photographer-calibration"),
          calibration_evidence.value(QStringLiteral("id")).toString(),
          true);
  const auto calibration_after_confirmed_forget =
      taste_controls_controller.photographerCalibrationProfile();
  const auto calibration_coach_after_confirmed_forget =
      taste_controls_controller.photographerCalibrationCoach();
  const auto calibrated_recipe_after_forget =
      adjustment_patch_for_asset(continuity_catalog.assetAt(0));
  if (calibration_fixture.isEmpty() ||
      tampered_calibration_import.value(QStringLiteral("ok")).toBool() ||
      !tampered_calibration_import
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("digest"), Qt::CaseInsensitive) ||
      internally_inconsistent_import.value(QStringLiteral("ok")).toBool() ||
      !internally_inconsistent_import
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("outcome"), Qt::CaseInsensitive) ||
      !calibration_import.value(QStringLiteral("ok")).toBool() ||
      calibration_import.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-calibration-import-v1") ||
      !taste_passport_export.value(QStringLiteral("ok")).toBool() ||
      taste_passport_export.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-taste-passport-export-v1") ||
      !taste_passport_document.isObject() ||
      taste_passport_object.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-taste-passport-v1") ||
      !taste_passport_object
           .value(QStringLiteral("explicitImportRequired"))
           .toBool() ||
      !taste_passport_object.value(QStringLiteral("targetProjectOnly")).toBool() ||
      taste_passport_object.value(QStringLiteral("containsProjectName")).toBool() ||
      taste_passport_object.value(QStringLiteral("storesImageBytes")).toBool() ||
      taste_passport_object.value(QStringLiteral("storesSourcePaths")).toBool() ||
      taste_passport_object
          .value(QStringLiteral("automaticModelTrainingAllowed"))
          .toBool() ||
      taste_passport_object
          .value(QStringLiteral("automaticThresholdMutationAllowed"))
          .toBool() ||
      taste_passport_object
          .value(QStringLiteral("professionalReadinessClaimed"))
          .toBool() ||
      taste_passport_bytes.contains("Project Taste Controls") ||
      !tampered_taste_passport_written ||
      !taste_passport_preview.value(QStringLiteral("ok")).toBool() ||
      taste_passport_preview.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-taste-passport-preview-v1") ||
      !taste_passport_preview
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      taste_passport_preview.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      taste_passport_preview.value(QStringLiteral("wouldReplaceExisting")).toBool() ||
      !taste_passport_import.value(QStringLiteral("ok")).toBool() ||
      taste_passport_import.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-taste-passport-import-v1") ||
      !taste_passport_import.value(QStringLiteral("previewDigestPinned")).toBool() ||
      !taste_passport_card.value(QStringLiteral("available")).toBool() ||
      !taste_passport_card
           .value(QStringLiteral("importedFromTastePassport"))
           .toBool() ||
      taste_passport_card.value(QStringLiteral("transferScope")).toString() !=
          QStringLiteral("explicit-project-only") ||
      taste_passport_card.value(QStringLiteral("caseCount")).toInt() != 2 ||
      !relaunched_taste_passport_card
           .value(QStringLiteral("importedFromTastePassport"))
           .toBool() ||
      relaunched_taste_passport_card
              .value(QStringLiteral("tastePassportDigestSha256"))
              .toString() !=
          taste_passport_card
              .value(QStringLiteral("tastePassportDigestSha256"))
              .toString() ||
      tampered_taste_passport_import.value(QStringLiteral("ok")).toBool() ||
      !tampered_taste_passport_import
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("digest"), Qt::CaseInsensitive) ||
      changed_after_preview_import.value(QStringLiteral("ok")).toBool() ||
      !changed_after_preview_import
           .value(QStringLiteral("failureReason"))
           .toString()
           .contains(QStringLiteral("changed after preview"),
                     Qt::CaseInsensitive) ||
      source_calibration_after_passport_transfer
              .value(QStringLiteral("profileDigestSha256"))
              .toString() !=
          calibration_card.value(QStringLiteral("profileDigestSha256")).toString() ||
      !calibration_card.value(QStringLiteral("available")).toBool() ||
      calibration_card.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-calibration-card-v1") ||
      calibration_card.value(QStringLiteral("caseCount")).toInt() != 2 ||
      calibration_card.value(QStringLiteral("weakestDimension")).toString() !=
          QStringLiteral("naturalness") ||
      calibration_card.value(QStringLiteral("validationConcernKeys"))
              .toStringList() != QStringList{QStringLiteral("warmth")} ||
      !calibration_card
           .value(QStringLiteral("requiresThresholdReview"))
           .toBool() ||
      !calibration_card.value(QStringLiteral("influencesPlanning")).toBool() ||
      !calibration_card
           .value(QStringLiteral("influencesProofCritique"))
           .toBool() ||
      calibration_card
          .value(QStringLiteral("numericThresholdOverridesAllowed"))
          .toBool() ||
      calibration_card
          .value(QStringLiteral("professionalReadinessClaimed"))
          .toBool() ||
      !calibration_coach.value(QStringLiteral("available")).toBool() ||
      calibration_coach.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-photographer-calibration-coach-v1") ||
      calibration_coach.value(QStringLiteral("lessonCount")).toInt() != 1 ||
      calibration_coach.value(QStringLiteral("falseConfidenceCount")).toInt() != 1 ||
      calibration_coach.value(QStringLiteral("overConservativeCount")).toInt() != 0 ||
      !calibration_coach
           .value(QStringLiteral("photographerDecisionIsAuthority"))
           .toBool() ||
      calibration_coach.value(QStringLiteral("machineCanRate")).toBool() ||
      calibration_coach
          .value(QStringLiteral("automaticModelTrainingAllowed"))
          .toBool() ||
      calibration_coach
          .value(QStringLiteral("automaticThresholdMutationAllowed"))
          .toBool() ||
      calibration_coach.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      calibration_coach
          .value(QStringLiteral("professionalReadinessClaimed"))
          .toBool() ||
      calibration_lesson.value(QStringLiteral("outcome")).toString() !=
          QStringLiteral("false-confidence") ||
      calibration_lesson.value(QStringLiteral("humanRating")).toInt() != 2 ||
      calibration_lesson.value(QStringLiteral("weakestDimension")).toString() !=
          QStringLiteral("naturalness") ||
      calibration_lesson.value(QStringLiteral("weakestDimensionScore")).toInt() != 2 ||
      calibration_lesson.value(QStringLiteral("issueLabels")).toStringList() !=
          QStringList{QStringLiteral("too warm")} ||
      calibration_lesson.value(QStringLiteral("relatedConcernKeys")).toStringList() !=
          QStringList{QStringLiteral("warmth")} ||
      !calibration_lesson.value(QStringLiteral("headline"))
           .toString()
           .contains(QStringLiteral("too confident")) ||
      !calibration_lesson.value(QStringLiteral("explanation"))
           .toString()
           .contains(QStringLiteral("naturalness was the weakest dimension at 2/5")) ||
      calibration_lesson.value(QStringLiteral("decisionContrast")).toString() !=
          QStringLiteral("MACHINE PASSED · PHOTOGRAPHER: DO NOT DELIVER") ||
      !calibration_lesson.value(QStringLiteral("nextMove"))
           .toString()
           .contains(QStringLiteral("fresh corpus")) ||
      !calibration_lesson.value(QStringLiteral("signedHumanJudgment")).toBool() ||
      calibration_lesson.value(QStringLiteral("automaticActionTaken")).toBool() ||
      serialized_calibration.contains("data:image") ||
      serialized_calibration.contains("base64") ||
      serialized_calibration.contains(calibration_fixture_path.toUtf8()) ||
      serialized_calibration_coach.contains("data:image") ||
      serialized_calibration_coach.contains("base64") ||
      serialized_calibration_coach.contains(calibration_fixture_path.toUtf8()) ||
      !calibrated_taste
           .value(QStringLiteral("photographerCalibrationAvailable"))
           .toBool() ||
      calibrated_taste.value(QStringLiteral("maturity")).toString() !=
          QStringLiteral("calibrated") ||
      calibration_evidence.value(QStringLiteral("id")).toString() !=
          QStringLiteral(
              "07c24e66ff911994f6b0b1bbf41d8a22ffe37c52add22542c7cf2a80cae62585") ||
      !calibrated_packet.value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("photographer-calibration")) ||
      calibrated_packet.value(QStringLiteral("photographerCalibrationCard"))
              .toMap()
              .value(QStringLiteral("profileDigestSha256"))
              .toString() !=
          calibration_card.value(QStringLiteral("profileDigestSha256"))
              .toString() ||
      calibrated_packet.value(QStringLiteral("relevancePolicy"))
          .toMap()
          .value(QStringLiteral("numericThresholdOverridesAllowed"))
          .toBool() ||
      calibrated_workflow.value(QStringLiteral("photographerCalibration"))
              .toMap()
              .value(QStringLiteral("caseCount"))
              .toInt() != 2 ||
      alpha_calibration.value(QStringLiteral("available")).toBool() ||
      alpha_calibration_coach.value(QStringLiteral("available")).toBool() ||
      relaunched_calibration.value(QStringLiteral("profileDigestSha256"))
              .toString() !=
          calibration_card.value(QStringLiteral("profileDigestSha256"))
              .toString() ||
      relaunched_calibration_coach
              .value(QStringLiteral("primaryLesson"))
              .toMap()
              .value(QStringLiteral("lessonId"))
              .toString() !=
          calibration_lesson.value(QStringLiteral("lessonId")).toString() ||
      unconfirmed_calibration_forget.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_calibration_forget
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      !calibration_after_unconfirmed_forget
           .value(QStringLiteral("available"))
           .toBool() ||
      !confirmed_calibration_forget.value(QStringLiteral("ok")).toBool() ||
      !confirmed_calibration_forget
           .value(QStringLiteral("forgotten"))
           .toBool() ||
      !confirmed_calibration_forget
           .value(QStringLiteral("activeDraftUnaffected"))
           .toBool() ||
      calibration_after_confirmed_forget
          .value(QStringLiteral("available"))
          .toBool() ||
      calibration_coach_after_confirmed_forget
          .value(QStringLiteral("available"))
          .toBool() ||
      calibrated_recipe_before_forget != calibrated_recipe_after_forget) {
    return fail("signed photographer calibration should be tamper-evident, project-local, prompt-visible, persistent, and forgettable without changing edits");
  }
#endif

  catalog_model.replaceAssetsForTesting(
      {story_asset_1, story_asset_2, story_asset_3, story_asset_4,
       story_asset_5});
  catalog_model.createProject(QStringLiteral("Story Director Project"));
  for (auto index = 1; index <= 5; ++index) {
    catalog_model.addAssetToProject(
        QStringLiteral("story-%1").arg(index),
        QStringLiteral("Story Director Project"));
  }
  catalog_model.openProject(QStringLiteral("Story Director Project"));
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  for (auto index = 0; index < 5; ++index) {
    catalog_model.toggleSelectionIndex(index);
  }
  QString atmosphere_asset_id;
  for (const auto& assignment_value :
       story_map.value(QStringLiteral("assignments")).toList()) {
    const auto assignment = assignment_value.toMap();
    if (assignment.value(QStringLiteral("role")).toString() ==
        QStringLiteral("atmosphere")) {
      atmosphere_asset_id =
          assignment.value(QStringLiteral("assetId")).toString();
      break;
    }
  }
  const auto model_before_story_revision = controller.selectedModelId();
  controller.setSelectedModelId(QStringLiteral("rulebook-deterministic"));
  const auto story_draft = controller.runDraftForScope(
      QStringLiteral("selected-assets"),
      QStringLiteral(
          "Balance this assignment while preserving natural color and highlights."));
  const auto memory_before_story_revision = controller.memoryVaultStatus();
  if (!story_draft.value(QStringLiteral("ok")).toBool() ||
      !story_draft.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      story_draft.value(QStringLiteral("remainingAssetIds"))
          .toStringList()
          .isEmpty() ||
      atmosphere_asset_id.isEmpty()) {
    return fail("a five-photo assignment should produce a real story-aware sample draft while leaving at least one photo untouched");
  }
  const auto story_revision =
      controller.reviseAssignmentStoryRole(atmosphere_asset_id,
                                           QStringLiteral("hero"));
  const auto revised_story_map =
      story_revision.value(QStringLiteral("assignmentStoryMap")).toMap();
  QString detail_holder_before_second_revision;
  for (const auto& assignment_value :
       revised_story_map.value(QStringLiteral("assignments")).toList()) {
    const auto assignment = assignment_value.toMap();
    if (assignment.value(QStringLiteral("role")).toString() ==
        QStringLiteral("detail")) {
      detail_holder_before_second_revision =
          assignment.value(QStringLiteral("assetId")).toString();
      break;
    }
  }
  const auto second_story_revision =
      controller.reviseAssignmentStoryRole(atmosphere_asset_id,
                                           QStringLiteral("detail"));
  const auto second_revised_story_map =
      second_story_revision.value(QStringLiteral("assignmentStoryMap")).toMap();
  const auto second_revised_role_evidence =
      second_revised_story_map.value(QStringLiteral("assignmentsByAssetId"))
          .toMap()
          .value(atmosphere_asset_id)
          .toMap()
          .value(QStringLiteral("roleEvidence"))
          .toMap();
  const auto memory_after_story_revision = controller.memoryVaultStatus();
  if (!story_revision.value(QStringLiteral("ok")).toBool() ||
      story_revision.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-assignment-story-role-revision-v1") ||
      story_revision.value(QStringLiteral("role")).toString() !=
          QStringLiteral("hero") ||
      !story_revision.value(QStringLiteral("fullBatchUntouched")).toBool() ||
      !story_revision.value(QStringLiteral("sourceOrderPreserved")).toBool() ||
      story_revision.value(QStringLiteral("durableMemoryWritten")).toBool() ||
      !story_revision.value(QStringLiteral("sampleOnlyDraft")).toBool() ||
      story_revision.value(QStringLiteral("remainingAssetIds"))
          .toStringList()
          .isEmpty() ||
      revised_story_map.value(QStringLiteral("heroAssetId")).toString() !=
          atmosphere_asset_id ||
      revised_story_map.value(QStringLiteral("heroBasis")).toString() !=
          QStringLiteral("photographer-story-role") ||
      !revised_story_map.value(QStringLiteral("photographerSteered")).toBool() ||
      revised_story_map.value(QStringLiteral("roleOverrideCount")).toInt() < 2 ||
      !story_revision.value(QStringLiteral("storyRevisionReceipt"))
           .toMap()
           .value(QStringLiteral("realProofsRebuilt"))
           .toBool() ||
      !second_story_revision.value(QStringLiteral("ok")).toBool() ||
      second_story_revision.value(QStringLiteral("role")).toString() !=
          QStringLiteral("detail") ||
      second_revised_story_map.value(QStringLiteral("assignmentsByAssetId"))
              .toMap()
              .value(atmosphere_asset_id)
              .toMap()
              .value(QStringLiteral("role"))
              .toString() != QStringLiteral("detail") ||
      detail_holder_before_second_revision.isEmpty() ||
      second_revised_story_map.value(QStringLiteral("assignmentsByAssetId"))
              .toMap()
              .value(detail_holder_before_second_revision)
              .toMap()
              .value(QStringLiteral("role"))
              .toString() != QStringLiteral("hero") ||
      !second_revised_story_map.value(QStringLiteral("photographerSteered"))
           .toBool() ||
      second_revised_role_evidence.value(QStringLiteral("basis")).toString() !=
          QStringLiteral("photographer-direction") ||
      !second_revised_role_evidence
           .value(QStringLiteral("photographerAssigned"))
           .toBool() ||
      second_revised_role_evidence
          .value(QStringLiteral("semanticRecognitionUsed"))
          .toBool() ||
      !second_revised_role_evidence.value(QStringLiteral("signals"))
           .toStringList()
           .contains(QStringLiteral("explicit-photographer-story-role")) ||
      memory_after_story_revision.value(QStringLiteral("vaultFingerprint")) !=
          memory_before_story_revision.value(QStringLiteral("vaultFingerprint")) ||
      memory_after_story_revision.value(QStringLiteral("totalNoteCount")) !=
          memory_before_story_revision.value(QStringLiteral("totalNoteCount"))) {
    return fail("Story Director should support repeated role swaps, rebuild real samples, and keep the remaining batch untouched without writing memory");
  }
  const auto approved_story_batch = controller.applySampleReviewToAll(true);
  const auto approved_story_receipt =
      approved_story_batch
          .value(QStringLiteral("approvedAssignmentStoryReceipt"))
          .toMap();
  const auto summary_story_receipt =
      approved_story_batch.value(QStringLiteral("batchSummary"))
          .toMap()
          .value(QStringLiteral("approvedAssignmentStoryReceipt"))
          .toMap();
  if (!approved_story_batch.value(QStringLiteral("ok")).toBool() ||
      approved_story_receipt.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-approved-assignment-story-receipt-v1") ||
      !approved_story_receipt.value(QStringLiteral("available")).toBool() ||
      !approved_story_receipt.value(QStringLiteral("approved")).toBool() ||
      approved_story_receipt.value(QStringLiteral("approvalSource")).toString() !=
          QStringLiteral("explicit-apply-to-all") ||
      approved_story_receipt.value(QStringLiteral("storyMapDigestSha256")) !=
          second_revised_story_map.value(QStringLiteral("digestSha256")) ||
      approved_story_receipt.value(QStringLiteral("approvedAssetCount")).toInt() !=
          5 ||
      approved_story_receipt.value(QStringLiteral("newlyDraftedCount")).toInt() !=
          approved_story_batch.value(QStringLiteral("draftedCount")).toInt() ||
      !approved_story_receipt.value(QStringLiteral("photographerSteered")).toBool() ||
      !approved_story_receipt.value(QStringLiteral("sourceOrderPreserved")).toBool() ||
      !approved_story_receipt.value(QStringLiteral("noAutoCull")).toBool() ||
      approved_story_receipt.value(QStringLiteral("recipeVariationApplied")).toBool() ||
      approved_story_receipt.value(QStringLiteral("semanticRecognitionUsed")).toBool() ||
      approved_story_receipt.value(QStringLiteral("storyDirectionMemoryWritten")).toBool() ||
      !approved_story_receipt.value(QStringLiteral("realProofsReviewed")).toBool() ||
      !approved_story_receipt.value(QStringLiteral("guardrailsPassed")).toBool() ||
      approved_story_receipt.value(QStringLiteral("deliveryInfluence")).toString() !=
          QStringLiteral("review-and-delivery-receipt-only") ||
      approved_story_receipt.value(QStringLiteral("recipePolicy")).toString() !=
          QStringLiteral("validated-lighting-group-plan-unchanged-by-story-role") ||
      approved_story_receipt.value(QStringLiteral("digestSha256")).toString().size() !=
          64 ||
      summary_story_receipt.value(QStringLiteral("digestSha256")) !=
          approved_story_receipt.value(QStringLiteral("digestSha256")) ||
      approved_story_batch.value(QStringLiteral("plannedPatchesByAssetId")) !=
          second_story_revision.value(QStringLiteral("plannedPatchesByAssetId")) ||
      !approved_story_batch
           .value(QStringLiteral("approvedFeedbackMemory"))
           .toMap()
           .isEmpty() ||
      !approved_story_batch.value(QStringLiteral("styleChangeSummary"))
           .toString()
           .isEmpty()) {
    return fail("Apply to All should preserve the photographer-approved story as a visible delivery receipt without role-based recipe changes, culling, reordering, or story-memory writes");
  }
  const auto committed_story_batch = controller.commitDraft(true);
  const auto committed_story_receipt =
      committed_story_batch
          .value(QStringLiteral("approvedAssignmentStoryReceipt"))
          .toMap();
  const auto committed_summary_story_receipt =
      committed_story_batch.value(QStringLiteral("batchSummary"))
          .toMap()
          .value(QStringLiteral("approvedAssignmentStoryReceipt"))
          .toMap();
  if (!committed_story_batch.value(QStringLiteral("ok")).toBool() ||
      committed_story_batch.value(QStringLiteral("draftActive")).toBool() ||
      committed_story_receipt.value(QStringLiteral("digestSha256")) !=
          approved_story_receipt.value(QStringLiteral("digestSha256")) ||
      committed_summary_story_receipt.value(QStringLiteral("digestSha256")) !=
          approved_story_receipt.value(QStringLiteral("digestSha256")) ||
      committed_story_receipt.value(QStringLiteral("approvalSource")).toString() !=
          QStringLiteral("explicit-apply-to-all") ||
      committed_story_receipt.value(QStringLiteral("storyDirectionMemoryWritten")).toBool()) {
    return fail("Commit Edits should preserve the approved story direction as a first-class delivery receipt");
  }
  const auto memory_before_story_remember = controller.memoryVaultStatus();
  const auto unconfirmed_story_remember =
      controller.rememberApprovedStoryPreference(false);
  const auto memory_after_unconfirmed_story_remember =
      controller.memoryVaultStatus();
  if (unconfirmed_story_remember.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_story_remember
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      controller.storyPreferenceStatus()
          .value(QStringLiteral("available"))
          .toBool() ||
      memory_after_unconfirmed_story_remember
              .value(QStringLiteral("vaultFingerprint")) !=
          memory_before_story_remember
              .value(QStringLiteral("vaultFingerprint"))) {
    return fail("Story preference memory should require an explicit confirmed action and remain a no-op before approval");
  }
  const auto remembered_story =
      controller.rememberApprovedStoryPreference(true);
  const auto remembered_story_preference =
      remembered_story.value(QStringLiteral("preference")).toMap();
  const auto remembered_story_status = controller.storyPreferenceStatus();
  const auto remembered_story_taste = controller.approvedProjectTaste();
  const auto remembered_story_packet =
      controller.memoryPacketForPhase(QStringLiteral("sample"));
  const auto remembered_delivery_receipt =
      controller.lastRunReport()
          .value(QStringLiteral("approvedAssignmentStoryReceipt"))
          .toMap();
  auto story_taste_evidence_found = false;
  for (const auto& evidence_value :
       remembered_story_taste.value(QStringLiteral("evidence")).toList()) {
    if (evidence_value.toMap().value(QStringLiteral("kind")).toString() ==
        QStringLiteral("story-preference")) {
      story_taste_evidence_found = true;
      break;
    }
  }
  if (!remembered_story.value(QStringLiteral("ok")).toBool() ||
      !remembered_story.value(QStringLiteral("remembered")).toBool() ||
      remembered_story.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-story-preference-remember-v1") ||
      remembered_story_preference.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-story-preference-memory-v1") ||
      !remembered_story_preference
           .value(QStringLiteral("explicitlyRemembered"))
           .toBool() ||
      remembered_story_preference
              .value(QStringLiteral("roleAnchors"))
              .toList()
              .size() != 4 ||
      remembered_story_preference
          .value(QStringLiteral("automaticRecipeInfluence"))
          .toBool() ||
      !remembered_story_preference
           .value(QStringLiteral("requiresFreshSampleReview"))
           .toBool() ||
      remembered_story_preference.value(QStringLiteral("storesAssetIds"))
          .toBool() ||
      remembered_story_preference.value(QStringLiteral("storesImageBytes"))
          .toBool() ||
      remembered_story_preference.value(QStringLiteral("storesSourcePaths"))
          .toBool() ||
      !remembered_story_status.value(QStringLiteral("available")).toBool() ||
      remembered_story_status.value(QStringLiteral("preferenceId")) !=
          remembered_story.value(QStringLiteral("preferenceId")) ||
      !story_taste_evidence_found ||
      remembered_story_taste.value(QStringLiteral("storyPreferenceCount"))
              .toInt() < 1 ||
      !remembered_story_packet
           .value(QStringLiteral("includedMemoryKinds"))
           .toStringList()
           .contains(QStringLiteral("story-preference")) ||
      remembered_story_packet.value(QStringLiteral("relevancePolicy"))
          .toMap()
          .value(QStringLiteral("storyPreferenceRecipeInfluence"))
          .toBool() ||
      !remembered_delivery_receipt
           .value(QStringLiteral("storyDirectionMemoryWritten"))
           .toBool() ||
      remembered_delivery_receipt
              .value(QStringLiteral("storyMemoryApprovalSource"))
              .toString() != QStringLiteral("explicit-remember-action")) {
    return fail("An explicit post-commit action should create a bounded, visible, project-local story preference without storing images, paths, or recipe authority");
  }

  catalog_model.createProject(QStringLiteral("Story Director Other"));
  catalog_model.addAssetToProject(
      QStringLiteral("story-1"), QStringLiteral("Story Director Other"));
  catalog_model.openProject(QStringLiteral("Story Director Other"));
  if (controller.storyPreferenceStatus()
          .value(QStringLiteral("available"))
          .toBool()) {
    return fail("Remembered story preferences should stay isolated to the exact project");
  }
  catalog_model.openProject(QStringLiteral("Story Director Project"));

  const QVector<photo_editor::desktop::CatalogAssetItem> future_story_assets{
      make_asset(QStringLiteral("future-story-1"), source_path,
                 original_recipe),
      make_asset(QStringLiteral("future-story-2"), second_source_path,
                 second_original_recipe),
      make_asset(QStringLiteral("future-story-3"), third_source_path,
                 original_recipe),
      make_asset(QStringLiteral("future-story-4"), source_path,
                 second_original_recipe),
      make_asset(QStringLiteral("future-story-5"), second_source_path,
                 original_recipe),
  };
  catalog_model.replaceAssetsForTesting(future_story_assets);
  for (auto index = 1; index <= 5; ++index) {
    catalog_model.addAssetToProject(
        QStringLiteral("future-story-%1").arg(index),
        QStringLiteral("Story Director Project"));
  }
  catalog_model.openProject(QStringLiteral("Story Director Project"));
  catalog_model.selectIndex(0);
  catalog_model.selectAll();
  QVariantMap future_recipe_before_story_memory;
  for (auto index = 0; index < catalog_model.assetCount(); ++index) {
    const auto asset = catalog_model.assetAt(index);
    future_recipe_before_story_memory.insert(
        asset.value(QStringLiteral("assetId")).toString(),
        adjustment_patch_for_asset(asset));
  }
  const auto future_remembered_story =
      controller.assignmentStoryMapForScope(QStringLiteral("selected-assets"));
  auto remembered_role_evidence_found = false;
  for (const auto& assignment_value :
       future_remembered_story.value(QStringLiteral("assignments")).toList()) {
    if (assignment_value.toMap()
            .value(QStringLiteral("roleEvidence"))
            .toMap()
            .value(QStringLiteral("basis"))
            .toString() ==
        QStringLiteral("remembered-photographer-story-preference")) {
      remembered_role_evidence_found = true;
      break;
    }
  }
  QVariantMap future_recipe_after_story_memory;
  for (auto index = 0; index < catalog_model.assetCount(); ++index) {
    const auto asset = catalog_model.assetAt(index);
    future_recipe_after_story_memory.insert(
        asset.value(QStringLiteral("assetId")).toString(),
        adjustment_patch_for_asset(asset));
  }
  if (!future_remembered_story
           .value(QStringLiteral("rememberedPreferenceApplied"))
           .toBool() ||
      future_remembered_story.value(QStringLiteral("storyPreferenceId")) !=
          remembered_story.value(QStringLiteral("preferenceId")) ||
      future_remembered_story.value(QStringLiteral("memoryInfluence"))
              .toString() != QStringLiteral("role-anchor-suggestion-only") ||
      !remembered_role_evidence_found ||
      !future_remembered_story.value(QStringLiteral("sourceOrderPreserved"))
           .toBool() ||
      !future_remembered_story.value(QStringLiteral("noAutoCull")).toBool() ||
      future_remembered_story
          .value(QStringLiteral("recipeVariationApplied"))
          .toBool() ||
      future_recipe_before_story_memory !=
          future_recipe_after_story_memory) {
    return fail("A remembered project arc should influence only future source-order role suggestions while recipes, culling, and fresh sample review remain protected");
  }

  const auto unconfirmed_story_forget =
      controller.forgetApprovedStoryPreference(false);
  if (unconfirmed_story_forget.value(QStringLiteral("ok")).toBool() ||
      !unconfirmed_story_forget
           .value(QStringLiteral("confirmationRequired"))
           .toBool() ||
      !controller.storyPreferenceStatus()
           .value(QStringLiteral("available"))
           .toBool()) {
    return fail("Forgetting a story preference should require confirmation and preserve it before approval");
  }
  const auto forgotten_story =
      controller.forgetApprovedStoryPreference(true);
  const auto future_story_after_forget =
      controller.assignmentStoryMapForScope(QStringLiteral("selected-assets"));
  if (!forgotten_story.value(QStringLiteral("ok")).toBool() ||
      !forgotten_story.value(QStringLiteral("forgotten")).toBool() ||
      forgotten_story.value(QStringLiteral("contract")).toString() !=
          QStringLiteral("agent-story-preference-forget-v1") ||
      controller.storyPreferenceStatus()
          .value(QStringLiteral("available"))
          .toBool() ||
      future_story_after_forget
          .value(QStringLiteral("rememberedPreferenceApplied"))
          .toBool() ||
      !forgotten_story.value(QStringLiteral("activeEditsUnaffected"))
           .toBool()) {
    return fail("A confirmed forget should remove future story influence without changing any existing edit");
  }
  controller.setSelectedModelId(model_before_story_revision);
  catalog_model.replaceAssetsForTesting({
      make_asset(QStringLiteral("asset-1"), source_path, original_recipe),
      make_asset(QStringLiteral("asset-2"), second_source_path,
                 second_original_recipe),
  });
  catalog_model.selectIndex(0);
  catalog_model.clearSelection();
  catalog_model.toggleSelectionIndex(0);

  if (read_file_bytes(source_path) != original_source_bytes ||
      read_file_bytes(second_source_path) != original_second_source_bytes ||
      read_file_bytes(third_source_path) != original_third_source_bytes ||
      read_file_bytes(semantic_source_path) != original_semantic_source_bytes ||
      read_file_bytes(ambiguous_object_source_path) !=
          original_ambiguous_object_source_bytes) {
    return fail("agent workflows must not overwrite source image files");
  }
  const auto memory_root_path = controller.memoryVaultStatus().value(QStringLiteral("rootPath")).toString();
  const auto legacy_memory_note_path =
      QDir(memory_root_path).filePath(QStringLiteral("feedback/legacy-unversioned-note.md"));
  if (!QDir().mkpath(QFileInfo(legacy_memory_note_path).dir().absolutePath())) {
    return fail("test should be able to create a legacy memory note for schema migration checks");
  }
  QFile legacy_memory_note(legacy_memory_note_path);
  if (!legacy_memory_note.open(QIODevice::WriteOnly | QIODevice::Text | QIODevice::Truncate) ||
      legacy_memory_note.write("---\ntype: agent_feedback_memory\n---\n# Legacy note\n") <= 0) {
    return fail("test should be able to write a legacy memory note for schema migration checks");
  }
  legacy_memory_note.close();
  const auto legacy_vault_status = controller.memoryVaultStatus();
  if (legacy_vault_status.value(QStringLiteral("schemaVersion")).toInt() != 1 ||
      legacy_vault_status.value(QStringLiteral("schemaVersionOk")).toBool() ||
      !legacy_vault_status.value(QStringLiteral("migrationRequired")).toBool() ||
      legacy_vault_status.value(QStringLiteral("schemaMismatchCount")).toInt() < 1 ||
      legacy_vault_status.value(QStringLiteral("schemaMismatches")).toList().isEmpty()) {
    return fail("memory vault status should detect legacy or unversioned notes that require migration");
  }
  const auto confirmed_memory_reset = controller.resetMemoryVault(true);
  const auto reset_vault_status = controller.memoryVaultStatus();
  if (!confirmed_memory_reset.value(QStringLiteral("ok")).toBool() ||
      confirmed_memory_reset.value(QStringLiteral("removedFileCount")).toInt() <= 0 ||
      reset_vault_status.value(QStringLiteral("exists")).toBool() ||
      reset_vault_status.value(QStringLiteral("totalNoteCount")).toInt() != 0) {
    return fail("confirmed memory vault reset should remove local memory notes and refresh vault status");
  }

  return EXIT_SUCCESS;
}
