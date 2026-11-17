#include "import_preview_image_provider.h"

#import <Foundation/Foundation.h>
#import <CoreGraphics/CoreGraphics.h>
#import <ImageIO/ImageIO.h>
#include <QColorSpace>

namespace photo_editor::desktop {

QImage read_import_preview_image(const QString& path, int maximum_edge) {
  @autoreleasepool {
    NSString* native_path = [NSString stringWithUTF8String:path.toUtf8().constData()];
    NSURL* url = [NSURL fileURLWithPath:native_path];
    auto source = CGImageSourceCreateWithURL((__bridge CFURLRef)url,
        (__bridge CFDictionaryRef)@{(__bridge NSString*)kCGImageSourceShouldCache: @NO});
    if (!source) return {};
    // ImageIO understands camera RAW containers. Request an oriented source
    // thumbnail instead of letting Qt's TIFF plugin probe RAW image directories.
    auto thumbnail = CGImageSourceCreateThumbnailAtIndex(source, 0,
        (__bridge CFDictionaryRef)@{
          (__bridge NSString*)kCGImageSourceCreateThumbnailFromImageIfAbsent: @YES,
          (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform: @YES,
          (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize: @(maximum_edge),
          (__bridge NSString*)kCGImageSourceShouldCacheImmediately: @YES});
    CFRelease(source);
    if (!thumbnail) return {};
    const auto width = CGImageGetWidth(thumbnail), height = CGImageGetHeight(thumbnail);
    if (width == 0 || height == 0 || width > static_cast<size_t>(maximum_edge)
        || height > static_cast<size_t>(maximum_edge)) {
      CGImageRelease(thumbnail);
      return {};
    }
    QImage result(static_cast<int>(width), static_cast<int>(height), QImage::Format_RGBA8888_Premultiplied);
    auto color_space = CGColorSpaceCreateWithName(kCGColorSpaceSRGB);
    auto context = CGBitmapContextCreate(result.bits(), width, height, 8,
        static_cast<size_t>(result.bytesPerLine()), color_space,
        static_cast<CGBitmapInfo>(kCGImageAlphaPremultipliedLast) | kCGBitmapByteOrder32Big);
    CGColorSpaceRelease(color_space);
    if (!context) { CGImageRelease(thumbnail); return {}; }
    CGContextDrawImage(context, CGRectMake(0, 0, static_cast<CGFloat>(width), static_cast<CGFloat>(height)), thumbnail);
    CGContextRelease(context);
    CGImageRelease(thumbnail);
    result.setColorSpace(QColorSpace(QColorSpace::SRgb));
    return result;
  }
}

}  // namespace photo_editor::desktop
