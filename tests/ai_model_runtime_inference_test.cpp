#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <future>
#include <thread>
#include <iostream>

#include <QColor>
#include <QColorSpace>
#include <QCoreApplication>
#include <QElapsedTimer>
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QPainter>
#include <QStringList>

#include "ai_model_runtime.h"
#include "object_pick_policy.h"
#include "qimage_color_conversion.h"

namespace {

int fail(const char* message) {
  std::cerr << message << '\n';
  return EXIT_FAILURE;
}

}  // namespace

int main(int argc, char** argv) {
  QCoreApplication application(argc, argv);
  qputenv("PHOTO_EDITOR_AI_MODEL_DIR", QByteArray(PHOTO_EDITOR_TEST_AI_MODEL_BUNDLE));

  const auto status = photo_editor::desktop::ai_model_runtime_status();
  if (!status.bundle_verified || !status.inpaint_artifact_verified ||
      !status.inpaint_available ||
      status.inpaint_runtime_label != QStringLiteral("LaMa Model Inpaint") ||
      !status.smart_selection_artifacts_verified || !status.smart_selection_available ||
      status.smart_selection_runtime_label != QStringLiteral("SAM Smart Select")) {
    std::cerr << status.summary.toStdString() << '\n';
    return fail("pinned LaMa and SAM2 runtimes should report ready before inference");
  }

  if (argc >= 4 && QString::fromLocal8Bit(argv[1]) == QStringLiteral("--smart-select-photo")) {
    QImage photo(QString::fromLocal8Bit(argv[2]));
    if (photo.isNull()) return fail("Smart Select diagnostic photo must load");
    QVector<photo_editor::desktop::SmartSelectPrompt> prompts;
    const auto prompt_specs = QString::fromLocal8Bit(argv[3]).split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const auto& prompt_spec : prompt_specs) {
      const auto fields = prompt_spec.split(QLatin1Char(','));
      if (fields.size() == 5 && fields.at(4) == QStringLiteral("box")) {
        bool valid = true;
        QVector<double> coordinates;
        for (int index = 0; index < 4; ++index) {
          bool ok = false;
          coordinates.append(fields.at(index).toDouble(&ok));
          valid = valid && ok;
        }
        if (!valid) return fail("Box diagnostic bounds must be numeric");
        const QRectF box(QPointF(coordinates.at(0), coordinates.at(1)),
                         QPointF(coordinates.at(2), coordinates.at(3)));
        prompts.append({.x = box.center().x(), .y = box.center().y(), .negative = false, .box = box});
        continue;
      }
      if (fields.size() != 3) return fail("Smart Select diagnostic points use x,y,+ or x,y,-");
      bool x_ok = false;
      bool y_ok = false;
      const auto x = fields.at(0).toDouble(&x_ok);
      const auto y = fields.at(1).toDouble(&y_ok);
      if (!x_ok || !y_ok || (fields.at(2) != QStringLiteral("+") && fields.at(2) != QStringLiteral("-"))) {
        return fail("Smart Select diagnostic point is invalid");
      }
      prompts.append({.x = x, .y = y, .negative = fields.at(2) == QStringLiteral("-")});
    }
    photo_editor::desktop::SegmentationResult previous_mask;
    const photo_editor::desktop::SegmentationResult* previous_mask_input = nullptr;
    if (argc >= 6) {
      const QImage previous_mask_image(QString::fromLocal8Bit(argv[5]));
      if (previous_mask_image.isNull() ||
          previous_mask_image.size() != photo.size()) {
        return fail("Smart Select refinement mask must match the source photo dimensions");
      }
      const auto grayscale_mask = previous_mask_image.convertToFormat(QImage::Format_Grayscale8);
      previous_mask.width = grayscale_mask.width();
      previous_mask.height = grayscale_mask.height();
      previous_mask.mask_data.resize(previous_mask.width * previous_mask.height);
      for (int y = 0; y < previous_mask.height; ++y) {
        std::copy_n(
            reinterpret_cast<const char*>(grayscale_mask.constScanLine(y)),
            previous_mask.width,
            previous_mask.mask_data.data() + y * previous_mask.width);
      }
      previous_mask.success = true;
      if (argc >= 7) {
        QFile refinement_logits_file(QString::fromLocal8Bit(argv[6]));
        if (!refinement_logits_file.open(QIODevice::ReadOnly)) {
          return fail("Smart Select refinement logits must load");
        }
        previous_mask.refinement_logits = refinement_logits_file.readAll();
        previous_mask.refinement_logits_width = 256;
        previous_mask.refinement_logits_height = 256;
        if (previous_mask.refinement_logits.size() != 256 * 256 * static_cast<int>(sizeof(float))) {
          return fail("Smart Select refinement logits must be a 256x256 float tensor");
        }
      }
      previous_mask_input = &previous_mask;
    }
    QElapsedTimer timer;
    timer.start();
    const auto mask = photo_editor::desktop::generate_ai_smart_selection_mask(
        photo,
        prompts,
        previous_mask_input);
    if (!mask.success || mask.width != photo.width() || mask.height != photo.height() ||
        mask.mask_data.size() != photo.width() * photo.height() ||
        mask.refinement_logits_width != 256 || mask.refinement_logits_height != 256 ||
        mask.refinement_logits.size() != 256 * 256 * static_cast<int>(sizeof(float))) {
      std::cerr << "smart-select error=" << mask.error_message.toStdString() << '\n';
      return fail("SAM2 must return a source-resolution mask and exact decoder logits for refinement");
    }
    auto prompts_match = true;
    for (const auto& prompt : prompts) {
      if (prompt.box.isValid()) continue;
      const auto x = std::clamp(qRound(prompt.x * (mask.width - 1)), 0, mask.width - 1);
      const auto y = std::clamp(qRound(prompt.y * (mask.height - 1)), 0, mask.height - 1);
      const auto alpha = static_cast<unsigned char>(mask.mask_data.at(y * mask.width + x));
      const auto matches = prompt.negative ? alpha < 96 : alpha >= 96;
      prompts_match = prompts_match && matches;
      std::cout << "prompt=" << (prompt.negative ? "negative" : "positive")
                << " x=" << prompt.x << " y=" << prompt.y
                << " alpha=" << static_cast<int>(alpha) << '\n';
    }
    const auto selected_pixels = std::count_if(
        mask.mask_data.cbegin(), mask.mask_data.cend(),
        [](char value) { return static_cast<unsigned char>(value) >= 96; });
    const auto coverage = static_cast<double>(selected_pixels) /
                          static_cast<double>(mask.mask_data.size());
    const auto result_honors_prompts =
        photo_editor::desktop::smart_select_result_honors_prompts(mask, prompts);
    if (!prompts_match || !result_honors_prompts || coverage <= 0.0 || coverage > 0.92) {
      std::cerr << "coverage=" << coverage << " promptsMatch=" << prompts_match
                << " singlePositiveComponent=" << result_honors_prompts << '\n';
      return fail("SAM2 diagnostic mask must honor every point, keep positive points connected, and select less than 92 percent of the photo");
    }
    if (argc >= 5) {
      const auto output_path = QString::fromLocal8Bit(argv[4]);
      QDir().mkpath(QFileInfo(output_path).absolutePath());
      QImage output(mask.width, mask.height, QImage::Format_Grayscale8);
      for (int y = 0; y < mask.height; ++y) {
        auto* row = output.scanLine(y);
        for (int x = 0; x < mask.width; ++x) {
          row[x] = static_cast<unsigned char>(mask.mask_data.at(y * mask.width + x));
        }
      }
      if (!output.save(output_path)) return fail("Smart Select diagnostic mask must save");
      if (!mask.refinement_logits.isEmpty()) {
        QFile refinement_logits_file(output_path + QStringLiteral(".logits"));
        if (!refinement_logits_file.open(QIODevice::WriteOnly) ||
            refinement_logits_file.write(mask.refinement_logits) != mask.refinement_logits.size()) {
          return fail("Smart Select refinement logits must save");
        }
      }
    }
    std::cout << "photo=" << photo.width() << 'x' << photo.height()
              << " coverage=" << coverage << " selectedPixels=" << selected_pixels
              << " elapsedMs=" << timer.elapsed() << '\n';
    return EXIT_SUCCESS;
  }

  constexpr auto width = 640;
  constexpr auto height = 480;
  QImage source(width, height, QImage::Format_ARGB32);
  for (int y = 0; y < height; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(source.scanLine(y));
    for (int x = 0; x < width; ++x) {
      const auto red = 42 + ((x * 151) / width);
      const auto green = 58 + ((y * 129) / height);
      const auto blue = 66 + (((x + y) * 97) / (width + height));
      scanline[x] = qRgb(red, green, blue);
    }
  }

  QByteArray mask(width * height, '\0');
  for (int y = 180; y < 300; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(source.scanLine(y));
    for (int x = 255; x < 385; ++x) {
      scanline[x] = qRgb(8, 8, 8);
      mask[y * width + x] = static_cast<char>(255);
    }
  }
  source.setColorSpace(QColorSpace(QColorSpace::SRgb));

  const QVector<photo_editor::desktop::SmartSelectPrompt> smart_prompts{
      {.x = 0.5, .y = 0.5, .negative = false},
      {.x = 0.08, .y = 0.08, .negative = true},
  };
  QElapsedTimer smart_select_timer;
  smart_select_timer.start();
  const auto smart_selection =
      photo_editor::desktop::generate_ai_smart_selection_mask(source, smart_prompts);
  const auto smart_select_cold_ms = smart_select_timer.elapsed();
  smart_select_timer.restart();
  const auto smart_selection_warm =
      photo_editor::desktop::generate_ai_smart_selection_mask(source, smart_prompts);
  const auto smart_select_warm_ms = smart_select_timer.elapsed();
  const auto center_alpha = smart_selection.success
      ? static_cast<unsigned char>(smart_selection.mask_data.at((height / 2) * width + width / 2))
      : 0;
  const auto negative_alpha = smart_selection.success
      ? static_cast<unsigned char>(smart_selection.mask_data.at((height / 12) * width + width / 12))
      : 255;
  if (!smart_selection.success || !smart_selection_warm.success ||
      smart_selection.width != width || smart_selection.height != height ||
      smart_selection.mask_data.size() != width * height || center_alpha < 128 ||
      smart_selection.refinement_logits_width != 256 ||
      smart_selection.refinement_logits_height != 256 ||
      smart_selection.refinement_logits.size() != 256 * 256 * static_cast<int>(sizeof(float)) ||
      negative_alpha > 127 ||
      !photo_editor::desktop::smart_select_result_honors_prompts(smart_selection, smart_prompts)) {
    std::cerr << "success=" << smart_selection.success
              << " warmSuccess=" << smart_selection_warm.success
              << " centerAlpha=" << static_cast<int>(center_alpha)
              << " negativeAlpha=" << static_cast<int>(negative_alpha)
              << " error=" << smart_selection.error_message.toStdString() << '\n';
    return fail("SAM2 must return a full-resolution mask that follows positive and negative prompts");
  }
  std::cout << "sam2_cold_ms=" << smart_select_cold_ms
            << " sam2_warm_ms=" << smart_select_warm_ms
            << " positiveAlpha=" << static_cast<int>(center_alpha)
            << " negativeAlpha=" << static_cast<int>(negative_alpha) << '\n';

  const QVector<photo_editor::desktop::SmartSelectPrompt> box_prompts{
      {.x = 0.5, .y = 0.5, .negative = false,
       .box = QRectF(QPointF(0.35, 0.30), QPointF(0.65, 0.70))},
      {.x = 0.08, .y = 0.08, .negative = true}};
  const auto box_selection = photo_editor::desktop::generate_ai_smart_selection_mask(source, box_prompts);
  if (!box_selection.success ||
      !photo_editor::desktop::smart_select_result_honors_prompts(box_selection, box_prompts) ||
      static_cast<unsigned char>(box_selection.mask_data.at((height / 2) * width + width / 2)) < 96 ||
      static_cast<unsigned char>(box_selection.mask_data.at(144 * width + 224)) >= 96) {
    std::cerr << "box error=" << box_selection.error_message.toStdString() << '\n';
    return fail("Real SAM2 box inference must select the enclosed item while allowing background corners");
  }

  const auto smart_photo_path = qEnvironmentVariable("PHOTO_SMART_SELECT_TEST_SOURCE");
  const auto smart_photo_output_path = qEnvironmentVariable("PHOTO_SMART_SELECT_TEST_OUTPUT");
  if (!smart_photo_path.isEmpty()) {
    QImage photo(smart_photo_path);
    if (photo.isNull()) return fail("Smart Select photographic fixture must load");
    const QVector<photo_editor::desktop::SmartSelectPrompt> photo_prompts{
        {.x = 0.50, .y = 0.18, .negative = false},
        {.x = 0.50, .y = 0.45, .negative = false},
        {.x = 0.52, .y = 0.70, .negative = false},
        {.x = 0.10, .y = 0.25, .negative = true},
        {.x = 0.90, .y = 0.55, .negative = true},
    };
    QElapsedTimer photo_timer;
    photo_timer.start();
    const auto photo_mask = photo_editor::desktop::generate_ai_smart_selection_mask(photo, photo_prompts);
    if (!photo_mask.success || photo_mask.width != photo.width() || photo_mask.height != photo.height() ||
        photo_mask.mask_data.size() != photo.width() * photo.height()) {
      std::cerr << "photo smart-select error=" << photo_mask.error_message.toStdString() << '\n';
      return fail("SAM2 photographic selection must return a source-resolution mask");
    }
    auto prompt_samples_valid = true;
    for (const auto& prompt : photo_prompts) {
      const auto x = std::clamp(qRound(prompt.x * (photo.width() - 1)), 0, photo.width() - 1);
      const auto y = std::clamp(qRound(prompt.y * (photo.height() - 1)), 0, photo.height() - 1);
      const auto alpha = static_cast<unsigned char>(photo_mask.mask_data.at(y * photo.width() + x));
      std::cout << "sam2_prompt=" << (prompt.negative ? "negative" : "positive")
                << " x=" << prompt.x << " y=" << prompt.y
                << " alpha=" << static_cast<int>(alpha) << '\n';
      prompt_samples_valid = prompt_samples_valid &&
          (prompt.negative ? alpha < 96 : alpha >= 96);
    }
    if (!smart_photo_output_path.isEmpty()) {
      QDir().mkpath(QFileInfo(smart_photo_output_path).absolutePath());
      QImage output(photo.width(), photo.height(), QImage::Format_Grayscale8);
      for (int y = 0; y < photo.height(); ++y) {
        auto* row = output.scanLine(y);
        for (int x = 0; x < photo.width(); ++x) {
          row[x] = static_cast<unsigned char>(photo_mask.mask_data.at(y * photo.width() + x));
        }
      }
      if (!output.save(smart_photo_output_path)) return fail("Smart Select photographic mask must save");
    }
    const auto selected_pixels = std::count_if(
        photo_mask.mask_data.cbegin(), photo_mask.mask_data.cend(),
        [](char value) { return static_cast<unsigned char>(value) >= 96; });
    const auto selected_coverage = static_cast<double>(selected_pixels) /
                                   static_cast<double>(photo_mask.mask_data.size());
    const auto photo_result_honors_prompts =
        photo_editor::desktop::smart_select_result_honors_prompts(photo_mask, photo_prompts);
    if (!prompt_samples_valid || !photo_result_honors_prompts ||
        selected_coverage <= 0.0 || selected_coverage > 0.92) {
      std::cerr << "selectedCoverage=" << selected_coverage
                << " promptSamplesValid=" << prompt_samples_valid
                << " singlePositiveComponent=" << photo_result_honors_prompts << '\n';
      return fail("SAM2 photographic mask must honor positive and negative prompts without selecting nearly the entire image");
    }
    std::cout << "sam2_photo=" << photo.width() << 'x' << photo.height()
              << " selectedPixels=" << selected_pixels
              << " selectedCoverage=" << selected_coverage
              << " elapsedMs=" << photo_timer.elapsed()
              << " maskOutput=" << smart_photo_output_path.toStdString() << '\n';
  }

  photo_editor::desktop::AiInpaintRequest request;
  request.source_image = source;
  request.mask_data = mask;
  request.mask_width = width;
  request.mask_height = height;
  request.retouch_mode = QStringLiteral("remove");
  request.quality_mode = QStringLiteral("production");

  QElapsedTimer timer;
  timer.start();
  const auto result = photo_editor::desktop::run_ai_inpaint(request);
  const auto cold_elapsed_ms = timer.elapsed();
  timer.restart();
  const auto warm_result = photo_editor::desktop::run_ai_inpaint(request);
  const auto warm_elapsed_ms = timer.elapsed();

  auto spot_source = source;
  QByteArray spot_mask(width * height, '\0');
  for (int y = 92; y < 108; ++y) {
    auto* scanline = reinterpret_cast<QRgb*>(spot_source.scanLine(y));
    for (int x = 92; x < 112; ++x) {
      scanline[x] = qRgb(8, 8, 8);
      spot_mask[y * width + x] = static_cast<char>(255);
    }
  }
  auto smooth_spot_request = request;
  smooth_spot_request.source_image = spot_source;
  smooth_spot_request.mask_data = spot_mask;
  smooth_spot_request.prompt =
      QStringLiteral("Remove the spot and preserve the smooth surrounding gradient.");
  const auto smooth_spot_result =
      photo_editor::desktop::run_ai_inpaint(smooth_spot_request);
  auto fabric_spot_request = smooth_spot_request;
  fabric_spot_request.prompt =
      QStringLiteral("Remove the spot and reconstruct the surrounding fabric texture.");
  const auto fabric_spot_result =
      photo_editor::desktop::run_ai_inpaint(fabric_spot_request);
  auto cancelled_request=request;
  cancelled_request.control=std::make_shared<photo_editor::desktop::AiInpaintControl>();
  auto pending=std::async(std::launch::async,[cancelled_request] { return photo_editor::desktop::run_ai_inpaint(cancelled_request); });
  std::this_thread::sleep_for(std::chrono::milliseconds(30));
  QElapsedTimer cancellation_timer;cancellation_timer.start();
  cancelled_request.control->cancel();
  if(pending.wait_for(std::chrono::seconds(5))!=std::future_status::ready)
    return fail("Real model inference must acknowledge cancellation within five seconds");
  const auto cancelled=pending.get();
  if(!cancelled.cancelled || cancelled.success || cancelled.fallback)
    return fail("Cancelled real model inference must not publish or launch CPU fallback");
  std::cout<<"real_model_cancel_ms="<<cancellation_timer.elapsed()<<'\n';
  // Optional real-photo proof uses a selection at the image edge, where the
  // model context is rectangular and must be letterboxed rather than stretched.
  const auto photo_path = qEnvironmentVariable("PHOTO_RETOUCH_TEST_SOURCE");
  const auto evidence_dir = qEnvironmentVariable("PHOTO_RETOUCH_TEST_OUTPUT");
  if (!photo_path.isEmpty()) {
    QImage photo(photo_path);
    if (photo.isNull()) return fail("Photographic retouch fixture must load");
    if (!photo.colorSpace().isValid()) photo.setColorSpace(QColorSpace(QColorSpace::SRgb));
    auto photo_request = request;
    photo_request.source_image = photo;
    photo_request.mask_width = photo.width();
    photo_request.mask_height = photo.height();
    photo_request.mask_data = QByteArray(photo.width()*photo.height(), '\0');
    photo_request.prompt = QStringLiteral("Reconstruct the surrounding photographic texture.");
    const QRect selected(0, photo.height()/8, photo.width()/8, photo.height()*3/4);
    for (int y=selected.top(); y<=selected.bottom(); ++y)
      for (int x=selected.left(); x<=selected.right(); ++x)
        photo_request.mask_data[y*photo.width()+x]=char(255);
    const auto photo_result = photo_editor::desktop::run_ai_inpaint(photo_request);
    if (!photo_result.success || photo_result.fallback || photo_result.patch_image.depth()!=64 ||
        photo_result.patch_width==photo_result.patch_height)
      return fail("Edge-context photographic inference must produce a real 16-bit model patch");
    if (!evidence_dir.isEmpty()) {
      QDir().mkpath(evidence_dir);
      auto composed=photo_editor::desktop::convert_tagged_qimage_color_space(
          photo, QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64,
          photo_editor::desktop::QImageColorConversionPurpose::EvidenceMeasurement);
      QPainter painter(&composed);
      painter.setClipRect(selected);
      painter.drawImage(QPoint(photo_result.patch_x,photo_result.patch_y),photo_result.patch_image);
      painter.end();
      if (!photo.save(evidence_dir+"/retouch-photo-before.png") ||
          !composed.save(evidence_dir+"/retouch-photo-after.png") ||
          !photo_result.patch_image.save(evidence_dir+"/retouch-photo-patch.png"))
        return fail("Photographic proof artifacts must save");
    }
    std::cout<<"photographic_model="<<photo_result.model_name.toStdString()
             <<" patch="<<photo_result.patch_width<<'x'<<photo_result.patch_height
             <<" depth="<<photo_result.patch_image.depth()<<'\n';
  }
  photo_editor::desktop::shutdown_ai_model_runtime();
  qunsetenv("PHOTO_EDITOR_AI_MODEL_DIR");
  if (!result.success || result.fallback || result.model_name != QStringLiteral("lama-onnx") ||
      (result.runtime_provider != QStringLiteral("coreml") &&
       result.runtime_provider != QStringLiteral("cpu")) ||
      result.patch_image.isNull() || result.patch_image.depth()!=64 || result.patch_image.size() != QSize(result.patch_width, result.patch_height)) {
    std::cerr << "success=" << result.success
              << " fallback=" << result.fallback
              << " model=" << result.model_name.toStdString()
              << " provider=" << result.runtime_provider.toStdString()
              << " error=" << result.error_message.toStdString() << '\n';
    return fail("pinned LaMa model should complete real ONNX inference without fallback");
  }
  if (!warm_result.success || warm_result.fallback ||
      warm_result.model_name != QStringLiteral("lama-onnx") ||
      warm_result.runtime_provider != result.runtime_provider ||
      warm_result.patch_image.isNull()) {
    return fail("warm LaMa inference should reuse the real ONNX session without fallback");
  }
  if (!smooth_spot_result.success || !smooth_spot_result.fallback ||
      smooth_spot_result.model_name !=
          QStringLiteral("cpu-diffusion-inpaint-v1") ||
      smooth_spot_result.candidate_selection !=
          QStringLiteral("smooth-spot-harmonic-fill") ||
      smooth_spot_result.alternate_candidate_evaluated ||
      smooth_spot_result.alternate_model_name != QStringLiteral("lama-onnx")) {
    return fail("smooth spot routing should choose the seam-safe harmonic candidate without running LaMa");
  }
  if (!fabric_spot_result.success || fabric_spot_result.fallback ||
      fabric_spot_result.model_name != QStringLiteral("lama-onnx") ||
      fabric_spot_result.candidate_selection !=
          QStringLiteral("moderate-texture-lama-model") ||
      !fabric_spot_result.alternate_candidate_evaluated ||
      fabric_spot_result.alternate_model_name !=
          QStringLiteral("cpu-diffusion-inpaint-v1")) {
    return fail("moderately textured spot routing should select real LaMa after evaluating the CPU candidate");
  }
  std::cout<<"cold_ms="<<cold_elapsed_ms<<" warm_ms="<<warm_elapsed_ms<<'\n';
  if (cold_elapsed_ms > 6000 || warm_elapsed_ms > 2500) {
    return fail("pinned LaMa inference exceeded the supported macOS arm64 latency gate");
  }

  double absolute_difference = 0.0;
  auto compared_pixels = 0;
  for (int y = 180; y < 300; ++y) {
    for (int x = 255; x < 385; ++x) {
      const auto patch_x = x - result.patch_x;
      const auto patch_y = y - result.patch_y;
      if (patch_x < 0 || patch_y < 0 || patch_x >= result.patch_image.width() ||
          patch_y >= result.patch_image.height()) {
        continue;
      }
      const auto before = source.pixelColor(x, y);
      const auto after = result.patch_image.pixelColor(patch_x, patch_y);
      absolute_difference += std::abs(after.red() - before.red());
      absolute_difference += std::abs(after.green() - before.green());
      absolute_difference += std::abs(after.blue() - before.blue());
      ++compared_pixels;
    }
  }
  const auto mean_channel_difference =
      compared_pixels > 0 ? absolute_difference / (3.0 * compared_pixels) : 0.0;
  if (compared_pixels == 0 || mean_channel_difference < 4.0) {
    return fail("real LaMa output should materially replace the masked synthetic object");
  }

  std::cout << "model=" << result.model_name.toStdString()
            << " provider=" << result.runtime_provider.toStdString()
            << " coldElapsedMs=" << cold_elapsed_ms
            << " warmElapsedMs=" << warm_elapsed_ms
            << " patch=" << result.patch_width << 'x' << result.patch_height
            << " meanMaskedChannelDifference=" << mean_channel_difference
            << " accelerationWarning=" << result.acceleration_warning.toStdString() << '\n';
  return EXIT_SUCCESS;
}
