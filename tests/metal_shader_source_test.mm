#import <Foundation/Foundation.h>
#import <Metal/Metal.h>

#include "color_adjustments_metal_source.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <string_view>

#ifndef PHOTO_EDITOR_METAL_CANONICAL_SOURCE_PATH
#error "PHOTO_EDITOR_METAL_CANONICAL_SOURCE_PATH must point to color_adjustments.metal"
#endif
#ifndef PHOTO_EDITOR_METAL_GRADING_HELPER_PATH
#error "PHOTO_EDITOR_METAL_GRADING_HELPER_PATH must point to color_look_grading_math.h"
#endif
#ifndef PHOTO_EDITOR_METAL_HARMONY_HELPER_PATH
#error "PHOTO_EDITOR_METAL_HARMONY_HELPER_PATH must point to color_look_harmony_math.h"
#endif

namespace {

[[nodiscard]] std::string read_file(const char* path) {
  std::ifstream stream(path, std::ios::binary);
  if (!stream) return {};
  return std::string(
      std::istreambuf_iterator<char>(stream),
      std::istreambuf_iterator<char>());
}

[[nodiscard]] bool expand_one_include(
    std::string* source,
    std::string_view include,
    std::string_view helper,
    std::string_view helper_name) {
  const auto position = source->find(include);
  if (position == std::string::npos ||
      source->find(include, position + include.size()) != std::string::npos) {
    std::cerr << "canonical Metal source must include exactly one shared "
              << helper_name << " helper\n";
    return false;
  }
  if (helper.empty()) {
    std::cerr << "failed to read shared Metal helper " << helper_name << '\n';
    return false;
  }
  source->replace(position, include.size(), helper);
  return true;
}

[[nodiscard]] bool verify_source_identity() {
  auto canonical = read_file(PHOTO_EDITOR_METAL_CANONICAL_SOURCE_PATH);
  const auto grading = read_file(PHOTO_EDITOR_METAL_GRADING_HELPER_PATH);
  const auto harmony = read_file(PHOTO_EDITOR_METAL_HARMONY_HELPER_PATH);
  if (canonical.empty() || grading.empty() || harmony.empty()) {
    std::cerr << "failed to read canonical Metal source or shared helpers\n";
    return false;
  }

  // This is the same source expansion performed by EmbedColorMetal.cmake.
  // The shared operators are included as bytes so runtime compilation has no
  // source-tree include search path and cannot use a stale duplicate.
  if (!expand_one_include(
          &canonical,
          "#include \"../color_look_grading_math.h\"",
          grading,
          "grading") ||
      !expand_one_include(
          &canonical,
          "#include \"../color_look_harmony_math.h\"",
          harmony,
          "harmony")) {
    return false;
  }

  @autoreleasepool {
    NSString* embedded = photo_editor_bundled_color_shader_source();
    if (embedded == nil) {
      std::cerr << "generated Metal source loader returned nil\n";
      return false;
    }
    NSData* data = [embedded dataUsingEncoding:NSUTF8StringEncoding];
    if (data == nil || [data length] != canonical.size() ||
        !std::equal(
            canonical.begin(), canonical.end(),
            static_cast<const char*>([data bytes]))) {
      const auto* bytes = data == nil
          ? nullptr
          : static_cast<const char*>([data bytes]);
      const auto length = data == nil ? std::size_t{0}
                                      : static_cast<std::size_t>([data length]);
      auto mismatch = std::size_t{0};
      while (mismatch < canonical.size() && mismatch < length &&
             canonical[mismatch] == bytes[mismatch]) {
        ++mismatch;
      }
      std::cerr << "generated Metal shader differs from canonical source after "
                   "shared-helper expansion: canonicalBytes="
                << canonical.size() << " generatedBytes=" << length
                << " firstMismatch=" << mismatch << '\n';
      return false;
    }
  }
  return true;
}

[[nodiscard]] int compile_with_system_metal() {
  @autoreleasepool {
    id<MTLDevice> device = MTLCreateSystemDefaultDevice();
    if (device == nil) {
      std::cerr << "Metal source compile unavailable: no system Metal device\n";
      return 77;
    }

    NSString* source = photo_editor_bundled_color_shader_source();
    if (source == nil) {
      std::cerr << "generated Metal source loader returned nil\n";
      return EXIT_FAILURE;
    }
    MTLCompileOptions* options = [[MTLCompileOptions alloc] init];
    if (@available(macOS 15.0, *)) {
      options.mathMode = MTLMathModeSafe;
    } else {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wdeprecated-declarations"
      options.fastMathEnabled = NO;
#pragma clang diagnostic pop
    }
    NSError* error = nil;
    id<MTLLibrary> library =
        [device newLibraryWithSource:source options:options error:&error];
    if (library == nil || error != nil) {
      std::cerr << "system Metal rejected canonical embedded shader: "
                << (error == nil ? "no library returned"
                                 : [[error localizedDescription] UTF8String])
                << '\n';
      return EXIT_FAILURE;
    }
    std::cout << "canonical embedded Metal source compiled on system device "
              << [[device name] UTF8String] << '\n';
    return EXIT_SUCCESS;
  }
}

}  // namespace

int main(int argc, char** argv) {
  if (argc > 2 || (argc == 2 && std::string_view(argv[1]) != "--compile")) {
    std::cerr << "usage: metal_shader_source_test [--compile]\n";
    return EXIT_FAILURE;
  }
  if (!verify_source_identity()) return EXIT_FAILURE;
  if (argc == 2) return compile_with_system_metal();
  std::cout << "generated Metal source matches canonical shader plus shared helpers\n";
  return EXIT_SUCCESS;
}
