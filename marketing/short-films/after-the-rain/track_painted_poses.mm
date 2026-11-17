#import <Foundation/Foundation.h>
#import <Vision/Vision.h>
#import <ImageIO/ImageIO.h>
#import <CoreVideo/CoreVideo.h>

#include <iostream>

// Read-only pose analysis. Only vector fields are written, never interpolated
// reference pixels. Objective-C avoids a mismatched installed Swift SDK.
int main(int argc, char** argv) {
  @autoreleasepool {
    if (argc != 4) return 2;
    auto load=[](const char* path) -> CGImageRef {
      NSURL* url=[NSURL fileURLWithPath:[NSString stringWithUTF8String:path]];
      CGImageSourceRef source=CGImageSourceCreateWithURL((__bridge CFURLRef)url,nullptr);
      if (!source) return nullptr;
      NSDictionary* options=@{(__bridge NSString*)kCGImageSourceCreateThumbnailFromImageAlways:@YES,
          (__bridge NSString*)kCGImageSourceThumbnailMaxPixelSize:@960,
          (__bridge NSString*)kCGImageSourceCreateThumbnailWithTransform:@YES};
      CGImageRef result=CGImageSourceCreateThumbnailAtIndex(source,0,(__bridge CFDictionaryRef)options);
      CFRelease(source); return result;
    };
    CGImageRef first=load(argv[1]),second=load(argv[2]);
    if (!first || !second) return 2;
    if (CGImageGetWidth(first)!=CGImageGetWidth(second) || CGImageGetHeight(first)!=CGImageGetHeight(second)) return 2;
    NSString* prefix=[NSString stringWithUTF8String:argv[3]];
    NSString* binary=[prefix stringByAppendingString:@".f32"];
    NSString* metadata=[prefix stringByAppendingString:@".json"];
    if ([[NSFileManager defaultManager] fileExistsAtPath:binary] || [[NSFileManager defaultManager] fileExistsAtPath:metadata]) return 2;
    VNGenerateOpticalFlowRequest* request=[[VNGenerateOpticalFlowRequest alloc] initWithTargetedCGImage:second options:@{}];
    request.revision=VNGenerateOpticalFlowRequestRevision2;
    request.computationAccuracy=VNGenerateOpticalFlowRequestComputationAccuracyVeryHigh;
    request.outputPixelFormat=kCVPixelFormatType_TwoComponent32Float;
    VNImageRequestHandler* handler=[[VNImageRequestHandler alloc] initWithCGImage:first options:@{}];
    NSError* error=nil;
    const BOOL success=[handler performRequests:@[request] error:&error];
    auto face=[](CGImageRef image) -> NSDictionary* {
      VNDetectFaceLandmarksRequest* detection=[[VNDetectFaceLandmarksRequest alloc] init];
      VNImageRequestHandler* face_handler=[[VNImageRequestHandler alloc] initWithCGImage:image options:@{}];
      NSError* face_error=nil;
      if (![face_handler performRequests:@[detection] error:&face_error]) return @{};
      VNFaceObservation* observation=detection.results.firstObject;
      if (!observation || !observation.landmarks) return @{};
      NSMutableDictionary* result=[NSMutableDictionary dictionary];
      for (NSString* name in @[@"faceContour",@"leftEye",@"rightEye",@"leftEyebrow",@"rightEyebrow",@"nose",@"noseCrest",@"medianLine",@"outerLips",@"innerLips"]) {
        VNFaceLandmarkRegion2D* region=[observation.landmarks valueForKey:name];
        if (!region) continue;
        NSMutableArray* points=[NSMutableArray array];
        const auto bounds=observation.boundingBox;
        for (NSUInteger i=0;i<region.pointCount;++i) {
          const auto point=region.normalizedPoints[i];
          [points addObject:@[@((bounds.origin.x+point.x*bounds.size.width)*CGImageGetWidth(image)),
              @((1-bounds.origin.y-point.y*bounds.size.height)*CGImageGetHeight(image))]];
        }
        result[name]=points;
      }
      return result;
    };
    NSDictionary* source_face=face(first);
    NSDictionary* target_face=face(second);
    CGImageRelease(first); CGImageRelease(second);
    if (!success || !request.results.firstObject) { std::cerr<<error.localizedDescription.UTF8String<<'\n'; return 1; }
    CVPixelBufferRef buffer=request.results.firstObject.pixelBuffer;
    CVPixelBufferLockBaseAddress(buffer,kCVPixelBufferLock_ReadOnly);
    const auto width=CVPixelBufferGetWidth(buffer),height=CVPixelBufferGetHeight(buffer),stride=CVPixelBufferGetBytesPerRow(buffer);
    const auto* pointer=static_cast<const unsigned char*>(CVPixelBufferGetBaseAddress(buffer));
    NSMutableData* data=[NSMutableData dataWithCapacity:width*height*8];
    for (size_t y=0;y<height;++y) [data appendBytes:pointer+y*stride length:width*8];
    CVPixelBufferUnlockBaseAddress(buffer,kCVPixelBufferLock_ReadOnly);
    if (![data writeToFile:binary options:NSDataWritingWithoutOverwriting error:&error]) return 1;
    NSDictionary* info=@{@"width":@(width),@"height":@(height),@"format":@"little-endian-float32-dx-dy",
        @"method":@"Apple Vision optical flow revision 2 with face landmarks",@"sourceFace":source_face,@"targetFace":target_face,
        @"from":[NSString stringWithUTF8String:argv[1]],@"to":[NSString stringWithUTF8String:argv[2]]};
    NSData* json=[NSJSONSerialization dataWithJSONObject:info options:NSJSONWritingPrettyPrinted error:&error];
    if (![json writeToFile:metadata options:NSDataWritingWithoutOverwriting error:&error]) return 1;
    std::cout<<"Native pose flow: "<<width<<'x'<<height<<'\n';
    return 0;
  }
}
