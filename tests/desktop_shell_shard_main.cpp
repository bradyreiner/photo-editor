#include "desktop_shell_smoke_runner.h"

#include <QByteArray>

#ifndef PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV
#error "PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV must name the shard stop environment variable"
#endif

int main(int argc, char** argv) {
  qputenv(PHOTO_EDITOR_DESKTOP_SHELL_STOP_ENV, QByteArrayLiteral("1"));
#if defined(PHOTO_EDITOR_SKIP_SHARED_SOURCE_CONTRACT_PREFLIGHT)
  // Keep an unrelated visible-label source check from blocking this pointer
  // shard; the standalone source-contract CTest still checks it.
  qputenv("PHOTO_EDITOR_SMOKE_SKIP_VISIBLE_TEXT_SOURCE_CONTRACT", QByteArrayLiteral("1"));
#endif
  return run_desktop_shell_smoke(argc, argv);
}
