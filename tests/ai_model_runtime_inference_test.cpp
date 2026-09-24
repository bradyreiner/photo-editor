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
#include <QPainter>

#include "ai_model_runtime.h"

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
      status.inpaint_runtime_label != QStringLiteral("LaMa Model Inpaint")) {
    std::cerr << status.summary.toStdString() << '\n';
    return fail("pinned LaMa runtime should report ready before inference");
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
      auto composed=photo.convertedToColorSpace(QColorSpace(QColorSpace::SRgb), QImage::Format_RGBA64);
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
