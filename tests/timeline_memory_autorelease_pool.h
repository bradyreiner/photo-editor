#pragma once

// Mirrors the native event's autorelease boundary for direct calls made by a
// polling test harness outside QCoreApplication::processEvents(). Test-only.
class TimelineMemoryAutoreleasePool final {
 public:
  explicit TimelineMemoryAutoreleasePool(bool enabled);
  ~TimelineMemoryAutoreleasePool();
  TimelineMemoryAutoreleasePool(const TimelineMemoryAutoreleasePool&) = delete;
  TimelineMemoryAutoreleasePool& operator=(const TimelineMemoryAutoreleasePool&) = delete;

 private:
  void* pool_ = nullptr;
};
