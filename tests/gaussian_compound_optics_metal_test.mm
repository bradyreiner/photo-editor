#import <Foundation/Foundation.h>
#import <Metal/Metal.h>
#include "gaussian_compound_optics_gpu.h"
#include "gaussian_compound_optics_metal.h"
#include <iostream>
#include <vector>

using namespace photo_editor::desktop;
namespace {
struct alignas(16) RayInput { std::array<float,4> samples; };
struct alignas(16) RayOutput { std::array<float,4> origin_weight,direction_transmission; };
double radical_inverse(unsigned index,unsigned base) {
  double result=0,factor=1.0/base;
  while (index) { result+=factor*(index%base); index/=base; factor/=base; }
  return result;
}
}
int main() {
  @autoreleasepool {
    const auto fail=[](const char* text) { std::cerr<<text<<'\n'; return 1; };
    id<MTLDevice> device=MTLCreateSystemDefaultDevice();
    if (!device) return fail("Metal device unavailable");
    const std::string shader=gaussian_compound_optics_msl+R"METAL(
struct RayOutput { float4 origin_weight; float4 direction_transmission; };
kernel void optical_ray_test(constant CompoundCamera& camera [[buffer(0)]],
    constant CompoundSurface* surfaces [[buffer(1)]],constant float4* samples [[buffer(2)]],
    device RayOutput* output [[buffer(3)]],uint index [[thread_position_in_grid]]) {
  const auto ray=compound_camera_ray(samples[index].xy,samples[index].zw,camera,surfaces);
  output[index].origin_weight=float4(ray.origin,ray.weight);
  output[index].direction_transmission=float4(ray.direction,ray.transmission);
})METAL";
    NSError* error=nil;
    MTLCompileOptions* options=[MTLCompileOptions new];
    if (@available(macOS 15.0,*)) options.mathMode=MTLMathModeSafe;
    id<MTLLibrary> library=[device newLibraryWithSource:[NSString stringWithUTF8String:shader.c_str()] options:options error:&error];
    if (!library) { std::cerr<<error.localizedDescription.UTF8String<<'\n'; return fail("Optical shader compilation failed"); }
    id<MTLComputePipelineState> pipeline=[device newComputePipelineStateWithFunction:[library newFunctionWithName:@"optical_ray_test"] error:&error];
    id<MTLCommandQueue> queue=[device newCommandQueue];
    if (!pipeline || !queue) return fail("Optical pipeline creation failed");
    std::vector<RayInput> inputs;
    // Non-boundary stratified fields, with unrelated low-discrepancy pupil samples.
    for (unsigned i=1;i<=8192;++i) inputs.push_back({{static_cast<float>(0.01+0.98*radical_inverse(i,2)),
        static_cast<float>(0.01+0.98*radical_inverse(i,3)),static_cast<float>(radical_inverse(i,5)),
        static_cast<float>(radical_inverse(i,7))}});
    id<MTLBuffer> samples=[device newBufferWithBytes:inputs.data() length:inputs.size()*sizeof(RayInput) options:MTLResourceStorageModeShared];
    id<MTLBuffer> results=[device newBufferWithLength:inputs.size()*sizeof(RayOutput) options:MTLResourceStorageModeShared];
    if (!samples || !results) return fail("Optical test allocation failed");
    double max_pixels=0,max_transmission=0,max_relative_weight=0;
    size_t passed=0,blocked=0;
    for (int configuration=0;configuration<3;++configuration) {
      auto lens=double_gauss_50mm();
      if (configuration==1) {
        const auto stopped=compound_set_f_number(lens,4);
        if (!stopped) return fail("Cannot configure f/4");
        lens=*stopped; lens.blades=5; lens.roundness=0.17; lens.rotation_degrees=31;
      }
      if (configuration==2) {
        OpticalGlass bk7; bk7.sellmeier=true;
        bk7.b={1.03961212,0.231792344,1.01046945};
        bk7.c={0.00600069867,0.0200179144,103.560653}; bk7.absorption_per_mm=0.003;
        lens.surfaces[0].glass_after=bk7;
      }
      const auto calibration=make_compound_camera(lens,36,24,8192,5461,1,2);
      if (!calibration) return fail("Cannot calibrate optical test camera");
      for (const double wavelength:{450.0,550.0,650.0}) {
        const auto packed=pack_compound_optics(*calibration,wavelength);
        if (!packed) return fail("Cannot pack optical prescription");
        id<MTLCommandBuffer> command=[queue commandBuffer];
        id<MTLComputeCommandEncoder> encoder=[command computeCommandEncoder];
        [encoder setComputePipelineState:pipeline];
        [encoder setBytes:&packed->camera length:sizeof(packed->camera) atIndex:0];
        [encoder setBytes:packed->surfaces.data() length:sizeof(packed->surfaces) atIndex:1];
        [encoder setBuffer:samples offset:0 atIndex:2]; [encoder setBuffer:results offset:0 atIndex:3];
        [encoder dispatchThreads:MTLSizeMake(inputs.size(),1,1) threadsPerThreadgroup:MTLSizeMake(64,1,1)];
        [encoder endEncoding]; [command commit]; [command waitUntilCompleted];
        if (command.status!=MTLCommandBufferStatusCompleted) return fail("Optical GPU command failed");
        const auto* gpu=static_cast<const RayOutput*>(results.contents);
        for (size_t i=0;i<inputs.size();++i) {
          const auto& s=inputs[i].samples;
          const auto cpu=compound_camera_ray(*calibration,s[0],s[1],s[2],s[3],wavelength);
          if (!cpu) return fail("Invalid optical reference sample");
          const auto& g=gpu[i];
          if ((cpu->irradiance_weight>0)!=(g.origin_weight[3]>0)) {
            std::cerr<<"Clip mismatch configuration="<<configuration<<" wavelength="<<wavelength<<" index="<<i<<'\n';
            return fail("CPU/GPU aperture mask differs");
          }
          if (cpu->irradiance_weight==0) { ++blocked; continue; }
          ++passed;
          const OpticalVector origin{g.origin_weight[0],g.origin_weight[1],g.origin_weight[2]};
          const OpticalVector direction{g.direction_transmission[0],g.direction_transmission[1],g.direction_transmission[2]};
          const double focus=calibration->focus_from_sensor_m*1000;
          const auto cpu_hit=cpu->ray.origin+cpu->ray.direction*((focus-cpu->ray.origin.z)/cpu->ray.direction.z);
          const auto gpu_hit=origin+direction*((focus-origin.z)/direction.z);
          // Object-space deviation projected to an 8192-pixel sensor. This
          // bounds both ray-origin and angle errors, rather than angle alone.
          const auto matrix=compound_paraxial(lens);
          const double pixels=std::hypot(cpu_hit.x-gpu_hit.x,cpu_hit.y-gpu_hit.y)/focus*
              matrix->focal_length_mm()*8192/calibration->gate_width_mm;
          max_pixels=std::max(max_pixels,pixels);
          max_transmission=std::max(max_transmission,std::abs(cpu->glass_transmission-g.direction_transmission[3]));
          max_relative_weight=std::max(max_relative_weight,std::abs(cpu->irradiance_weight-g.origin_weight[3])/cpu->irradiance_weight);
          if (!std::isfinite(pixels) || pixels>0.05 || max_transmission>1e-4 || max_relative_weight>1e-4)
            return fail("Optical GPU deviation exceeds 0.05 pixel / 1e-4 transmission and weight guards");
        }
      }
    }
    if (passed<5000 || blocked<5000) return fail("Insufficient transmitted and clipped ray coverage");
    std::cout<<"Compound GPU optics: "<<passed<<" transmitted, "<<blocked<<" blocked; max error at 8K="<<max_pixels
        <<" pixels, transmission="<<max_transmission<<", relative weight="<<max_relative_weight<<'\n';
    return 0;
  }
}
