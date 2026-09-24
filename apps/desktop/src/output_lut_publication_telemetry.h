#pragma once

#include <QString>
#include <QtGlobal>

namespace photo_editor::desktop {

struct OutputLutPublicationTelemetry final {
  QString status;
  qint64 cpu_to_gpu_bytes = 0;
  qint64 gpu_to_cpu_bytes = 0;
  qint64 command_buffer_count = 0;
  qint64 kernel_dispatch_count = 0;

  void add(const OutputLutPublicationTelemetry& telemetry) {
    if (!telemetry.status.isEmpty()) {
      status = telemetry.status;
    }
    cpu_to_gpu_bytes += telemetry.cpu_to_gpu_bytes;
    gpu_to_cpu_bytes += telemetry.gpu_to_cpu_bytes;
    command_buffer_count += telemetry.command_buffer_count;
    kernel_dispatch_count += telemetry.kernel_dispatch_count;
  }
};

}  // namespace photo_editor::desktop
