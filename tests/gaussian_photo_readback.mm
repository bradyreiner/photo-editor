#include "gaussian_scene_renderer.h"
#include <QColorSpace>
#import <Metal/Metal.h>

// Test-only readback of the exact texture published to Qt. Production preview
// remains texture-only; this independently checks it against explicit export.
QImage read_gaussian_photo_texture(const photo_editor::desktop::GaussianFrame& frame) {
  if (!frame.native_texture || !frame.native_lease) return {};
  id<MTLTexture> texture = (__bridge id<MTLTexture>)reinterpret_cast<void*>(frame.native_texture);
  if (texture.pixelFormat != MTLPixelFormatBGRA8Unorm || texture.width < 1 || texture.height < 1 ||
      texture.width > 8192 || texture.height > 8192) return {};
  const NSUInteger stride = (texture.width * 4 + 255) / 256 * 256;
  id<MTLBuffer> buffer = [texture.device newBufferWithLength:stride * texture.height options:MTLResourceStorageModeShared];
  id<MTLCommandQueue> queue = [texture.device newCommandQueue];
  id<MTLCommandBuffer> command = [queue commandBuffer];
  id<MTLBlitCommandEncoder> copy = [command blitCommandEncoder];
  if (!buffer || !queue || !command || !copy) return {};
  [copy copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
      sourceSize:MTLSizeMake(texture.width, texture.height, 1) toBuffer:buffer destinationOffset:0
      destinationBytesPerRow:stride destinationBytesPerImage:stride * texture.height];
  [copy endEncoding]; [command commit]; [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted) return {};
  auto image = QImage(static_cast<const uchar*>(buffer.contents), static_cast<int>(texture.width),
      static_cast<int>(texture.height), static_cast<qsizetype>(stride), QImage::Format_ARGB32).copy();
  image.setColorSpace(QColorSpace(QColorSpace::SRgb));
  return image;
}
