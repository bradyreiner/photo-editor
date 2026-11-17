#include "desktop_shell_smoke_runner.h"

#include <cstdlib>
#include <QByteArray>
#include <QFile>

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
  const auto result = run_desktop_shell_smoke(argc, argv);
  // Launch Services does not forward an app's exit status to `open -W`.
  // Native UI automation can request an explicit receipt without relaxing
  // the shard's assertions or changing its return code.
  const auto receipt_path = qEnvironmentVariable("PHOTO_EDITOR_TEST_EXIT_STATUS_FILE");
  if (!receipt_path.isEmpty()) {
    QFile receipt(receipt_path);
    if (!receipt.open(QIODevice::WriteOnly) ||
        receipt.write(QByteArray::number(result)) < 0)
      return EXIT_FAILURE;
  }
  return result;
}
