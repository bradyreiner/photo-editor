#!/usr/bin/env bash
# Source this helper before configuring or building a CMake directory.
# Descriptor 9 is reserved for the lock and inherited by build children, so a
# killed launcher cannot release it while its compiler processes are still alive.

acquire_build_lock() {
  local build_dir="$1"
  local status
  mkdir -p "${build_dir}" || return
  # Never unlink this file: competing processes must lock the same inode.
  exec 9>>"${build_dir}/.looksy-build.lock" || return
  if python3 - "${build_dir}" "$$" <<'PY'
import fcntl
import os
import sys

build_dir, owner_pid = sys.argv[1:]
try:
    fcntl.flock(9, fcntl.LOCK_EX | fcntl.LOCK_NB)
except BlockingIOError:
    print(f"A build is already running in {build_dir}. "
          "Wait for it to finish before building again.", file=sys.stderr)
    sys.exit(1)
# The shell's descriptor references the same open file description, retaining
# the OS lock after this Python process exits. Old metadata does not hold a lock.
os.ftruncate(9, 0)
os.write(9, f"Build owner PID: {owner_pid}\n".encode())
PY
  then
    return 0
  else
    status=$?
    exec 9>&-
    return "${status}"
  fi
}

release_build_lock() {
  exec 9>&-
}
