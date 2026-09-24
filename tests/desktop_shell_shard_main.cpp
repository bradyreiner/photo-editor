#include "desktop_shell_smoke_runner.h"

#include <QByteArray>

#ifndef PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV
#error "PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV must name the shard stop environment variable"
#endif

int main(int argc, char** argv) {
  qputenv(PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV, QByteArrayLiteral("1"));
  return run_desktop_shell_smoke(argc, argv);
}
