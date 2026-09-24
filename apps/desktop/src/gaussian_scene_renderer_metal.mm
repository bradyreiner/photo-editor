#include "gaussian_scene_renderer.h"
#include "gaussian_splat_shader.h"
#include "gaussian_spacetime.h"
#include "gaussian_lens_sampling.h"
#include "gaussian_physical_camera.h"
#import <Metal/Metal.h>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QRect>
#include <bit>
#include <cmath>
#include <cstring>

namespace photo_editor::desktop {
namespace {
struct TextureLease { id<MTLTexture> texture; };
struct alignas(16) Uniforms { float view[16]; float intrinsics[4]; float image[4]; float position[4]; float temporal[4]; float raster[4]; float aperture[4]; float aperture_world[4]; };
struct DepthKey { quint32 key, index; };
struct alignas(16) FilmUniforms {
  float grain_amount, grain_size, halation_amount, halation_radius, threshold;
  quint32 seed;
  float padding[2]{};
  std::array<float, 4> canvas{}; // Full photo width/height and tile origin.
};
static_assert(sizeof(FilmUniforms) == 48);
class MetalGaussianRenderer final : public GaussianSceneRenderer {
 public:
  GaussianFrame render(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds = {}) override {
    return renderWithOptions(scene, camera, readback, scene_seconds, {});
  }
  GaussianFrame renderWithOptions(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds,
      const GaussianRenderOptions& options) override {
    if (options.export_tile_edge != 0 && (!readback || options.export_tile_edge < 32 || options.export_tile_edge > 2048)) {
      GaussianFrame frame; frame.error = "Photo tile edges must be 32–2048 pixels and require explicit readback."; return frame;
    }
    const auto size = camera.image_size;
    if (readback && size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192 &&
        (options.export_tile_edge > 0 || size.width() > 4096 || size.height() > 4096))
      return renderTiled(scene, camera, scene_seconds, options);
    return renderRegion(scene, camera, readback, scene_seconds, options, QRect(QPoint{}, size));
  }
 private:
  GaussianFrame renderRegion(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds,
      const GaussianRenderOptions& options, const QRect& region, id<MTLBuffer> __strong* tile_order = nullptr) {
    @autoreleasepool {
      GaussianFrame frame;
      const auto canceled = [&] { return options.canceled && options.canceled->load(); };
      const auto cancellation = [&] { GaussianFrame result; result.canceled = true; return result; };
      if (canceled()) return cancellation();
      QElapsedTimer elapsed; elapsed.start();
      if (!scene || scene->points.empty() || scene->points.size() > 2000000 || scene->sh_degree < 0 || scene->sh_degree > 3 ||
          camera.image_size.width() < 1 || camera.image_size.height() < 1 ||
          camera.image_size.width() > 8192 || camera.image_size.height() > 8192 || !camera.photo.valid() ||
          !gaussian_physical_camera_consistent(camera)) {
        frame.error = "Invalid scene, photo settings or output dimensions (maximum 8192 pixels per edge)."; return frame;
      }
      const bool optical = !options.interactive && camera.photo.lens.aperture_radius > 0;
      const auto film = options.interactive ? GaussianFilmSettings{} : camera.photo.film;
      if (film.active() && (region.width() > 4096 || region.height() > 4096)) {
        frame.error = "Film effects are currently limited to 4096 pixels per edge."; return frame;
      }
      if (optical && (region.width() > 4096 || region.height() > 4096)) {
        frame.error = "Thin-lens rendering is currently limited to 4096 pixels per edge."; return frame;
      }
      const auto offsets = optical ? gaussian_aperture_samples(camera.photo.lens) : std::vector<QVector2D>{{0, 0}};
      if (offsets.empty()) { frame.error = "Cannot sample the selected aperture."; return frame; }
      const bool temporal = !scene->temporal.empty();
      float local_seconds = 0;
      if (scene_seconds && !std::isfinite(*scene_seconds)) { frame.error = "Scene time must be finite."; return frame; }
      if (temporal) {
        const auto start = scene->local_time_range[0], end = scene->local_time_range[1];
        if (!scene_seconds || !std::isfinite(scene->time_origin_seconds) || !std::isfinite(start) || !std::isfinite(end) ||
            end <= start || !std::isfinite(end-start) || *scene_seconds-scene->time_origin_seconds < start ||
            *scene_seconds-scene->time_origin_seconds > end) {
          frame.error = "A temporal scene requires an explicit time inside its finite capture range."; return frame;
        }
        local_seconds = static_cast<float>(*scene_seconds-scene->time_origin_seconds);
      }
      if (scene->temporal_sh_degree < 0 || scene->temporal_sh_degree > 2 ||
          (temporal && scene->temporal.size() != scene->points.size()) ||
          (!temporal && scene->temporal_sh_degree != 0) ||
          (scene->temporal_sh_degree > 0 && scene->temporal_appearance.size() != scene->points.size()) ||
          (scene->temporal_sh_degree == 0 && !scene->temporal_appearance.empty())) {
        frame.error = "Incomplete or unsupported temporal Gaussian layout."; return frame;
      }
      if (!initialize()) { frame.error = error_; return frame; }
      auto source_camera = camera;
      source_camera.name.clear();
      source_camera.photo.exposure_ev = 0; source_camera.photo.look = 0; source_camera.photo.film = {};
      const auto source_key = source_camera.description();
      const bool reused = tile_order == nullptr && !options.interactive && resolved_source_ != nil && scene_ == scene &&
          resolved_key_ == source_key && resolved_seconds_ == scene_seconds;
      // Retain only one completed immutable pre-film image. Never reuse partial
      // aperture integration, another scene/time/pose, or an interactive draft.
      if (!reused && (!options.interactive || scene_ != scene)) resolved_source_ = nil;
      if (scene_ != scene) {
        if (temporal) {
          for (size_t i = 0; i < scene->points.size(); ++i) {
            const auto& motion = scene->temporal[i];
            const auto& point = scene->points[i];
            if (!valid_gaussian_temporal_factor(motion)) { frame.error = "Invalid conditional covariance factor."; return frame; }
            for (const auto v : point.center_opacity) if (!std::isfinite(v)) { frame.error = "Invalid temporal position or opacity."; return frame; }
            for (const auto v : point.sh) if (!std::isfinite(v)) { frame.error = "Invalid spatial appearance."; return frame; }
            for (const auto v : motion.velocity_time) if (!std::isfinite(v)) { frame.error = "Invalid temporal motion."; return frame; }
            if (!std::isfinite(motion.normalization[0]) || motion.normalization[0] <= 0 ||
                !std::isfinite(point.center_opacity[3]) || point.center_opacity[3] < 0 || point.center_opacity[3] > 1) {
              frame.error = "Invalid temporal variance or peak opacity."; return frame;
            }
          }
          for (const auto& appearance : scene->temporal_appearance) for (const auto v : appearance.sh)
            if (!std::isfinite(v)) { frame.error = "Invalid temporal appearance."; return frame; }
        }
        const GaussianTemporalPoint dummy_motion{};
        const GaussianTemporalAppearance dummy_appearance{};
        id<MTLBuffer> points = [device_ newBufferWithBytes:scene->points.data() length:scene->points.size() * sizeof(GaussianPoint) options:MTLResourceStorageModeShared];
        id<MTLBuffer> motion = [device_ newBufferWithBytes:temporal ? scene->temporal.data() : &dummy_motion
            length:std::max(size_t{1}, scene->temporal.size()) * sizeof(GaussianTemporalPoint) options:MTLResourceStorageModeShared];
        id<MTLBuffer> appearance = [device_ newBufferWithBytes:scene->temporal_appearance.empty() ? &dummy_appearance : scene->temporal_appearance.data()
            length:std::max(size_t{1}, scene->temporal_appearance.size()) * sizeof(GaussianTemporalAppearance) options:MTLResourceStorageModeShared];
        if (points == nil || motion == nil || appearance == nil) { frame.error = "Cannot allocate Gaussian scene memory."; return frame; }
        // Depth ordering needs only xyz, not the much larger covariance/SH
        // record. Keep a compact immutable position stream for static scenes.
        // It is replaced with the scene and bounded by the point admission cap.
        std::vector<std::array<float, 3>> next_centers(temporal ? 0 : scene->points.size());
        auto* centers = next_centers.data();
        const auto* source_points = scene->points.data();
        for (size_t i = 0; i < next_centers.size(); ++i) {
          if ((i & 4095U) == 0 && canceled()) return cancellation();
          std::memcpy(centers + i, source_points[i].center_opacity.data(), sizeof(*centers));
        }
        points_ = points; temporal_ = motion; appearance_ = appearance;
        depth_centers_ = std::move(next_centers);
        scene_ = scene;
      }
      if (points_ == nil) { frame.error = "Cannot allocate Gaussian scene memory."; return frame; }
      id<MTLBuffer> order_buffer = nil;
      if (!reused && (tile_order == nullptr || *tile_order == nil)) {
        keys_.resize(scene->points.size());
        auto* depth_keys = keys_.data();
        const auto* scene_points = scene->points.data();
        const auto* centers = depth_centers_.data();
        const float px = camera.position.x(), py = camera.position.y(), pz = camera.position.z();
        const float dx = camera.forward.x(), dy = camera.forward.y(), dz = camera.forward.z();
        size_t front_count = 0;
        for (size_t i = 0; i < scene->points.size(); ++i) {
          if ((i & 4095U) == 0 && canceled()) return cancellation();
          const auto* center = temporal ? scene_points[i].center_opacity.data() : centers[i].data();
          std::array<float,3> moving;
          if (temporal) { moving = gaussian_spacetime_center(scene_points[i],scene->temporal[i],local_seconds); center = moving.data(); }
          // Same subtract-then-dot arithmetic/order, without constructing and
          // indexing millions of temporary Qt vectors in the hot loop.
          const float depth = (center[0]-px)*dx + (center[1]-py)*dy + (center[2]-pz)*dz;
          if (depth >= 0.01F && depth <= 1e10F)
            depth_keys[front_count++] = {std::bit_cast<quint32>(depth), static_cast<quint32>(i)};
        }
        keys_.resize(front_count);
        // Stable, exact 32-bit near-to-far radix order; no quantized depth bins.
        scratch_.resize(keys_.size());
        for (unsigned shift = 0; shift < 32; shift += 8) {
          if (canceled()) return cancellation();
          std::array<size_t, 256> counts{};
          auto* offsets = counts.data();
          const auto* input = keys_.data();
          auto* output = scratch_.data();
          for (size_t i = 0; i < front_count; ++i) ++offsets[(input[i].key >> shift) & 255U];
          size_t prefix = 0;
          for (auto& count : counts) { const auto next = prefix + count; count = prefix; prefix = next; }
          for (size_t i = 0; i < front_count; ++i) output[offsets[(input[i].key >> shift) & 255U]++] = input[i];
          keys_.swap(scratch_);
        }
        order_.resize(keys_.size());
        const auto* sorted = keys_.data();
        auto* order = order_.data();
        for (size_t i = 0; i < front_count; ++i) order[i] = sorted[i].index;
        const auto count = std::max(size_t{1}, order_.size());
        order_buffer = [device_ newBufferWithLength:count * sizeof(quint32) options:MTLResourceStorageModeShared];
        if (order_buffer == nil) { frame.error = "Cannot allocate Gaussian depth order."; return frame; }
        if (!order_.empty()) std::memcpy(order_buffer.contents, order_.data(), order_.size() * sizeof(quint32));
        if (tile_order != nullptr) *tile_order = order_buffer;
      } else if (!reused) {
        // The tiled coordinator owns one immutable scene/pose/time and invokes
        // these regions serially. Their exact depth order is shared, not resorted.
        order_buffer = *tile_order;
      }
      const double sort_ms = static_cast<double>(elapsed.nsecsElapsed()) / 1e6;
      const auto width = static_cast<NSUInteger>(region.width());
      const auto height = static_cast<NSUInteger>(region.height());
      auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:width height:height mipmapped:NO];
      descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead;
      descriptor.storageMode = MTLStorageModePrivate;
      id<MTLTexture> accumulation = reused ? resolved_source_ : [device_ newTextureWithDescriptor:descriptor];
      descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
      id<MTLTexture> integrated = optical ? (reused ? resolved_source_ : [device_ newTextureWithDescriptor:descriptor]) : nil;
      descriptor.pixelFormat = MTLPixelFormatR32Float;
      id<MTLTexture> glow = film.halation_amount > 0 ? [device_ newTextureWithDescriptor:descriptor] : nil;
      descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
      descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
      auto lease = std::make_shared<TextureLease>();
      lease->texture = [device_ newTextureWithDescriptor:descriptor];
      if (accumulation == nil || lease->texture == nil || (optical && integrated == nil) || (film.halation_amount > 0 && glow == nil)) {
        frame.error = "Cannot allocate Gaussian render targets."; return frame;
      }
      id<MTLBuffer> readback_buffer = nil;
      const NSUInteger row_bytes = ((width * 4 + 255) / 256) * 256;
      double gpu_ms = 0;
      int progressive_frames = 0;
      const size_t passes = reused ? 1 : offsets.size();
      for (size_t sample_index = 0; sample_index < passes; ++sample_index) {
        if (canceled()) return cancellation();
        const auto completed_samples = sample_index + 1;
        const bool intermediate = options.progressive_frames && !readback && !reused && optical &&
            completed_samples < passes && completed_samples >= 4 && std::has_single_bit(completed_samples);
        const auto offset = offsets[sample_index];
        const auto aperture_camera = gaussian_aperture_camera(camera, offset);
        Uniforms uniforms{};
        const auto view = camera.worldToCamera();
        std::memcpy(uniforms.view, view.constData(), sizeof(uniforms.view));
        // Keep small pupil shifts separate from the potentially large matrix
        // translation. Apply after the shared base projection on the GPU.
        // The unchanged depth row still supports one sort for all samples.
        uniforms.aperture[0] = offset.x(); uniforms.aperture[1] = offset.y();
        const auto aperture_world = camera.right * offset.x() + camera.down * offset.y();
        uniforms.aperture_world[0] = aperture_world.x(); uniforms.aperture_world[1] = aperture_world.y();
        uniforms.aperture_world[2] = aperture_world.z();
        uniforms.intrinsics[0] = camera.fx * camera.photo.focal_scale;
        uniforms.intrinsics[1] = camera.fy * camera.photo.focal_scale;
        uniforms.intrinsics[2] = aperture_camera.cx; uniforms.intrinsics[3] = aperture_camera.cy;
        // Projection/clamping remain in the full camera domain. Cropping the
        // camera intrinsics per tile would change covariance and edge geometry.
        uniforms.image[0] = static_cast<float>(camera.image_size.width());
        uniforms.image[1] = static_cast<float>(camera.image_size.height());
        uniforms.raster[0] = static_cast<float>(region.x()); uniforms.raster[1] = static_cast<float>(region.y());
        uniforms.raster[2] = static_cast<float>(width); uniforms.raster[3] = static_cast<float>(height);
        uniforms.image[2] = static_cast<float>(scene->sh_degree);
        uniforms.position[0] = camera.position.x(); uniforms.position[1] = camera.position.y();
        uniforms.position[2] = camera.position.z();
        uniforms.temporal[0] = local_seconds;
        uniforms.temporal[1] = temporal ? 1.0F : 0.0F;
        uniforms.temporal[2] = static_cast<float>(scene->temporal_sh_degree);
        uniforms.temporal[3] = temporal ? scene->local_time_range[1]-scene->local_time_range[0] : 1.0F;
        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        if (command == nil) { frame.error = "Cannot allocate Gaussian GPU command buffer."; return frame; }
        if (!reused) {
          auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
          pass.colorAttachments[0].texture = accumulation;
          pass.colorAttachments[0].loadAction = MTLLoadActionClear;
          pass.colorAttachments[0].storeAction = MTLStoreActionStore;
          pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
          id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
          if (encoder == nil) { frame.error = "Cannot create Gaussian GPU render pass."; return frame; }
          [encoder setRenderPipelineState:pipeline_];
          [encoder setVertexBuffer:points_ offset:0 atIndex:0];
          [encoder setVertexBuffer:order_buffer offset:0 atIndex:1];
          [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
          [encoder setVertexBuffer:temporal_ offset:0 atIndex:3];
          [encoder setVertexBuffer:appearance_ offset:0 atIndex:4];
          [encoder setCullMode:MTLCullModeNone];
          if (!order_.empty()) [encoder drawPrimitives:MTLPrimitiveTypeTriangle vertexStart:0 vertexCount:6 instanceCount:order_.size()];
          [encoder endEncoding];
          if (optical) {
            id<MTLComputeCommandEncoder> integration = [command computeCommandEncoder];
            if (integration == nil) { frame.error = "Cannot create aperture integration pass."; return frame; }
            [integration setComputePipelineState:integrate_];
            [integration setTexture:accumulation atIndex:0];
            [integration setTexture:integrated atIndex:1];
            const auto index = static_cast<quint32>(sample_index);
            [integration setBytes:&index length:sizeof(index) atIndex:0];
            [integration dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [integration endEncoding];
          }
        }
        if (intermediate || completed_samples == passes) {
          const std::array<float, 4> photo{camera.photo.exposure_ev, static_cast<float>(camera.photo.look), optical ? 1.0F : 0.0F, 0};
          FilmUniforms film_uniforms{film.grain_amount, film.grain_size, film.halation_amount,
              film.halation_radius, film.halation_threshold, film.seed};
          film_uniforms.canvas = {static_cast<float>(camera.image_size.width()), static_cast<float>(camera.image_size.height()),
              static_cast<float>(region.x()), static_cast<float>(region.y())};
          if (film.halation_amount > 0) {
            id<MTLComputeCommandEncoder> halo = [command computeCommandEncoder];
            if (halo == nil) { frame.error = "Cannot create halation pass."; return frame; }
            [halo setComputePipelineState:halation_];
            [halo setTexture:optical ? integrated : accumulation atIndex:0];
            [halo setTexture:glow atIndex:1];
            [halo setBytes:photo.data() length:sizeof(photo) atIndex:0];
            [halo setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
            [halo dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [halo endEncoding];
          }
          id<MTLComputeCommandEncoder> conversion = [command computeCommandEncoder];
          if (conversion == nil) { frame.error = "Cannot create Gaussian photo conversion pass."; return frame; }
          [conversion setComputePipelineState:encode_];
          [conversion setTexture:optical ? integrated : accumulation atIndex:0];
          [conversion setTexture:lease->texture atIndex:1];
          [conversion setTexture:glow != nil ? glow : accumulation atIndex:2];
          [conversion setBytes:photo.data() length:sizeof(photo) atIndex:0];
          [conversion setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
          [conversion dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
          [conversion endEncoding];
          if (readback) {
            readback_buffer = [device_ newBufferWithLength:row_bytes * height options:MTLResourceStorageModeShared];
            if (readback_buffer == nil) { frame.error = "Cannot allocate export readback."; return frame; }
            id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
            if (copy == nil) { frame.error = "Cannot create Gaussian photo readback pass."; return frame; }
            [copy copyFromTexture:lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                sourceSize:MTLSizeMake(width, height, 1) toBuffer:readback_buffer destinationOffset:0
                destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * height];
            [copy endEncoding];
          }
        }
        [command commit];
        // This API is worker-only. Publishing only completed immutable textures
        // keeps Qt's render queue independent and prevents resource reuse races.
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
          frame.error = command.error ? QString::fromNSString(command.error.localizedDescription) : "Gaussian GPU command did not complete.";
          return frame;
        }
        gpu_ms += (command.GPUEndTime - command.GPUStartTime) * 1000.0;
        if (options.samples_completed) options.samples_completed->store(static_cast<int>(reused ? offsets.size() : sample_index + 1));
        if (intermediate && !canceled()) {
          GaussianFrame partial;
          partial.native_lease = lease;
          partial.native_texture = reinterpret_cast<quint64>((__bridge void*)lease->texture);
          partial.size = region.size();
          partial.metrics = {{"backend", "metal-gaussian-v1"}, {"camera", camera.description()},
              {"photoSettings", camera.photo.description()}, {"progressivePreview", true},
              {"apertureSamples", static_cast<int>(completed_samples)}, {"targetApertureSamples", static_cast<int>(offsets.size())},
              {"previewReadback", false}, {"filmEffectsApplied", film.active()},
              {"lensModel", "thin-lens-scene-units-v1"}, {"colorContract", "sRGB-display-referred"},
              {"sceneSeconds", scene_seconds ? QVariant(*scene_seconds) : QVariant{}}, {"temporal", temporal},
              {"width", region.width()}, {"height", region.height()}, {"gpuMs", gpu_ms},
              {"renderMs", static_cast<double>(elapsed.nsecsElapsed()) / 1e6}};
          options.progressive_frames->publish(std::move(partial));
          ++progressive_frames;
          // Qt may now sample the published texture. Never encode into it again.
          lease = std::make_shared<TextureLease>();
          lease->texture = [device_ newTextureWithDescriptor:descriptor];
          if (lease->texture == nil) { frame.error = "Cannot allocate progressive photo texture."; return frame; }
        }
      }
      if (canceled()) return cancellation();
      if (tile_order == nullptr && !options.interactive && !reused && width <= 4096 && height <= 4096) {
        resolved_source_ = optical ? integrated : accumulation;
        resolved_key_ = source_key; resolved_seconds_ = scene_seconds;
        resolved_front_count_ = order_.size();
      }
      frame.native_lease = lease;
      frame.native_texture = reinterpret_cast<quint64>((__bridge void*)lease->texture);
      frame.size = region.size();
      if (readback) {
        frame.image = QImage(static_cast<const uchar*>(readback_buffer.contents), region.width(), region.height(),
            static_cast<qsizetype>(row_bytes), QImage::Format_ARGB32).copy();
        frame.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      }
      frame.metrics = {{"backend", "metal-gaussian-v1"}, {"pointCount", static_cast<qulonglong>(scene->points.size())},
          {"temporal", temporal}, {"sceneSeconds", scene_seconds ? QVariant(*scene_seconds) : QVariant{}},
          {"temporalShDegree", scene->temporal_sh_degree},
          {"frontFacingCount", static_cast<qulonglong>(reused ? resolved_front_count_ : order_.size())}, {"sortAndSetupMs", sort_ms},
          {"gpuMs", gpu_ms}, {"apertureSamples", static_cast<int>(offsets.size())},
          {"progressiveFramesProduced", progressive_frames},
          {"depthCenterBytes", static_cast<qulonglong>(depth_centers_.capacity() * sizeof(std::array<float, 3>))},
          {"lensModel", optical ? "thin-lens-scene-units-v1" : "pinhole"},
          {"filmEffectsApplied", film.active()},
          {"resolvedSourceReused", reused}, {"rasterizedApertureSamples", reused ? 0 : static_cast<int>(offsets.size())},
          {"resolvedSourceBytes", resolved_source_ != nil ? static_cast<qulonglong>(resolved_source_.width * resolved_source_.height * 16) : 0},
          {"renderMs", static_cast<double>(elapsed.nsecsElapsed()) / 1e6}, {"previewReadback", readback},
          {"photoSettings", camera.photo.description()},
          {"colorContract", "sRGB-display-referred"}, {"width", region.width()}, {"height", region.height()}};
      return frame;
    }
  }
  GaussianFrame renderTiled(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, std::optional<double> seconds, const GaussianRenderOptions& options) {
    @autoreleasepool {
      QElapsedTimer elapsed; elapsed.start();
      GaussianFrame output;
      const auto canceled = [&] { return options.canceled && options.canceled->load(); };
      const auto cancellation = [] { GaussianFrame frame; frame.canceled = true; return frame; };
      if (canceled()) return cancellation();
      if (options.tiles_completed) options.tiles_completed->store(0);
      if (options.samples_completed) options.samples_completed->store(0);
      // Validate before allocating a potentially large CPU photograph.
      if (!scene || scene->points.empty() || scene->points.size() > 2000000 || scene->sh_degree < 0 ||
          scene->sh_degree > 3 || !camera.photo.valid() || (seconds && !std::isfinite(*seconds))) {
        output.error = "Invalid scene or photo settings."; return output;
      }
      const int edge = options.export_tile_edge == 0 ? 2048 : options.export_tile_edge;
      const int longest = std::max(camera.image_size.width(), camera.image_size.height());
      const auto film = options.interactive ? GaussianFilmSettings{} : camera.photo.film;
      const int halo = film.halation_amount > 0 ? static_cast<int>(std::ceil(3 * std::max(0.25F,
          film.halation_radius * static_cast<float>(longest) / 2048.0F))) : 0;
      const QRect canvas(QPoint{}, camera.image_size);
      output.image = QImage(camera.image_size, QImage::Format_ARGB32);
      if (output.image.isNull()) { output.error = "Cannot allocate the full-resolution photo."; return output; }
      output.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      output.size = camera.image_size;
      GaussianRenderOptions tile_options = options;
      tile_options.samples_completed.reset(); tile_options.export_tile_edge = 0;
      tile_options.tiles_completed.reset();
      id<MTLBuffer> tile_order = nil;
      int tiles = 0, rasterized_samples = 0, maximum_tile_pixels = 0;
      double gpu_ms = 0, sort_ms = 0;
      for (int y = 0; y < canvas.height(); y += edge) for (int x = 0; x < canvas.width(); x += edge) {
        if (canceled()) return cancellation();
        const QRect core(x, y, std::min(edge, canvas.width()-x), std::min(edge, canvas.height()-y));
        // Both separable halation passes need neighboring emitters. Render the
        // full support around each core, then discard the overlap when stitching.
        const auto region = core.adjusted(-halo, -halo, halo, halo).intersected(canvas);
        const auto tile = renderRegion(scene, camera, true, seconds, tile_options, region, &tile_order);
        if (tile.canceled || canceled()) return cancellation();
        if (!tile.error.isEmpty()) { GaussianFrame failed; failed.error = tile.error; return failed; }
        if (tile.image.size() != region.size()) { GaussianFrame failed; failed.error = "Incomplete photo tile."; return failed; }
        for (int row = 0; row < core.height(); ++row) {
          std::memcpy(output.image.scanLine(core.y()+row) + core.x()*4,
              tile.image.constScanLine(core.y()-region.y()+row) + (core.x()-region.x())*4,
              static_cast<size_t>(core.width())*4);
        }
        output.metrics = tile.metrics;
        gpu_ms += tile.metrics.value("gpuMs").toDouble(); sort_ms += tile.metrics.value("sortAndSetupMs").toDouble();
        rasterized_samples += tile.metrics.value("rasterizedApertureSamples").toInt();
        maximum_tile_pixels = std::max(maximum_tile_pixels, region.width()*region.height());
        ++tiles;
        if (options.tiles_completed) options.tiles_completed->store(tiles);
      }
      if (canceled()) return cancellation();
      if (options.samples_completed) options.samples_completed->store(output.metrics.value("apertureSamples").toInt());
      output.metrics.insert("tiledPhoto", true); output.metrics.insert("tileCount", tiles);
      output.metrics.insert("tileEdge", edge); output.metrics.insert("tileHalo", halo);
      output.metrics.insert("maximumTilePixels", maximum_tile_pixels);
      output.metrics.insert("width", canvas.width()); output.metrics.insert("height", canvas.height());
      output.metrics.insert("gpuMs", gpu_ms); output.metrics.insert("sortAndSetupMs", sort_ms);
      output.metrics.insert("rasterizedApertureSamples", rasterized_samples);
      output.metrics.insert("renderMs", static_cast<double>(elapsed.nsecsElapsed())/1e6);
      // No oversized GPU texture is published; explicit photo callers consume
      // the assembled color-tagged CPU image. Preview remains texture-only.
      return output;
    }
  }
  bool initialize() {
    if (pipeline_ != nil) return true;
    if (!error_.isEmpty()) return false;
    device_ = MTLCreateSystemDefaultDevice();
    queue_ = [device_ newCommandQueue];
    NSError* error = nil;
    auto* options = [MTLCompileOptions new];
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    id<MTLLibrary> library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:gaussian_splat_msl] options:options error:&error];
    if (library == nil) { error_ = error ? QString::fromNSString(error.localizedDescription) : "Metal is unavailable."; return false; }
    auto* descriptor = [MTLRenderPipelineDescriptor new];
    descriptor.vertexFunction = [library newFunctionWithName:@"splat_vertex"];
    descriptor.fragmentFunction = [library newFunctionWithName:@"splat_fragment"];
    auto* color = descriptor.colorAttachments[0];
    color.pixelFormat = MTLPixelFormatRGBA32Float;
    color.blendingEnabled = NO;  // Ordered framebuffer fetch preserves float32 transmittance.
    pipeline_ = [device_ newRenderPipelineStateWithDescriptor:descriptor error:&error];
    encode_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"encode_output"] error:&error];
    integrate_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"integrate_aperture"] error:&error];
    halation_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"halation_horizontal"] error:&error];
    if (pipeline_ == nil || encode_ == nil || integrate_ == nil || halation_ == nil || queue_ == nil) {
      pipeline_ = nil; error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize Metal Gaussian rendering.";
      return false;
    }
    return true;
  }
  id<MTLDevice> device_;
  id<MTLCommandQueue> queue_;
  id<MTLRenderPipelineState> pipeline_;
  id<MTLComputePipelineState> encode_;
  id<MTLComputePipelineState> integrate_;
  id<MTLComputePipelineState> halation_;
  id<MTLTexture> resolved_source_;
  QVariantMap resolved_key_;
  std::optional<double> resolved_seconds_;
  size_t resolved_front_count_ = 0;
  id<MTLBuffer> points_;
  id<MTLBuffer> temporal_, appearance_;
  std::shared_ptr<const GaussianSceneData> scene_;
  std::vector<DepthKey> keys_, scratch_;
  std::vector<std::array<float, 3>> depth_centers_;
  std::vector<quint32> order_;
  QString error_;
};
}
std::shared_ptr<GaussianSceneRenderer> make_gaussian_scene_renderer() { return std::make_shared<MetalGaussianRenderer>(); }
}  // namespace photo_editor::desktop
