#include "ai_model_runtime.h"
#include <QtGui/qrgbafloat.h>

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QByteArrayView>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRect>
#include <QSet>
#include <QSize>
#include <QStandardPaths>
#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
#include <onnxruntime_cxx_api.h>
#if defined(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML) && PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML
#include <coreml_provider_factory.h>
#endif
#endif

namespace photo_editor::desktop {
namespace {

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
constexpr bool kOnnxRuntimeLinked = true;
#else
constexpr bool kOnnxRuntimeLinked = false;
#endif
constexpr bool kSamRuntimeImplemented = kOnnxRuntimeLinked;
constexpr bool kLamaRuntimeImplemented = kOnnxRuntimeLinked;
constexpr auto kInpaintModelRelativePath = "inpaint/lama.onnx";
constexpr auto kInpaintModelContract = "opencv-lama-image-mask-nchw-float32-512-v1";
constexpr auto kSam2EncoderRelativePath = "selection/sam2_hiera_tiny_image_encoder.onnx";
constexpr auto kSam2EncoderContract = "sam2-hiera-tiny-image-encoder-fp32-1024-v1";
constexpr auto kSam2DecoderRelativePath = "selection/sam2_hiera_tiny_image_decoder.onnx";
constexpr auto kSam2DecoderContract = "sam2-hiera-tiny-image-decoder-fp32-1024-v1";
// Keep soft SAM2 edges while preserving explicit prompt polarity at alpha 96.
[[maybe_unused]] constexpr auto kSam2MaskAlphaLogitScale = 0.40F;
[[maybe_unused]] constexpr auto kSam2MaximumMaskAlphaLogitScale = 2.0F;
[[maybe_unused]] constexpr auto kSam2PositivePromptAlphaLogitThreshold = -0.50F;
[[maybe_unused]] constexpr auto kSam2NegativePromptAlphaLogitThreshold = -0.60F;
[[maybe_unused]] constexpr auto kHighTextureExemplarResidualMean = 6.0;

QString default_model_root() {
  const auto env_root = qEnvironmentVariable("PHOTO_EDITOR_AI_MODEL_DIR");
  if (!env_root.trimmed().isEmpty()) {
    return env_root.trimmed();
  }

#if defined(Q_OS_MACOS)
  const QDir app_dir(QCoreApplication::applicationDirPath());
  const auto bundle_resources = app_dir.filePath(QStringLiteral("../Resources/models"));
  if (QFileInfo::exists(bundle_resources)) {
    return QFileInfo(bundle_resources).canonicalFilePath();
  }
#endif

  return QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("models"));
}

QString inpaint_model_path(const QString& root) {
  return QDir(root).filePath(QString::fromLatin1(kInpaintModelRelativePath));
}

AiModelBundleVerification invalid_model_bundle(
    const QString& root,
    const QString& manifest_path,
    bool manifest_available,
    const QString& error_code,
    const QString& error_message) {
  AiModelBundleVerification result;
  result.root = root;
  result.manifest_path = manifest_path;
  result.manifest_available = manifest_available;
  result.error_code = error_code;
  result.error_message = error_message;
  return result;
}

bool valid_sha256(const QString& value) {
  if (value.size() != 64 || value != value.toLower()) {
    return false;
  }
  return std::all_of(
      value.cbegin(), value.cend(), [](const QChar character) {
        return character.isDigit() ||
               (character >= QLatin1Char('a') && character <= QLatin1Char('f'));
      });
}

QString required_json_string(
    const QJsonObject& object,
    const QString& key) {
  const auto value = object.value(key);
  return value.isString() ? value.toString().trimmed() : QString{};
}

AiModelBundleVerification verify_model_bundle_impl(const QString& model_root) {
  const auto absolute_root = QFileInfo(model_root).absoluteFilePath();
  const auto canonical_root = QFileInfo(absolute_root).canonicalFilePath();
  const auto normalized_root = QDir::cleanPath(
      canonical_root.isEmpty() ? absolute_root : canonical_root);
  const auto manifest_path = QDir(normalized_root).filePath(QStringLiteral("manifest.json"));
  const QFileInfo manifest_info(manifest_path);
  if (!manifest_info.isFile()) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        false,
        QStringLiteral("manifest_missing"),
        QStringLiteral("Model manifest is missing: %1").arg(manifest_path));
  }

  QFile manifest_file(manifest_path);
  if (!manifest_file.open(QIODevice::ReadOnly)) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        true,
        QStringLiteral("manifest_unreadable"),
        QStringLiteral("Unable to read model manifest: %1")
            .arg(manifest_file.errorString()));
  }
  QJsonParseError parse_error;
  const auto document =
      QJsonDocument::fromJson(manifest_file.readAll(), &parse_error);
  if (parse_error.error != QJsonParseError::NoError || !document.isObject()) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        true,
        QStringLiteral("manifest_invalid_json"),
        QStringLiteral("Model manifest must contain one valid JSON object."));
  }
  const auto manifest = document.object();
  if (required_json_string(manifest, QStringLiteral("schema")) !=
      QStringLiteral("framehouse-ai-model-bundle-v1")) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        true,
        QStringLiteral("manifest_schema_mismatch"),
        QStringLiteral("Model manifest schema must be framehouse-ai-model-bundle-v1."));
  }
  if (!manifest.value(QStringLiteral("version")).isDouble() ||
      manifest.value(QStringLiteral("version")).toInt() != 1) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        true,
        QStringLiteral("manifest_version_mismatch"),
        QStringLiteral("Model manifest version must be 1."));
  }
  const auto artifacts_value = manifest.value(QStringLiteral("artifacts"));
  if (!artifacts_value.isArray() || artifacts_value.toArray().isEmpty()) {
    return invalid_model_bundle(
        normalized_root,
        manifest_path,
        true,
        QStringLiteral("manifest_artifacts_missing"),
        QStringLiteral("Model manifest artifacts must be a non-empty array."));
  }

  QSet<QString> artifact_ids;
  QSet<QString> artifact_paths;
  auto verified_artifact_count = 0;
  auto verified_inpaint_count = 0;
  auto verified_sam2_encoder_count = 0;
  auto verified_sam2_decoder_count = 0;
  QString verified_inpaint_path;
  QString verified_inpaint_sha256;
  qint64 verified_inpaint_bytes = 0;
  QString verified_sam2_encoder_path;
  QString verified_sam2_encoder_sha256;
  qint64 verified_sam2_encoder_bytes = 0;
  QString verified_sam2_decoder_path;
  QString verified_sam2_decoder_sha256;
  qint64 verified_sam2_decoder_bytes = 0;
  const auto artifacts = artifacts_value.toArray();
  for (auto index = 0; index < artifacts.size(); ++index) {
    if (!artifacts.at(index).isObject()) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_invalid"),
          QStringLiteral("Model artifact %1 must be an object.").arg(index));
    }
    const auto artifact = artifacts.at(index).toObject();
    const auto artifact_id = required_json_string(artifact, QStringLiteral("id"));
    const auto role = required_json_string(artifact, QStringLiteral("role"));
    const auto contract = required_json_string(artifact, QStringLiteral("contract"));
    auto relative_path = required_json_string(artifact, QStringLiteral("path"));
    const auto expected_sha256 =
        required_json_string(artifact, QStringLiteral("sha256"));
    const auto source_url =
        required_json_string(artifact, QStringLiteral("sourceUrl"));
    const auto license_value = artifact.value(QStringLiteral("license"));
    const auto license = license_value.isObject() ? license_value.toObject()
                                                  : QJsonObject{};
    const auto license_name = required_json_string(license, QStringLiteral("name"));
    const auto license_url = required_json_string(license, QStringLiteral("url"));
    if (artifact_id.isEmpty() || artifact_ids.contains(artifact_id)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_id_invalid"),
          QStringLiteral("Model artifact %1 has a missing or duplicate id.").arg(index));
    }
    artifact_ids.insert(artifact_id);
    if (role != QStringLiteral("inpaint") && role != QStringLiteral("selection")) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_role_invalid"),
              QStringLiteral("Model artifact %1 has an unsupported role.").arg(index));
    }
    relative_path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    const auto path_parts = relative_path.split(QLatin1Char('/'), Qt::KeepEmptyParts);
    if (relative_path.isEmpty() || QDir::isAbsolutePath(relative_path) ||
        path_parts.contains(QStringLiteral("..")) || artifact_paths.contains(relative_path)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_path_invalid"),
          QStringLiteral("Model artifact %1 path must be unique and stay inside the bundle.")
              .arg(index));
    }
    const auto is_supported_inpaint_artifact =
        role == QStringLiteral("inpaint") &&
        contract == QString::fromLatin1(kInpaintModelContract) &&
        relative_path == QString::fromLatin1(kInpaintModelRelativePath);
    const auto is_supported_sam2_encoder =
        role == QStringLiteral("selection") &&
        contract == QString::fromLatin1(kSam2EncoderContract) &&
        relative_path == QString::fromLatin1(kSam2EncoderRelativePath);
    const auto is_supported_sam2_decoder =
        role == QStringLiteral("selection") &&
        contract == QString::fromLatin1(kSam2DecoderContract) &&
        relative_path == QString::fromLatin1(kSam2DecoderRelativePath);
    if (contract.isEmpty() ||
        (role == QStringLiteral("inpaint") && !is_supported_inpaint_artifact) ||
        (role == QStringLiteral("selection") &&
         !is_supported_sam2_encoder && !is_supported_sam2_decoder)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_contract_invalid"),
          QStringLiteral("Model artifact %1 has an unsupported tensor contract.").arg(index));
    }
    artifact_paths.insert(relative_path);
    const auto artifact_path = QDir(normalized_root).filePath(relative_path);
    const QFileInfo artifact_info(artifact_path);
    const auto canonical_artifact_path = artifact_info.canonicalFilePath();
    const auto root_prefix = normalized_root + QDir::separator();
    if (!artifact_info.isFile() || canonical_artifact_path.isEmpty() ||
        !canonical_artifact_path.startsWith(root_prefix)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_missing_or_escaped"),
          QStringLiteral("Model artifact is missing or escapes the bundle: %1")
              .arg(artifact_path));
    }
    if (!artifact.value(QStringLiteral("bytes")).isDouble()) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_bytes_invalid"),
          QStringLiteral("Model artifact %1 bytes must be a positive integer.").arg(index));
    }
    const auto expected_bytes_value =
        artifact.value(QStringLiteral("bytes")).toDouble();
    const auto expected_bytes = static_cast<qint64>(expected_bytes_value);
    if (!std::isfinite(expected_bytes_value) ||
        std::floor(expected_bytes_value) != expected_bytes_value ||
        expected_bytes <= 0 || artifact_info.size() != expected_bytes) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_size_mismatch"),
          QStringLiteral("Model artifact %1 byte size does not match its manifest.")
              .arg(index));
    }
    if (!valid_sha256(expected_sha256)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_sha256_invalid"),
          QStringLiteral("Model artifact %1 SHA-256 is invalid.").arg(index));
    }
    QFile artifact_file(canonical_artifact_path);
    if (!artifact_file.open(QIODevice::ReadOnly)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_unreadable"),
          QStringLiteral("Unable to read model artifact: %1")
              .arg(canonical_artifact_path));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    if (!hash.addData(&artifact_file)) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_hash_failed"),
          QStringLiteral("Unable to hash model artifact: %1")
              .arg(canonical_artifact_path));
    }
    const auto actual_sha256 = QString::fromLatin1(hash.result().toHex());
    if (actual_sha256 != expected_sha256) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_sha256_mismatch"),
          QStringLiteral("Model artifact %1 checksum does not match its manifest.")
              .arg(index));
    }
    if (source_url.isEmpty() || license_name.isEmpty() || license_url.isEmpty()) {
      return invalid_model_bundle(
          normalized_root, manifest_path, true,
          QStringLiteral("artifact_provenance_missing"),
          QStringLiteral("Model artifact %1 must include source and license evidence.")
              .arg(index));
    }
    ++verified_artifact_count;
    if (is_supported_inpaint_artifact) {
      ++verified_inpaint_count;
      verified_inpaint_path = canonical_artifact_path;
      verified_inpaint_sha256 = actual_sha256;
      verified_inpaint_bytes = artifact_info.size();
    } else if (is_supported_sam2_encoder) {
      ++verified_sam2_encoder_count;
      verified_sam2_encoder_path = canonical_artifact_path;
      verified_sam2_encoder_sha256 = actual_sha256;
      verified_sam2_encoder_bytes = artifact_info.size();
    } else if (is_supported_sam2_decoder) {
      ++verified_sam2_decoder_count;
      verified_sam2_decoder_path = canonical_artifact_path;
      verified_sam2_decoder_sha256 = actual_sha256;
      verified_sam2_decoder_bytes = artifact_info.size();
    }
  }
  if (verified_inpaint_count != 1) {
    return invalid_model_bundle(
        normalized_root, manifest_path, true,
        QStringLiteral("inpaint_artifact_missing"),
        QStringLiteral("The bundle must contain exactly one verified inpaint/lama.onnx artifact."));
  }
  if (verified_sam2_encoder_count != verified_sam2_decoder_count ||
      verified_sam2_encoder_count > 1 || verified_sam2_decoder_count > 1) {
    return invalid_model_bundle(
        normalized_root, manifest_path, true,
        QStringLiteral("selection_artifact_pair_incomplete"),
        QStringLiteral("SAM2 selection requires exactly one verified image encoder and image decoder."));
  }

  AiModelBundleVerification result;
  result.ok = true;
  result.manifest_available = true;
  result.inpaint_artifact_verified = true;
  result.root = normalized_root;
  result.manifest_path = manifest_path;
  result.inpaint_model_path = verified_inpaint_path;
  result.inpaint_sha256 = verified_inpaint_sha256;
  result.inpaint_bytes = verified_inpaint_bytes;
  result.smart_selection_artifacts_verified = verified_sam2_encoder_count == 1;
  result.smart_selection_encoder_path = verified_sam2_encoder_path;
  result.smart_selection_encoder_sha256 = verified_sam2_encoder_sha256;
  result.smart_selection_encoder_bytes = verified_sam2_encoder_bytes;
  result.smart_selection_decoder_path = verified_sam2_decoder_path;
  result.smart_selection_decoder_sha256 = verified_sam2_decoder_sha256;
  result.smart_selection_decoder_bytes = verified_sam2_decoder_bytes;
  result.artifact_count = verified_artifact_count;
  return result;
}

AiModelBundleVerification verified_model_bundle_for_runtime(
    const QString& model_root) {
  struct Cache final {
    QString root;
    qint64 manifest_bytes = -1;
    qint64 manifest_modified_ms = -1;
    qint64 inpaint_bytes = -1;
    qint64 inpaint_modified_ms = -1;
    qint64 selection_encoder_bytes = -1;
    qint64 selection_encoder_modified_ms = -1;
    qint64 selection_decoder_bytes = -1;
    qint64 selection_decoder_modified_ms = -1;
    AiModelBundleVerification verification;
  };
  static std::mutex cache_mutex;
  static Cache cache;

  const auto absolute_root = QFileInfo(model_root).absoluteFilePath();
  const QFileInfo manifest_info(
      QDir(absolute_root).filePath(QStringLiteral("manifest.json")));
  const QFileInfo inpaint_info(inpaint_model_path(absolute_root));
  const QFileInfo selection_encoder_info(
      QDir(absolute_root).filePath(QString::fromLatin1(kSam2EncoderRelativePath)));
  const QFileInfo selection_decoder_info(
      QDir(absolute_root).filePath(QString::fromLatin1(kSam2DecoderRelativePath)));
  const auto manifest_modified_ms =
      manifest_info.lastModified().toMSecsSinceEpoch();
  const auto inpaint_modified_ms =
      inpaint_info.lastModified().toMSecsSinceEpoch();
  const auto selection_encoder_modified_ms =
      selection_encoder_info.lastModified().toMSecsSinceEpoch();
  const auto selection_decoder_modified_ms =
      selection_decoder_info.lastModified().toMSecsSinceEpoch();
  std::lock_guard<std::mutex> lock(cache_mutex);
  if (cache.root == absolute_root &&
      cache.manifest_bytes == manifest_info.size() &&
      cache.manifest_modified_ms == manifest_modified_ms &&
      cache.inpaint_bytes == inpaint_info.size() &&
      cache.inpaint_modified_ms == inpaint_modified_ms &&
      cache.selection_encoder_bytes == selection_encoder_info.size() &&
      cache.selection_encoder_modified_ms == selection_encoder_modified_ms &&
      cache.selection_decoder_bytes == selection_decoder_info.size() &&
      cache.selection_decoder_modified_ms == selection_decoder_modified_ms) {
    return cache.verification;
  }
  cache.root = absolute_root;
  cache.manifest_bytes = manifest_info.size();
  cache.manifest_modified_ms = manifest_modified_ms;
  cache.inpaint_bytes = inpaint_info.size();
  cache.inpaint_modified_ms = inpaint_modified_ms;
  cache.selection_encoder_bytes = selection_encoder_info.size();
  cache.selection_encoder_modified_ms = selection_encoder_modified_ms;
  cache.selection_decoder_bytes = selection_decoder_info.size();
  cache.selection_decoder_modified_ms = selection_decoder_modified_ms;
  cache.verification = verify_model_bundle_impl(absolute_root);
  return cache.verification;
}

bool inpaint_cancelled(const AiInpaintRequest& request) {
  return request.control && request.control->cancelled();
}
AiInpaintResult cancelled_inpaint_result() {
  AiInpaintResult result; result.cancelled=true;
  result.error_message=QStringLiteral("Inpaint cancelled.");
  return result;
}

bool valid_inpaint_mask(const AiInpaintRequest& request) {
  if (request.source_image.isNull() || request.mask_width <= 0 ||
      request.mask_height <= 0 ||
      request.source_image.width() != request.mask_width ||
      request.source_image.height() != request.mask_height) {
    return false;
  }

  const auto expected_size =
      static_cast<qint64>(request.mask_width) * static_cast<qint64>(request.mask_height);
  return expected_size > 0 && request.mask_data.size() == expected_size;
}

QRect inpaint_mask_bounds(const AiInpaintRequest& request) {
  auto min_x = request.mask_width;
  auto min_y = request.mask_height;
  auto max_x = -1;
  auto max_y = -1;
  for (int y = 0; y < request.mask_height; ++y) {
    for (int x = 0; x < request.mask_width; ++x) {
      const auto value =
          static_cast<unsigned char>(request.mask_data.at(y * request.mask_width + x));
      if (value <= 0U) {
        continue;
      }
      min_x = std::min(min_x, x);
      min_y = std::min(min_y, y);
      max_x = std::max(max_x, x);
      max_y = std::max(max_y, y);
    }
  }

  if (max_x < min_x || max_y < min_y) {
    return {};
  }

  const auto padding = std::clamp(std::max(request.mask_width, request.mask_height) / 32, 8, 32);
  min_x = std::max(0, min_x - padding);
  min_y = std::max(0, min_y - padding);
  max_x = std::min(request.mask_width - 1, max_x + padding);
  max_y = std::min(request.mask_height - 1, max_y + padding);
  return QRect(min_x, min_y, max_x - min_x + 1, max_y - min_y + 1);
}

QColor average_patch_boundary_color(const QImage& source, const QRect& bounds) {
  qint64 red = 0;
  qint64 green = 0;
  qint64 blue = 0;
  qint64 alpha = 0;
  qint64 count = 0;
  const auto expanded = bounds.adjusted(-1, -1, 1, 1).intersected(source.rect());
  for (int y = expanded.top(); y <= expanded.bottom(); ++y) {
    for (int x = expanded.left(); x <= expanded.right(); ++x) {
      if (bounds.contains(x, y)) {
        continue;
      }
      const auto color = source.pixelColor(x, y);
      red += color.red();
      green += color.green();
      blue += color.blue();
      alpha += color.alpha();
      ++count;
    }
  }

  if (count <= 0) {
    const auto center = source.pixelColor(bounds.center());
    return center.isValid() ? center : QColor(Qt::transparent);
  }
  return QColor(
      static_cast<int>(red / count),
      static_cast<int>(green / count),
      static_cast<int>(blue / count),
      static_cast<int>(alpha / count));
}

QRect patch_source_rect(const AiInpaintRequest& request, const QSize& source_size) {
  if (!request.patch_source_set || !source_size.isValid()) {
    return {};
  }
  const auto width = std::clamp(
      static_cast<int>(std::lround(request.patch_source_width * source_size.width())),
      1,
      source_size.width());
  const auto height = std::clamp(
      static_cast<int>(std::lround(request.patch_source_height * source_size.height())),
      1,
      source_size.height());
  const auto x = std::clamp(
      static_cast<int>(std::lround(request.patch_source_x * source_size.width())),
      0,
      std::max(0, source_size.width() - width));
  const auto y = std::clamp(
      static_cast<int>(std::lround(request.patch_source_y * source_size.height())),
      0,
      std::max(0, source_size.height() - height));
  return QRect(x, y, width, height).intersected(QRect(QPoint(0, 0), source_size));
}

double inpaint_mask_coverage(const AiInpaintRequest& request, const QRect& bounds) {
  if (!bounds.isValid()) {
    return 0.0;
  }

  auto masked = 0;
  auto area = 0;
  for (int y = bounds.top(); y <= bounds.bottom(); ++y) {
    for (int x = bounds.left(); x <= bounds.right(); ++x) {
      ++area;
      if (static_cast<unsigned char>(request.mask_data.at(y * request.mask_width + x)) > 0U) {
        ++masked;
      }
    }
  }
  return area > 0 ? static_cast<double>(masked) / static_cast<double>(area) : 0.0;
}

QString inpaint_region_kind(
    const AiInpaintRequest& request,
    double coverage,
    const QRect& bounds) {
  const auto mode = request.retouch_mode.trimmed().toLower();
  const auto source_area =
      static_cast<double>(std::max(1, request.source_image.width())) *
      static_cast<double>(std::max(1, request.source_image.height()));
  const auto padded_footprint =
      static_cast<double>(std::max(0, bounds.width())) *
      static_cast<double>(std::max(0, bounds.height())) / source_area;
  const auto footprint = padded_footprint * std::clamp(coverage, 0.0, 1.0);
  if (mode == QStringLiteral("heal") && footprint <= 0.03) {
    return QStringLiteral("detail-heal");
  }
  if (mode == QStringLiteral("patch")) {
    return footprint >= 0.12 ? QStringLiteral("large-patch")
                             : QStringLiteral("patch-region");
  }
  if (footprint <= 0.015) {
    return QStringLiteral("spot");
  }
  if (footprint >= 0.12) {
    return QStringLiteral("large-object");
  }
  return QStringLiteral("object");
}

bool prompt_requests_texture(const QString& prompt) {
  const auto normalized = prompt.trimmed().toLower();
  return normalized.contains(QStringLiteral("texture")) ||
         normalized.contains(QStringLiteral("grain")) ||
         normalized.contains(QStringLiteral("detail")) ||
         normalized.contains(QStringLiteral("fabric")) ||
         normalized.contains(QStringLiteral("weave")) ||
         normalized.contains(QStringLiteral("bark"));
}

double prompt_texture_multiplier(const QString& prompt) {
  const auto normalized = prompt.trimmed().toLower();
  if (normalized.isEmpty()) {
    return 1.0;
  }
  if (normalized.contains(QStringLiteral("smooth")) ||
      normalized.contains(QStringLiteral("soft")) ||
      normalized.contains(QStringLiteral("clean"))) {
    return 0.65;
  }
  if (prompt_requests_texture(prompt)) {
    return 1.25;
  }
  return 1.0;
}

void populate_inpaint_region_result(
    AiInpaintResult* result,
    const AiInpaintRequest& request,
    const QRect& bounds) {
  if (result == nullptr || !bounds.isValid()) {
    return;
  }

  const auto coverage = inpaint_mask_coverage(request, bounds);
  result->prompt = request.prompt.trimmed();
  const auto quality_mode = request.quality_mode.trimmed().toLower();
  result->quality_mode =
      quality_mode == QStringLiteral("final") ? QStringLiteral("final") : QStringLiteral("preview");
  result->seed = std::max(1, request.seed);
  result->region_kind = inpaint_region_kind(request, coverage, bounds);
  result->region_coverage = coverage;
  result->region_x = bounds.x();
  result->region_y = bounds.y();
  result->region_width = bounds.width();
  result->region_height = bounds.height();
}

AiInpaintResult run_source_region_patch(const AiInpaintRequest& request) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }

  auto source = request.source_image;
  if (!source.colorSpace().isValid()) {
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  source = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA64);
  const auto source_rect = patch_source_rect(request, source.size());
  if (!source_rect.isValid() || source_rect.isEmpty()) {
    result.error_message = QStringLiteral("Patch source region is empty.");
    return result;
  }

  auto patch = source.copy(source_rect).scaled(
      bounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
  if (patch.format() != QImage::Format_RGBA64) {
    patch = patch.convertToFormat(QImage::Format_RGBA64);
  }
  patch.setColorSpace(source.colorSpace());

  if (inpaint_cancelled(request)) return cancelled_inpaint_result();
  result.success = true;
  result.model_name = QStringLiteral("source-region-patch-v1");
  populate_inpaint_region_result(&result, request, bounds);
  result.patch_image = patch;
  result.candidate_images = {patch};
  result.candidate_index = 0;
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

AiInpaintResult run_cpu_diffusion_inpaint(const AiInpaintRequest& request, const QString& source_error = {}) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }
  populate_inpaint_region_result(&result, request, bounds);

  auto source = request.source_image;
  if (!source.colorSpace().isValid()) {
    source.setColorSpace(QColorSpace(QColorSpace::SRgb));
  }
  source = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_ARGB32);

  auto patch = source.copy(bounds);
  const auto patch_width = patch.width();
  const auto patch_height = patch.height();
  std::vector<QRgb> pixels(static_cast<std::size_t>(patch_width * patch_height));
  std::vector<uint8_t> masked(static_cast<std::size_t>(patch_width * patch_height), 0U);
  std::vector<uint8_t> known(static_cast<std::size_t>(patch_width * patch_height), 0U);
  for (int y = 0; y < patch_height; ++y) {
    const auto* scanline = reinterpret_cast<const QRgb*>(patch.constScanLine(y));
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      pixels[index] = scanline[x];
      const auto source_x = bounds.x() + x;
      const auto source_y = bounds.y() + y;
      const auto mask_value =
          static_cast<unsigned char>(request.mask_data.at(source_y * request.mask_width + source_x));
      masked[index] = mask_value > 0U ? 1U : 0U;
      known[index] = mask_value > 0U ? 0U : 1U;
    }
  }

  const auto fallback_color = average_patch_boundary_color(source, bounds);
  const auto fallback_rgb = fallback_color.rgba();
  const auto source_pixels = pixels;
  if (result.region_kind == QStringLiteral("spot")) {
    std::vector<int> nearest_left(pixels.size(), -1);
    std::vector<int> nearest_right(pixels.size(), -1);
    std::vector<int> nearest_top(pixels.size(), -1);
    std::vector<int> nearest_bottom(pixels.size(), -1);
    for (int y = 0; y < patch_height; ++y) {
      auto known_x = -1;
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          known_x = x;
        }
        nearest_left[index] = known_x;
      }
      known_x = -1;
      for (int x = patch_width - 1; x >= 0; --x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          known_x = x;
        }
        nearest_right[index] = known_x;
      }
    }
    for (int x = 0; x < patch_width; ++x) {
      auto known_y = -1;
      for (int y = 0; y < patch_height; ++y) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          known_y = y;
        }
        nearest_top[index] = known_y;
      }
      known_y = -1;
      for (int y = patch_height - 1; y >= 0; --y) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          known_y = y;
        }
        nearest_bottom[index] = known_y;
      }
    }
    const auto interpolate = [](QRgb first, QRgb second, double amount) {
      const auto blend = [&](int first_channel, int second_channel) {
        return static_cast<int>(std::lround(std::lerp(
            static_cast<double>(first_channel),
            static_cast<double>(second_channel),
            std::clamp(amount, 0.0, 1.0))));
      };
      return qRgba(
          blend(qRed(first), qRed(second)),
          blend(qGreen(first), qGreen(second)),
          blend(qBlue(first), qBlue(second)),
          blend(qAlpha(first), qAlpha(second)));
    };
    for (std::size_t index = 0; index < pixels.size(); ++index) {
      if (!masked[index]) {
        continue;
      }
      const auto x = static_cast<int>(index % static_cast<std::size_t>(patch_width));
      const auto y = static_cast<int>(index / static_cast<std::size_t>(patch_width));
      std::array<QRgb, 2> estimates{fallback_rgb, fallback_rgb};
      std::size_t estimate_count = 0;
      const auto left = nearest_left[index];
      const auto right = nearest_right[index];
      if (left >= 0 && right >= 0 && right > left) {
        estimates[estimate_count++] = interpolate(
            source_pixels[static_cast<std::size_t>(y * patch_width + left)],
            source_pixels[static_cast<std::size_t>(y * patch_width + right)],
            static_cast<double>(x - left) / static_cast<double>(right - left));
      }
      const auto top = nearest_top[index];
      const auto bottom = nearest_bottom[index];
      if (top >= 0 && bottom >= 0 && bottom > top) {
        estimates[estimate_count++] = interpolate(
            source_pixels[static_cast<std::size_t>(top * patch_width + x)],
            source_pixels[static_cast<std::size_t>(bottom * patch_width + x)],
            static_cast<double>(y - top) / static_cast<double>(bottom - top));
      }
      if (estimate_count == 2U) {
        pixels[index] = qRgba(
            (qRed(estimates[0]) + qRed(estimates[1])) / 2,
            (qGreen(estimates[0]) + qGreen(estimates[1])) / 2,
            (qBlue(estimates[0]) + qBlue(estimates[1])) / 2,
            (qAlpha(estimates[0]) + qAlpha(estimates[1])) / 2);
      } else if (estimate_count == 1U) {
        pixels[index] = estimates[0];
      } else {
        pixels[index] = fallback_rgb;
      }
      known[index] = 1U;
    }
  }
  const auto max_iterations = std::max(8, patch_width + patch_height);
  std::vector<QRgb> next_pixels = pixels;
  std::vector<uint8_t> next_known = known;
  for (int iteration = 0; iteration < max_iterations; ++iteration) {
    if (inpaint_cancelled(request)) return cancelled_inpaint_result();
    auto progressed = false;
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index] || known[index]) {
          continue;
        }

        double red = 0.0;
        double green = 0.0;
        double blue = 0.0;
        double alpha = 0.0;
        double weight = 0.0;
        for (int offset_y = -1; offset_y <= 1; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= patch_height) {
            continue;
          }
          for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const auto sample_x = x + offset_x;
            if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
              continue;
            }
            const auto sample_index = static_cast<std::size_t>(sample_y * patch_width + sample_x);
            if (!known[sample_index]) {
              continue;
            }
            const auto sample = pixels[sample_index];
            auto candidate_red = qRed(sample);
            auto candidate_green = qGreen(sample);
            auto candidate_blue = qBlue(sample);
            auto candidate_alpha = qAlpha(sample);
            auto candidate_weight = 1.0;
            const auto far_x = x + (offset_x * 2);
            const auto far_y = y + (offset_y * 2);
            if (far_x >= 0 && far_x < patch_width && far_y >= 0 && far_y < patch_height) {
              const auto far_index = static_cast<std::size_t>(far_y * patch_width + far_x);
              if (known[far_index]) {
                const auto far = pixels[far_index];
                candidate_red = std::clamp((qRed(sample) * 2) - qRed(far), 0, 255);
                candidate_green = std::clamp((qGreen(sample) * 2) - qGreen(far), 0, 255);
                candidate_blue = std::clamp((qBlue(sample) * 2) - qBlue(far), 0, 255);
                candidate_alpha = std::clamp((qAlpha(sample) * 2) - qAlpha(far), 0, 255);
                candidate_weight = 2.0;
              }
            }
            red += static_cast<double>(candidate_red) * candidate_weight;
            green += static_cast<double>(candidate_green) * candidate_weight;
            blue += static_cast<double>(candidate_blue) * candidate_weight;
            alpha += static_cast<double>(candidate_alpha) * candidate_weight;
            weight += candidate_weight;
          }
        }
        if (weight <= 0.0) {
          continue;
        }
        next_pixels[index] =
            qRgba(static_cast<int>(std::lround(red / weight)),
                  static_cast<int>(std::lround(green / weight)),
                  static_cast<int>(std::lround(blue / weight)),
                  static_cast<int>(std::lround(alpha / weight)));
        next_known[index] = 1U;
        progressed = true;
      }
    }
    pixels.swap(next_pixels);
    known.swap(next_known);
    if (!progressed) {
      break;
    }
  }

  for (std::size_t index = 0; index < pixels.size(); ++index) {
    if (masked[index] && !known[index]) {
      pixels[index] = fallback_rgb;
      known[index] = 1U;
    }
  }

  next_pixels = pixels;
  // The spot initializer interpolates independently along each row and column.
  // Relax it into a harmonic fill so smooth backgrounds (especially skies)
  // do not retain faint row/column bands from that initialization.
  const auto smoothing_passes = result.region_kind == QStringLiteral("spot")
                                  ? (prompt_requests_texture(result.prompt) ? 1 : 6)
                                  : result.region_kind == QStringLiteral("detail-heal")
                                  ? 1
                                  : result.region_kind == QStringLiteral("large-object") ||
                                        result.region_kind == QStringLiteral("large-patch")
                                      ? 3
                                      : 2;
  for (int pass = 0; pass < smoothing_passes; ++pass) {
    if (inpaint_cancelled(request)) return cancelled_inpaint_result();
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        int red = qRed(pixels[index]) * 2;
        int green = qGreen(pixels[index]) * 2;
        int blue = qBlue(pixels[index]) * 2;
        int alpha = qAlpha(pixels[index]) * 2;
        int weight = 2;
        for (int offset_y = -1; offset_y <= 1; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= patch_height) {
            continue;
          }
          for (int offset_x = -1; offset_x <= 1; ++offset_x) {
            const auto sample_x = x + offset_x;
            if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
              continue;
            }
            const auto sample = pixels[static_cast<std::size_t>(sample_y * patch_width + sample_x)];
            red += qRed(sample);
            green += qGreen(sample);
            blue += qBlue(sample);
            alpha += qAlpha(sample);
            ++weight;
          }
        }
        next_pixels[index] =
            qRgba(red / weight, green / weight, blue / weight, alpha / weight);
      }
    }
    pixels.swap(next_pixels);
  }

  if (result.region_kind == QStringLiteral("spot") &&
      prompt_requests_texture(result.prompt)) {
    std::vector<std::size_t> target_indices;
    std::vector<std::size_t> boundary_indices;
    auto target_left = patch_width;
    auto target_top = patch_height;
    auto target_right = -1;
    auto target_bottom = -1;
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (masked[index]) {
          target_indices.push_back(index);
          target_left = std::min(target_left, x);
          target_top = std::min(target_top, y);
          target_right = std::max(target_right, x);
          target_bottom = std::max(target_bottom, y);
          continue;
        }
        auto near_target = false;
        for (int offset_y = -2; offset_y <= 2 && !near_target; ++offset_y) {
          const auto sample_y = y + offset_y;
          if (sample_y < 0 || sample_y >= patch_height) {
            continue;
          }
          for (int offset_x = -2; offset_x <= 2; ++offset_x) {
            const auto sample_x = x + offset_x;
            if (sample_x < 0 || sample_x >= patch_width) {
              continue;
            }
            if (masked[static_cast<std::size_t>(sample_y * patch_width + sample_x)]) {
              near_target = true;
              break;
            }
          }
        }
        if (near_target) {
          boundary_indices.push_back(index);
        }
      }
    }

    if (!target_indices.empty() && boundary_indices.size() >= 8U) {
      const auto target_width = target_right - target_left + 1;
      const auto target_height = target_bottom - target_top + 1;
      const auto search_x = std::max(target_width * 2, 24);
      const auto search_y = std::max(target_height * 2, 24);
      const auto step = std::max(1, std::min(target_width, target_height) / 10);
      auto best_score = -1.0;
      auto best_dx = 0;
      auto best_dy = 0;
      const auto evaluate_offset = [&](int offset_x, int offset_y) {
        if (offset_x == 0 && offset_y == 0) {
          return;
        }
        if (std::abs(offset_x) < target_width &&
            std::abs(offset_y) < target_height) {
          return;
        }
        const auto shifted_left = bounds.x() + target_left + offset_x;
        const auto shifted_top = bounds.y() + target_top + offset_y;
        const auto shifted_right = bounds.x() + target_right + offset_x;
        const auto shifted_bottom = bounds.y() + target_bottom + offset_y;
        if (shifted_left < 0 || shifted_top < 0 ||
            shifted_right >= source.width() || shifted_bottom >= source.height()) {
          return;
        }
        for (const auto index : target_indices) {
          const auto x = static_cast<int>(index % static_cast<std::size_t>(patch_width));
          const auto y = static_cast<int>(index / static_cast<std::size_t>(patch_width));
          const auto source_x = bounds.x() + x + offset_x;
          const auto source_y = bounds.y() + y + offset_y;
          if (static_cast<unsigned char>(
                  request.mask_data.at(source_y * request.mask_width + source_x)) > 0U) {
            return;
          }
        }
        auto score = 0.0;
        for (const auto index : boundary_indices) {
          const auto x = static_cast<int>(index % static_cast<std::size_t>(patch_width));
          const auto y = static_cast<int>(index / static_cast<std::size_t>(patch_width));
          const auto source_x = bounds.x() + x + offset_x;
          const auto source_y = bounds.y() + y + offset_y;
          if (source_x < 0 || source_x >= source.width() ||
              source_y < 0 || source_y >= source.height()) {
            return;
          }
          const auto reference = source_pixels[index];
          const auto candidate = source.pixel(source_x, source_y);
          const auto red = qRed(reference) - qRed(candidate);
          const auto green = qGreen(reference) - qGreen(candidate);
          const auto blue = qBlue(reference) - qBlue(candidate);
          score += static_cast<double>(red * red + green * green + blue * blue);
        }
        score /= static_cast<double>(boundary_indices.size());
        score += 0.02 * static_cast<double>(offset_x * offset_x + offset_y * offset_y);
        if (best_score < 0.0 || score < best_score) {
          best_score = score;
          best_dx = offset_x;
          best_dy = offset_y;
        }
      };
      for (int offset_y = -search_y; offset_y <= search_y; offset_y += step) {
        for (int offset_x = -search_x; offset_x <= search_x; offset_x += step) {
          evaluate_offset(offset_x, offset_y);
        }
      }
      if (best_score >= 0.0 && step > 1) {
        const auto coarse_dx = best_dx;
        const auto coarse_dy = best_dy;
        for (int offset_y = coarse_dy - step; offset_y <= coarse_dy + step;
             ++offset_y) {
          for (int offset_x = coarse_dx - step; offset_x <= coarse_dx + step;
               ++offset_x) {
            evaluate_offset(offset_x, offset_y);
          }
        }
      }
      if (best_score >= 0.0) {
        for (const auto index : target_indices) {
          const auto x = static_cast<int>(index % static_cast<std::size_t>(patch_width));
          const auto y = static_cast<int>(index / static_cast<std::size_t>(patch_width));
          pixels[index] = source.pixel(
              bounds.x() + x + best_dx, bounds.y() + y + best_dy);
        }
        result.texture_exemplar = true;
        result.texture_synthesis = true;
        result.texture_strength = 1.0;
      }
    }
  }

  std::vector<std::array<int, 3>> texture_residuals;
  texture_residuals.reserve(static_cast<std::size_t>(patch_width + patch_height));
  for (int y = 0; y < patch_height; ++y) {
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      if (masked[index]) {
        continue;
      }

      auto touches_mask = false;
      int red = 0;
      int green = 0;
      int blue = 0;
      int count = 0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= patch_height) {
          continue;
        }
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = x + offset_x;
          if ((offset_x == 0 && offset_y == 0) || sample_x < 0 || sample_x >= patch_width) {
            continue;
          }
          const auto sample_index = static_cast<std::size_t>(sample_y * patch_width + sample_x);
          if (masked[sample_index]) {
            touches_mask = true;
            continue;
          }
          const auto sample = pixels[sample_index];
          red += qRed(sample);
          green += qGreen(sample);
          blue += qBlue(sample);
          ++count;
        }
      }
      if (!touches_mask || count <= 0) {
        continue;
      }

      const auto pixel = pixels[index];
      const std::array<int, 3> residual{
          qRed(pixel) - (red / count),
          qGreen(pixel) - (green / count),
          qBlue(pixel) - (blue / count),
      };
      if (std::abs(residual[0]) + std::abs(residual[1]) + std::abs(residual[2]) > 2) {
        texture_residuals.push_back(residual);
      }
    }
  }

  if (!texture_residuals.empty()) {
    auto residual_sum = 0.0;
    for (const auto& residual : texture_residuals) {
      residual_sum += static_cast<double>(
          std::abs(residual[0]) + std::abs(residual[1]) +
          std::abs(residual[2]));
    }
    result.texture_residual_mean =
        residual_sum /
        (static_cast<double>(texture_residuals.size()) * 3.0);
  }

  const auto synthesize_texture =
      !texture_residuals.empty() &&
      !result.texture_exemplar &&
      (result.region_kind != QStringLiteral("spot") ||
       prompt_requests_texture(result.prompt));
  if (synthesize_texture) {
    const auto base_texture_strength = result.region_kind == QStringLiteral("detail-heal") ||
                                               result.region_kind == QStringLiteral("spot")
                                           ? 0.36
                                           : result.region_kind == QStringLiteral("large-object") ||
                                                 result.region_kind == QStringLiteral("large-patch")
                                               ? 0.34
                                               : 0.28;
    const auto heal_texture_multiplier =
        request.retouch_mode.trimmed().toLower() == QStringLiteral("heal")
            ? std::clamp(request.heal_texture_blend, 0.0, 1.0)
            : 1.0;
    const auto texture_strength =
        std::clamp(base_texture_strength * prompt_texture_multiplier(result.prompt) *
                       heal_texture_multiplier,
                   0.0,
                   0.5);
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        const auto residual_index =
            (static_cast<std::size_t>(x) * 73856093ULL +
             static_cast<std::size_t>(y) * 19349663ULL +
             static_cast<std::size_t>(result.seed) * 83492791ULL) %
            texture_residuals.size();
        const auto residual = texture_residuals[residual_index];
        const auto pixel = pixels[index];
        pixels[index] = qRgba(
            std::clamp(qRed(pixel) + static_cast<int>(std::lround(residual[0] * texture_strength)), 0, 255),
            std::clamp(qGreen(pixel) + static_cast<int>(std::lround(residual[1] * texture_strength)), 0, 255),
            std::clamp(qBlue(pixel) + static_cast<int>(std::lround(residual[2] * texture_strength)), 0, 255),
            qAlpha(pixel));
      }
    }
    result.texture_synthesis = true;
    result.texture_strength = texture_strength;
  }

  for (int y = 0; y < patch_height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(patch.scanLine(y));
    for (int x = 0; x < patch_width; ++x) {
      scanline[x] = pixels[static_cast<std::size_t>(y * patch_width + x)];
    }
  }
  patch.setColorSpace(source.colorSpace());

  const auto image_from_pixels = [&](const std::vector<QRgb>& candidate_pixels) {
    auto image = patch;
    for (int y = 0; y < patch_height; ++y) {
      auto* scanline = reinterpret_cast<QRgb*>(image.scanLine(y));
      for (int x = 0; x < patch_width; ++x) {
        scanline[x] = candidate_pixels[static_cast<std::size_t>(y * patch_width + x)];
      }
    }
    image.setColorSpace(source.colorSpace());
    return image;
  };

  auto smooth_candidate = pixels;
  for (int y = 0;
       result.region_kind != QStringLiteral("spot") && y < patch_height;
       ++y) {
    for (int x = 0; x < patch_width; ++x) {
      const auto index = static_cast<std::size_t>(y * patch_width + x);
      if (!masked[index]) {
        continue;
      }
      int red = 0;
      int green = 0;
      int blue = 0;
      int alpha = 0;
      int count = 0;
      for (int offset_y = -1; offset_y <= 1; ++offset_y) {
        const auto sample_y = y + offset_y;
        if (sample_y < 0 || sample_y >= patch_height) {
          continue;
        }
        for (int offset_x = -1; offset_x <= 1; ++offset_x) {
          const auto sample_x = x + offset_x;
          if (sample_x < 0 || sample_x >= patch_width) {
            continue;
          }
          const auto sample = pixels[static_cast<std::size_t>(sample_y * patch_width + sample_x)];
          red += qRed(sample);
          green += qGreen(sample);
          blue += qBlue(sample);
          alpha += qAlpha(sample);
          ++count;
        }
      }
      if (count > 0) {
        smooth_candidate[index] = qRgba(red / count, green / count, blue / count, alpha / count);
      }
    }
  }

  auto textured_candidate = pixels;
  if (!texture_residuals.empty()) {
    const auto boost = std::clamp(result.texture_strength * 1.35, 0.0, 0.65);
    for (int y = 0; y < patch_height; ++y) {
      for (int x = 0; x < patch_width; ++x) {
        const auto index = static_cast<std::size_t>(y * patch_width + x);
        if (!masked[index]) {
          continue;
        }
        const auto residual_index =
            (static_cast<std::size_t>(x) * 83492791ULL +
             static_cast<std::size_t>(y) * 2971215073ULL +
             static_cast<std::size_t>(result.seed) * 73856093ULL) %
            texture_residuals.size();
        const auto residual = texture_residuals[residual_index];
        const auto pixel = pixels[index];
        textured_candidate[index] = qRgba(
            std::clamp(qRed(pixel) + static_cast<int>(std::lround(residual[0] * boost)), 0, 255),
            std::clamp(qGreen(pixel) + static_cast<int>(std::lround(residual[1] * boost)), 0, 255),
            std::clamp(qBlue(pixel) + static_cast<int>(std::lround(residual[2] * boost)), 0, 255),
            qAlpha(pixel));
      }
    }
  }

  result.success = true;
  result.fallback = true;
  result.edge_continuation = true;
  result.model_name = QStringLiteral("cpu-diffusion-inpaint-v1");
  result.error_message = source_error;
  result.patch_image = patch;
  result.candidate_images = {patch, image_from_pixels(smooth_candidate), image_from_pixels(textured_candidate)};
  result.candidate_index = 0;
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

AiInpaintResult run_fake_inpaint(const AiInpaintRequest& request) {
  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

  const auto bounds = inpaint_mask_bounds(request);
  if (!bounds.isValid()) {
    result.error_message = QStringLiteral("Inpaint mask is empty.");
    return result;
  }

  auto source = request.source_image;
  source = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_ARGB32);

  auto patch = source.copy(bounds);
  const auto fill_color = average_patch_boundary_color(source, bounds);
  for (int y = 0; y < patch.height(); ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(patch.scanLine(y));
    const auto source_y = bounds.y() + y;
    for (int x = 0; x < patch.width(); ++x) {
      const auto source_x = bounds.x() + x;
      const auto mask_value =
          static_cast<unsigned char>(request.mask_data.at(source_y * request.mask_width + source_x));
      if (mask_value <= 0U) {
        continue;
      }
      scanline[x] = fill_color.rgba();
    }
  }

  result.success = true;
  result.model_name = QStringLiteral("test-fake-lama");
  populate_inpaint_region_result(&result, request, bounds);
  result.patch_image = patch;
  result.candidate_images = {patch};
  result.patch_x = bounds.x();
  result.patch_y = bounds.y();
  result.patch_width = bounds.width();
  result.patch_height = bounds.height();
  return result;
}

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
QByteArray mask_patch_bytes(const AiInpaintRequest& request, const QRect& bounds) {
  QByteArray patch(bounds.width() * bounds.height(), '\0');
  for (int y = 0; y < bounds.height(); ++y) {
    const auto source_y = bounds.y() + y;
    for (int x = 0; x < bounds.width(); ++x) {
      const auto source_x = bounds.x() + x;
      patch[y * bounds.width() + x] =
          request.mask_data.at(source_y * request.mask_width + source_x);
    }
  }
  return patch;
}

QSize model_input_size_for_patch(const QSize& patch_size) {
  if (!patch_size.isValid()) {
    return {};
  }

  return {512, 512};
}

QRect model_context_bounds(
    const AiInpaintRequest& request,
    const QRect& reviewed_bounds) {
  const auto source_bounds = QRect(
      0,
      0,
      request.source_image.width(),
      request.source_image.height());
  if (!reviewed_bounds.isValid() || !source_bounds.isValid()) {
    return {};
  }
  const auto requested_edge = std::max(
      256,
      std::max(reviewed_bounds.width(), reviewed_bounds.height()) * 2);
  const auto context_width = std::min(requested_edge, source_bounds.width());
  const auto context_height = std::min(requested_edge, source_bounds.height());
  const auto center = reviewed_bounds.center();
  const auto context_x = std::clamp(
      center.x() - (context_width / 2),
      source_bounds.left(),
      source_bounds.right() - context_width + 1);
  const auto context_y = std::clamp(
      center.y() - (context_height / 2),
      source_bounds.top(),
      source_bounds.bottom() - context_height + 1);
  return QRect(context_x, context_y, context_width, context_height);
}

std::string lower_ascii(std::string value) {
  std::transform(value.begin(), value.end(), value.begin(), [](unsigned char character) {
    return static_cast<char>(std::tolower(character));
  });
  return value;
}

struct NamedOrtValue final {
  std::string value;

  [[nodiscard]] const char* name() const {
    return value.c_str();
  }
};

NamedOrtValue ort_value_name(
    Ort::Session* session,
    std::size_t index,
    bool input,
    const char* fallback) {
  Ort::AllocatorWithDefaultOptions allocator;
  try {
    auto allocated_name = input ? session->GetInputNameAllocated(index, allocator)
                                : session->GetOutputNameAllocated(index, allocator);
    return NamedOrtValue{
        .value = allocated_name ? allocated_name.get() : fallback,
    };
  } catch (const Ort::Exception&) {
    return NamedOrtValue{
        .value = fallback,
    };
  }
}

class OnnxAiRuntime final {
 public:
  [[nodiscard]] SegmentationResult runSmartSelect(
      const QImage& source_image,
      const QVector<SmartSelectPrompt>& prompts,
      const SegmentationResult* previous_mask) {
    SegmentationResult result;
    if (source_image.isNull() || source_image.width() <= 0 || source_image.height() <= 0) {
      result.error_message = QStringLiteral("Smart Select requires a valid source image.");
      return result;
    }
    if (prompts.isEmpty() || prompts.size() > 64) {
      result.error_message = QStringLiteral("Smart Select requires between one and 64 prompt points.");
      return result;
    }

    auto has_positive_prompt = false;
    for (const auto& prompt : prompts) {
      if (!std::isfinite(prompt.x) || !std::isfinite(prompt.y) ||
          prompt.x < 0.0 || prompt.x > 1.0 || prompt.y < 0.0 || prompt.y > 1.0) {
        result.error_message = QStringLiteral("Smart Select prompt coordinates must be normalized image positions.");
        return result;
      }
      has_positive_prompt = has_positive_prompt || !prompt.negative;
      if (!prompt.box.isNull() &&
          (!prompt.box.isValid() || !std::isfinite(prompt.box.left()) ||
           !std::isfinite(prompt.box.top()) || !std::isfinite(prompt.box.right()) ||
           !std::isfinite(prompt.box.bottom()) || prompt.box.left() < 0.0 ||
           prompt.box.top() < 0.0 || prompt.box.right() > 1.0 ||
           prompt.box.bottom() > 1.0 || prompt.negative)) {
        result.error_message = QStringLiteral("Smart Select boxes must have valid normalized bounds.");
        return result;
      }
    }
    if (!has_positive_prompt) {
      result.error_message = QStringLiteral("Smart Select requires at least one positive point.");
      return result;
    }

    const auto bundle = verify_model_bundle_impl(default_model_root());
    if (!bundle.ok || !bundle.smart_selection_artifacts_verified) {
      result.error_message = bundle.error_message.isEmpty()
          ? QStringLiteral("SAM2 model bundle failed integrity verification.")
          : bundle.error_message;
      return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (!ensureSamSessions(bundle, &result.error_message)) return result;
    try {
      constexpr auto model_size = 1024;
      constexpr auto model_mask_size = 256;
      auto color_managed_source = source_image;
      if (!color_managed_source.colorSpace().isValid()) {
        color_managed_source.setColorSpace(QColorSpace(QColorSpace::SRgb));
      }
      const auto image = color_managed_source.convertedToColorSpace(
          QColorSpace(QColorSpace::SRgb), QImage::Format_RGB888);
      const auto scale = static_cast<double>(model_size) /
                         static_cast<double>(std::max(image.width(), image.height()));
      const auto resized_width = std::clamp(
          qRound(static_cast<double>(image.width()) * scale), 1, model_size);
      const auto resized_height = std::clamp(
          qRound(static_cast<double>(image.height()) * scale), 1, model_size);
      const auto resized = image.scaled(
          QSize(resized_width, resized_height), Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
                               .convertToFormat(QImage::Format_RGB888);

      QCryptographicHash image_hasher(QCryptographicHash::Sha256);
      const auto dimensions = QByteArray::number(image.width()) + 'x' + QByteArray::number(image.height());
      image_hasher.addData(dimensions);
      for (int y = 0; y < image.height(); ++y) {
        image_hasher.addData(QByteArrayView(
            reinterpret_cast<const char*>(image.constScanLine(y)), image.width() * 3));
      }
      const auto image_key = image_hasher.result();
      if (sam_image_key_ != image_key || sam_encoder_features_.empty()) {
        std::vector<float> input(static_cast<std::size_t>(3 * model_size * model_size), 0.0F);
        constexpr std::array<float, 3> means{0.485F, 0.456F, 0.406F};
        constexpr std::array<float, 3> standard_deviations{0.229F, 0.224F, 0.225F};
        const auto plane_size = static_cast<std::size_t>(model_size * model_size);
        for (int y = 0; y < resized_height; ++y) {
          const auto* scanline = resized.constScanLine(y);
          for (int x = 0; x < resized_width; ++x) {
            const auto input_index = static_cast<std::size_t>(y * model_size + x);
            for (int channel = 0; channel < 3; ++channel) {
              const auto value = static_cast<float>(scanline[x * 3 + channel]) / 255.0F;
              input[static_cast<std::size_t>(channel) * plane_size + input_index] =
                  (value - means[static_cast<std::size_t>(channel)]) /
                  standard_deviations[static_cast<std::size_t>(channel)];
            }
          }
        }

        const std::array<int64_t, 4> image_shape{1, 3, model_size, model_size};
        auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
        auto input_tensor = Ort::Value::CreateTensor<float>(
            memory_info, input.data(), input.size(), image_shape.data(), image_shape.size());
        const std::array<const char*, 1> input_names{"image"};
        const std::array<const char*, 3> output_names{
            "image_embed", "high_res_feats_0", "high_res_feats_1"};
        auto output_values = sam_encoder_session_->Run(
            Ort::RunOptions{nullptr}, input_names.data(), &input_tensor, 1,
            output_names.data(), output_names.size());
        if (output_values.size() != output_names.size()) {
          result.error_message = QStringLiteral("SAM2 image encoder returned an incomplete feature set.");
          return result;
        }
        sam_encoder_features_.clear();
        sam_encoder_features_.reserve(output_values.size());
        for (auto& value : output_values) {
          if (!value.IsTensor() || value.GetTensorTypeAndShapeInfo().GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT) {
            result.error_message = QStringLiteral("SAM2 image encoder returned a non-float feature tensor.");
            sam_encoder_features_.clear();
            return result;
          }
          const auto info = value.GetTensorTypeAndShapeInfo();
          const auto* data = value.GetTensorData<float>();
          const auto count = static_cast<std::size_t>(info.GetElementCount());
          if (data == nullptr || count == 0) {
            result.error_message = QStringLiteral("SAM2 image encoder returned an empty feature tensor.");
            sam_encoder_features_.clear();
            return result;
          }
          sam_encoder_features_.emplace_back(data, data + count);
        }
        sam_image_key_ = image_key;
      }

      std::vector<float> point_coordinates;
      std::vector<float> point_labels;
      point_coordinates.reserve(static_cast<std::size_t>(prompts.size() * 2));
      point_labels.reserve(static_cast<std::size_t>(prompts.size()));
      const auto append_prompt = [&](double x, double y, float label) {
        const auto source_x = std::clamp(x * image.width(), 0.0, static_cast<double>(image.width() - 1));
        const auto source_y = std::clamp(y * image.height(), 0.0, static_cast<double>(image.height() - 1));
        point_coordinates.push_back(static_cast<float>(source_x * resized_width / image.width()));
        point_coordinates.push_back(static_cast<float>(source_y * resized_height / image.height()));
        point_labels.push_back(label);
      };
      for (const auto& prompt : prompts) {
        if (prompt.box.isValid()) {
          // SAM's trained corner embeddings use labels 2 and 3.
          append_prompt(prompt.box.left(), prompt.box.top(), 2.0F);
          append_prompt(prompt.box.right(), prompt.box.bottom(), 3.0F);
        } else {
          append_prompt(prompt.x, prompt.y, prompt.negative ? 0.0F : 1.0F);
        }
      }
      const auto prompt_count = static_cast<int64_t>(point_labels.size());
      const std::array<int64_t, 3> coordinates_shape{1, prompt_count, 2};
      const std::array<int64_t, 2> labels_shape{1, prompt_count};
      const std::array<int64_t, 4> mask_shape{1, 1, model_mask_size, model_mask_size};
      const std::array<int64_t, 1> flag_shape{1};
      std::vector<float> mask_input(static_cast<std::size_t>(model_mask_size * model_mask_size), 0.0F);
      std::array<float, 1> has_mask_input{0.0F};
      if (previous_mask != nullptr && previous_mask->success &&
          previous_mask->width > 0 && previous_mask->height > 0 &&
          previous_mask->mask_data.size() == previous_mask->width * previous_mask->height) {
        const auto refinement_logit_bytes =
            static_cast<qsizetype>(model_mask_size * model_mask_size) *
            static_cast<qsizetype>(sizeof(float));
        const auto has_exact_refinement_logits =
            previous_mask->width == image.width() &&
            previous_mask->height == image.height() &&
            previous_mask->refinement_logits_width == model_mask_size &&
            previous_mask->refinement_logits_height == model_mask_size &&
            previous_mask->refinement_logits.size() == refinement_logit_bytes;
        if (has_exact_refinement_logits) {
          std::memcpy(
              mask_input.data(),
              previous_mask->refinement_logits.constData(),
              static_cast<std::size_t>(refinement_logit_bytes));
          has_mask_input[0] = 1.0F;
        } else {
          QImage previous_alpha(
              reinterpret_cast<const uchar*>(previous_mask->mask_data.constData()),
              previous_mask->width,
              previous_mask->height,
              previous_mask->width,
              QImage::Format_Grayscale8);
          const auto decoder_mask_width = std::clamp(
              qRound(static_cast<double>(resized_width) * model_mask_size / model_size),
              1,
              model_mask_size);
          const auto decoder_mask_height = std::clamp(
              qRound(static_cast<double>(resized_height) * model_mask_size / model_size),
              1,
              model_mask_size);
          const auto scaled_previous_alpha = previous_alpha.copy().scaled(
              QSize(decoder_mask_width, decoder_mask_height),
              Qt::IgnoreAspectRatio,
              Qt::SmoothTransformation);
          constexpr auto kMaskLogitScale = 1.5;
          constexpr auto kMaskProbabilityFloor = 1.0 / 255.0;
          for (int y = 0; y < decoder_mask_height; ++y) {
            const auto* row = scaled_previous_alpha.constScanLine(y);
            for (int x = 0; x < decoder_mask_width; ++x) {
              const auto probability = std::clamp(
                  static_cast<double>(row[x]) / 255.0,
                  kMaskProbabilityFloor,
                  1.0 - kMaskProbabilityFloor);
              const auto index = static_cast<std::size_t>(y * model_mask_size + x);
              mask_input[index] = static_cast<float>(
                  std::log(probability / (1.0 - probability)) / kMaskLogitScale);
            }
          }
          for (int y = 0; y < model_mask_size; ++y) {
            for (int x = 0; x < model_mask_size; ++x) {
              if (x >= decoder_mask_width || y >= decoder_mask_height) {
                mask_input[static_cast<std::size_t>(y * model_mask_size + x)] = -32.0F;
              }
            }
          }
          has_mask_input[0] = 1.0F;
        }
      }
      auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      std::vector<Ort::Value> decoder_inputs;
      decoder_inputs.reserve(7);
      const auto add_feature = [&](std::size_t index, std::array<int64_t, 4> shape) {
        decoder_inputs.emplace_back(Ort::Value::CreateTensor<float>(
            memory_info, sam_encoder_features_.at(index).data(), sam_encoder_features_.at(index).size(),
            shape.data(), shape.size()));
      };
      add_feature(0, {1, 256, 64, 64});
      add_feature(1, {1, 32, 256, 256});
      add_feature(2, {1, 64, 128, 128});
      decoder_inputs.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info, point_coordinates.data(), point_coordinates.size(), coordinates_shape.data(), coordinates_shape.size()));
      decoder_inputs.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info, point_labels.data(), point_labels.size(), labels_shape.data(), labels_shape.size()));
      decoder_inputs.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info, mask_input.data(), mask_input.size(), mask_shape.data(), mask_shape.size()));
      decoder_inputs.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info, has_mask_input.data(), has_mask_input.size(), flag_shape.data(), flag_shape.size()));
      const std::array<const char*, 7> decoder_input_names{
          "image_embed", "high_res_feats_0", "high_res_feats_1", "point_coords",
          "point_labels", "mask_input", "has_mask_input"};
      const std::array<const char*, 2> decoder_output_names{"masks", "iou_predictions"};
      auto decoder_outputs = sam_decoder_session_->Run(
          Ort::RunOptions{nullptr}, decoder_input_names.data(), decoder_inputs.data(), decoder_inputs.size(),
          decoder_output_names.data(), decoder_output_names.size());
      if (decoder_outputs.size() != decoder_output_names.size() ||
          !decoder_outputs[0].IsTensor() || !decoder_outputs[1].IsTensor()) {
        result.error_message = QStringLiteral("SAM2 prompt decoder returned invalid outputs.");
        return result;
      }
      const auto mask_info = decoder_outputs[0].GetTensorTypeAndShapeInfo();
      const auto score_info = decoder_outputs[1].GetTensorTypeAndShapeInfo();
      const auto mask_shape_out = mask_info.GetShape();
      const auto score_shape_out = score_info.GetShape();
      if (mask_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
          score_info.GetElementType() != ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT ||
          mask_shape_out.size() != 4 || score_shape_out.size() != 2 ||
          mask_shape_out[0] != 1 || mask_shape_out[1] < 1 ||
          mask_shape_out[2] != model_mask_size || mask_shape_out[3] != model_mask_size ||
          score_shape_out[0] != 1 || score_shape_out[1] < mask_shape_out[1]) {
        result.error_message = QStringLiteral("SAM2 prompt decoder output tensor contract did not match the pinned model.");
        return result;
      }

      const auto* masks = decoder_outputs[0].GetTensorData<float>();
      const auto* scores = decoder_outputs[1].GetTensorData<float>();
      if (masks == nullptr || scores == nullptr) {
        result.error_message = QStringLiteral("SAM2 prompt decoder returned empty output data.");
        return result;
      }
      const auto candidate_count = static_cast<int>(mask_shape_out[1]);
      const auto plane_size = static_cast<std::size_t>(model_mask_size * model_mask_size);
      constexpr auto prompt_logit_threshold = -0.34F;
      const auto sample_candidate_logit = [&](int candidate, const SmartSelectPrompt& prompt) {
        const auto resized_x = std::clamp(prompt.x * resized_width, 0.0, static_cast<double>(resized_width - 1));
        const auto resized_y = std::clamp(prompt.y * resized_height, 0.0, static_cast<double>(resized_height - 1));
        const auto model_x = (resized_x + 0.5) * model_mask_size / model_size - 0.5;
        const auto model_y = (resized_y + 0.5) * model_mask_size / model_size - 0.5;
        const auto x0 = std::clamp(static_cast<int>(std::floor(model_x)), 0, model_mask_size - 1);
        const auto x1 = std::min(x0 + 1, model_mask_size - 1);
        const auto y0 = std::clamp(static_cast<int>(std::floor(model_y)), 0, model_mask_size - 1);
        const auto y1 = std::min(y0 + 1, model_mask_size - 1);
        const auto fx = static_cast<float>(std::clamp(model_x - std::floor(model_x), 0.0, 1.0));
        const auto fy = static_cast<float>(std::clamp(model_y - std::floor(model_y), 0.0, 1.0));
        const auto* logits = masks + static_cast<std::size_t>(candidate) * plane_size;
        const auto a = logits[static_cast<std::size_t>(y0 * model_mask_size + x0)] * (1.0F - fx) +
                       logits[static_cast<std::size_t>(y0 * model_mask_size + x1)] * fx;
        const auto b = logits[static_cast<std::size_t>(y1 * model_mask_size + x0)] * (1.0F - fx) +
                       logits[static_cast<std::size_t>(y1 * model_mask_size + x1)] * fx;
        return a * (1.0F - fy) + b * fy;
      };
      auto best_candidate = -1;
      auto best_score = -std::numeric_limits<float>::infinity();
      auto best_mask_alpha_logit_scale = kSam2MaskAlphaLogitScale;
      for (int candidate = 0; candidate < candidate_count; ++candidate) {
        auto honors_all_prompts = true;
        auto candidate_mask_alpha_logit_scale = kSam2MaskAlphaLogitScale;
        auto maximum_candidate_mask_alpha_logit_scale =
            kSam2MaximumMaskAlphaLogitScale;
        for (const auto& prompt : prompts) {
          if (prompt.box.isValid()) continue;
          const auto logit = sample_candidate_logit(candidate, prompt);
          honors_all_prompts = honors_all_prompts &&
              (prompt.negative ? logit < prompt_logit_threshold
                               : logit >= prompt_logit_threshold);
          if (prompt.negative && logit < 0.0F) {
            candidate_mask_alpha_logit_scale = std::max(
                candidate_mask_alpha_logit_scale,
                kSam2NegativePromptAlphaLogitThreshold / logit + 0.01F);
          } else if (!prompt.negative && logit < 0.0F) {
            maximum_candidate_mask_alpha_logit_scale = std::min(
                maximum_candidate_mask_alpha_logit_scale,
                kSam2PositivePromptAlphaLogitThreshold / logit);
          }
        }
        honors_all_prompts = honors_all_prompts &&
            candidate_mask_alpha_logit_scale <= maximum_candidate_mask_alpha_logit_scale;
        if (honors_all_prompts && scores[candidate] > best_score) {
          best_candidate = candidate;
          best_score = scores[candidate];
          best_mask_alpha_logit_scale = candidate_mask_alpha_logit_scale;
        }
      }
      if (best_candidate < 0) {
        result.error_message = QStringLiteral(
            "SAM2 found no mask candidate that honors all positive and negative points. "
            "Adjust the prompts or refine the mask manually.");
        return result;
      }
      const auto* logits = masks + static_cast<std::size_t>(best_candidate) * plane_size;
      result.refinement_logits.resize(static_cast<qsizetype>(plane_size * sizeof(float)));
      std::memcpy(
          result.refinement_logits.data(),
          logits,
          plane_size * sizeof(float));
      result.refinement_logits_width = model_mask_size;
      result.refinement_logits_height = model_mask_size;
      result.width = image.width();
      result.height = image.height();
      result.mask_data.resize(result.width * result.height);
      for (int y = 0; y < result.height; ++y) {
        auto* output = result.mask_data.data() + y * result.width;
        const auto resized_y = (static_cast<double>(y) + 0.5) * resized_height / result.height - 0.5;
        const auto model_y = (resized_y + 0.5) * model_mask_size / model_size - 0.5;
        const auto y0 = std::clamp(static_cast<int>(std::floor(model_y)), 0, model_mask_size - 1);
        const auto y1 = std::min(y0 + 1, model_mask_size - 1);
        const auto fy = std::clamp(model_y - std::floor(model_y), 0.0, 1.0);
        for (int x = 0; x < result.width; ++x) {
          const auto resized_x = (static_cast<double>(x) + 0.5) * resized_width / result.width - 0.5;
          const auto model_x = (resized_x + 0.5) * model_mask_size / model_size - 0.5;
          const auto x0 = std::clamp(static_cast<int>(std::floor(model_x)), 0, model_mask_size - 1);
          const auto x1 = std::min(x0 + 1, model_mask_size - 1);
          const auto fx = std::clamp(model_x - std::floor(model_x), 0.0, 1.0);
          const auto a = logits[static_cast<std::size_t>(y0 * model_mask_size + x0)] * static_cast<float>(1.0 - fx) +
                         logits[static_cast<std::size_t>(y0 * model_mask_size + x1)] * static_cast<float>(fx);
          const auto b = logits[static_cast<std::size_t>(y1 * model_mask_size + x0)] * static_cast<float>(1.0 - fx) +
                         logits[static_cast<std::size_t>(y1 * model_mask_size + x1)] * static_cast<float>(fx);
          const auto logit = a * static_cast<float>(1.0 - fy) + b * static_cast<float>(fy);
          const auto probability =
              1.0F / (1.0F + std::exp(-std::clamp(
                  logit * best_mask_alpha_logit_scale, -16.0F, 16.0F)));
          const auto alpha = std::clamp(qRound(probability * 255.0F), 0, 255);
          output[x] = static_cast<char>(alpha);
        }
      }
      result.success = true;
      return result;
    } catch (const Ort::Exception& exception) {
      result.error_message = QStringLiteral("SAM2 ONNX inference failed: %1").arg(QString::fromUtf8(exception.what()));
    } catch (const std::exception& exception) {
      result.error_message = QStringLiteral("SAM2 ONNX inference failed: %1").arg(QString::fromUtf8(exception.what()));
    }
    return result;
  }

  [[nodiscard]] AiInpaintResult runInpaint(const AiInpaintRequest& request) {
    AiInpaintResult result;
    if (!valid_inpaint_mask(request)) {
      result.error_message = QStringLiteral("Invalid inpaint request.");
      return result;
    }

    const auto bounds = inpaint_mask_bounds(request);
    if (!bounds.isValid()) {
      result.error_message = QStringLiteral("Inpaint mask is empty.");
      return result;
    }

    const auto bundle = verify_model_bundle_impl(default_model_root());
    if (!bundle.ok || !bundle.inpaint_artifact_verified) {
      result.error_message =
          bundle.error_message.isEmpty()
              ? QStringLiteral("LaMa inpaint model bundle failed integrity verification.")
              : bundle.error_message;
      return result;
    }
    const auto model_path = bundle.inpaint_model_path;
    const QFileInfo model_info(model_path);
    if (!model_info.exists()) {
      result.error_message = QStringLiteral("LaMa inpaint model is missing at %1.").arg(model_path);
      return result;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (inpaint_cancelled(request)) return cancelled_inpaint_result();
    if (!ensureInpaintSession(
            model_info.absoluteFilePath(),
            bundle.inpaint_sha256,
            &result.error_message)) {
      return result;
    }

    const auto context_bounds = model_context_bounds(request, bounds);
    if (!context_bounds.isValid()) {
      result.error_message = QStringLiteral("Unable to construct LaMa context bounds.");
      return result;
    }
    return runInpaintSession(request, context_bounds, bounds);
  }

 private:
  [[nodiscard]] bool ensureSamSessions(
      const AiModelBundleVerification& bundle,
      QString* error_message) {
    if (sam_encoder_session_ && sam_decoder_session_ &&
        sam_encoder_model_path_ == bundle.smart_selection_encoder_path &&
        sam_encoder_model_sha256_ == bundle.smart_selection_encoder_sha256 &&
        sam_decoder_model_path_ == bundle.smart_selection_decoder_path &&
        sam_decoder_model_sha256_ == bundle.smart_selection_decoder_sha256) {
      return true;
    }
    try {
      if (!env_) env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "photo_editor_ai");
      Ort::SessionOptions options;
      options.SetIntraOpNumThreads(6);
      options.SetInterOpNumThreads(1);
      options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
      auto encoder = std::make_unique<Ort::Session>(
          *env_, bundle.smart_selection_encoder_path.toUtf8().constData(), options);
      auto decoder = std::make_unique<Ort::Session>(
          *env_, bundle.smart_selection_decoder_path.toUtf8().constData(), options);
      sam_encoder_session_ = std::move(encoder);
      sam_decoder_session_ = std::move(decoder);
      sam_encoder_model_path_ = bundle.smart_selection_encoder_path;
      sam_encoder_model_sha256_ = bundle.smart_selection_encoder_sha256;
      sam_decoder_model_path_ = bundle.smart_selection_decoder_path;
      sam_decoder_model_sha256_ = bundle.smart_selection_decoder_sha256;
      sam_encoder_features_.clear();
      sam_image_key_.clear();
      return true;
    } catch (const Ort::Exception& exception) {
      if (error_message) *error_message = QStringLiteral("Unable to load SAM2 ONNX sessions: %1").arg(QString::fromUtf8(exception.what()));
    } catch (const std::exception& exception) {
      if (error_message) *error_message = QStringLiteral("Unable to load SAM2 ONNX sessions: %1").arg(QString::fromUtf8(exception.what()));
    }
    sam_encoder_session_.reset();
    sam_decoder_session_.reset();
    sam_encoder_features_.clear();
    sam_image_key_.clear();
    return false;
  }

  [[nodiscard]] bool ensureInpaintSession(
      const QString& model_path,
      const QString& model_sha256,
      QString* error_message) {
    if (inpaint_session_ && inpaint_model_path_ == model_path &&
        inpaint_model_sha256_ == model_sha256) {
      return true;
    }

    try {
      if (!env_) {
        env_ = std::make_unique<Ort::Env>(ORT_LOGGING_LEVEL_ERROR, "photo_editor_ai");
      }

      const auto make_options = []() {
        Ort::SessionOptions options;
        options.SetIntraOpNumThreads(8);
        options.SetInterOpNumThreads(1);
        options.SetGraphOptimizationLevel(GraphOptimizationLevel::ORT_ENABLE_ALL);
        return options;
      };
#if defined(PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML) && PHOTO_EDITOR_ONNXRUNTIME_ENABLE_COREML
      auto cache_base = QStandardPaths::writableLocation(
          QStandardPaths::GenericCacheLocation);
      if (cache_base.isEmpty()) {
        cache_base = QDir::tempPath();
      }
      auto cache_root = QDir(cache_base).filePath(
          QStringLiteral("Framehouse/onnxruntime-coreml/%1")
              .arg(model_sha256));
      if (!QDir().mkpath(cache_root)) {
        cache_root = QDir(QDir::tempPath()).filePath(
            QStringLiteral("Framehouse/onnxruntime-coreml/%1")
                .arg(model_sha256));
      }
      if (QDir().mkpath(cache_root)) {
        try {
          auto coreml_options = make_options();
          coreml_options.AppendExecutionProvider(
              "CoreML",
              std::unordered_map<std::string, std::string>{
                  {kCoremlProviderOption_ModelFormat, "MLProgram"},
                  {kCoremlProviderOption_MLComputeUnits, "ALL"},
                  {kCoremlProviderOption_RequireStaticInputShapes, "1"},
                  {kCoremlProviderOption_EnableOnSubgraphs, "0"},
                  {kCoremlProviderOption_ModelCacheDirectory,
                   cache_root.toStdString()},
              });
          inpaint_session_ = std::make_unique<Ort::Session>(
              *env_, model_path.toUtf8().constData(), coreml_options);
          inpaint_provider_ = QStringLiteral("coreml");
          inpaint_acceleration_warning_.clear();
        } catch (const Ort::Exception& exception) {
          // CoreML is acceleration only. The verified ONNX graph remains the
          // production model and must continue through ONNX Runtime CPU.
          inpaint_session_.reset();
          inpaint_acceleration_warning_ =
              QStringLiteral("CoreML acceleration unavailable: %1")
                  .arg(QString::fromUtf8(exception.what()));
        }
      } else {
        inpaint_acceleration_warning_ =
            QStringLiteral("CoreML acceleration cache directory is not writable.");
      }
#endif
      if (!inpaint_session_) {
        auto cpu_options = make_options();
        inpaint_session_ = std::make_unique<Ort::Session>(
            *env_, model_path.toUtf8().constData(), cpu_options);
        inpaint_provider_ = QStringLiteral("cpu");
      }
      inpaint_model_path_ = model_path;
      inpaint_model_sha256_ = model_sha256;
      return true;
    } catch (const Ort::Exception& exception) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to load LaMa ONNX session: %1")
                             .arg(QString::fromUtf8(exception.what()));
      }
    } catch (const std::exception& exception) {
      if (error_message != nullptr) {
        *error_message = QStringLiteral("Unable to load LaMa ONNX session: %1")
                             .arg(QString::fromUtf8(exception.what()));
      }
    }

    inpaint_session_.reset();
    inpaint_model_path_.clear();
    inpaint_model_sha256_.clear();
    inpaint_provider_.clear();
    return false;
  }

  [[nodiscard]] AiInpaintResult runInpaintSession(
      const AiInpaintRequest& request,
      const QRect& bounds,
      const QRect& reviewed_bounds) {
    AiInpaintResult result;
    if (!inpaint_session_) {
      result.error_message = QStringLiteral("LaMa inpaint session is not loaded.");
      return result;
    }

    const auto input_count = inpaint_session_->GetInputCount();
    const auto output_count = inpaint_session_->GetOutputCount();
    if (input_count < 2 || output_count < 1) {
      result.error_message =
          QStringLiteral("LaMa ONNX model must expose at least two inputs and one output.");
      return result;
    }

    try {
      auto source = request.source_image;
      if (!source.colorSpace().isValid()) {
        source.setColorSpace(QColorSpace(QColorSpace::SRgb));
      }
      source = source.convertedToColorSpace(QColorSpace(QColorSpace::SRgb),QImage::Format_RGBA32FPx4);
      const auto patch_source = source.copy(bounds);
      const auto patch_mask = mask_patch_bytes(request, bounds);
      const auto model_size = model_input_size_for_patch(bounds.size());
      if (!model_size.isValid()) {
        result.error_message = QStringLiteral("Invalid LaMa inpaint patch bounds.");
        return result;
      }

      const auto model_width = model_size.width();
      const auto model_height = model_size.height();
      const auto scaled_content=patch_source.scaled(model_size,Qt::KeepAspectRatio,Qt::SmoothTransformation)
          .convertToFormat(QImage::Format_RGBA32FPx4);
      const QPoint padding((model_width-scaled_content.width())/2,(model_height-scaled_content.height())/2);
      QImage scaled_patch(model_size,QImage::Format_RGBA32FPx4);
      for(int y=0;y<model_height;++y) {
        auto* out=reinterpret_cast<QRgbaFloat32*>(scaled_patch.scanLine(y));
        const auto* in=reinterpret_cast<const QRgbaFloat32*>(scaled_content.constScanLine(
            std::clamp(y-padding.y(),0,scaled_content.height()-1)));
        for(int x=0;x<model_width;++x) out[x]=in[std::clamp(x-padding.x(),0,scaled_content.width()-1)];
      }
      std::vector<float> image_tensor(static_cast<std::size_t>(3 * model_width * model_height), 0.0F);
      std::vector<float> mask_tensor(static_cast<std::size_t>(model_width * model_height), 0.0F);
      const auto plane_size = static_cast<std::size_t>(model_width * model_height);
      for (int y = 0; y < model_height; ++y) {
        if (inpaint_cancelled(request)) return cancelled_inpaint_result();
        const auto* scanline=reinterpret_cast<const QRgbaFloat32*>(scaled_patch.constScanLine(y));
        const int source_y=std::clamp((y-padding.y())*bounds.height()/scaled_content.height(),0,bounds.height()-1);
        for (int x=0;x<model_width;++x) {
          const int source_x=std::clamp((x-padding.x())*bounds.width()/scaled_content.width(),0,bounds.width()-1);
          const auto index=static_cast<std::size_t>(y*model_width+x);
          const auto pixel=scanline[x];
          image_tensor[index]=std::clamp(pixel.red(),0.0F,1.0F);
          image_tensor[plane_size+index]=std::clamp(pixel.green(),0.0F,1.0F);
          image_tensor[2*plane_size+index]=std::clamp(pixel.blue(),0.0F,1.0F);
          const bool inside=x>=padding.x() && y>=padding.y() &&
              x<padding.x()+scaled_content.width() && y<padding.y()+scaled_content.height();
          mask_tensor[index]=inside && static_cast<unsigned char>(patch_mask.at(source_y*bounds.width()+source_x))>0 ? 1.0F:0.0F;
        }
      }

      std::array<int64_t, 4> image_shape{
          1,
          3,
          static_cast<int64_t>(model_height),
          static_cast<int64_t>(model_width)};
      std::array<int64_t, 4> mask_shape{
          1,
          1,
          static_cast<int64_t>(model_height),
          static_cast<int64_t>(model_width)};
      auto memory_info = Ort::MemoryInfo::CreateCpu(OrtArenaAllocator, OrtMemTypeDefault);
      std::vector<Ort::Value> input_tensors;
      input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info,
          image_tensor.data(),
          image_tensor.size(),
          image_shape.data(),
          image_shape.size()));
      input_tensors.emplace_back(Ort::Value::CreateTensor<float>(
          memory_info,
          mask_tensor.data(),
          mask_tensor.size(),
          mask_shape.data(),
          mask_shape.size()));

      std::vector<NamedOrtValue> input_names;
      input_names.reserve(2);
      input_names.push_back(ort_value_name(inpaint_session_.get(), 0, true, "image"));
      input_names.push_back(ort_value_name(inpaint_session_.get(), 1, true, "mask"));
      if (lower_ascii(input_names.at(0).name()).find("mask") != std::string::npos &&
          lower_ascii(input_names.at(1).name()).find("mask") == std::string::npos) {
        std::swap(input_names[0], input_names[1]);
        std::swap(input_tensors[0], input_tensors[1]);
      }
      std::vector<const char*> input_name_ptrs;
      input_name_ptrs.reserve(input_names.size());
      for (const auto& input_name : input_names) {
        input_name_ptrs.push_back(input_name.name());
      }

      auto output_name = ort_value_name(inpaint_session_.get(), 0, false, "output");
      std::array<const char*, 1> output_name_ptrs{output_name.name()};
      Ort::RunOptions run_options;
      struct InterruptScope {
        std::shared_ptr<AiInpaintControl> control;
        ~InterruptScope() { if (control) control->setInterrupt({}); }
      } interrupt_scope{request.control};
      if (request.control) request.control->setInterrupt([&run_options] { run_options.SetTerminate(); });
      if (inpaint_cancelled(request)) return cancelled_inpaint_result();
      auto outputs = inpaint_session_->Run(
          run_options,
          input_name_ptrs.data(),
          input_tensors.data(),
          input_tensors.size(),
          output_name_ptrs.data(),
          output_name_ptrs.size());
      if (inpaint_cancelled(request)) return cancelled_inpaint_result();
      if (outputs.empty() || !outputs.front().IsTensor()) {
        result.error_message = QStringLiteral("LaMa ONNX model did not return an image tensor.");
        return result;
      }

      auto& output = outputs.front();
      const auto output_info = output.GetTensorTypeAndShapeInfo();
      const auto output_shape = output_info.GetShape();
      const auto* output_data = output.GetTensorData<float>();
      if (output_data == nullptr || output_shape.size() != 4) {
        result.error_message =
            QStringLiteral("LaMa ONNX output tensor must be 4D float image data.");
        return result;
      }

      const auto nchw = output_shape[1] == 3;
      const auto nhwc = output_shape[3] == 3;
      if (!nchw && !nhwc) {
        result.error_message =
            QStringLiteral("LaMa ONNX output tensor must have three color channels.");
        return result;
      }
      const auto output_height = static_cast<int>(nchw ? output_shape[2] : output_shape[1]);
      const auto output_width = static_cast<int>(nchw ? output_shape[3] : output_shape[2]);
      if (output_width <= 0 || output_height <= 0) {
        result.error_message = QStringLiteral("LaMa ONNX output image is empty.");
        return result;
      }

      QImage output_image(output_width, output_height, QImage::Format_RGBA64);
      const auto output_plane_size = static_cast<std::size_t>(output_width * output_height);
      auto output_max = 0.0F;
      for (std::size_t index = 0; index < output_info.GetElementCount(); ++index) {
        output_max = std::max(output_max, output_data[index]);
      }
      const auto output_scale = output_max > 2.0F ? 255.0F : 1.0F;
      const auto sample_output = [&](int x, int y, int channel) {
        const auto value =
            nchw
                ? output_data[(static_cast<std::size_t>(channel) * output_plane_size) +
                              static_cast<std::size_t>(y * output_width + x)]
                : output_data[(static_cast<std::size_t>(y * output_width + x) * 3U) +
                              static_cast<std::size_t>(channel)];
        return std::clamp(static_cast<int>(std::lround((value / output_scale) * 65535.0F)), 0, 65535);
      };
      for (int y = 0; y < output_height; ++y) {
        auto* scanline = reinterpret_cast<QRgba64*>(output_image.scanLine(y));
        for (int x = 0; x < output_width; ++x) {
          scanline[x] = QRgba64::fromRgba64(static_cast<quint16>(sample_output(x,y,0)), static_cast<quint16>(sample_output(x,y,1)), static_cast<quint16>(sample_output(x,y,2)), 65535);
        }
      }

      const QRect content_rect(qRound(padding.x()*double(output_width)/model_width),
          qRound(padding.y()*double(output_height)/model_height),
          qRound(scaled_content.width()*double(output_width)/model_width),
          qRound(scaled_content.height()*double(output_height)/model_height));
      output_image=output_image.copy(content_rect);
      if (output_image.size() != bounds.size()) {
        output_image =
            output_image.scaled(bounds.size(), Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
      }
      output_image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      result.success = true;
      result.model_name = QStringLiteral("lama-onnx");
      result.runtime_provider = inpaint_provider_;
      result.acceleration_warning = inpaint_acceleration_warning_;
      populate_inpaint_region_result(&result, request, reviewed_bounds);
      result.patch_image = output_image;
      result.candidate_images = {output_image};
      result.patch_x = bounds.x();
      result.patch_y = bounds.y();
      result.patch_width = bounds.width();
      result.patch_height = bounds.height();
      return result;
    } catch (const Ort::Exception& exception) {
      result.error_message = QStringLiteral("LaMa ONNX inference failed: %1")
                                 .arg(QString::fromUtf8(exception.what()));
    } catch (const std::exception& exception) {
      result.error_message = QStringLiteral("LaMa ONNX inference failed: %1")
                                 .arg(QString::fromUtf8(exception.what()));
    }
    return result;
  }

  std::mutex mutex_;
  std::unique_ptr<Ort::Env> env_;
  std::unique_ptr<Ort::Session> sam_encoder_session_;
  std::unique_ptr<Ort::Session> sam_decoder_session_;
  QString sam_encoder_model_path_;
  QString sam_encoder_model_sha256_;
  QString sam_decoder_model_path_;
  QString sam_decoder_model_sha256_;
  QByteArray sam_image_key_;
  std::vector<std::vector<float>> sam_encoder_features_;
  std::unique_ptr<Ort::Session> inpaint_session_;
  QString inpaint_model_path_;
  QString inpaint_model_sha256_;
  QString inpaint_provider_;
  QString inpaint_acceleration_warning_;
};

struct OnnxAiRuntimeStorage final {
  std::mutex mutex;
  std::unique_ptr<OnnxAiRuntime> runtime;
};

OnnxAiRuntimeStorage& onnx_ai_runtime_storage() {
  // The storage itself is process-lifetime so its mutex remains valid while
  // ONNX Runtime finishes background work during an explicit shutdown.
  static auto* storage = new OnnxAiRuntimeStorage();
  return *storage;
}

OnnxAiRuntime& onnx_ai_runtime() {
  auto& storage = onnx_ai_runtime_storage();
  std::lock_guard<std::mutex> lock(storage.mutex);
  if (!storage.runtime) {
    storage.runtime = std::make_unique<OnnxAiRuntime>();
  }
  return *storage.runtime;
}
#endif

}  // namespace

AiModelBundleVerification verify_ai_model_bundle(
    const QString& model_root) {
  return verify_model_bundle_impl(model_root);
}

AiModelRuntimeStatus ai_model_runtime_status() {
  AiModelRuntimeStatus status;
  status.model_root = default_model_root();

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    status.smart_selection_available = true;
    status.inpaint_available = true;
    status.bundle_verified = true;
    status.inpaint_artifact_verified = true;
    status.smart_selection_runtime_label = QStringLiteral("SAM Smart Select");
    status.inpaint_runtime_label = QStringLiteral("LaMa Model Inpaint");
    status.diagnostics = QStringLiteral(
        "root:%1|manifest:test|sam:test-ready|lama:test-ready|onnx:%2")
                             .arg(
                                 status.model_root,
                                 kOnnxRuntimeLinked ? QStringLiteral("linked")
                                                    : QStringLiteral("not-linked"));
    status.summary = QStringLiteral("Test SAM Smart Select and LaMa Model Inpaint runtimes active.");
    return status;
  }

  const auto bundle = verified_model_bundle_for_runtime(status.model_root);
  const auto manifest_available = bundle.manifest_available;
  status.bundle_verified = bundle.ok;
  status.inpaint_artifact_verified = bundle.inpaint_artifact_verified;
  status.smart_selection_artifacts_verified =
      bundle.smart_selection_artifacts_verified;
  status.bundle_verification_error_code = bundle.error_code;
  status.bundle_verification_error = bundle.error_message;
  const auto smart_selection_files_available =
      bundle.ok && bundle.smart_selection_artifacts_verified;
  const auto inpaint_files_available =
      bundle.ok && bundle.inpaint_artifact_verified;
  status.smart_selection_available =
      kSamRuntimeImplemented && smart_selection_files_available;
  status.inpaint_available =
      kLamaRuntimeImplemented && inpaint_files_available;
  status.smart_selection_runtime_label =
      status.smart_selection_available ? QStringLiteral("SAM Smart Select")
                                       : QStringLiteral("Local Smart Select");
  status.inpaint_runtime_label =
      status.inpaint_available ? QStringLiteral("LaMa Model Inpaint")
                               : QStringLiteral("Local CPU Fill");
  const auto sam_state =
      status.smart_selection_available
          ? QStringLiteral("ready")
          : (!smart_selection_files_available
                 ? QStringLiteral("model-missing")
                 : QStringLiteral("runtime-missing"));
  const auto lama_state =
      status.inpaint_available
          ? QStringLiteral("ready")
          : (!bundle.ok ? QStringLiteral("bundle-unverified")
                        : (!inpaint_files_available
                               ? QStringLiteral("model-missing")
                               : QStringLiteral("runtime-missing")));

  status.diagnostics =
      QStringLiteral("root:%1|manifest:%2|bundle:%3|sam:%4|lama:%5|onnx:%6|bundleError:%7")
          .arg(
              status.model_root,
              manifest_available ? QStringLiteral("present") : QStringLiteral("missing"),
              bundle.ok ? QStringLiteral("verified") : QStringLiteral("unverified"),
              sam_state,
              lama_state,
              kOnnxRuntimeLinked ? QStringLiteral("linked") : QStringLiteral("not-linked"),
              bundle.error_code.isEmpty() ? QStringLiteral("none") : bundle.error_code);

  if (status.smart_selection_available && status.inpaint_available) {
    status.summary = QStringLiteral("Bundled SAM Smart Select and LaMa Model Inpaint runtimes ready.");
  } else if (manifest_available && !bundle.ok) {
    status.summary = QStringLiteral("Local image-model bundle failed integrity verification (%1); retouch uses Local CPU Fill.")
                         .arg(bundle.error_code.isEmpty()
                                  ? QStringLiteral("invalid bundle")
                                  : bundle.error_code);
  } else if (smart_selection_files_available && !kSamRuntimeImplemented) {
    status.summary = QStringLiteral(
        "Bundled SAM2 image encoder and decoder are verified, but SAM ONNX inference is not implemented in this build.");
  } else if (inpaint_files_available && !kLamaRuntimeImplemented) {
    status.summary = QStringLiteral("Bundled LaMa inpaint model found, but ONNX Runtime is not linked in this build; retouch uses Local CPU Fill.");
  } else if (manifest_available) {
    status.summary = QStringLiteral("Bundled model manifest found, but required SAM/LaMa model files are incomplete; retouch uses Local CPU Fill.");
  } else {
    status.summary = QStringLiteral("Bundled SAM/LaMa models missing; retouch uses Local CPU Fill.");
  }
  return status;
}

void shutdown_ai_model_runtime() {
#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
  auto& storage = onnx_ai_runtime_storage();
  std::lock_guard<std::mutex> lock(storage.mutex);
  storage.runtime.reset();
#endif
}

SegmentationResult generate_ai_smart_selection_mask(
    const QImage& source_image,
    const QVector<SmartSelectPrompt>& prompts,
    const SegmentationResult* previous_mask) {
  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    return generate_smart_select_mask(source_image, prompts);
  }

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
  return onnx_ai_runtime().runSmartSelect(source_image, prompts, previous_mask);
#else
  static_cast<void>(previous_mask);
  SegmentationResult result;
  result.error_message =
      QStringLiteral("SAM smart-selection runtime is not linked in this build.");
  return result;
#endif
}

AiInpaintResult run_ai_inpaint(const AiInpaintRequest& request) {
  if (inpaint_cancelled(request)) return cancelled_inpaint_result();
  if (request.retouch_mode.trimmed().toLower() == QStringLiteral("patch") &&
      request.patch_source_set) {
    return run_source_region_patch(request);
  }

  if (qEnvironmentVariableIsSet("PHOTO_EDITOR_TEST_FAKE_AI_MODELS")) {
    return run_fake_inpaint(request);
  }

  AiInpaintResult result;
  if (!valid_inpaint_mask(request)) {
    result.error_message = QStringLiteral("Invalid inpaint request.");
    return result;
  }

#if defined(PHOTO_EDITOR_HAS_ONNXRUNTIME) && PHOTO_EDITOR_HAS_ONNXRUNTIME
  const auto bounds = inpaint_mask_bounds(request);
  AiInpaintResult spot_preflight;
  if (bounds.isValid()) {
    const auto coverage = inpaint_mask_coverage(request, bounds);
    if (inpaint_region_kind(request, coverage, bounds) ==
        QStringLiteral("spot")) {
      spot_preflight = run_cpu_diffusion_inpaint(request);
      if (spot_preflight.success && !prompt_requests_texture(request.prompt)) {
        spot_preflight.candidate_selection =
            QStringLiteral("smooth-spot-harmonic-fill");
        spot_preflight.alternate_model_name = QStringLiteral("lama-onnx");
        return spot_preflight;
      }
      if (spot_preflight.success && spot_preflight.texture_exemplar &&
          spot_preflight.texture_residual_mean >=
              kHighTextureExemplarResidualMean) {
        spot_preflight.candidate_selection =
            QStringLiteral("high-texture-local-exemplar");
        spot_preflight.alternate_model_name = QStringLiteral("lama-onnx");
        return spot_preflight;
      }
    }
  }
  result = onnx_ai_runtime().runInpaint(request);
  if (inpaint_cancelled(request)) return cancelled_inpaint_result();
  if (result.success) {
    if (spot_preflight.success) {
      result.alternate_candidate_evaluated = true;
      result.alternate_model_name = spot_preflight.model_name;
      result.candidate_selection =
          QStringLiteral("moderate-texture-lama-model");
      result.texture_residual_mean =
          spot_preflight.texture_residual_mean;
    }
    return result;
  }
  if (spot_preflight.success) {
    spot_preflight.alternate_candidate_evaluated = true;
    spot_preflight.alternate_model_name = QStringLiteral("lama-onnx");
    spot_preflight.candidate_selection =
        QStringLiteral("lama-runtime-failure-cpu-fallback");
    spot_preflight.error_message = result.error_message;
    return spot_preflight;
  }
  return run_cpu_diffusion_inpaint(request, result.error_message);
#else
  result.error_message =
      QStringLiteral("LaMa inpaint runtime is not linked in this build.");
  return run_cpu_diffusion_inpaint(request, result.error_message);
#endif
}

}  // namespace photo_editor::desktop
