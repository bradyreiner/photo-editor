#include "gaussian_scene_renderer.h"
#include "gaussian_splat_shader.h"
#include "gaussian_spacetime.h"
#include "gaussian_scan_visibility.h"
#include "gaussian_lens_sampling.h"
#include "gaussian_lens_projection.h"
#include "gaussian_physical_camera.h"
#include "gaussian_autofocus.h"
#include "gaussian_film_plane.h"
#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include <QColorSpace>
#include <QElapsedTimer>
#include <QRect>
#include <bit>
#include <cmath>
#include <cstring>

namespace photo_editor::desktop {
namespace {
class GaussianRenderActivity {
 public:
  GaussianRenderActivity() : token_([[NSProcessInfo processInfo]
      beginActivityWithOptions:NSActivityUserInitiatedAllowingIdleSystemSleep
      reason:@"Rendering a Gaussian scene requested by the user"]) {}
  ~GaussianRenderActivity() { if (token_ != nil) [[NSProcessInfo processInfo] endActivity:token_]; }
  GaussianRenderActivity(const GaussianRenderActivity&) = delete;
  GaussianRenderActivity& operator=(const GaussianRenderActivity&) = delete;
 private:
  id<NSObject> token_;
};
struct TextureLease { id<MTLTexture> texture; };
struct alignas(16) Uniforms { float view[16]; float intrinsics[4]; float image[4]; float position[4]; float temporal[4]; float raster[4]; float aperture[4]; float aperture_world[4]; float hybrid[4]; float atmosphere[4]; float medium[4]; float medium_grid[4]; };
struct alignas(16) SolidUniforms {
  std::array<float, 4> center_kind{}, axis_x{}, axis_y{}, axis_z{}, color_roughness{}, material{};
  std::array<quint32,4> geometry{};
};
struct alignas(16) LightUniforms { std::array<float, 4> vector_kind{}, color_intensity{}, axis_u{}, axis_v{}, shape{}; };
struct alignas(16) CompositionUniforms { quint32 objects = 0, lights = 0; float ambient = 0, reflections = 0; std::array<float,4> capture{}; std::array<quint32,4> ray_modes{}; };
static_assert(sizeof(SolidUniforms) == 112 && sizeof(LightUniforms) == 80 && sizeof(CompositionUniforms) == 48);
struct alignas(16) SurfaceMaterialUniforms {
  std::array<float,4> color_roughness{}, emission_metallic{}, uv_transform{};
  std::array<quint32,4> image{};
};
static_assert(sizeof(SurfaceMaterialUniforms) == 64);
struct alignas(16) LightIntegrationUniforms {
  quint32 first = 0, count = 1, total = 1, flags = 0, point_offset = 0, point_count = 0, pixel_y = 0, reserved = 0;
};
static_assert(sizeof(LightIntegrationUniforms) == 32);
struct alignas(16) ScanVisibilityUniforms { quint32 nodes = 0; float alpha_limit = 0.999F, cutoff = gaussian_scan_cutoff, termination = 1e-4F; };
static_assert(sizeof(ScanVisibilityUniforms) == 16);
struct DepthKey { quint32 key, index; };
int haze_cell_pixels(const GaussianCamera& camera,const GaussianRenderOptions& options,bool readback) {
  int cell = options.interactive ? 8 : 4;
  if (!readback) {
    // A Retina viewport should not multiply visibility work just because its
    // display has more pixels. Keep an angular sampling budget for previews;
    // exported photographs retain the fine four-pixel grid.
    const float focal = std::min(camera.fx,camera.fy)*camera.photo.focal_scale;
    while (cell < 64 && focal/static_cast<float>(cell) > 240) cell *= 2;
  }
  return cell;
}
int haze_region_edge(const GaussianCamera& camera,const GaussianRenderOptions& options,bool readback) {
  if (!camera.composition || !camera.composition->haze_shadows || camera.composition->haze_density <= 0) return 8192;
  const int slices = std::clamp(4*(options.interactive ? 8 : std::clamp(camera.composition->light_samples,1,1024)),64,1024);
  const int cell = haze_cell_pixels(camera,options,readback);
  // Leave 32 MiB for halo growth before the hard 128 MiB allocation guard.
  const int cells = static_cast<int>(std::sqrt((96.0*1024*1024)/(24*(slices+1))))-4;
  return std::clamp(cells*cell,128,2048);
}
struct alignas(16) FilmUniforms {
  float grain_amount, grain_size, halation_amount, halation_radius, threshold;
  quint32 seed;
  float padding[2]{};
  std::array<float, 4> canvas{}; // Full photo width/height and tile origin.
  std::array<float, 4> plane{}; // Microns/pixel x,y; film-plane model flag; filtered grain variance.
  std::array<float, 4> returning{}; // RGB return fractions; diffusion sigma in microns.
  FilmGrainSpectrum modes{};
};
static_assert(sizeof(FilmUniforms) == 80+film_grain_modes*16 && sizeof(FilmUniforms)<=4096);
std::array<float,2> film_pixel_pitch(const GaussianCamera& camera,const GaussianFilmSettings& film) {
  const double width=camera.image_size.width(),height=camera.image_size.height();
  const double gate_width=camera.physical ? camera.physical->gate_width_mm :
      std::min(static_cast<double>(film.format_width_mm),film.format_height_mm*width/height);
  const double gate_height=camera.physical ? camera.physical->gate_height_mm : gate_width*height/width;
  return {static_cast<float>(1000*gate_width/width),static_cast<float>(1000*gate_height/height)};
}
struct alignas(16) PostUniforms {
  std::array<float,4> bloom_glare{}, glare{}, color{};
};
static_assert(sizeof(PostUniforms) == 48);
struct alignas(16) PupilUniforms { std::array<float,4> point{}, intrinsics{}, raster{}, optics{}, sample{}, softness{}; };
static_assert(sizeof(PupilUniforms) == 96);
struct alignas(16) PrimaryRayCaptureUniforms {
  std::array<quint32,4> counts{};
  std::array<float,4> lens{}, pupil{}, optics{};
};
static_assert(sizeof(PrimaryRayCaptureUniforms) == 64);
void describe_autofocus(GaussianFrame& frame, const std::shared_ptr<const GaussianFocusRenderContext>& context) {
  if (!context) return;
  frame.metrics.insert("camera",context->authored_camera);
  frame.metrics.insert("resolvedCamera",context->resolved_camera);
  frame.metrics.insert("photoSettings",context->authored_camera.value("photo"));
  frame.metrics.insert("autofocus",context->report);
  if (!frame.metrics.contains("autofocusMs")) {
    const double ms = context->report.value("elapsedMs").toDouble();
    frame.metrics.insert("autofocusMs",ms);
    frame.metrics.insert("renderMs",frame.metrics.value("renderMs").toDouble()+ms);
  }
}
class MetalGaussianRenderer final : public GaussianSceneRenderer {
 public:
  GaussianFrame render(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds = {}) override {
    return renderWithOptions(scene, camera, readback, scene_seconds, {});
  }
  GaussianFrame renderWithOptions(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds,
      const GaussianRenderOptions& options) override {
    // Keep an asynchronous user-requested render active when its window is
    // covered. Scope ends on success, cancellation, error or exception; idle
    // system sleep remains allowed, and no persistent power setting changes.
    const GaussianRenderActivity activity;
    QElapsedTimer call_elapsed; call_elapsed.start(); scan_probe_ms_ = 0;
    if (!valid_gaussian_alpha_model(gaussian_alpha_model(camera))) {
      GaussianFrame frame; frame.error = "Invalid Gaussian alpha model."; return frame;
    }
    auto prepared_options = options;
    prepared_options.focus_context.reset();
    auto resolved = camera;
    if (scene && camera.photo.lens.auto_focus) {
      if (camera.composition && !gaussian_scene_composition(camera.composition->description())) {
        GaussianFrame frame; frame.error = "Invalid autofocus scene composition."; return frame;
      }
      if (options.lighting_batches_completed) options.lighting_batches_completed->store(0);
      const auto focus = resolve_gaussian_autofocus(*scene,camera,scene_seconds,options.canceled);
      if (focus.canceled) { GaussianFrame frame; frame.canceled = true; return frame; }
      resolved = focus.camera;
      prepared_options.focus_context = std::make_shared<GaussianFocusRenderContext>(GaussianFocusRenderContext{
          camera.description(),resolved.description(),focus.report});
    }
    auto frame = renderPrepared(scene,resolved,readback,scene_seconds,prepared_options);
    if (frame.error.isEmpty() && !frame.canceled) {
      describe_autofocus(frame,prepared_options.focus_context);
      if (resolved.composition && resolved.composition->needsProbe() && resolved.composition->active()) {
        frame.metrics.insert("scanProbeMs",scan_probe_ms_);
        frame.metrics.insert("scanProbeCaptureLightingEvaluations",probe_capture_evaluations_);
        frame.metrics.insert("scanProbeCaptureLightingCacheHits",probe_capture_hits_);
      }
      // Measure the complete public call, including probe creation/reuse, lens
      // preparation and autofocus. Region/tile timers omit those earlier phases.
      frame.metrics.insert("renderMs",static_cast<double>(call_elapsed.nsecsElapsed())/1e6);
    }
    return frame;
  }
 private:
  GaussianFrame renderPrepared(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds,
      const GaussianRenderOptions& options) {
    if (options.lighting_batches_completed) options.lighting_batches_completed->store(0);
    if (options.export_tile_edge != 0 && (!readback || options.export_tile_edge < 32 || options.export_tile_edge > 2048)) {
      GaussianFrame frame; frame.error = "Photo tile edges must be 32–2048 pixels and require explicit readback."; return frame;
    }
    const auto size = camera.image_size;
    const auto haze_edge = haze_region_edge(camera,options,readback);
    if (scene && camera.composition && camera.composition->needsProbe() && camera.composition->active()) {
      if (!gaussian_scene_composition(camera.composition->description())) { GaussianFrame frame; frame.error = "Invalid environment scene composition."; return frame; }
      QElapsedTimer probe_elapsed; probe_elapsed.start();
      auto prepared = prepareReflectionProbe(scene,camera,scene_seconds,options);
      scan_probe_ms_ = static_cast<double>(probe_elapsed.nsecsElapsed())/1e6;
      if (!prepared.error.isEmpty() || prepared.canceled) return prepared;
    }
    if (readback && size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192 &&
        (options.export_tile_edge > 0 || std::max(size.width(),size.height()) > haze_edge || size.width() > 4096 || size.height() > 4096 ||
         (camera.photo.lens.ray_mapping() && std::max(size.width(),size.height()) > 2048) ||
         (!options.interactive && camera.photo.lens.ray_traced && camera.photo.lens.aperture_radius > 0 && std::max(size.width(),size.height()) > 720)))
      return renderTiled(scene, camera, true, scene_seconds, options);
    if (!readback && ((camera.photo.lens.ray_mapping() && std::max(size.width(),size.height()) > 2048) || std::max(size.width(),size.height()) > haze_edge ||
        (!options.interactive && camera.photo.lens.ray_traced && camera.photo.lens.aperture_radius > 0 && std::max(size.width(),size.height()) > 720)) &&
        size.width() > 0 && size.height() > 0 && size.width() <= 8192 && size.height() <= 8192)
      return renderTiled(scene,camera,false,scene_seconds,options);
    return renderRegion(scene, camera, readback, scene_seconds, options, QRect(QPoint{}, size));
  }
 private:
  GaussianFrame prepareReflectionProbe(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, std::optional<double> seconds, const GaussianRenderOptions& options) {
    @autoreleasepool {
      GaussianFrame result;
      probe_capture_evaluations_ = probe_capture_hits_ = 0;
      if (!initialize()) { result.error = error_; return result; }
      QVector3D origin;
      for (const auto& object : camera.composition->objects) if (object.visible) {
        origin = object.position;
        if (object.kind == 2 && object.mesh) {
          auto center = (object.mesh->lower+object.mesh->upper)*0.5F;
          for (int axis = 0; axis < 3; ++axis) center[axis] *= object.size[axis];
          origin += QQuaternion::fromEulerAngles(object.rotation).rotatedVector(center);
        }
        break;
      }
      const bool sampled_capture = camera.composition->haze_shadows || camera.composition->softLights() ||
          (camera.composition->capture_lighting == 1 && camera.composition->capture_emission &&
           (camera.composition->capture_emission_importance || camera.composition->capture_emission_sampling_explicit) && camera.composition->hasEmittingObjects());
      const int probe_light_samples = camera.composition->changesCapturedAppearance() && sampled_capture ? (options.interactive ? 8 : camera.composition->light_samples) : 1;
      auto capture_effects = *camera.composition;
      // Probe output contains only the scan. Object reflection/diffuse switches
      // and diffuse strength cannot change that input radiance or recurse.
      capture_effects.environment_diffuse = false; capture_effects.environment_strength = 1; capture_effects.reflections = false; capture_effects.spatial_reflections = false; capture_effects.spatial_environment = false;
      const auto effects = camera.composition->changesCapturedAppearance() ? capture_effects.description() : QVariantMap{};
      const auto alpha_model = gaussian_alpha_model(camera);
      if (alpha_model == probe_alpha_model_ && probe_light_samples == probe_light_samples_ && effects == probe_effects_ && reflection_probe_ != nil && probe_scene_.lock() == scene && origin == probe_origin_ && seconds == probe_seconds_) {
        probe_reused_ = true; return result;
      }
      probe_reused_ = false;
      constexpr int edge = 256;
      auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA16Float width:edge height:edge mipmapped:YES];
      descriptor.textureType = MTLTextureType2DArray; descriptor.arrayLength = 6;
      descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite; descriptor.storageMode = MTLStorageModePrivate;
      id<MTLTexture> next = [device_ newTextureWithDescriptor:descriptor];
      if (next == nil) { result.error = "Cannot allocate the scan reflection probe."; return result; }
      const std::array<QVector3D,6> forwards{{{1,0,0},{-1,0,0},{0,1,0},{0,-1,0},{0,0,1},{0,0,-1}}};
      const std::array<QVector3D,6> rights{{{0,0,-1},{0,0,1},{1,0,0},{1,0,0},{1,0,0},{-1,0,0}}};
      const std::array<QVector3D,6> downs{{{0,1,0},{0,1,0},{0,0,-1},{0,0,1},{0,1,0},{0,1,0}}};
      GaussianRenderOptions probe_options; probe_options.canceled = options.canceled; probe_options.interactive = true;
      auto probe_composition = camera.composition->changesCapturedAppearance() ? camera.composition : std::shared_ptr<const GaussianSceneComposition>{};
      if (!probe_composition && alpha_model != GaussianAlphaModel::Brush999) {
        auto profile = std::make_shared<GaussianSceneComposition>(); profile->alpha_model = alpha_model; probe_composition = profile;
      }
      for (size_t face = 0; face < 6; ++face) {
        if (options.canceled && options.canceled->load()) { result.canceled = true; return result; }
        GaussianCamera probe; probe.name = "Scan reflection probe"; probe.position = origin;
        probe.image_size = {edge,edge}; probe.fx = probe.fy = edge*0.5F; probe.cx = probe.cy = edge*0.5F;
        probe.composition = probe_composition;
        probe.forward = forwards[face]; probe.right = rights[face]; probe.down = downs[face];
        auto frame = renderRegion(scene,probe,false,seconds,probe_options,QRect(0,0,edge,edge),nullptr,true,probe_light_samples);
        if (!frame.error.isEmpty() || frame.canceled) return frame;
        probe_capture_evaluations_ += frame.metrics.value("captureLightingEvaluated").toBool() ? 1 : 0;
        probe_capture_hits_ += frame.metrics.value("captureLightingReused").toBool() ? 1 : 0;
        const auto lease = std::static_pointer_cast<TextureLease>(frame.native_lease);
        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        id<MTLComputeCommandEncoder> pack = [command computeCommandEncoder];
        if (!lease || lease->texture == nil || command == nil || pack == nil) { result.error = "Cannot stage the scan reflection probe."; return result; }
        [pack setComputePipelineState:pack_probe_]; [pack setTexture:lease->texture atIndex:0]; [pack setTexture:next atIndex:1];
        const auto slice = static_cast<quint32>(face); [pack setBytes:&slice length:sizeof(slice) atIndex:0];
        [pack dispatchThreads:MTLSizeMake(edge,edge,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [pack endEncoding];
        if (face == 5) {
          id<MTLBlitCommandEncoder> mip = [command blitCommandEncoder];
          if (mip == nil) { result.error = "Cannot filter the scan reflection probe."; return result; }
          [mip generateMipmapsForTexture:next]; [mip endEncoding];
        }
        [command commit]; [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) { result.error = "Scan reflection probe GPU copy failed."; return result; }
      }
      if (options.canceled && options.canceled->load()) { result.canceled = true; return result; }
      reflection_probe_ = next; probe_scene_ = scene; probe_origin_ = origin; probe_seconds_ = seconds; probe_effects_ = effects; probe_light_samples_ = probe_light_samples; probe_alpha_model_ = alpha_model;
      return result;
    }
  }
  GaussianFrame renderRegion(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> scene_seconds,
      const GaussianRenderOptions& options, const QRect& region, id<MTLBuffer> __strong* tile_order = nullptr, bool source_only = false, int lighting_override = 0) {
    @autoreleasepool {
      GaussianFrame frame;
      const auto canceled = [&] { return options.canceled && options.canceled->load(); };
      const auto cancellation = [&] { GaussianFrame result; result.canceled = true; return result; };
      if (canceled()) return cancellation();
      QElapsedTimer elapsed; elapsed.start();
      if (!scene || scene->points.empty() || scene->points.size() > gaussian_static_point_limit || scene->sh_degree < 0 || scene->sh_degree > 3 ||
          camera.image_size.width() < 1 || camera.image_size.height() < 1 ||
          camera.image_size.width() > 8192 || camera.image_size.height() > 8192 || !camera.photo.valid() ||
          !gaussian_physical_camera_consistent(camera)) {
        frame.error = "Invalid scene, photo settings or output dimensions (maximum 8192 pixels per edge)."; return frame;
      }
      const bool ray_traced = !options.interactive && camera.photo.lens.ray_traced &&
          camera.photo.lens.aperture_radius > 0 && !source_only;
      const bool optical = ray_traced || (!options.interactive && camera.photo.lens.aperture_radius > 0);
      const bool lens_resolve = optical || camera.photo.lens.spherical_distortion != 0;
      const auto film = options.interactive ? GaussianFilmSettings{} : camera.photo.film;
      const auto post = options.interactive ? GaussianPostSettings{} : camera.photo.post;
      if ((film.active() || post.active()) && (region.width() > 4096 || region.height() > 4096)) {
        frame.error = "Camera effects are currently limited to 4096 pixels per edge."; return frame;
      }
      if (optical && (region.width() > 4096 || region.height() > 4096)) {
        frame.error = "Thin-lens rendering is currently limited to 4096 pixels per edge."; return frame;
      }
      const auto source_bounds = ray_traced ? std::optional<QRect>(region) : gaussian_lens_source_region(camera,region,optical);
      if (!source_bounds || source_bounds->width() > 8192 || source_bounds->height() > 8192 ||
          ((camera.photo.lens.spherical_distortion != 0 || optical) && static_cast<qint64>(source_bounds->width())*source_bounds->height() > 16777216)) {
        frame.error = "Lens coverage exceeds the render budget. Reduce aberration strength, aperture or output size."; return frame;
      }
      const auto source_region = *source_bounds;
      auto offsets = optical ? gaussian_aperture_samples(camera.photo.lens) : std::vector<QVector2D>{{0, 0}};
      if (ray_traced && camera.photo.lens.aperture_radius == 0)
        offsets.assign(static_cast<size_t>(camera.photo.lens.samples),QVector2D{});
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
      const bool hybrid = !source_only && camera.composition && camera.composition->active();
      const bool solid_receivers = !source_only && camera.composition && camera.composition->hasVisibleObjects();
      const bool capture_lit = camera.composition && camera.composition->capture_lighting != 0;
      const bool shadow_receiver = capture_lit && camera.composition->capture_lighting == 2;
      const bool capture_emission = capture_lit && !shadow_receiver && camera.composition->capture_emission && camera.composition->hasEmittingObjects();
      const bool capture_scan_shadows = capture_emission && camera.composition->scan_shadows;
      const bool hazy = camera.composition && camera.composition->haze_density > 0;
      const bool shadowed_haze = hazy && camera.composition->haze_shadows;
      const bool scan_shadows = (solid_receivers || shadowed_haze) && camera.composition->scan_shadows;
      if (shadowed_haze && !ray_traced && !initializeHaze()) { frame.error = error_; return frame; }
      const bool spatial_specular = solid_receivers && camera.composition->reflections && camera.composition->spatial_reflections;
      const bool spatial_diffuse = solid_receivers && camera.composition->environmentLighting() && camera.composition->spatial_environment;
      const bool spatial = spatial_specular || spatial_diffuse;
      const bool needs_scan = ray_traced || scan_shadows || spatial || capture_scan_shadows;
      active_spatial_ = spatial;
      if (spatial && !initializeSpatial(camera.composition->surface_bounces > 1)) { frame.error = error_; return frame; }
      if (ray_traced && spatial && !initializeRaySpatialCapture()) { frame.error = error_; return frame; }
      const bool soft_lighting = capture_emission || spatial || (camera.composition && (((solid_receivers || capture_lit) && camera.composition->softLights()) || (solid_receivers && camera.composition->environmentLighting())));
      const bool batched_lighting = soft_lighting || needs_scan || shadowed_haze || (ray_traced && capture_lit);
      const auto batch_samples = spatial ? 1U : (scan_shadows || capture_scan_shadows) ? 4U : 8U;
      const auto light_samples = static_cast<quint32>(soft_lighting ? (lighting_override > 0 ? lighting_override : options.interactive ? 8 : camera.composition->light_samples) : 1);
      if (camera.composition && !gaussian_scene_composition(camera.composition->description())) { frame.error = "Invalid object, material or light configuration."; return frame; }
      QStringList geometry_key;
      if (camera.composition) for (const auto& object : camera.composition->objects) if (object.visible && object.kind == 2) {
        if (!object.mesh || object.mesh->sha256 != object.mesh_sha256) { frame.error = "Imported geometry is unavailable."; return frame; }
        if (!geometry_key.contains(object.mesh_sha256)) geometry_key.append(object.mesh_sha256);
      }
      geometry_key.sort();
      if (geometry_key != mesh_key_) {
        std::vector<GaussianMeshNode> nodes; std::vector<GaussianMeshTriangle> triangles; QMap<QString,quint32> roots;
        for (const auto& hash : geometry_key) {
          std::shared_ptr<const GaussianSceneMesh> mesh;
          for (const auto& object : camera.composition->objects) if (object.mesh_sha256 == hash && object.mesh) { mesh = object.mesh; break; }
          if (!mesh || mesh->nodes.empty() || mesh->triangles.empty() || triangles.size()+mesh->triangles.size() > 1000000) { frame.error = "Geometry exceeds the supported scene limits."; return frame; }
          const auto node_offset = static_cast<quint32>(nodes.size()), triangle_offset = static_cast<quint32>(triangles.size());
          roots.insert(hash,node_offset);
          for (auto node : mesh->nodes) {
            if (node.links[3] == 0) { node.links[0] += node_offset; node.links[1] += node_offset; } else node.links[2] += triangle_offset;
            nodes.push_back(node);
          }
          triangles.insert(triangles.end(),mesh->triangles.begin(),mesh->triangles.end());
        }
        const GaussianMeshNode dummy_node{}; const GaussianMeshTriangle dummy_triangle{};
        id<MTLBuffer> next_nodes = [device_ newBufferWithBytes:nodes.empty() ? &dummy_node : nodes.data() length:std::max(size_t{1},nodes.size())*sizeof(GaussianMeshNode) options:MTLResourceStorageModeShared];
        id<MTLBuffer> next_triangles = [device_ newBufferWithBytes:triangles.empty() ? &dummy_triangle : triangles.data() length:std::max(size_t{1},triangles.size())*sizeof(GaussianMeshTriangle) options:MTLResourceStorageModeShared];
        if (next_nodes == nil || next_triangles == nil) { frame.error = "Cannot allocate imported scene geometry."; return frame; }
        mesh_nodes_ = next_nodes; mesh_triangles_ = next_triangles; mesh_roots_ = roots; mesh_key_ = geometry_key;
        mesh_triangle_count_ = triangles.size(); mesh_node_count_ = nodes.size();
      }
      QMap<QString,std::shared_ptr<const GaussianColorTexture>> images;
      if (camera.composition) for (const auto& object : camera.composition->objects) if (object.visible && object.use_imported_materials && !object.material_set_sha256.isEmpty()) {
        if (!object.material_assets || object.material_assets->sha256 != object.material_set_sha256 || object.material_assets->mesh_sha256 != object.mesh_sha256 || object.materials.size() != static_cast<size_t>(object.mesh->material_names.size())) { frame.error = "Imported materials are unavailable."; return frame; }
        for (const auto& material : object.materials) if (material.use_texture && !material.texture_sha256.isEmpty()) {
          const auto image = object.material_assets->textures.value(material.texture_sha256);
          if (!image || image->image.isNull()) { frame.error = "A material color image is unavailable."; return frame; }
          images.insert(material.texture_sha256,image);
        }
      }
      qint64 image_bytes = 0; for (const auto& image : images) image_bytes += image->image.sizeInBytes();
      if (images.size() > 32 || image_bytes > 256*1024*1024) { frame.error = "Scene color images exceed their memory limit."; return frame; }
      const auto image_key = images.keys();
      const bool images_reused = image_key == color_image_key_;
      if (!images_reused) {
        auto* next_images = [NSMutableArray<id<MTLTexture>> arrayWithCapacity:32];
        id<MTLCommandBuffer> upload = [queue_ commandBuffer]; id<MTLBlitCommandEncoder> mip = [upload blitCommandEncoder];
        if (upload == nil || mip == nil) { frame.error = "Cannot upload material color images."; return frame; }
        for (const auto& image : images) {
          const auto& pixels = image->image;
          auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB width:static_cast<NSUInteger>(pixels.width()) height:static_cast<NSUInteger>(pixels.height()) mipmapped:YES];
          descriptor.usage = MTLTextureUsageShaderRead; descriptor.storageMode = device_.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
          id<MTLTexture> texture = [device_ newTextureWithDescriptor:descriptor];
          if (texture == nil) { [mip endEncoding]; frame.error = "Cannot allocate a material color image."; return frame; }
          [texture replaceRegion:MTLRegionMake2D(0,0,texture.width,texture.height) mipmapLevel:0 withBytes:pixels.constBits() bytesPerRow:static_cast<NSUInteger>(pixels.bytesPerLine())];
          if (texture.mipmapLevelCount > 1) [mip generateMipmapsForTexture:texture];
          [next_images addObject:texture];
        }
        [mip endEncoding]; [upload commit]; [upload waitUntilCompleted];
        if (upload.status != MTLCommandBufferStatusCompleted) { frame.error = "Material mipmap generation failed."; return frame; }
        while (next_images.count < 32) [next_images addObject:white_image_];
        color_images_ = next_images; color_image_key_ = image_key;
      }
      std::vector<SurfaceMaterialUniforms> surface_materials;
      CompositionUniforms composition;
      std::array<SolidUniforms, 16> solids{};
      std::array<std::array<float,4>,16> emission_bounds{};
      std::array<LightUniforms, 8> lights{};
      if (camera.composition) {
        composition.ambient = camera.composition->ambient;
        composition.capture = {camera.composition->capture_power,camera.composition->shadow_strength,
            camera.composition->environmentLighting() ? camera.composition->environment_strength : 0,camera.composition->scan_shadows ? 1.0F : 0.0F};
        composition.reflections = camera.composition->reflections ? 1.0F : 0.0F;
        composition.ray_modes = {spatial_specular ? 1U : 0U,spatial_diffuse ? 1U : 0U,static_cast<quint32>(camera.composition->surface_bounces),
            (shadow_receiver ? 8U : 0U) | (capture_emission ? 1U : 0U) | (capture_scan_shadows ? 2U : 0U) | (capture_emission && camera.composition->capture_emission_importance ? 4U : 0U)};
        for (const auto& object : camera.composition->objects) {
          if (!object.visible) continue;
          auto& solid = solids[composition.objects++];
          solid.center_kind = {object.position.x(), object.position.y(), object.position.z(), static_cast<float>(object.kind)};
          const auto rotation = QQuaternion::fromEulerAngles(object.rotation).toRotationMatrix();
          for (int axis = 0; axis < 3; ++axis) {
            auto& column = axis == 0 ? solid.axis_x : axis == 1 ? solid.axis_y : solid.axis_z;
            column = {rotation(0,axis), rotation(1,axis), rotation(2,axis), 1.0F/object.size[axis]};
          }
          solid.color_roughness = {object.color.x(), object.color.y(), object.color.z(), object.roughness};
          solid.material = {object.metallic, object.emission, 0, 0};
          if (capture_emission && camera.composition->capture_emission_importance) {
            QVector3D lower, upper;
            bool emitting = false;
            if (object.kind == 2) {
              for (size_t triangle_index = 0; triangle_index < object.mesh->triangles.size(); ++triangle_index) {
                if ((triangle_index & 4095U) == 0 && canceled()) return cancellation();
                const auto& triangle = object.mesh->triangles[triangle_index];
                const auto slot = static_cast<size_t>(triangle.uv2_material[2]);
                const bool emits = object.use_imported_materials && !object.material_set_sha256.isEmpty()
                    ? slot < object.materials.size() && object.materials[slot].emission.lengthSquared() > 0
                    : object.emission > 0 && object.color.lengthSquared() > 0;
                if (!emits) continue;
                const auto p = gaussian_mesh_vector(triangle.position);
                for (auto vertex : {p,p+gaussian_mesh_vector(triangle.edge1),p+gaussian_mesh_vector(triangle.edge2)}) {
                  for (int axis = 0; axis < 3; ++axis) vertex[axis] *= object.size[axis];
                  if (!emitting) { lower = upper = vertex; emitting = true; }
                  else for (int axis = 0; axis < 3; ++axis) { lower[axis] = std::min(lower[axis],vertex[axis]); upper[axis] = std::max(upper[axis],vertex[axis]); }
                }
              }
            } else emitting = object.emission > 0 && object.color.lengthSquared() > 0;
            if (emitting) {
              const auto local_center = (lower+upper)*0.5F;
              const auto world_center = object.position+QQuaternion::fromRotationMatrix(rotation).rotatedVector(local_center);
              const float radius = object.kind == 2 ? (upper-lower).length()*0.5F : object.kind == 1 ? object.size.length() : std::max({object.size.x(),object.size.y(),object.size.z()});
              emission_bounds[composition.objects-1] = {world_center.x(),world_center.y(),world_center.z(),radius*(1+2e-6F)};
            }
          }
          if (object.kind == 2) solid.geometry[0] = mesh_roots_.value(object.mesh_sha256);
          if (object.use_imported_materials && !object.material_set_sha256.isEmpty()) {
            solid.geometry[1] = static_cast<quint32>(surface_materials.size()+1); solid.geometry[2] = static_cast<quint32>(object.materials.size());
            for (const auto& material : object.materials) {
              SurfaceMaterialUniforms surface;
              surface.color_roughness = {material.color.x(),material.color.y(),material.color.z(),material.roughness};
              surface.emission_metallic = {material.emission.x(),material.emission.y(),material.emission.z(),material.metallic};
              surface.uv_transform = {material.uv_scale.x(),material.uv_scale.y(),material.uv_offset.x(),material.uv_offset.y()};
              const auto image_index = image_key.indexOf(material.texture_sha256);
              surface.image = {image_index >= 0 ? static_cast<quint32>(image_index) : 0U,material.use_texture && image_index >= 0 ? 1U : 0U,material.clamp ? 1U : 0U,0};
              surface_materials.push_back(surface);
            }
          }
        }
        for (const auto& light : camera.composition->lights) {
          if (!light.visible) continue;
          auto& lamp = lights[composition.lights++];
          lamp.vector_kind = {light.vector.x(),light.vector.y(),light.vector.z(),static_cast<float>(light.kind)};
          lamp.color_intensity = {light.color.x(),light.color.y(),light.color.z(),light.intensity};
          const auto axes = QQuaternion::fromEulerAngles(light.rotation).toRotationMatrix();
          lamp.axis_u = {axes(0,0),axes(1,0),axes(2,0),light.size.x()}; lamp.axis_v = {axes(0,1),axes(1,1),axes(2,1),light.size.y()};
          lamp.shape = {light.radius,light.angular_radius*static_cast<float>(M_PI/180.0),light.camera_visible ? 1.0F : 0.0F,0};
        }
      }
      const SurfaceMaterialUniforms dummy_surface{};
      id<MTLBuffer> material_buffer = [device_ newBufferWithBytes:surface_materials.empty() ? &dummy_surface : surface_materials.data() length:std::max(size_t{1},surface_materials.size())*sizeof(SurfaceMaterialUniforms) options:MTLResourceStorageModeShared];
      if (material_buffer == nil) { frame.error = "Cannot allocate material settings."; return frame; }
      auto source_camera = camera;
      source_camera.name.clear();
      source_camera.photo.exposure_ev = 0; source_camera.photo.look = 0; source_camera.photo.film = {}; source_camera.photo.post = {}; source_camera.photo.tone = {};
      auto source_key = source_camera.description();
      source_key.insert("alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(camera)));
      if (hybrid || capture_lit || hazy) source_key.insert("sceneObjects", camera.composition->description());
      if (shadowed_haze) source_key.insert("hazeFieldCellPixels",haze_cell_pixels(camera,options,readback));
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
        scan_acceleration_ = nil;
        if (native_scan_) {
          [scan_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [capture_scan_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [spatial_shadow_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [spatial_radiance_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [path_shadow_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [path_radiance_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
          [haze_field_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
        }
        scene_ = scene; scan_ready_ = false; scan_nodes_ = nil; scan_particles_ = nil; scan_node_count_ = scan_particle_count_ = 0; normals_ = nil; illumination_ = nil; shadow_accumulation_ = nil; normals_ready_ = false;
        capture_lighting_key_.clear();
      }
      if (points_ == nil) { frame.error = "Cannot allocate Gaussian scene memory."; return frame; }
      const bool scan_reused = scan_ready_ && (!temporal || scan_seconds_ == local_seconds);
      double scan_build_ms = 0;
      if (needs_scan && !scan_reused) {
        QElapsedTimer scan_elapsed; scan_elapsed.start();
        const auto index = build_gaussian_scan_index(*scene,local_seconds,options.canceled,native_scan_);
        if (index.canceled) return cancellation();
        if (!index.error.isEmpty()) { frame.error = index.error; return frame; }
        const GaussianScanNode dummy_node{}; const GaussianScanParticle dummy_particle{};
        auto nodes = [device_ newBufferWithBytes:index.nodes.empty() ? &dummy_node : index.nodes.data()
            length:std::max(size_t{1},index.nodes.size())*sizeof(GaussianScanNode) options:MTLResourceStorageModeShared];
        auto particles = [device_ newBufferWithBytes:index.particles.empty() ? &dummy_particle : index.particles.data()
            length:std::max(size_t{1},index.particles.size())*sizeof(GaussianScanParticle) options:MTLResourceStorageModeShared];
        if (nodes == nil || particles == nil) { frame.error = "Cannot allocate scan shadow visibility."; return frame; }
        id<MTLAccelerationStructure> acceleration = native_scan_ ? buildScanAcceleration(index.bounds) : nil;
        if (native_scan_ && acceleration == nil) { frame.error = "Cannot build native scan visibility."; return frame; }
        if (canceled()) return cancellation();
        // Commit geometry, particle data and the scene/time key together. A
        // canceled native build must leave the previous complete cache intact.
        scan_acceleration_ = acceleration;
        if (native_scan_) [capture_scan_functions_ setBuffer:dummy_scan_particles_ offset:0 atIndex:0];
        scan_nodes_ = nodes; scan_particles_ = particles;
        scan_node_count_ = static_cast<quint32>(index.nodes.size()); scan_particle_count_ = index.particles.size();
        scan_seconds_ = local_seconds; scan_ready_ = true;
        scan_build_ms = static_cast<double>(scan_elapsed.nsecsElapsed())/1e6;
      }
      if (capture_lit && normals_ == nil) {
        const auto bytes = scene->points.size()*sizeof(std::array<float,4>);
        normals_ = [device_ newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
        illumination_ = [device_ newBufferWithLength:bytes options:MTLResourceStorageModePrivate];
        if (normals_ == nil || illumination_ == nil) { normals_ = nil; illumination_ = nil; frame.error = "Cannot allocate captured-scene lighting."; return frame; }
      }
      if (!shadow_receiver) shadow_accumulation_ = nil;
      if (shadow_receiver && shadow_accumulation_ == nil) {
        shadow_accumulation_ = [device_ newBufferWithLength:scene->points.size()*32 options:MTLResourceStorageModePrivate];
        if (shadow_accumulation_ == nil) { frame.error = "Cannot allocate captured-scene shadow accumulation."; return frame; }
      }
      // shade_capture depends on the observer position, not the view axes,
      // raster grid or lens/post settings. All six probe faces can share its
      // completed field. Keep the existing buffer; retain no second scene-sized
      // allocation. Full composition/quality/time keys conservatively invalidate
      // geometry, materials, emitters, haze, alpha and draft/final changes.
      const QVariantMap capture_lighting_key = capture_lit ? QVariantMap{
          {"position",QVariantList{camera.position.x(),camera.position.y(),camera.position.z()}},
          {"composition",camera.composition->description()},
          {"alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(camera))},
          {"samples",light_samples},{"batchSamples",batched_lighting ? std::min(batch_samples,light_samples) : 1U}} : QVariantMap{};
      const bool capture_lighting_reused = capture_lit && !reused && !capture_lighting_key_.isEmpty() &&
          capture_lighting_key_ == capture_lighting_key && capture_lighting_seconds_ == scene_seconds;
      bool capture_lighting_evaluated = false;
      // Invalidate before the first write, including a canceled partial batch.
      // Revalidate only after the first complete GPU pass has finished.
      if (capture_lit && !reused && !capture_lighting_reused) capture_lighting_key_.clear();
      id<MTLBuffer> order_buffer = nil;
      if (ray_traced) order_.clear();
      if (!ray_traced && !reused && (tile_order == nullptr || *tile_order == nil)) {
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
        // Eleven-bit digits fit the histogram in 16 KiB and cover the key in
        // three passes, avoiding a fourth full-scene histogram/scatter sweep.
        scratch_.resize(keys_.size());
        for (unsigned shift = 0; shift < 32; shift += 11) {
          if (canceled()) return cancellation();
          std::array<size_t, 2048> counts{};
          auto* offsets = counts.data();
          const auto* input = keys_.data();
          auto* output = scratch_.data();
          for (size_t i = 0; i < front_count; ++i) ++offsets[(input[i].key >> shift) & 2047U];
          size_t prefix = 0;
          for (auto& count : counts) { const auto next = prefix + count; count = prefix; prefix = next; }
          for (size_t i = 0; i < front_count; ++i) output[offsets[(input[i].key >> shift) & 2047U]++] = input[i];
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
      } else if (!ray_traced && !reused) {
        // The tiled coordinator owns one immutable scene/pose/time and invokes
        // these regions serially. Their exact depth order is shared, not resorted.
        order_buffer = *tile_order;
      }
      const double sort_ms = ray_traced ? 0 : static_cast<double>(elapsed.nsecsElapsed()) / 1e6;
      const auto width = static_cast<NSUInteger>(region.width());
      const auto height = static_cast<NSUInteger>(region.height());
      const auto raster_width = static_cast<NSUInteger>(source_region.width()), raster_height = static_cast<NSUInteger>(source_region.height());
      auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:raster_width height:raster_height mipmapped:NO];
      descriptor.usage = MTLTextureUsageRenderTarget | MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
      descriptor.storageMode = MTLStorageModePrivate;
      id<MTLTexture> accumulation = reused ? resolved_source_ : [device_ newTextureWithDescriptor:descriptor];
      descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
      id<MTLTexture> solid_surface = hybrid && !reused ? [device_ newTextureWithDescriptor:descriptor] : dummy_solid_;
      descriptor.pixelFormat = MTLPixelFormatRGBA16Float;
      id<MTLTexture> haze_surface = hazy && !reused ? [device_ newTextureWithDescriptor:descriptor] : dummy_solid_;
      const int haze_cell = haze_cell_pixels(camera,options,readback);
      const int haze_slices = camera.composition ? std::clamp(4*(options.interactive ? 8 : camera.composition->light_samples),64,1024) : 64;
      const int haze_grid_x = static_cast<int>(std::floor(static_cast<double>(source_region.x())/haze_cell))-1;
      const int haze_grid_y = static_cast<int>(std::floor(static_cast<double>(source_region.y())/haze_cell))-1;
      const int haze_grid_width = static_cast<int>(std::ceil(static_cast<double>(source_region.x()+source_region.width())/haze_cell))-haze_grid_x+1;
      const int haze_grid_height = static_cast<int>(std::ceil(static_cast<double>(source_region.y()+source_region.height())/haze_cell))-haze_grid_y+1;
      const auto haze_field_bytes = static_cast<quint64>(haze_grid_width)*static_cast<quint64>(haze_grid_height)*static_cast<quint64>(haze_slices+1)*8;
      const auto haze_storage_bytes = static_cast<quint64>(haze_grid_width)*static_cast<quint64>(haze_grid_height)*(static_cast<quint64>(haze_slices)*24+8);
      id<MTLTexture> haze_field = dummy_haze_field_;
      id<MTLTexture> haze_samples = dummy_haze_field_;
      if (shadowed_haze && !reused) {
        if (haze_storage_bytes > 128ULL*1024*1024) { frame.error = "Haze field exceeds the bounded region budget."; return frame; }
        auto* field_descriptor = [MTLTextureDescriptor new]; field_descriptor.textureType = MTLTextureType3D;
        field_descriptor.pixelFormat = MTLPixelFormatRGBA16Float; field_descriptor.storageMode = MTLStorageModePrivate;
        field_descriptor.width = static_cast<NSUInteger>(haze_grid_width); field_descriptor.height = static_cast<NSUInteger>(haze_grid_height);
        field_descriptor.depth = static_cast<NSUInteger>(haze_slices+1); field_descriptor.usage = MTLTextureUsageShaderRead|MTLTextureUsageShaderWrite;
        haze_field = [device_ newTextureWithDescriptor:field_descriptor];
        field_descriptor.pixelFormat = MTLPixelFormatRGBA32Float; field_descriptor.depth = static_cast<NSUInteger>(haze_slices);
        haze_samples = [device_ newTextureWithDescriptor:field_descriptor];
      }
      descriptor.pixelFormat = MTLPixelFormatRGBA32Float;
      descriptor.width = width; descriptor.height = height;
      id<MTLTexture> integrated = lens_resolve ? (reused ? resolved_source_ : [device_ newTextureWithDescriptor:descriptor]) : nil;
      id<MTLTexture> post_surface = post.spatial() ? [device_ newTextureWithDescriptor:descriptor] : nil;
      id<MTLTexture> highlights = post.bloom_amount > 0 || post.glare_amount > 0 ? [device_ newTextureWithDescriptor:descriptor] : nil;
      id<MTLTexture> bloom = post.bloom_amount > 0 ? [device_ newTextureWithDescriptor:descriptor] : nil;
      descriptor.pixelFormat = film.film_plane ? MTLPixelFormatRGBA32Float : MTLPixelFormatR32Float;
      id<MTLTexture> glow = film.halation_amount > 0 ? [device_ newTextureWithDescriptor:descriptor] : nil;
      descriptor.pixelFormat = MTLPixelFormatBGRA8Unorm;
      descriptor.usage = MTLTextureUsageShaderWrite | MTLTextureUsageShaderRead;
      auto lease = std::make_shared<TextureLease>();
      lease->texture = source_only ? accumulation : [device_ newTextureWithDescriptor:descriptor];
      if (accumulation == nil || lease->texture == nil || solid_surface == nil || haze_surface == nil || haze_field == nil || haze_samples == nil || (lens_resolve && integrated == nil) || (film.halation_amount > 0 && glow == nil) ||
          (post.spatial() && post_surface == nil) || ((post.bloom_amount > 0 || post.glare_amount > 0) && highlights == nil) ||
          (post.bloom_amount > 0 && bloom == nil)) {
        frame.error = "Cannot allocate Gaussian render targets."; return frame;
      }
      id<MTLBuffer> readback_buffer = nil;
      const NSUInteger row_bytes = ((width * 4 + 255) / 256) * 256;
      double gpu_ms = 0;
      size_t ray_trace_batch_size = 0;
      int progressive_frames = 0, lighting_progressive_frames = 0, lighting_batches = 0;
      double first_lighting_preview_ms = 0, max_lighting_gpu_ms = 0, last_lighting_gpu_ms = 0;
      id<MTLTexture> lighting_snapshot = nil;
      const auto finish_lighting = [&](id<MTLCommandBuffer> batch) {
        [batch commit]; [batch waitUntilCompleted];
        if (batch.status != MTLCommandBufferStatusCompleted) { frame.error = batch.error ? QString::fromNSString(batch.error.localizedDescription) : "Soft lighting did not complete."; return false; }
        const double ms = (batch.GPUEndTime-batch.GPUStartTime)*1000.0; last_lighting_gpu_ms = ms; gpu_ms += ms; max_lighting_gpu_ms = std::max(max_lighting_gpu_ms,ms);
        ++lighting_batches; if (options.lighting_batches_completed) options.lighting_batches_completed->fetch_add(1); return true;
      };
      const size_t passes = reused || ray_traced ? 1 : offsets.size();
      id<MTLBuffer> ray_pupil_buffer = nil;
      if (ray_traced) {
        std::vector<std::array<float,2>> pupil_points; pupil_points.reserve(offsets.size());
        for (const auto& pupil : offsets) pupil_points.push_back({pupil.x(),pupil.y()});
        ray_pupil_buffer = [device_ newBufferWithBytes:pupil_points.data()
            length:pupil_points.size()*sizeof(std::array<float,2>) options:MTLResourceStorageModeShared];
        if (ray_pupil_buffer == nil) { frame.error = "Cannot upload primary-ray pupil samples."; return frame; }
      }
      for (size_t sample_index = 0; sample_index < passes; ++sample_index) {
        if (canceled()) return cancellation();
        const auto completed_samples = sample_index + 1;
        const bool intermediate = options.progressive_frames && !readback && !reused && optical &&
            completed_samples < passes && std::has_single_bit(completed_samples) &&
            (completed_samples >= 4 || (completed_samples == 1 && hybrid && batched_lighting));
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
        uniforms.intrinsics[2] = ray_traced ? camera.cx : aperture_camera.cx;
        uniforms.intrinsics[3] = ray_traced ? camera.cy : aperture_camera.cy;
        // Projection/clamping remain in the full camera domain. Cropping the
        // camera intrinsics per tile would change covariance and edge geometry.
        uniforms.image[0] = static_cast<float>(camera.image_size.width());
        uniforms.image[1] = static_cast<float>(camera.image_size.height());
        uniforms.raster[0] = static_cast<float>(ray_traced ? region.x() : source_region.x());
        uniforms.raster[1] = static_cast<float>(ray_traced ? region.y() : source_region.y());
        uniforms.raster[2] = static_cast<float>(ray_traced ? width : raster_width);
        uniforms.raster[3] = static_cast<float>(ray_traced ? height : raster_height);
        uniforms.image[2] = static_cast<float>(scene->sh_degree);
        uniforms.image[3] = gaussian_alpha_limit(gaussian_alpha_model(camera));
        uniforms.position[0] = camera.position.x(); uniforms.position[1] = camera.position.y();
        uniforms.position[2] = camera.position.z();
        uniforms.temporal[0] = local_seconds;
        uniforms.temporal[1] = temporal ? 1.0F : 0.0F;
        uniforms.temporal[2] = static_cast<float>(scene->temporal_sh_degree);
        uniforms.temporal[3] = temporal ? scene->local_time_range[1]-scene->local_time_range[0] : 1.0F;
        uniforms.hybrid[0] = hybrid ? 1.0F : 0.0F;
        uniforms.hybrid[1] = shadow_receiver ? 2.0F : capture_lit ? 1.0F : 0.0F;
        if (camera.composition) {
          uniforms.hybrid[2] = camera.composition->haze_density; uniforms.hybrid[3] = camera.composition->haze_distance;
          uniforms.atmosphere[0] = camera.composition->haze_color.x(); uniforms.atmosphere[1] = camera.composition->haze_color.y();
          uniforms.atmosphere[2] = camera.composition->haze_color.z(); uniforms.atmosphere[3] = camera.composition->haze_anisotropy;
          uniforms.medium[0] = shadowed_haze ? 1.0F : 0.0F;
          uniforms.medium[1] = static_cast<float>(options.interactive ? 8 : camera.composition->light_samples);
          uniforms.medium[2] = static_cast<float>(haze_cell); uniforms.medium[3] = static_cast<float>(haze_slices);
          uniforms.medium_grid[0] = static_cast<float>(haze_grid_x); uniforms.medium_grid[1] = static_cast<float>(haze_grid_y);
          uniforms.medium_grid[2] = static_cast<float>(haze_grid_width); uniforms.medium_grid[3] = static_cast<float>(haze_grid_height);
        }
        // One bounded visibility field serves every primary depth, including
        // Gaussian fragments. It is rebuilt per pupil, time, camera and tile.
        if (shadowed_haze && !ray_traced && !reused) {
          const auto maximum_rows = std::max(1,512/haze_grid_width);
          auto band_rows = std::max(1,128/haze_grid_width);
          for (int y = 0; y < haze_grid_height;) {
            if (canceled()) return cancellation();
            const auto rows = std::min(band_rows,haze_grid_height-y);
            const std::array<quint32,2> band{static_cast<quint32>(y),static_cast<quint32>(rows)};
            auto batch = [queue_ commandBuffer];
            if (batch == nil || !encodeHaze(batch,uniforms,composition,solids,lights,band,true,haze_samples,nil,haze_field)) { frame.error = "Cannot build haze visibility field."; return frame; }
            if (!finish_lighting(batch)) return frame;
            y += rows;
            // The independent strata permit larger row groups without changing
            // sample positions. Target short submissions and retain a hard
            // 512-node row budget (or one wider row) and cancellation checkpoints.
            const auto next = std::max(1,static_cast<int>(std::floor(rows*4.0/std::max(0.05,last_lighting_gpu_ms))));
            band_rows = std::clamp(next,std::max(1,rows/2),std::min(maximum_rows,rows*2));
          }
          if (canceled()) return cancellation();
          auto prefix = [queue_ commandBuffer]; auto integrate = [prefix computeCommandEncoder];
          if (prefix == nil || integrate == nil) { frame.error = "Cannot integrate haze visibility field."; return frame; }
          [integrate setComputePipelineState:haze_integrate_pipeline_]; [integrate setTexture:haze_samples atIndex:0]; [integrate setTexture:haze_field atIndex:1];
          [integrate dispatchThreads:MTLSizeMake(haze_field.width,haze_field.height,1) threadsPerThreadgroup:MTLSizeMake(16,8,1)]; [integrate endEncoding];
          if (!finish_lighting(prefix)) return frame;
        }
        const auto prepare_haze = [&](id<MTLTexture> surface) {
          if (!shadowed_haze || ray_traced || reused) return true;
          // This pass only samples the completed prefix texture; it no longer
          // traces visibility rays. Keep a bounded lookup batch without paying
          // a command submission and CPU/GPU wait for each image row.
          const auto rows = std::max(NSUInteger{1},NSUInteger{32768}/raster_width);
          for (NSUInteger y = 0; y < raster_height; y += rows) {
            if (canceled()) return false;
            const std::array<quint32,2> band{static_cast<quint32>(y),static_cast<quint32>(std::min(rows,raster_height-y))};
            auto batch = [queue_ commandBuffer];
            if (batch == nil || !encodeHaze(batch,uniforms,composition,solids,lights,band,false,haze_surface,surface,haze_field)) { frame.error = "Cannot integrate scene haze."; return false; }
            if (!finish_lighting(batch)) return false;
          }
          return true;
        };
        const auto compose_sample = [&](id<MTLCommandBuffer> command,id<MTLTexture> display_solids,bool convert_output) {
          if (!reused && !ray_traced) {
            if (capture_lit && !capture_lighting_reused && sample_index == 0 && !batched_lighting) {
              const auto count = static_cast<quint32>(scene->points.size()); LightIntegrationUniforms integration; integration.point_count = count;
              if (!encodeCaptureLighting(command,uniforms,composition,solids,lights,material_buffer,emission_bounds,count,integration,!normals_ready_)) { frame.error = "Cannot create capture lighting."; return false; }
            }
            if (hazy && !shadowed_haze) {
              id<MTLComputeCommandEncoder> fog = [command computeCommandEncoder];
              if (fog == nil) { frame.error = "Cannot create haze lighting pass."; return false; }
              [fog setComputePipelineState:render_haze_]; [fog setTexture:haze_surface atIndex:0];
              [fog setBytes:&uniforms length:sizeof(uniforms) atIndex:0]; [fog setBytes:&composition length:sizeof(composition) atIndex:1];
              [fog setBytes:lights.data() length:sizeof(lights) atIndex:2];
              [fog dispatchThreads:MTLSizeMake(raster_width,raster_height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [fog endEncoding];
            }
            if (hybrid) {
              if (batched_lighting) {
                id<MTLComputeCommandEncoder> finish = [command computeCommandEncoder];
                if (finish == nil) { frame.error = "Cannot finish sampled object lighting."; return false; }
                [finish setComputePipelineState:finish_soft_solids_]; [finish setTexture:display_solids atIndex:0]; [finish setTexture:haze_surface atIndex:1];
                [finish setBytes:&uniforms length:sizeof(uniforms) atIndex:0]; [finish dispatchThreads:MTLSizeMake(raster_width,raster_height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [finish endEncoding];
              } else {
                const LightIntegrationUniforms integration;
                if (!encodeSolidLighting(command,uniforms,composition,solids,lights,material_buffer,display_solids,haze_surface,integration,raster_height)) { frame.error = "Cannot render scene objects."; return false; }
              }
            }
            auto* pass = [MTLRenderPassDescriptor renderPassDescriptor];
            pass.colorAttachments[0].texture = accumulation;
            pass.colorAttachments[0].loadAction = MTLLoadActionClear;
            pass.colorAttachments[0].storeAction = MTLStoreActionStore;
            pass.colorAttachments[0].clearColor = MTLClearColorMake(0, 0, 0, 1);
            id<MTLRenderCommandEncoder> encoder = [command renderCommandEncoderWithDescriptor:pass];
            if (encoder == nil) { frame.error = "Cannot create Gaussian GPU render pass."; return false; }
            [encoder setRenderPipelineState:pipeline_];
            [encoder setVertexBuffer:points_ offset:0 atIndex:0];
            [encoder setVertexBuffer:order_buffer offset:0 atIndex:1];
            [encoder setVertexBytes:&uniforms length:sizeof(uniforms) atIndex:2];
            [encoder setVertexBuffer:temporal_ offset:0 atIndex:3];
            [encoder setVertexBuffer:appearance_ offset:0 atIndex:4];
            [encoder setVertexBuffer:capture_lit ? illumination_ : points_ offset:0 atIndex:5];
            [encoder setFragmentBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [encoder setFragmentTexture:display_solids atIndex:0];
            [encoder setFragmentTexture:haze_surface atIndex:1];
            [encoder setFragmentTexture:haze_field atIndex:2];
            [encoder setCullMode:MTLCullModeNone];
            if (!order_.empty()) [encoder drawPrimitives:MTLPrimitiveTypeTriangleStrip vertexStart:0 vertexCount:4 instanceCount:order_.size()];
            [encoder endEncoding];
            if (hybrid) {
              id<MTLComputeCommandEncoder> composite = [command computeCommandEncoder];
              if (composite == nil) { frame.error = "Cannot composite scene objects."; return false; }
              [composite setComputePipelineState:composite_solids_];
              [composite setTexture:accumulation atIndex:0]; [composite setTexture:display_solids atIndex:1];
              [composite dispatchThreads:MTLSizeMake(raster_width,raster_height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)];
              [composite endEncoding];
            }
            if (hazy) {
              id<MTLComputeCommandEncoder> fog = [command computeCommandEncoder];
              if (fog == nil) { frame.error = "Cannot complete scene haze."; return false; }
              [fog setComputePipelineState:complete_haze_]; [fog setTexture:accumulation atIndex:0]; [fog setTexture:haze_surface atIndex:1];
              [fog setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
              [fog dispatchThreads:MTLSizeMake(raster_width,raster_height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [fog endEncoding];
            }
            if (lens_resolve) {
              id<MTLComputeCommandEncoder> integration = [command computeCommandEncoder];
              if (integration == nil) { frame.error = "Cannot create aperture integration pass."; return false; }
              [integration setComputePipelineState:integrate_];
              [integration setTexture:accumulation atIndex:0];
              [integration setTexture:integrated atIndex:1];
              const auto index = static_cast<quint32>(sample_index);
              [integration setBytes:&index length:sizeof(index) atIndex:0];
              const auto& lens = camera.photo.lens;
              PupilUniforms pupil;
              pupil.point = {optical ? offset.x()/lens.aperture_radius : 0, optical ? offset.y()/lens.aperture_radius : 0,
                  optical ? static_cast<float>(std::min(1e12,static_cast<double>(lens.vignetting_distance)/lens.aperture_radius)) : 0,lens.vignetting_scale};
              pupil.intrinsics = {camera.fx*camera.photo.focal_scale,camera.fy*camera.photo.focal_scale,camera.cx,camera.cy};
              pupil.raster = {static_cast<float>(region.x()),static_cast<float>(region.y()),
                  1-lens.vignetting_distance/lens.focus_distance,optical && (lens.vignetting_distance > 0 || lens.vignetting_scale < 1) ? 1.0F : 0.0F};
              pupil.optics = {lens.spherical_distortion,lens.astigmatism,lens.field_curvature,1/lens.focus_distance};
              pupil.sample = {offset.x(),offset.y(),static_cast<float>(source_region.x()),static_cast<float>(source_region.y())};
              pupil.softness = {lens.pupil_softness,lens.bokeh_aspect,0,0};
              [integration setBytes:&pupil length:sizeof(pupil) atIndex:1];
              [integration dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
              [integration endEncoding];
            }
          }
          if (!source_only && convert_output) {
            std::array<float, 4> photo{camera.photo.exposure_ev, static_cast<float>(camera.photo.look), lens_resolve ? 1.0F : 0.0F, 0};
            FilmUniforms film_uniforms{film.grain_amount, film.grain_size, film.halation_amount,
                film.halation_radius, film.halation_threshold, film.seed};
            film_uniforms.padding[0] = film.highlight_halation ? 1.0F : 0.0F;
            film_uniforms.canvas = {static_cast<float>(camera.image_size.width()), static_cast<float>(camera.image_size.height()),
                static_cast<float>(region.x()), static_cast<float>(region.y())};
            if (film.film_plane) {
              const auto pitch=film_pixel_pitch(camera,film);
              film_uniforms.modes=film_grain_spectrum(film.grain_microns,pitch[0],pitch[1],film.seed);
              film_uniforms.plane={pitch[0],pitch[1],1,static_cast<float>(film_grain_variance(film_uniforms.modes))};
              film_uniforms.returning={film.return_rgb[0],film.return_rgb[1],film.return_rgb[2],film.halation_microns};
            }
            if (film.halation_amount > 0) {
              id<MTLComputeCommandEncoder> halo = [command computeCommandEncoder];
              if (halo == nil) { frame.error = "Cannot create halation pass."; return false; }
              [halo setComputePipelineState:halation_];
              [halo setTexture:lens_resolve ? integrated : accumulation atIndex:0];
              [halo setTexture:glow atIndex:1];
              [halo setBytes:photo.data() length:sizeof(photo) atIndex:0];
              [halo setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
              [halo dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
              [halo endEncoding];
            }
            const PostUniforms post_uniforms{{post.bloom_amount,post.bloom_radius,post.glare_amount,post.glare_radius},
                {static_cast<float>(post.glare_rays),post.glare_rotation*static_cast<float>(M_PI)/180.0F,post.spectral_intensity,0},
                {post.threshold,post.chromatic_aberration,post.saturation,0}};
            if (highlights != nil) {
              id<MTLComputeCommandEncoder> bright = [command computeCommandEncoder];
              if (bright == nil) { frame.error = "Cannot extract camera highlights."; return false; }
              [bright setComputePipelineState:camera_highlights_];
              [bright setTexture:lens_resolve ? integrated : accumulation atIndex:0]; [bright setTexture:highlights atIndex:1];
              [bright setBytes:photo.data() length:sizeof(photo) atIndex:0];
              [bright setBytes:&post_uniforms length:sizeof(post_uniforms) atIndex:1];
              [bright dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [bright endEncoding];
            }
            if (bloom != nil) {
              id<MTLComputeCommandEncoder> blur = [command computeCommandEncoder];
              if (blur == nil) { frame.error = "Cannot filter camera bloom."; return false; }
              [blur setComputePipelineState:camera_bloom_]; [blur setTexture:highlights atIndex:0]; [blur setTexture:bloom atIndex:1];
              [blur setBytes:&post_uniforms length:sizeof(post_uniforms) atIndex:0];
              [blur setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
              [blur dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [blur endEncoding];
            }
            if (post_surface != nil) {
              id<MTLComputeCommandEncoder> effects = [command computeCommandEncoder];
              if (effects == nil) { frame.error = "Cannot resolve camera post effects."; return false; }
              [effects setComputePipelineState:camera_post_];
              [effects setTexture:lens_resolve ? integrated : accumulation atIndex:0];
              [effects setTexture:glow != nil ? glow : dummy_solid_ atIndex:1];
              [effects setTexture:highlights != nil ? highlights : dummy_solid_ atIndex:2];
              [effects setTexture:bloom != nil ? bloom : dummy_solid_ atIndex:3]; [effects setTexture:post_surface atIndex:4];
              [effects setBytes:photo.data() length:sizeof(photo) atIndex:0];
              [effects setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
              [effects setBytes:&post_uniforms length:sizeof(post_uniforms) atIndex:2];
              [effects dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(16,16,1)]; [effects endEncoding];
              // Post surface already contains exposure and halation in linear light.
              photo[0] = 0; photo[2] = 1; film_uniforms.halation_amount = 0;
            }
            id<MTLComputeCommandEncoder> conversion = [command computeCommandEncoder];
            if (conversion == nil) { frame.error = "Cannot create Gaussian photo conversion pass."; return false; }
            [conversion setComputePipelineState:encode_];
            [conversion setTexture:post_surface != nil ? post_surface : lens_resolve ? integrated : accumulation atIndex:0];
            [conversion setTexture:lease->texture atIndex:1];
            [conversion setTexture:glow != nil ? glow : accumulation atIndex:2];
            [conversion setBytes:photo.data() length:sizeof(photo) atIndex:0];
            [conversion setBytes:&film_uniforms length:sizeof(film_uniforms) atIndex:1];
            [conversion setBytes:&post_uniforms length:sizeof(post_uniforms) atIndex:2];
            const std::array<float,4> tone{camera.photo.tone.contrast,camera.photo.tone.highlight_compression,0,0};
            [conversion setBytes:tone.data() length:sizeof(tone) atIndex:3];
            const std::array<float,4> grade{camera.photo.tone.warmth,camera.photo.tone.tint,camera.photo.tone.vibrance,camera.photo.tone.fade};
            [conversion setBytes:grade.data() length:sizeof(grade) atIndex:4];
            [conversion dispatchThreads:MTLSizeMake(width, height, 1) threadsPerThreadgroup:MTLSizeMake(16, 16, 1)];
            [conversion endEncoding];
            if (readback) {
              readback_buffer = [device_ newBufferWithLength:row_bytes * height options:MTLResourceStorageModeShared];
              if (readback_buffer == nil) { frame.error = "Cannot allocate export readback."; return false; }
              id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
              if (copy == nil) { frame.error = "Cannot create Gaussian photo readback pass."; return false; }
              [copy copyFromTexture:lease->texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
                  sourceSize:MTLSizeMake(width, height, 1) toBuffer:readback_buffer destinationOffset:0
                  destinationBytesPerRow:row_bytes destinationBytesPerImage:row_bytes * height];
              [copy endEncoding];
            }
          }
          return true;
        };
        const auto publish_preview = [&](quint32 completed_lighting,bool lighting_prefix,int completed_capture_samples) {
          GaussianFrame partial;
          partial.native_lease = lease;
          partial.native_texture = reinterpret_cast<quint64>((__bridge void*)lease->texture);
          partial.size = region.size();
          partial.metrics = {{"backend", "metal-gaussian-v1"}, {"camera", camera.description()},
              {"photoSettings", camera.photo.description()}, {"progressivePreview", true},
              {"alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(camera))}, {"splatAlphaLimit",gaussian_alpha_limit(gaussian_alpha_model(camera))},
              {"cameraToneApplied",camera.photo.tone != GaussianToneSettings{}}, {"toneModel","linear-luminance-contrast-peak-shoulder-v1"},
              {"apertureSamples", completed_capture_samples}, {"targetApertureSamples", static_cast<int>(offsets.size())},
              {"previewReadback", false}, {"progressiveLighting",lighting_prefix},
              {"softLightSamples",static_cast<int>(completed_lighting)},{"targetLightingSamples",static_cast<int>(light_samples)},
              {"softLightTargetSamples",camera.composition ? camera.composition->light_samples : 1},{"softLightDraft",options.interactive}, {"filmEffectsApplied", film.active()}, {"cameraPostApplied",post.active()},
              {"lensModel",ray_traced ? "thin-lens-primary-rays-v1" : camera.photo.lens.ray_mapping() ? "analytic-ray-lens-v1" : optical ? "thin-lens-scene-units-v1" : "pinhole"}, {"colorContract", "sRGB-display-referred"},
              {"rayTracedPrimary",ray_traced}, {"primaryRaySamplesPerPixel",ray_traced ? completed_capture_samples : 0},
              {"primaryRayCount",ray_traced ? static_cast<qint64>(width)*static_cast<qint64>(height)*static_cast<qint64>(completed_capture_samples) : 0},
              {"primaryRayCaptureExecuted",ray_traced}, {"primaryRaysTraced",ray_traced ? static_cast<qint64>(width)*static_cast<qint64>(height)*static_cast<qint64>(completed_capture_samples) : 0},
              {"primaryRayTransportModel",ray_traced ? (spatial ? "linear-radiance-spatial-path-gaussian-over-scene-v1" : "linear-radiance-depth-ordered-gaussian-over-scene-v1") : "none"},
              {"sceneSeconds", scene_seconds ? QVariant(*scene_seconds) : QVariant{}}, {"temporal", temporal},
              {"width", region.width()}, {"height", region.height()}, {"gpuMs", gpu_ms},
              {"renderMs", static_cast<double>(elapsed.nsecsElapsed()) / 1e6}};
          describe_autofocus(partial,options.focus_context);
          if (hybrid && camera.composition->needsProbe()) {
            partial.metrics.insert("scanProbeMs",scan_probe_ms_);
            partial.metrics.insert("renderMs",partial.metrics.value("renderMs").toDouble()+scan_probe_ms_);
          }
          if (lighting_prefix && first_lighting_preview_ms == 0) first_lighting_preview_ms = partial.metrics.value("renderMs").toDouble();
          options.progressive_frames->publish(std::move(partial));
          ++progressive_frames; if (lighting_prefix) ++lighting_progressive_frames;
          // Qt may now sample the published texture. Never encode into it again.
          lease = std::make_shared<TextureLease>();
          lease->texture = [device_ newTextureWithDescriptor:descriptor];
          if (lease->texture == nil) { frame.error = "Cannot allocate progressive photo texture."; return false; }
          return true;
        };
        if (!reused && batched_lighting) {
          if (capture_lit && !capture_lighting_reused && sample_index == 0) {
            const auto count = static_cast<quint32>(scene->points.size());
            const bool derive_normals = !normals_ready_;
            for (quint32 first = 0; first < light_samples; first += batch_samples) {
              for (quint32 offset = 0; offset < count; offset += 65536) {
                if (canceled()) return cancellation();
                LightIntegrationUniforms integration; integration.first = first; integration.count = std::min(batch_samples,light_samples-first); integration.total = light_samples;
                integration.point_offset = offset; integration.point_count = std::min(65536U,count-offset);
                id<MTLCommandBuffer> batch = [queue_ commandBuffer];
                if (batch == nil || !encodeCaptureLighting(batch,uniforms,composition,solids,lights,material_buffer,emission_bounds,count,integration,derive_normals && first == 0)) { frame.error = "Cannot create sampled capture lighting."; return frame; }
                if (!finish_lighting(batch)) return frame;
              }
              if (first == 0) normals_ready_ = true;
            }
          }
          if (hybrid && !ray_traced) {
            const auto maximum_rows = std::max(NSUInteger{1},(spatial ? NSUInteger{8192} : NSUInteger{65536})/raster_width);
            auto band_rows = needs_scan ? std::max(NSUInteger{1},NSUInteger{4096}/raster_width) : maximum_rows;
            for (quint32 first = 0; first < light_samples; first += batch_samples) {
              for (NSUInteger y = 0; y < raster_height;) {
                if (canceled()) return cancellation();
                LightIntegrationUniforms integration; integration.first = first; integration.count = std::min(batch_samples,light_samples-first); integration.total = light_samples; integration.flags = 1; integration.pixel_y = static_cast<quint32>(y);
                id<MTLCommandBuffer> batch = [queue_ commandBuffer];
                const auto rows = std::min(band_rows,raster_height-y);
                if (batch == nil || !encodeSolidLighting(batch,uniforms,composition,solids,lights,material_buffer,solid_surface,haze_surface,integration,rows)) { frame.error = "Cannot create sampled object lighting."; return frame; }
                if (!finish_lighting(batch)) return frame;
                y += rows;
                // Avoid thousands of tiny empty-background commands at Retina
                // sizes. Adapt to measured cost, retaining a 65,536-pixel ceiling
                // and at most four scan-visibility samples per submission.
                if (needs_scan) {
                  const auto next = static_cast<NSUInteger>(std::max(1.0,std::floor(static_cast<double>(rows)*8.0/std::max(0.05,last_lighting_gpu_ms))));
                  band_rows = std::clamp(next,std::max(NSUInteger{1},rows/2),std::min(maximum_rows,rows*2));
                }
              }
              const auto completed = first+std::min(batch_samples,light_samples-first);
              if (options.progressive_frames && !readback && !source_only && !ray_traced && sample_index == 0 && completed < light_samples && std::has_single_bit(completed)) {
                if (canceled()) return cancellation();
                if (lighting_snapshot == nil) {
                  auto* snapshot_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:solid_surface.pixelFormat width:raster_width height:raster_height mipmapped:NO];
                  snapshot_descriptor.usage = MTLTextureUsageShaderRead | MTLTextureUsageShaderWrite;
                  snapshot_descriptor.storageMode = MTLStorageModePrivate;
                  lighting_snapshot = [device_ newTextureWithDescriptor:snapshot_descriptor];
                  if (lighting_snapshot == nil) { frame.error = "Cannot allocate progressive lighting surface."; return frame; }
                }
                id<MTLCommandBuffer> snapshot = [queue_ commandBuffer];
                id<MTLBlitCommandEncoder> copy = [snapshot blitCommandEncoder];
                if (snapshot == nil || copy == nil) { frame.error = "Cannot copy progressive lighting."; return frame; }
                [copy copyFromTexture:solid_surface sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0,0,0)
                    sourceSize:MTLSizeMake(raster_width,raster_height,1) toTexture:lighting_snapshot destinationSlice:0 destinationLevel:0 destinationOrigin:MTLOriginMake(0,0,0)];
                [copy endEncoding];
                if (shadowed_haze) {
                  [snapshot commit]; [snapshot waitUntilCompleted];
                  if (snapshot.status != MTLCommandBufferStatusCompleted) { frame.error = "Cannot snapshot haze lighting."; return frame; }
                  gpu_ms += (snapshot.GPUEndTime-snapshot.GPUStartTime)*1000;
                  if (!prepare_haze(lighting_snapshot)) return canceled() ? cancellation() : frame;
                  snapshot = [queue_ commandBuffer];
                  if (snapshot == nil) { frame.error = "Cannot allocate haze preview."; return frame; }
                }
                if (!compose_sample(snapshot,lighting_snapshot,true)) return frame;
                [snapshot commit]; [snapshot waitUntilCompleted];
                if (snapshot.status != MTLCommandBufferStatusCompleted) { frame.error = "Progressive lighting composition failed."; return frame; }
                gpu_ms += (snapshot.GPUEndTime-snapshot.GPUStartTime)*1000;
                if (canceled()) return cancellation();
                if (!publish_preview(completed,true,static_cast<int>(completed_samples))) return frame;
              }
            }
          }
        }
        if (ray_traced && !reused) {
          if (ray_pupil_buffer == nil || integrated == nil) {
            frame.error = "Primary-ray capture targets are unavailable."; return frame;
          }
          const auto ray_pixels = std::max<NSUInteger>(1,width*height);
          ray_trace_batch_size = std::clamp<size_t>(static_cast<size_t>(1048576/ray_pixels),1,8);
          const auto pupil_sample_count = static_cast<quint32>(offsets.size());
          const auto scan_count = scan_particle_count_ > 0;
          ScanVisibilityUniforms scan_uniforms;
          scan_uniforms.nodes = scan_count ? (native_scan_ ? static_cast<quint32>(scan_particle_count_) : scan_node_count_) : 0;
          scan_uniforms.alpha_limit = uniforms.image[3];
          const auto& lens = camera.photo.lens;
          const float symmetry = lens.blades > 0 ? static_cast<float>(lens.blades)
              : lens.bokeh_aspect != 1 ? 2.0F : 0.0F;
          PrimaryRayCaptureUniforms capture;
          capture.counts = {pupil_sample_count,0,0,0x6d2b79f5U};
          capture.lens = {lens.focus_distance,lens.aperture_radius,lens.bokeh_aspect,lens.vignetting_distance};
          capture.pupil = {lens.vignetting_scale,symmetry,
              lens.vignetting_distance > 0 || lens.vignetting_scale < 1 ? 1.0F : 0.0F,lens.pupil_softness};
          capture.optics = {lens.spherical_distortion,lens.astigmatism,lens.field_curvature,1/lens.focus_distance};
          const auto environment = (composition.reflections > 0 || composition.capture[2] > 0) && reflection_probe_ != nil
              ? reflection_probe_ : dummy_environment_;
          for (size_t first = 0; first < offsets.size();) {
            if (canceled()) return cancellation();
            auto batch_count = std::min(ray_trace_batch_size,offsets.size()-first);
            // Keep progressive prefixes independent of output dimensions.
            // Shorten only the batch that would cross the next displayable
            // power-of-two prefix, preserving the one-million-ray ceiling.
            if (options.progressive_frames && !readback && offsets.size() > 4) {
              const size_t next_prefix = first < 4 ? 4 : std::bit_ceil(first+1);
              if (next_prefix > first && next_prefix < offsets.size())
                batch_count = std::min(batch_count,next_prefix-first);
            }
            const auto count = static_cast<quint32>(batch_count);
            capture.counts[1] = static_cast<quint32>(first);
            capture.counts[2] = count;
            id<MTLCommandBuffer> batch = [queue_ commandBuffer];
            id<MTLComputeCommandEncoder> trace = [batch computeCommandEncoder];
            if (batch == nil || trace == nil) { frame.error = "Cannot allocate a primary-ray batch."; return frame; }
            [trace setComputePipelineState:spatial ? ray_spatial_capture_ : trace_primary_capture_];
            [trace setTexture:integrated atIndex:0]; [trace setTexture:environment atIndex:1];
            for (NSUInteger image = 0; image < 32; ++image) [trace setTexture:color_images_[image] atIndex:image+3];
            [trace setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
            [trace setBytes:&composition length:sizeof(composition) atIndex:1];
            [trace setBytes:lights.data() length:sizeof(lights) atIndex:2];
            [trace setBytes:solids.data() length:sizeof(solids) atIndex:3];
            [trace setBuffer:mesh_nodes_ offset:0 atIndex:4]; [trace setBuffer:mesh_triangles_ offset:0 atIndex:5];
            [trace setBuffer:material_buffer offset:0 atIndex:6];
            [trace setBuffer:scan_nodes_ offset:0 atIndex:7]; [trace setBuffer:scan_particles_ offset:0 atIndex:8];
            [trace setBytes:&scan_uniforms length:sizeof(scan_uniforms) atIndex:9];
            [trace setBuffer:points_ offset:0 atIndex:10]; [trace setBuffer:temporal_ offset:0 atIndex:11];
            [trace setBuffer:appearance_ offset:0 atIndex:12];
            [trace setBuffer:illumination_ != nil ? illumination_ : points_ offset:0 atIndex:13];
            [trace setBytes:&capture length:sizeof(capture) atIndex:14]; [trace setBuffer:ray_pupil_buffer offset:0 atIndex:15];
            if (native_scan_) {
              const auto particles = scan_count ? scan_particles_ : dummy_scan_particles_;
              auto shadow_functions = spatial ? ray_spatial_shadow_functions_ : ray_capture_shadow_functions_;
              auto radiance_functions = spatial ? ray_spatial_radiance_functions_ : ray_capture_radiance_functions_;
              [shadow_functions setBuffer:particles offset:0 atIndex:0];
              [radiance_functions setBuffer:particles offset:0 atIndex:0];
              [trace useResource:particles usage:MTLResourceUsageRead];
              [trace setAccelerationStructure:scan_count ? scan_acceleration_ : dummy_scan_acceleration_ atBufferIndex:16];
              [trace setIntersectionFunctionTable:shadow_functions atBufferIndex:17];
              [trace setIntersectionFunctionTable:radiance_functions atBufferIndex:18];
            }
            [trace dispatchThreads:MTLSizeMake(width,height,1) threadsPerThreadgroup:MTLSizeMake(8,8,1)];
            [trace endEncoding];
            [batch commit]; [batch waitUntilCompleted];
            if (batch.status != MTLCommandBufferStatusCompleted) {
              frame.error = batch.error ? QString::fromNSString(batch.error.localizedDescription) : "Primary-ray capture did not complete.";
              return frame;
            }
            gpu_ms += (batch.GPUEndTime-batch.GPUStartTime)*1000.0;
            if (options.samples_completed) options.samples_completed->store(static_cast<int>(first+count));
            const auto completed = first+count;
            if (options.progressive_frames && !readback && completed < offsets.size() && completed >= 4 && std::has_single_bit(completed)) {
              if (canceled()) return cancellation();
              id<MTLCommandBuffer> preview = [queue_ commandBuffer];
              if (preview == nil || !compose_sample(preview,solid_surface,true)) {
                if (frame.error.isEmpty()) frame.error = "Cannot compose a primary-ray preview.";
                return frame;
              }
              [preview commit]; [preview waitUntilCompleted];
              if (preview.status != MTLCommandBufferStatusCompleted) {
                frame.error = preview.error ? QString::fromNSString(preview.error.localizedDescription) : "Primary-ray preview composition failed.";
                return frame;
              }
              gpu_ms += (preview.GPUEndTime-preview.GPUStartTime)*1000.0;
              if (canceled()) return cancellation();
              if (!publish_preview(light_samples,false,static_cast<int>(completed))) return frame;
            }
            first += count;
          }
        }
        if (!prepare_haze(solid_surface)) return canceled() ? cancellation() : frame;
        id<MTLCommandBuffer> command = [queue_ commandBuffer];
        if (command == nil) { frame.error = "Cannot allocate Gaussian GPU command buffer."; return frame; }
        if (!compose_sample(command,solid_surface,intermediate || completed_samples == passes)) return frame;
        [command commit];
        // This API is worker-only. Publishing only completed immutable textures
        // keeps Qt's render queue independent and prevents resource reuse races.
        [command waitUntilCompleted];
        if (command.status != MTLCommandBufferStatusCompleted) {
          frame.error = command.error ? QString::fromNSString(command.error.localizedDescription) : "Gaussian GPU command did not complete.";
          return frame;
        }
        gpu_ms += (command.GPUEndTime - command.GPUStartTime) * 1000.0;
        if (options.samples_completed) options.samples_completed->store(static_cast<int>(reused || ray_traced ? offsets.size() : sample_index + 1));
        if (capture_lit && !reused) normals_ready_ = true;
        if (capture_lit && !reused && !capture_lighting_reused && sample_index == 0) {
          capture_lighting_key_ = capture_lighting_key; capture_lighting_seconds_ = scene_seconds;
          capture_lighting_evaluated = true;
        }
        if (intermediate && !canceled() && !publish_preview(light_samples,false,static_cast<int>(completed_samples))) return frame;
      }
      if (canceled()) return cancellation();
      if (tile_order == nullptr && !options.interactive && !reused && width <= 4096 && height <= 4096) {
        resolved_source_ = lens_resolve ? integrated : accumulation;
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
          {"frontFacingCount", static_cast<qulonglong>(reused ? resolved_front_count_ : ray_traced ? scene->points.size() : order_.size())}, {"sortAndSetupMs", sort_ms},
          {"gpuMs", gpu_ms}, {"apertureSamples", static_cast<int>(offsets.size())},
          {"rayTracedPrimary",ray_traced},
          {"primaryRaySamplesPerPixel",ray_traced ? static_cast<int>(offsets.size()) : 0},
          {"primaryRayCount",ray_traced ? static_cast<qint64>(width)*static_cast<qint64>(height)*static_cast<qint64>(offsets.size()) : 0},
          {"primaryRayCaptureExecuted",ray_traced && !reused},
          {"primaryRaysTraced",ray_traced && !reused ? static_cast<qint64>(width)*static_cast<qint64>(height)*static_cast<qint64>(offsets.size()) : 0},
          {"rayTraceBatchSize",static_cast<int>(ray_traced && !reused ? ray_trace_batch_size : 0)},
          {"primaryRayTransportModel",ray_traced ? (spatial ? "linear-radiance-spatial-path-gaussian-over-scene-v1" : "linear-radiance-depth-ordered-gaussian-over-scene-v1") : "none"},
          {"progressiveFramesProduced", progressive_frames}, {"lightingProgressiveFramesProduced",lighting_progressive_frames},
          {"firstLightingPreviewMs",first_lighting_preview_ms},
          {"depthCenterBytes", static_cast<qulonglong>(depth_centers_.capacity() * sizeof(std::array<float, 3>))},
          {"lensModel",ray_traced ? "thin-lens-primary-rays-v1" : camera.photo.lens.ray_mapping() ? "analytic-ray-lens-v1" : optical ? "thin-lens-scene-units-v1" : "pinhole"},
          {"analyticLensMappingApplied",ray_traced && camera.photo.lens.ray_mapping()},
          {"lensSourceWidth",source_region.width()},{"lensSourceHeight",source_region.height()},
          {"lensSourcePixels",static_cast<qint64>(source_region.width())*source_region.height()},
          {"filmEffectsApplied", film.active()}, {"cameraPostApplied",post.active()},
          {"resolvedSourceReused", reused}, {"rasterizedApertureSamples", reused || ray_traced ? 0 : static_cast<int>(offsets.size())},
          {"resolvedSourceBytes", resolved_source_ != nil ? static_cast<qulonglong>(resolved_source_.width * resolved_source_.height * 16) : 0},
          {"renderMs", static_cast<double>(elapsed.nsecsElapsed()) / 1e6}, {"previewReadback", readback},
          {"photoSettings", camera.photo.description()},
          {"alphaModel",gaussian_alpha_model_name(gaussian_alpha_model(camera))}, {"splatAlphaLimit",gaussian_alpha_limit(gaussian_alpha_model(camera))},
          {"cameraToneApplied",camera.photo.tone != GaussianToneSettings{}}, {"toneModel","linear-luminance-contrast-peak-shoulder-v1"},
          {"colorContract", "sRGB-display-referred"}, {"width", region.width()}, {"height", region.height()}};
      if (capture_lit) {
        frame.metrics.insert("captureLightingModel",shadow_receiver ? "differential-captured-shadow-receiver-v1" : "covariance-normal-lit-captured-color-v1");
        frame.metrics.insert("captureLightingBytes",static_cast<qulonglong>(scene->points.size()*(shadow_receiver ? 64 : 32)));
        frame.metrics.insert("captureLightingReused",capture_lighting_reused);
        frame.metrics.insert("captureLightingEvaluated",capture_lighting_evaluated);
        frame.metrics.insert("captureShadowModel","analytic-object-to-gaussian-center-v1");
        if (capture_emission) {
          frame.metrics.insert("captureEmissionModel",camera.composition->capture_emission_importance ? "bounds-cosine-mixture-authored-material-emission-v1" : "cosine-sampled-authored-material-emission-v1");
          frame.metrics.insert("captureEmissionSamples",static_cast<int>(light_samples));
          frame.metrics.insert("captureEmissionScanShadows",capture_scan_shadows);
          frame.metrics.insert("captureEmissionReceiverExcluded",true);
        }
      }
      if (shadowed_haze) {
        frame.metrics.insert("hazeModel",ray_traced ? "primary-ray-homogeneous-single-scattering-v1" : "shadowed-homogeneous-single-scattering-v1");
        frame.metrics.insert("hazeSamples",options.interactive ? 8 : camera.composition->light_samples);
        frame.metrics.insert("hazePointBytes",0);
        frame.metrics.insert("hazeFieldBytes",reused || ray_traced ? 0 : haze_storage_bytes);
        frame.metrics.insert("hazeFieldPrefixBytes",reused || ray_traced ? 0 : haze_field_bytes);
        frame.metrics.insert("hazeFieldBuildModel",ray_traced ? "integrated-per-primary-ray-segment-v1" : "parallel-strata-then-ordered-prefix-v1");
        if (!ray_traced) {
          frame.metrics.insert("hazeFieldDepthSlices",haze_slices); frame.metrics.insert("hazeFieldCellPixels",haze_cell);
          frame.metrics.insert("hazeSamplingDomain",readback ? "fine-export-grid" : "angular-viewport-grid");
        }
        frame.metrics.insert("hazePrimaryModel",ray_traced ? "per-ray-segment-homogeneous-medium-v1" : "shared-extinction-prefix-trilinear-v1");
        frame.metrics.insert("lightingBatches",lighting_batches); frame.metrics.insert("maxLightingBatchGpuMs",max_lighting_gpu_ms);
      }
      if (hazy) {
        if (!shadowed_haze) frame.metrics.insert("hazeModel","beer-lambert-per-layer-display-composite-v1");
        frame.metrics.insert("hazeDensity",camera.composition->haze_density);
        frame.metrics.insert("hazeSurfaceBytes",reused || ray_traced ? 0 : static_cast<qulonglong>(width*height*8));
      }
      if (soft_lighting) {
        frame.metrics.insert("softLightModel",spatial_diffuse ? (spatial_specular ? "sampled-spatial-diffuse-reflections-and-direct-v1" : "sampled-spatial-diffuse-and-direct-v1") : spatial ? "sampled-spatial-reflections-and-direct-v1" : composition.capture[2] > 0 ? "sampled-direct-and-probe-diffuse-v2" : "sampled-rectangle-sphere-sun-direct-v1"); frame.metrics.insert("softLightSamples",light_samples);
        frame.metrics.insert("softLightTargetSamples",camera.composition->light_samples); frame.metrics.insert("softLightDraft",options.interactive && lighting_override == 0);
        frame.metrics.insert("lightingBatches",lighting_batches); frame.metrics.insert("maxLightingBatchGpuMs",max_lighting_gpu_ms);
      }
      if (!surface_materials.empty()) {
        frame.metrics.insert("materialModel","mtl-ggx-linear-color-mipmapped-srgb-v1");
        frame.metrics.insert("surfaceMaterialCount",static_cast<qulonglong>(surface_materials.size()));
        frame.metrics.insert("colorImageCount",image_key.size()); frame.metrics.insert("colorImagePixelBytes",image_bytes);
        frame.metrics.insert("colorImagesReused",images_reused); frame.metrics.insert("colorImageUploadBytes",images_reused ? 0 : image_bytes);
      }
      if (!geometry_key.isEmpty()) {
        frame.metrics.insert("meshModel","triangle-bvh-smooth-normals-v1");
        frame.metrics.insert("meshTriangleCount",static_cast<qulonglong>(mesh_triangle_count_));
        frame.metrics.insert("meshGeometryBytes",static_cast<qulonglong>(mesh_triangle_count_*sizeof(GaussianMeshTriangle)+mesh_node_count_*sizeof(GaussianMeshNode)));
      }
      if (needs_scan) {
        frame.metrics.insert("scanRayModel","anisotropic-segment-peak-product-v1");
        if (scan_shadows) frame.metrics.insert("scanShadowModel","anisotropic-segment-peak-product-v1");
        frame.metrics.insert("scanShadowIndexReused",scan_reused); frame.metrics.insert("scanShadowBuildMs",scan_build_ms);
        frame.metrics.insert("scanShadowParticleCount",static_cast<qulonglong>(scan_particle_count_));
        frame.metrics.insert("scanShadowNodeCount",scan_node_count_);
        frame.metrics.insert("scanShadowBackend",native_scan_ ? "metal-acceleration-structure" : "metal-software-bvh");
        frame.metrics.insert("scanShadowIndexBytes",static_cast<qulonglong>(scan_particle_count_*sizeof(GaussianScanParticle)+(native_scan_ ? scan_acceleration_.size : scan_node_count_*sizeof(GaussianScanNode))));
        frame.metrics.insert("scanShadowAlphaCutoff",gaussian_scan_cutoff);
        frame.metrics.insert("lightingBatches",lighting_batches); frame.metrics.insert("maxLightingBatchGpuMs",max_lighting_gpu_ms);
      }
      if (hybrid) {
        frame.metrics.insert("solidObjectCount", composition.objects); frame.metrics.insert("objectLightCount", composition.lights);
        frame.metrics.insert("cameraVisibleEmitterCount",static_cast<int>(std::count_if(camera.composition->lights.begin(),camera.composition->lights.end(),[](const auto& light) { return light.primaryVisible(); })));
        frame.metrics.insert("primaryEmitterModel","one-sided-area-radiance-v1");
        frame.metrics.insert("solidSurfaceBytes", reused ? 0 : static_cast<qulonglong>(width*height*16));
        frame.metrics.insert("solidModel", "analytic-ggx-direct-hard-shadows-v1");
        frame.metrics.insert("splatOcclusion", "per-pixel-solid-hit-vs-gaussian-center-depth");
        if (composition.reflections > 0 && solid_receivers) {
          frame.metrics.insert("reflectionModel",spatial_specular ? "spatial-gaussian-peak-ordered-ggx-v1" : "object-center-linear-half-capture-probe-v1");
          if (spatial_specular) {
            frame.metrics.insert("reflectionSamples",light_samples); frame.metrics.insert("reflectionDepth",camera.composition->surface_bounces);
            frame.metrics.insert("reflectionSampling","ggx-visible-normals-correlated-smith-v1");
            frame.metrics.insert("reflectionParticleOrdering","peak-distance-then-source-index");
          } else {
            frame.metrics.insert("reflectionProbeEdge",256); frame.metrics.insert("reflectionProbeReused",probe_reused_);
            frame.metrics.insert("reflectionProbeLightingSamples",probe_light_samples_);
            frame.metrics.insert("reflectionProbeOrigin",QVariantList{probe_origin_.x(),probe_origin_.y(),probe_origin_.z()});
          }
        }
        if (composition.capture[2] > 0 && solid_receivers) {
          frame.metrics.insert("environmentLightingModel",spatial_diffuse ? "surface-cosine-spatial-gaussian-v1" : "object-center-cosine-probe-diffuse-visibility-v1");
          frame.metrics.insert("environmentSamples",light_samples); frame.metrics.insert("environmentStrength",composition.capture[2]);
          if (spatial_diffuse) {
            frame.metrics.insert("environmentDepth",camera.composition->surface_bounces); frame.metrics.insert("environmentParticleOrdering","peak-distance-then-source-index");
            frame.metrics.insert("environmentEmitterSampling","light-sampled-finite-sources-bsdf-sampled-material-emission-v1");
          } else {
            frame.metrics.insert("environmentProbeEdge",256); frame.metrics.insert("environmentProbeReused",probe_reused_);
            frame.metrics.insert("environmentProbeOrigin",QVariantList{probe_origin_.x(),probe_origin_.y(),probe_origin_.z()});
          }
        }
      }
      if (spatial && camera.composition->surface_bounces > 1) {
        frame.metrics.insert("surfaceTransportModel","bounded-surface-paths-ggx-cosine-v1");
        frame.metrics.insert("surfaceBounces",camera.composition->surface_bounces);
        frame.metrics.insert("surfaceCaptureModel","linear-opacity-radiance-v1");
        frame.metrics.insert("surfacePathSampling","one-lobe-per-vertex-probability-weighted-v1");
      }
      return frame;
    }
  }
  GaussianFrame renderTiled(const std::shared_ptr<const GaussianSceneData>& scene,
      const GaussianCamera& camera, bool readback, std::optional<double> seconds, const GaussianRenderOptions& options) {
    @autoreleasepool {
      QElapsedTimer elapsed; elapsed.start();
      GaussianFrame output;
      const auto canceled = [&] { return options.canceled && options.canceled->load(); };
      const auto cancellation = [] { GaussianFrame frame; frame.canceled = true; return frame; };
      if (canceled()) return cancellation();
      if (options.tiles_completed) options.tiles_completed->store(0);
      if (options.samples_completed) options.samples_completed->store(0);
      // Validate before allocating a potentially large CPU photograph.
      if (!scene || scene->points.empty() || scene->points.size() > gaussian_static_point_limit || scene->sh_degree < 0 ||
          scene->sh_degree > 3 || !camera.photo.valid() || (seconds && !std::isfinite(*seconds))) {
        output.error = "Invalid scene or photo settings."; return output;
      }
      const int requested_edge = options.export_tile_edge == 0 ? 2048 : options.export_tile_edge;
      const bool ray_capture = !options.interactive && camera.photo.lens.ray_traced && camera.photo.lens.aperture_radius > 0;
      const int base_edge = ray_capture ? std::min(720,requested_edge) : camera.photo.lens.ray_mapping() ? std::min(1024,requested_edge) : requested_edge;
      const int longest = std::max(camera.image_size.width(), camera.image_size.height());
      const auto film = options.interactive ? GaussianFilmSettings{} : camera.photo.film;
      const auto post = options.interactive ? GaussianPostSettings{} : camera.photo.post;
      int halo = film.halation_amount > 0 ? static_cast<int>(std::ceil(3 * std::max(0.25F,
          film.halation_radius * static_cast<float>(longest) / 2048.0F))) : 0;
      if (film.film_plane && film.halation_amount>0) {
        const auto pitch=film_pixel_pitch(camera,film);
        halo=static_cast<int>(std::ceil(3*std::max(0.25F,film.halation_microns/std::min(pitch[0],pitch[1]))));
      }
      const float scale = static_cast<float>(longest)/2048.0F;
      if (post.bloom_amount > 0) halo = std::max(halo,static_cast<int>(std::ceil(3*std::max(0.25F,post.bloom_radius*scale))));
      if (post.glare_amount > 0) halo = std::max(halo,static_cast<int>(std::ceil(std::max(1.0F,post.glare_radius*scale)))+1);
      if (post.chromatic_aberration > 0) halo += static_cast<int>(std::ceil(post.chromatic_aberration*scale))+1;
      int edge = std::min(base_edge,std::max(32,haze_region_edge(camera,options,readback)-2*halo));
      const QRect canvas(QPoint{}, camera.image_size);
      if (camera.composition && camera.composition->haze_shadows && camera.composition->haze_density > 0) {
        const int cell = haze_cell_pixels(camera,options,readback);
        const int slices = std::clamp(4*(options.interactive ? 8 : std::clamp(camera.composition->light_samples,1,1024)),64,1024);
        // Include the actual whole-pupil source bounds before allocating the
        // photograph. Strong lens mappings can need more than the output halo.
        for (;;) {
          bool fits = true;
          for (int y = 0; fits && y < canvas.height(); y += edge) for (int x = 0; fits && x < canvas.width(); x += edge) {
            if (canceled()) return cancellation();
            const QRect core(x,y,std::min(edge,canvas.width()-x),std::min(edge,canvas.height()-y));
            const auto region = core.adjusted(-halo,-halo,halo,halo).intersected(canvas);
            const auto source = gaussian_lens_source_region(camera,region,!options.interactive && camera.photo.lens.aperture_radius > 0);
            if (!source || source->width() > 8192 || source->height() > 8192) { fits = false; break; }
            const auto columns = static_cast<quint64>((source->width()+cell-1)/cell+4);
            const auto rows = static_cast<quint64>((source->height()+cell-1)/cell+4);
            fits = columns*rows*static_cast<quint64>(slices+1)*24 <= 128ULL*1024*1024;
          }
          if (fits) break;
          if (edge <= 32) { output.error = "Lens coverage exceeds the bounded haze field budget."; return output; }
          edge = std::max(32,edge/2);
        }
      }
      auto assembled = std::make_shared<TextureLease>();
      if (readback) {
        output.image = QImage(camera.image_size,QImage::Format_ARGB32);
        if (output.image.isNull()) { output.error = "Cannot allocate the full-resolution photo."; return output; }
        output.image.setColorSpace(QColorSpace(QColorSpace::SRgb));
      } else {
        if (!initialize()) { output.error = error_; return output; }
        auto* descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm width:static_cast<NSUInteger>(canvas.width()) height:static_cast<NSUInteger>(canvas.height()) mipmapped:NO];
        descriptor.usage = MTLTextureUsageShaderRead; descriptor.storageMode = MTLStorageModePrivate;
        assembled->texture = [device_ newTextureWithDescriptor:descriptor];
        if (assembled->texture == nil) { output.error = "Cannot allocate the full-resolution lens preview."; return output; }
      }
      output.size = camera.image_size;
      GaussianRenderOptions tile_options = options;
      tile_options.samples_completed.reset(); tile_options.export_tile_edge = 0;
      tile_options.tiles_completed.reset(); tile_options.progressive_frames.reset();
      id<MTLBuffer> tile_order = nil;
      int tiles = 0, rasterized_samples = 0, maximum_tile_pixels = 0, maximum_lens_source_pixels = 0, lighting_batches = 0;
      double gpu_ms = 0, sort_ms = 0, max_lighting_gpu_ms = 0, scan_build_ms = 0;
      bool scan_index_reused = true;
      quint64 maximum_haze_field_bytes = 0, maximum_haze_prefix_bytes = 0;
      for (int y = 0; y < canvas.height(); y += edge) for (int x = 0; x < canvas.width(); x += edge) {
        if (canceled()) return cancellation();
        const QRect core(x, y, std::min(edge, canvas.width()-x), std::min(edge, canvas.height()-y));
        // Bloom, glare, halation and chromatic sampling need neighbors. Render the
        // full support around each core, then discard the overlap when stitching.
        const auto region = core.adjusted(-halo, -halo, halo, halo).intersected(canvas);
        const auto tile = renderRegion(scene, camera, readback, seconds, tile_options, region, &tile_order);
        if (tile.canceled || canceled()) return cancellation();
        if (!tile.error.isEmpty()) { GaussianFrame failed; failed.error = tile.error; return failed; }
        if (readback) {
          if (tile.image.size() != region.size()) { GaussianFrame failed; failed.error = "Incomplete photo tile."; return failed; }
          for (int row = 0; row < core.height(); ++row) {
            std::memcpy(output.image.scanLine(core.y()+row) + core.x()*4,
                tile.image.constScanLine(core.y()-region.y()+row) + (core.x()-region.x())*4,
                static_cast<size_t>(core.width())*4);
          }
        } else {
          const auto input = std::static_pointer_cast<TextureLease>(tile.native_lease);
          id<MTLCommandBuffer> command = [queue_ commandBuffer]; id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
          if (!input || input->texture == nil || command == nil || copy == nil) { output.error = "Cannot assemble the lens preview."; return output; }
          [copy copyFromTexture:input->texture sourceSlice:0 sourceLevel:0
              sourceOrigin:MTLOriginMake(static_cast<NSUInteger>(core.x()-region.x()),static_cast<NSUInteger>(core.y()-region.y()),0)
              sourceSize:MTLSizeMake(static_cast<NSUInteger>(core.width()),static_cast<NSUInteger>(core.height()),1)
              toTexture:assembled->texture destinationSlice:0 destinationLevel:0
              destinationOrigin:MTLOriginMake(static_cast<NSUInteger>(core.x()),static_cast<NSUInteger>(core.y()),0)];
          [copy endEncoding]; [command commit]; [command waitUntilCompleted];
          if (command.status != MTLCommandBufferStatusCompleted) { output.error = "Lens preview assembly failed."; return output; }
          gpu_ms += (command.GPUEndTime-command.GPUStartTime)*1000;
        }
        output.metrics = tile.metrics;
        maximum_haze_field_bytes = std::max(maximum_haze_field_bytes,tile.metrics.value("hazeFieldBytes").toULongLong());
        maximum_haze_prefix_bytes = std::max(maximum_haze_prefix_bytes,tile.metrics.value("hazeFieldPrefixBytes").toULongLong());
        scan_build_ms += tile.metrics.value("scanShadowBuildMs").toDouble();
        scan_index_reused = scan_index_reused && tile.metrics.value("scanShadowIndexReused").toBool();
        gpu_ms += tile.metrics.value("gpuMs").toDouble(); sort_ms += tile.metrics.value("sortAndSetupMs").toDouble();
        rasterized_samples += tile.metrics.value("rasterizedApertureSamples").toInt();
        lighting_batches += tile.metrics.value("lightingBatches").toInt(); max_lighting_gpu_ms = std::max(max_lighting_gpu_ms,tile.metrics.value("maxLightingBatchGpuMs").toDouble());
        maximum_tile_pixels = std::max(maximum_tile_pixels, region.width()*region.height());
        maximum_lens_source_pixels = std::max(maximum_lens_source_pixels,tile.metrics.value("lensSourcePixels").toInt());
        ++tiles;
        if (options.tiles_completed) options.tiles_completed->store(tiles);
      }
      if (canceled()) return cancellation();
      if (options.samples_completed) options.samples_completed->store(output.metrics.value("apertureSamples").toInt());
      output.metrics.insert("tiledPhoto", true); output.metrics.insert("tileCount", tiles);
      output.metrics.insert("tileEdge", edge); output.metrics.insert("tileHalo", halo);
      output.metrics.insert("maximumTilePixels", maximum_tile_pixels);
      if (output.metrics.contains("hazeFieldBytes")) output.metrics.insert("hazeFieldBytes",maximum_haze_field_bytes);
      if (output.metrics.contains("hazeFieldPrefixBytes")) output.metrics.insert("hazeFieldPrefixBytes",maximum_haze_prefix_bytes);
      output.metrics.insert("maximumLensSourcePixels", maximum_lens_source_pixels);
      output.metrics.insert("width", canvas.width()); output.metrics.insert("height", canvas.height());
      output.metrics.insert("gpuMs", gpu_ms); output.metrics.insert("sortAndSetupMs", sort_ms);
      output.metrics.insert("rasterizedApertureSamples", rasterized_samples);
      if (output.metrics.contains("scanShadowModel")) { output.metrics.insert("scanShadowBuildMs",scan_build_ms); output.metrics.insert("scanShadowIndexReused",scan_index_reused); }
      if (output.metrics.contains("softLightModel") || output.metrics.contains("scanShadowModel") || output.metrics.contains("hazeSamples")) { output.metrics.insert("lightingBatches",lighting_batches); output.metrics.insert("maxLightingBatchGpuMs",max_lighting_gpu_ms); }
      output.metrics.insert("renderMs", static_cast<double>(elapsed.nsecsElapsed())/1e6);
      output.metrics.insert("previewReadback",readback);
      if (!readback) {
        output.native_lease = assembled; output.native_texture = reinterpret_cast<quint64>((__bridge void*)assembled->texture);
        output.metrics.insert("nativeTiledLens",true);
      }
      // Publish only the completed image. Native assembly stays on the GPU and
      // never places partial tiles in the full-frame progressive preview queue.
      return output;
    }
  }
  bool initializeHaze() {
    if (haze_field_pipeline_ != nil && haze_pixel_pipeline_ != nil && haze_integrate_pipeline_ != nil) return true;
    NSError* error = nil;
    const auto create = [&](NSString* name,id<MTLIntersectionFunctionTable> __strong& table) -> id<MTLComputePipelineState> {
      id<MTLFunction> function = [scan_library_ newFunctionWithName:name];
      if (!native_scan_) return [device_ newComputePipelineStateWithFunction:function error:&error];
      id<MTLFunction> intersection = [scan_library_ newFunctionWithName:@"scan_particle_intersection"];
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[intersection];
      auto* descriptor = [MTLComputePipelineDescriptor new]; descriptor.computeFunction = function; descriptor.linkedFunctions = linked;
      auto pipeline = [device_ newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&error];
      auto* table_descriptor = [MTLIntersectionFunctionTableDescriptor new]; table_descriptor.functionCount = 1;
      table = [pipeline newIntersectionFunctionTableWithDescriptor:table_descriptor];
      [table setFunction:[pipeline functionHandleWithFunction:intersection] atIndex:0];
      return table == nil ? nil : pipeline;
    };
    haze_field_pipeline_ = create(@"sample_haze_field",haze_field_functions_);
    haze_integrate_pipeline_ = [device_ newComputePipelineStateWithFunction:[scan_library_ newFunctionWithName:@"integrate_haze_field"] error:&error];
    haze_pixel_pipeline_ = [device_ newComputePipelineStateWithFunction:[scan_library_ newFunctionWithName:@"shadowed_haze"] error:&error];
    if (haze_field_pipeline_ == nil || haze_pixel_pipeline_ == nil || haze_integrate_pipeline_ == nil) {
      error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize shadowed haze."; return false;
    }
    return true;
  }
  bool encodeHaze(id<MTLCommandBuffer> command,const Uniforms& uniforms,const CompositionUniforms& composition,
      const std::array<SolidUniforms,16>& solids,const std::array<LightUniforms,8>& lights,
      const std::array<quint32,2>& range,bool build,id<MTLTexture> haze,id<MTLTexture> surface,id<MTLTexture> field) {
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder]; if (encoder == nil) return false;
    [encoder setComputePipelineState:build ? haze_field_pipeline_ : haze_pixel_pipeline_];
    [encoder setTexture:haze atIndex:0];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    if (build) {
      [encoder setBytes:&composition length:sizeof(composition) atIndex:1];
      [encoder setBytes:solids.data() length:sizeof(solids) atIndex:2]; [encoder setBytes:lights.data() length:sizeof(lights) atIndex:3];
      [encoder setBuffer:mesh_nodes_ offset:0 atIndex:4]; [encoder setBuffer:mesh_triangles_ offset:0 atIndex:5];
      const bool scan_enabled = composition.capture[3] > 0;
      ScanVisibilityUniforms scan; scan.nodes = scan_enabled ? (native_scan_ ? static_cast<quint32>(scan_particle_count_) : scan_node_count_) : 0;
      scan.alpha_limit = uniforms.image[3];
      [encoder setBuffer:scan_enabled ? scan_nodes_ : mesh_nodes_ offset:0 atIndex:6];
      [encoder setBuffer:scan_enabled ? scan_particles_ : points_ offset:0 atIndex:7]; [encoder setBytes:&scan length:sizeof(scan) atIndex:8];
      if (native_scan_) {
        const auto particles = scan_enabled ? scan_particles_ : dummy_scan_particles_;
        [haze_field_functions_ setBuffer:particles offset:0 atIndex:0]; [encoder useResource:particles usage:MTLResourceUsageRead];
        [encoder setAccelerationStructure:scan_enabled ? scan_acceleration_ : dummy_scan_acceleration_ atBufferIndex:9];
        [encoder setIntersectionFunctionTable:haze_field_functions_ atBufferIndex:10];
      }
      [encoder setBytes:range.data() length:sizeof(range) atIndex:11];
    } else {
      [encoder setTexture:surface atIndex:1]; [encoder setTexture:field atIndex:2];
      [encoder setBytes:range.data() length:sizeof(range) atIndex:1];
    }
    const auto lanes = haze_field_pipeline_.threadExecutionWidth;
    const auto strata = std::min(NSUInteger{4},std::max(NSUInteger{1},haze_field_pipeline_.maxTotalThreadsPerThreadgroup/lanes));
    [encoder dispatchThreads:MTLSizeMake(haze.width,range[1],build ? haze.depth : 1)
        threadsPerThreadgroup:build ? MTLSizeMake(lanes,1,strata) : MTLSizeMake(16,8,1)];
    [encoder endEncoding]; return true;
  }
  bool encodeCaptureLighting(id<MTLCommandBuffer> command,const Uniforms& uniforms,const CompositionUniforms& composition,
      const std::array<SolidUniforms,16>& solids,const std::array<LightUniforms,8>& lights,id<MTLBuffer> materials,const std::array<std::array<float,4>,16>& emission_bounds,quint32 count,const LightIntegrationUniforms& integration,bool derive) {
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder]; if (encoder == nil) return false;
    if (derive) {
      const std::array<quint32,4> layout{count,uniforms.temporal[1] > 0 ? 1U : 0U,integration.point_offset,0};
      [encoder setComputePipelineState:derive_normals_]; [encoder setBuffer:points_ offset:0 atIndex:0]; [encoder setBuffer:temporal_ offset:0 atIndex:1];
      [encoder setBuffer:normals_ offset:0 atIndex:2]; [encoder setBytes:layout.data() length:sizeof(layout) atIndex:3];
      [encoder dispatchThreads:MTLSizeMake(integration.point_count,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)]; [encoder memoryBarrierWithScope:MTLBarrierScopeBuffers];
    }
    [encoder setComputePipelineState:shade_capture_];
    [encoder setBuffer:points_ offset:0 atIndex:0]; [encoder setBuffer:temporal_ offset:0 atIndex:1];
    [encoder setBuffer:normals_ offset:0 atIndex:2]; [encoder setBuffer:illumination_ offset:0 atIndex:3];
    [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:4]; [encoder setBytes:&composition length:sizeof(composition) atIndex:5];
    [encoder setBytes:solids.data() length:sizeof(solids) atIndex:6]; [encoder setBytes:lights.data() length:sizeof(lights) atIndex:7];
    [encoder setBytes:&count length:sizeof(count) atIndex:8]; [encoder setBuffer:mesh_nodes_ offset:0 atIndex:9]; [encoder setBuffer:mesh_triangles_ offset:0 atIndex:10];
    [encoder setBytes:&integration length:sizeof(integration) atIndex:11];
    [encoder setBuffer:materials offset:0 atIndex:12];
    const bool scan_enabled = (composition.ray_modes[3] & 2U) != 0;
    ScanVisibilityUniforms scan; scan.nodes = scan_enabled ? (native_scan_ ? static_cast<quint32>(scan_particle_count_) : scan_node_count_) : 0; scan.alpha_limit = uniforms.image[3];
    [encoder setBuffer:scan_enabled ? scan_nodes_ : mesh_nodes_ offset:0 atIndex:13];
    [encoder setBuffer:scan_enabled ? scan_particles_ : points_ offset:0 atIndex:14]; [encoder setBytes:&scan length:sizeof(scan) atIndex:15];
    if (native_scan_) {
      const auto particles = scan_enabled ? scan_particles_ : dummy_scan_particles_;
      [capture_scan_functions_ setBuffer:particles offset:0 atIndex:0]; [encoder useResource:particles usage:MTLResourceUsageRead];
      [encoder setAccelerationStructure:scan_enabled ? scan_acceleration_ : dummy_scan_acceleration_ atBufferIndex:16];
      [encoder setIntersectionFunctionTable:capture_scan_functions_ atBufferIndex:17];
    }
    [encoder setBytes:emission_bounds.data() length:sizeof(emission_bounds) atIndex:18];
    [encoder setBuffer:shadow_accumulation_ != nil ? shadow_accumulation_ : illumination_ offset:0 atIndex:19];
    [encoder dispatchThreads:MTLSizeMake(integration.point_count,1,1) threadsPerThreadgroup:MTLSizeMake(256,1,1)]; [encoder endEncoding]; return true;
  }
  bool encodeSolidLighting(id<MTLCommandBuffer> command,const Uniforms& uniforms,const CompositionUniforms& composition,
      const std::array<SolidUniforms,16>& solids,const std::array<LightUniforms,8>& lights,id<MTLBuffer> materials,
      id<MTLTexture> surface,id<MTLTexture> haze,const LightIntegrationUniforms& integration,NSUInteger rows) {
    id<MTLComputeCommandEncoder> encoder = [command computeCommandEncoder]; if (encoder == nil) return false;
    const bool spatial = active_spatial_;
    const bool paths = spatial && composition.ray_modes[2] > 1;
    const auto pipeline = spatial ? (paths ? path_solids_ : spatial_solids_) : render_solids_;
    [encoder setComputePipelineState:pipeline]; [encoder setTexture:surface atIndex:0];
    [encoder setTexture:(composition.reflections > 0 || composition.capture[2] > 0) && reflection_probe_ != nil ? reflection_probe_ : dummy_environment_ atIndex:1];
    [encoder setTexture:haze atIndex:2]; [encoder setBytes:&uniforms length:sizeof(uniforms) atIndex:0];
    [encoder setBytes:&composition length:sizeof(composition) atIndex:1]; [encoder setBytes:solids.data() length:sizeof(solids) atIndex:2];
    [encoder setBytes:lights.data() length:sizeof(lights) atIndex:3]; [encoder setBuffer:mesh_nodes_ offset:0 atIndex:4]; [encoder setBuffer:mesh_triangles_ offset:0 atIndex:5];
    [encoder setBuffer:materials offset:0 atIndex:6]; [encoder setBytes:&integration length:sizeof(integration) atIndex:7];
    const bool scan_enabled = composition.objects > 0 && (composition.capture[3] > 0 || spatial);
    ScanVisibilityUniforms scan; scan.nodes = scan_enabled ? (native_scan_ ? static_cast<quint32>(scan_particle_count_) : scan_node_count_) : 0; scan.alpha_limit = uniforms.image[3];
    [encoder setBuffer:scan_enabled ? scan_nodes_ : mesh_nodes_ offset:0 atIndex:8];
    [encoder setBuffer:scan_enabled ? scan_particles_ : points_ offset:0 atIndex:9];
    [encoder setBytes:&scan length:sizeof(scan) atIndex:10];
    if (native_scan_) {
      const auto particles = scan_enabled ? scan_particles_ : dummy_scan_particles_;
      const auto functions = spatial ? (paths ? path_shadow_functions_ : spatial_shadow_functions_) : scan_functions_;
      const auto radiance = paths ? path_radiance_functions_ : spatial_radiance_functions_;
      [functions setBuffer:particles offset:0 atIndex:0];
      if (spatial) [radiance setBuffer:particles offset:0 atIndex:0];
      [encoder useResource:particles usage:MTLResourceUsageRead];
      [encoder setAccelerationStructure:scan_enabled ? scan_acceleration_ : dummy_scan_acceleration_ atBufferIndex:11];
      [encoder setIntersectionFunctionTable:functions atBufferIndex:12];
      if (spatial) [encoder setIntersectionFunctionTable:radiance atBufferIndex:13];
    }
    [encoder setBuffer:points_ offset:0 atIndex:14]; [encoder setBuffer:appearance_ offset:0 atIndex:15];
    [encoder setBuffer:illumination_ != nil ? illumination_ : points_ offset:0 atIndex:16]; [encoder setBuffer:temporal_ offset:0 atIndex:17];
    for (NSUInteger image = 0; image < 32; ++image) [encoder setTexture:color_images_[image] atIndex:image+3];
    const auto group = composition.ray_modes[1] > 0 ? MTLSizeMake(pipeline.threadExecutionWidth,1,1) : MTLSizeMake(16,16,1);
    [encoder dispatchThreads:MTLSizeMake(surface.width,rows,1) threadsPerThreadgroup:group]; [encoder endEncoding]; return true;
  }
  bool initializeSpatial(bool paths) {
    if ((paths ? path_solids_ : spatial_solids_) != nil) return true;
    NSError* error = nil;
    auto* constants = [MTLFunctionConstantValues new]; const bool spatial = true;
    [constants setConstantValue:&spatial type:MTLDataTypeBool atIndex:0];
    [constants setConstantValue:&paths type:MTLDataTypeBool atIndex:1];
    id<MTLFunction> function = [scan_library_ newFunctionWithName:@"render_solids" constantValues:constants error:&error];
    id<MTLComputePipelineState> pipeline;
    id<MTLIntersectionFunctionTable> shadow, radiance;
    if (native_scan_) {
      id<MTLFunction> shadow_function = [scan_library_ newFunctionWithName:@"scan_particle_intersection"];
      id<MTLFunction> radiance_function = [scan_library_ newFunctionWithName:@"scan_radiance_intersection"];
      if (function == nil || shadow_function == nil || radiance_function == nil) { error_ = "Cannot load spatial reflection functions."; return false; }
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[shadow_function,radiance_function];
      auto* descriptor = [MTLComputePipelineDescriptor new]; descriptor.computeFunction = function; descriptor.linkedFunctions = linked;
      pipeline = [device_ newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&error];
      auto* table = [MTLIntersectionFunctionTableDescriptor new]; table.functionCount = 1;
      shadow = [pipeline newIntersectionFunctionTableWithDescriptor:table]; radiance = [pipeline newIntersectionFunctionTableWithDescriptor:table];
      [shadow setFunction:[pipeline functionHandleWithFunction:shadow_function] atIndex:0];
      [radiance setFunction:[pipeline functionHandleWithFunction:radiance_function] atIndex:0];
    } else if (function != nil) pipeline = [device_ newComputePipelineStateWithFunction:function error:&error];
    if (pipeline == nil || (native_scan_ && (shadow == nil || radiance == nil))) {
      error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize spatial reflections."; return false;
    }
    if (paths) { path_solids_ = pipeline; path_shadow_functions_ = shadow; path_radiance_functions_ = radiance; }
    else { spatial_solids_ = pipeline; spatial_shadow_functions_ = shadow; spatial_radiance_functions_ = radiance; }
    return true;
  }
  bool initializeRaySpatialCapture() {
    if (ray_spatial_capture_ != nil) return true;
    NSError* error = nil;
    auto* constants = [MTLFunctionConstantValues new];
    const bool spatial = true, paths = true;
    [constants setConstantValue:&spatial type:MTLDataTypeBool atIndex:0];
    [constants setConstantValue:&paths type:MTLDataTypeBool atIndex:1];
    id<MTLFunction> function = [scan_library_ newFunctionWithName:@"trace_primary_capture" constantValues:constants error:&error];
    if (native_scan_) {
      id<MTLFunction> shadow_function = [scan_library_ newFunctionWithName:@"scan_particle_intersection"];
      id<MTLFunction> radiance_function = [scan_library_ newFunctionWithName:@"scan_radiance_intersection"];
      if (function == nil || shadow_function == nil || radiance_function == nil) {
        error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot load spatial primary-ray functions."; return false;
      }
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[shadow_function,radiance_function];
      auto* descriptor = [MTLComputePipelineDescriptor new]; descriptor.computeFunction = function; descriptor.linkedFunctions = linked;
      ray_spatial_capture_ = [device_ newComputePipelineStateWithDescriptor:descriptor options:MTLPipelineOptionNone reflection:nil error:&error];
      auto* table = [MTLIntersectionFunctionTableDescriptor new]; table.functionCount = 1;
      ray_spatial_shadow_functions_ = [ray_spatial_capture_ newIntersectionFunctionTableWithDescriptor:table];
      ray_spatial_radiance_functions_ = [ray_spatial_capture_ newIntersectionFunctionTableWithDescriptor:table];
      if (ray_spatial_capture_ != nil && ray_spatial_shadow_functions_ != nil && ray_spatial_radiance_functions_ != nil) {
        [ray_spatial_shadow_functions_ setFunction:[ray_spatial_capture_ functionHandleWithFunction:shadow_function] atIndex:0];
        [ray_spatial_radiance_functions_ setFunction:[ray_spatial_capture_ functionHandleWithFunction:radiance_function] atIndex:0];
      }
    } else if (function != nil) ray_spatial_capture_ = [device_ newComputePipelineStateWithFunction:function error:&error];
    if (ray_spatial_capture_ == nil || (native_scan_ && (ray_spatial_shadow_functions_ == nil || ray_spatial_radiance_functions_ == nil))) {
      error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize spatial primary-ray transport."; return false;
    }
    return true;
  }
  id<MTLAccelerationStructure> buildScanAcceleration(const std::vector<std::array<float,6>>& bounds) {
    static_assert(sizeof(std::array<float,6>) == sizeof(MTLAxisAlignedBoundingBox));
    const std::array<float,6> dummy{};
    id<MTLBuffer> boxes = [device_ newBufferWithBytes:bounds.empty() ? &dummy : bounds.data()
        length:std::max(size_t{1},bounds.size())*sizeof(dummy) options:MTLResourceStorageModeShared];
    if (boxes == nil) return nil;
    auto* geometry = [MTLAccelerationStructureBoundingBoxGeometryDescriptor descriptor];
    geometry.boundingBoxBuffer = boxes; geometry.boundingBoxStride = sizeof(dummy); geometry.boundingBoxCount = std::max(size_t{1},bounds.size());
    geometry.opaque = NO;
    // Accumulating a ray payload requires exactly one callback per particle.
    geometry.allowDuplicateIntersectionFunctionInvocation = NO;
    auto* descriptor = [MTLPrimitiveAccelerationStructureDescriptor descriptor]; descriptor.geometryDescriptors = @[geometry];
    const auto sizes = [device_ accelerationStructureSizesWithDescriptor:descriptor];
    id<MTLAccelerationStructure> structure = [device_ newAccelerationStructureWithSize:sizes.accelerationStructureSize];
    id<MTLBuffer> scratch = [device_ newBufferWithLength:std::max(NSUInteger{1},sizes.buildScratchBufferSize) options:MTLResourceStorageModePrivate];
    if (structure == nil || scratch == nil) return nil;
    id<MTLCommandBuffer> command = [queue_ commandBuffer];
    id<MTLAccelerationStructureCommandEncoder> encoder = [command accelerationStructureCommandEncoder];
    if (command == nil || encoder == nil) return nil;
    [encoder buildAccelerationStructure:structure descriptor:descriptor scratchBuffer:scratch scratchBufferOffset:0]; [encoder endEncoding];
    [command commit]; [command waitUntilCompleted];
    if (command.status != MTLCommandBufferStatusCompleted) return nil;
    return structure;
  }
  bool initialize() {
    if (pipeline_ != nil) return true;
    if (!error_.isEmpty()) return false;
    device_ = MTLCreateSystemDefaultDevice();
    queue_ = [device_ newCommandQueue];
    native_scan_ = device_.supportsRaytracing && device_.supportsFunctionPointers && qEnvironmentVariableIntValue("LOOKSY_GAUSSIAN_SCAN_SOFTWARE") == 0;
    NSError* error = nil;
    auto* options = [MTLCompileOptions new];
    options.preprocessorMacros = @{@"LOOKSY_SCAN_RT":@(native_scan_)};
    if (@available(macOS 15.0, *)) options.mathMode = MTLMathModeSafe;
    else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    id<MTLLibrary> library = [device_ newLibraryWithSource:[NSString stringWithUTF8String:gaussian_splat_msl.c_str()] options:options error:&error];
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
    if (native_scan_) {
      auto* ray_constants = [MTLFunctionConstantValues new]; const bool no_spatial = false;
      [ray_constants setConstantValue:&no_spatial type:MTLDataTypeBool atIndex:0];
      [ray_constants setConstantValue:&no_spatial type:MTLDataTypeBool atIndex:1];
      id<MTLFunction> ray_capture_function = [library newFunctionWithName:@"trace_primary_capture" constantValues:ray_constants error:&error];
      id<MTLFunction> shadow_intersection = [library newFunctionWithName:@"scan_particle_intersection"];
      id<MTLFunction> radiance_intersection = [library newFunctionWithName:@"scan_radiance_intersection"];
      if (ray_capture_function == nil || shadow_intersection == nil || radiance_intersection == nil) {
        error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize primary-ray functions."; return false;
      }
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[shadow_intersection,radiance_intersection];
      auto* compute = [MTLComputePipelineDescriptor new]; compute.computeFunction = ray_capture_function; compute.linkedFunctions = linked;
      trace_primary_capture_ = [device_ newComputePipelineStateWithDescriptor:compute options:MTLPipelineOptionNone reflection:nil error:&error];
      if (trace_primary_capture_ == nil) { error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize primary-ray capture."; return false; }
      auto* table = [MTLIntersectionFunctionTableDescriptor new]; table.functionCount = 1;
      ray_capture_shadow_functions_ = [trace_primary_capture_ newIntersectionFunctionTableWithDescriptor:table];
      ray_capture_radiance_functions_ = [trace_primary_capture_ newIntersectionFunctionTableWithDescriptor:table];
      if (ray_capture_shadow_functions_ == nil || ray_capture_radiance_functions_ == nil) {
        error_ = error ? QString::fromNSString(error.localizedDescription) : "Cannot initialize primary-ray intersection tables."; return false;
      }
      [ray_capture_shadow_functions_ setFunction:[trace_primary_capture_ functionHandleWithFunction:shadow_intersection] atIndex:0];
      [ray_capture_radiance_functions_ setFunction:[trace_primary_capture_ functionHandleWithFunction:radiance_intersection] atIndex:0];
    } else {
      trace_primary_capture_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"trace_primary_capture"] error:&error];
    }
    halation_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"halation_horizontal"] error:&error];
    camera_highlights_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"camera_highlights"] error:&error];
    camera_bloom_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"camera_bloom_horizontal"] error:&error];
    camera_post_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"camera_post"] error:&error];
    finish_soft_solids_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"finish_soft_solids"] error:&error];
    scan_library_ = library;
    auto* constants = [MTLFunctionConstantValues new]; const bool spatial = false; [constants setConstantValue:&spatial type:MTLDataTypeBool atIndex:0];
    [constants setConstantValue:&spatial type:MTLDataTypeBool atIndex:1];
    id<MTLFunction> solid_function = [library newFunctionWithName:@"render_solids" constantValues:constants error:&error];
    if (native_scan_) {
      id<MTLFunction> intersection = [library newFunctionWithName:@"scan_particle_intersection"];
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[intersection];
      auto* compute = [MTLComputePipelineDescriptor new]; compute.computeFunction = solid_function; compute.linkedFunctions = linked;
      render_solids_ = [device_ newComputePipelineStateWithDescriptor:compute options:MTLPipelineOptionNone reflection:nil error:&error];
      auto* table = [MTLIntersectionFunctionTableDescriptor new]; table.functionCount = 1;
      scan_functions_ = [render_solids_ newIntersectionFunctionTableWithDescriptor:table];
      [scan_functions_ setFunction:[render_solids_ functionHandleWithFunction:intersection] atIndex:0];
      const GaussianScanParticle dummy{};
      dummy_scan_particles_ = [device_ newBufferWithBytes:&dummy length:sizeof(dummy) options:MTLResourceStorageModeShared];
      dummy_scan_acceleration_ = buildScanAcceleration({});
      if (scan_functions_ == nil || dummy_scan_particles_ == nil || dummy_scan_acceleration_ == nil) { error_ = "Cannot initialize native scan ray tracing."; pipeline_ = nil; return false; }
    } else render_solids_ = [device_ newComputePipelineStateWithFunction:solid_function error:&error];
    composite_solids_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"composite_solids"] error:&error];
    pack_probe_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"pack_scan_probe"] error:&error];
    derive_normals_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"derive_capture_normals"] error:&error];
    if (native_scan_) {
      id<MTLFunction> intersection = [library newFunctionWithName:@"capture_shadow_intersection"];
      auto* linked = [MTLLinkedFunctions new]; linked.functions = @[intersection];
      auto* compute = [MTLComputePipelineDescriptor new]; compute.computeFunction = [library newFunctionWithName:@"shade_capture"]; compute.linkedFunctions = linked;
      shade_capture_ = [device_ newComputePipelineStateWithDescriptor:compute options:MTLPipelineOptionNone reflection:nil error:&error];
      auto* table = [MTLIntersectionFunctionTableDescriptor new]; table.functionCount = 1;
      capture_scan_functions_ = [shade_capture_ newIntersectionFunctionTableWithDescriptor:table];
      [capture_scan_functions_ setFunction:[shade_capture_ functionHandleWithFunction:intersection] atIndex:0];
      if (capture_scan_functions_ == nil) { error_ = "Cannot initialize captured emission ray tracing."; pipeline_ = nil; return false; }
    } else shade_capture_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"shade_capture"] error:&error];
    render_haze_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"render_haze"] error:&error];
    complete_haze_ = [device_ newComputePipelineStateWithFunction:[library newFunctionWithName:@"complete_haze"] error:&error];
    auto* dummy = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA32Float width:1 height:1 mipmapped:NO];
    dummy.usage = MTLTextureUsageShaderRead;
    dummy_solid_ = [device_ newTextureWithDescriptor:dummy];
    auto* dummy_field = [MTLTextureDescriptor new]; dummy_field.textureType = MTLTextureType3D;
    dummy_field.width = 1; dummy_field.height = 1; dummy_field.depth = 1; dummy_field.pixelFormat = MTLPixelFormatRGBA16Float;
    dummy_field.usage = MTLTextureUsageShaderRead; dummy_haze_field_ = [device_ newTextureWithDescriptor:dummy_field];
    auto* white_descriptor = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatRGBA8Unorm_sRGB width:1 height:1 mipmapped:NO];
    white_descriptor.usage = MTLTextureUsageShaderRead; white_descriptor.storageMode = device_.hasUnifiedMemory ? MTLStorageModeShared : MTLStorageModeManaged;
    white_image_ = [device_ newTextureWithDescriptor:white_descriptor]; const quint32 white = 0xffffffff;
    [white_image_ replaceRegion:MTLRegionMake2D(0,0,1,1) mipmapLevel:0 withBytes:&white bytesPerRow:4];
    color_images_ = [NSMutableArray<id<MTLTexture>> arrayWithCapacity:32];
    for (int i = 0; i < 32 && white_image_ != nil; ++i) [color_images_ addObject:white_image_];
    const GaussianMeshNode dummy_node{}; const GaussianMeshTriangle dummy_triangle{};
    mesh_nodes_ = [device_ newBufferWithBytes:&dummy_node length:sizeof(dummy_node) options:MTLResourceStorageModeShared];
    mesh_triangles_ = [device_ newBufferWithBytes:&dummy_triangle length:sizeof(dummy_triangle) options:MTLResourceStorageModeShared];
    dummy.textureType = MTLTextureType2DArray; dummy.arrayLength = 6;
    dummy_environment_ = [device_ newTextureWithDescriptor:dummy];
    if (pipeline_ == nil || encode_ == nil || integrate_ == nil || trace_primary_capture_ == nil ||
        (native_scan_ && (ray_capture_shadow_functions_ == nil || ray_capture_radiance_functions_ == nil)) ||
        halation_ == nil || camera_highlights_ == nil || camera_bloom_ == nil || camera_post_ == nil || render_solids_ == nil || finish_soft_solids_ == nil ||
        composite_solids_ == nil || pack_probe_ == nil || derive_normals_ == nil || shade_capture_ == nil || render_haze_ == nil || complete_haze_ == nil || white_image_ == nil || mesh_nodes_ == nil || mesh_triangles_ == nil || dummy_solid_ == nil || dummy_haze_field_ == nil || dummy_environment_ == nil || queue_ == nil) {
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
  id<MTLComputePipelineState> trace_primary_capture_, ray_spatial_capture_;
  id<MTLComputePipelineState> halation_;
  id<MTLComputePipelineState> camera_highlights_, camera_bloom_, camera_post_;
  id<MTLComputePipelineState> render_solids_, spatial_solids_, path_solids_, composite_solids_, finish_soft_solids_;
  id<MTLLibrary> scan_library_;
  id<MTLIntersectionFunctionTable> spatial_shadow_functions_, spatial_radiance_functions_, path_shadow_functions_, path_radiance_functions_;
  id<MTLIntersectionFunctionTable> ray_capture_shadow_functions_, ray_capture_radiance_functions_, ray_spatial_shadow_functions_, ray_spatial_radiance_functions_;
  id<MTLComputePipelineState> pack_probe_, derive_normals_, shade_capture_, render_haze_, complete_haze_;
  id<MTLTexture> dummy_solid_, dummy_haze_field_;
  id<MTLTexture> dummy_environment_, reflection_probe_;
  std::weak_ptr<const GaussianSceneData> probe_scene_;
  QVector3D probe_origin_;
  std::optional<double> probe_seconds_;
  bool probe_reused_ = false;
  GaussianAlphaModel probe_alpha_model_ = GaussianAlphaModel::Brush999;
  double scan_probe_ms_ = 0;
  int probe_light_samples_ = 0;
  int probe_capture_evaluations_ = 0, probe_capture_hits_ = 0;
  QVariantMap probe_effects_;
  id<MTLTexture> resolved_source_;
  QVariantMap resolved_key_;
  std::optional<double> resolved_seconds_;
  size_t resolved_front_count_ = 0;
  id<MTLBuffer> points_;
  id<MTLBuffer> temporal_, appearance_, normals_, illumination_, shadow_accumulation_;
  id<MTLComputePipelineState> haze_field_pipeline_, haze_pixel_pipeline_, haze_integrate_pipeline_;
  id<MTLIntersectionFunctionTable> haze_field_functions_;
  bool normals_ready_ = false;
  QVariantMap capture_lighting_key_;
  std::optional<double> capture_lighting_seconds_;
  id<MTLBuffer> scan_nodes_, scan_particles_;
  bool native_scan_ = false;
  bool active_spatial_ = false;
  id<MTLAccelerationStructure> scan_acceleration_, dummy_scan_acceleration_;
  id<MTLIntersectionFunctionTable> scan_functions_, capture_scan_functions_;
  id<MTLBuffer> dummy_scan_particles_;
  bool scan_ready_ = false;
  float scan_seconds_ = 0;
  quint32 scan_node_count_ = 0;
  size_t scan_particle_count_ = 0;
  id<MTLBuffer> mesh_nodes_, mesh_triangles_;
  QStringList mesh_key_, color_image_key_;
  id<MTLTexture> white_image_;
  NSMutableArray<id<MTLTexture>>* color_images_;
  QMap<QString,quint32> mesh_roots_;
  size_t mesh_triangle_count_ = 0, mesh_node_count_ = 0;
  std::shared_ptr<const GaussianSceneData> scene_;
  std::vector<DepthKey> keys_, scratch_;
  std::vector<std::array<float, 3>> depth_centers_;
  std::vector<quint32> order_;
  QString error_;
};
}
std::shared_ptr<GaussianSceneRenderer> make_gaussian_scene_renderer() { return std::make_shared<MetalGaussianRenderer>(); }
}  // namespace photo_editor::desktop
