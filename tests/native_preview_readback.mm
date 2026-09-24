#include "native_preview_readback.h"
#import <Metal/Metal.h>
#include <cstring>

QImage read_native_preview_texture(photo_editor::desktop::EditorPreviewTexture* preview) {
  if (!preview || !preview->hasNativeTexture()) return {};
  const auto lease = preview->nativeTextureLease();
  if (!lease) return {};
  id<MTLTexture> texture = (__bridge id<MTLTexture>)lease.get();
  QImage::Format format;
  NSUInteger bytes_per_pixel;
  if (texture.pixelFormat == MTLPixelFormatBGRA8Unorm) {
    format = QImage::Format_ARGB32_Premultiplied; bytes_per_pixel = 4;
  } else if (texture.pixelFormat == MTLPixelFormatRGBA16Float) {
    format = QImage::Format_RGBA16FPx4; bytes_per_pixel = 8;
  } else return {};
  if (texture.width == 0 || texture.height == 0 || texture.width > 8192 || texture.height > 8192) return {};
  const NSUInteger stride = (texture.width * bytes_per_pixel + 255) / 256 * 256;
  id<MTLBuffer> buffer = [texture.device newBufferWithLength:stride * texture.height options:MTLResourceStorageModeShared];
  id<MTLCommandQueue> queue = [texture.device newCommandQueue];
  id<MTLCommandBuffer> command = [queue commandBuffer];
  id<MTLBlitCommandEncoder> blit = [command blitCommandEncoder];
  if (!buffer || !queue || !command || !blit) return {};
  [blit copyFromTexture:texture sourceSlice:0 sourceLevel:0 sourceOrigin:MTLOriginMake(0, 0, 0)
      sourceSize:MTLSizeMake(texture.width, texture.height, 1) toBuffer:buffer destinationOffset:0
      destinationBytesPerRow:stride destinationBytesPerImage:stride * texture.height];
  [blit endEncoding]; [command commit]; [command waitUntilCompleted];
  if (command.status != MTLCommandBufferStatusCompleted) return {};
  QImage image(static_cast<int>(texture.width), static_cast<int>(texture.height), format);
  if (image.isNull()) return {};
  const auto* data = static_cast<const unsigned char*>(buffer.contents);
  for (NSUInteger row = 0; row < texture.height; ++row)
    std::memcpy(image.scanLine(static_cast<int>(row)), data + row * stride, texture.width * bytes_per_pixel);
  return image;
}
