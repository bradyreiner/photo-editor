#include "timeline_memory_autorelease_pool.h"

#import <Foundation/Foundation.h>

TimelineMemoryAutoreleasePool::TimelineMemoryAutoreleasePool(bool enabled) {
  if (enabled) pool_ = [[NSAutoreleasePool alloc] init];
}

TimelineMemoryAutoreleasePool::~TimelineMemoryAutoreleasePool() {
  if (pool_ != nullptr) [static_cast<NSAutoreleasePool*>(pool_) drain];
}
