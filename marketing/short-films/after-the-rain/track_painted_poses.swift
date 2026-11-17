// Read-only keypose analysis with the system Vision optical-flow API.
// The output contains motion vectors, never composited or interpolated pixels.
import Foundation
import Vision
import ImageIO
import CoreVideo

guard CommandLine.arguments.count == 4 else {
    fputs("Usage: track_painted_poses FIRST.png SECOND.png NEW-output-prefix\n", stderr)
    exit(2)
}
func image(_ path: String) throws -> CGImage {
    guard let source = CGImageSourceCreateWithURL(URL(fileURLWithPath: path) as CFURL, nil),
          let result = CGImageSourceCreateThumbnailAtIndex(source, 0, [
            kCGImageSourceCreateThumbnailFromImageAlways: true,
            kCGImageSourceThumbnailMaxPixelSize: 960,
            kCGImageSourceCreateThumbnailWithTransform: true
          ] as CFDictionary) else { throw NSError(domain: "ImageRead", code: 1) }
    return result
}
do {
    let first = try image(CommandLine.arguments[1])
    let second = try image(CommandLine.arguments[2])
    guard first.width == second.width && first.height == second.height else {
        throw NSError(domain: "PoseDimensions", code: 2)
    }
    let prefix = CommandLine.arguments[3]
    guard !FileManager.default.fileExists(atPath: prefix + ".json"),
          !FileManager.default.fileExists(atPath: prefix + ".f32") else { exit(2) }
    let request = VNGenerateOpticalFlowRequest(targetedCGImage: second, options: [:])
    request.revision = VNGenerateOpticalFlowRequestRevision1
    request.computationAccuracy = .veryHigh
    request.outputPixelFormat = kCVPixelFormatType_TwoComponent32Float
    let handler = VNImageRequestHandler(cgImage: first, options: [:])
    try handler.perform([request])
    guard let observation = request.results?.first else { exit(1) }
    let buffer = observation.pixelBuffer
    CVPixelBufferLockBaseAddress(buffer, .readOnly)
    defer { CVPixelBufferUnlockBaseAddress(buffer, .readOnly) }
    guard let pointer = CVPixelBufferGetBaseAddress(buffer) else { exit(1) }
    let width = CVPixelBufferGetWidth(buffer), height = CVPixelBufferGetHeight(buffer)
    let stride = CVPixelBufferGetBytesPerRow(buffer)
    var bytes = Data()
    for row in 0..<height { bytes.append(pointer.advanced(by: row * stride).assumingMemoryBound(to: UInt8.self), count: width * 8) }
    try bytes.write(to: URL(fileURLWithPath: prefix + ".f32"), options: .withoutOverwriting)
    let metadata: [String: Any] = ["width": width, "height": height,
        "format": "little-endian-float32-dx-dy", "method": "Apple Vision optical flow revision 1",
        "from": CommandLine.arguments[1], "to": CommandLine.arguments[2]]
    try JSONSerialization.data(withJSONObject: metadata, options: .prettyPrinted)
        .write(to: URL(fileURLWithPath: prefix + ".json"), options: .withoutOverwriting)
    print("Native pose flow: \(width)x\(height)")
} catch { fputs("Native pose-flow analysis failed: \(error)\n", stderr); exit(1) }
