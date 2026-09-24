#!/usr/bin/env python3
"""Self-test deterministic inner-to-outer macOS bundle signing."""

from __future__ import annotations

import os
import subprocess
import tempfile
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SIGNER = REPO_ROOT / "scripts" / "sign-macos-bundle.sh"


def require(condition: bool, message: str) -> None:
    if not condition:
        raise AssertionError(message)


def main() -> int:
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary)
        fake_bin = root / "bin"
        fake_bin.mkdir()
        log = root / "codesign.log"
        fake_codesign = fake_bin / "codesign"
        fake_codesign.write_text(
            "#!/usr/bin/env bash\n"
            "printf '%s\\n' \"$*\" >> \"${FRAMEHOUSE_SIGN_TEST_LOG}\"\n",
            encoding="utf-8",
        )
        fake_file = fake_bin / "file"
        fake_file.write_text(
            "#!/usr/bin/env bash\n"
            "case \"$*\" in\n"
            "  *.macho) printf 'Mach-O 64-bit executable\\n' ;;\n"
            "  *) printf 'data\\n' ;;\n"
            "esac\n",
            encoding="utf-8",
        )
        fake_codesign.chmod(0o755)
        fake_file.chmod(0o755)

        app = root / "Looksy.app"
        app_binary = app / "Contents" / "MacOS" / "Looksy.macho"
        framework = app / "Contents" / "Frameworks" / "Fixture.framework"
        framework_binary = framework / "Versions" / "A" / "Fixture.macho"
        resource = app / "Contents" / "Resources" / "not-code.txt"
        for fixture in (app_binary, framework_binary, resource):
            fixture.parent.mkdir(parents=True, exist_ok=True)
            fixture.write_text("fixture", encoding="utf-8")

        environment = os.environ.copy()
        environment["PATH"] = f"{fake_bin}{os.pathsep}{environment['PATH']}"
        environment["FRAMEHOUSE_SIGN_TEST_LOG"] = str(log)
        result = subprocess.run(
            [
                str(SIGNER),
                str(app),
                "Developer ID Fixture",
                "--hardened-runtime",
                "--timestamp",
            ],
            check=False,
            capture_output=True,
            text=True,
            env=environment,
        )
        require(result.returncode == 0, result.stderr)
        calls = log.read_text(encoding="utf-8").splitlines()
        sign_calls = [line for line in calls if "--sign Developer ID Fixture" in line]
        require(len(sign_calls) == 4, f"expected four signing calls, got: {calls}")
        require(
            all("--options runtime" in line and "--timestamp" in line for line in sign_calls),
            "release signing options were not applied consistently",
        )
        app_leaf = next(index for index, line in enumerate(calls) if str(app_binary) in line)
        framework_leaf = next(
            index for index, line in enumerate(calls) if str(framework_binary) in line
        )
        framework_seal = next(
            index for index, line in enumerate(calls) if line.endswith(str(framework))
        )
        app_seal = next(index for index, line in enumerate(calls) if line.endswith(str(app)))
        verification = next(index for index, line in enumerate(calls) if "--verify" in line)
        require(
            app_leaf < framework_seal
            and framework_leaf < framework_seal
            and framework_seal < app_seal < verification,
            f"bundle signing order is not leaves-to-outer-seal: {calls}",
        )
        require(
            all(str(resource) not in line for line in calls),
            "non-Mach-O resources must not be individually signed",
        )

        # A runtime added without the app's configured digest must fail before
        # ANY signing writes. Never repair its receipt after compilation.
        runtime = app / "Contents/Resources/search-runtime"
        runtime.mkdir()
        (runtime / "framehouse-search-worker.macho").write_text("unverified fixture")
        failed = subprocess.run([str(SIGNER), str(app), "-"], capture_output=True,
                                text=True, env=environment, check=False)
        require(failed.returncode != 0 and "search runtime preflight failed" in failed.stderr,
                "missing trusted runtime must fail signing preflight")
        require(log.read_text(encoding="utf-8").splitlines() == calls,
                "invalid search runtime must be detected before signing any app files")

        # Exercise the shell's preservation routing separately from the real
        # inventory/signature verifier (covered by its own tests/live proof).
        runtime_framework = runtime / "Python.framework"
        runtime_framework.mkdir()
        (runtime_framework / "Python.macho").write_text("prepared fixture")
        fake_python = fake_bin / "python3"
        fake_python.write_text(
            "#!/usr/bin/env bash\n"
            "printf 'runtime-check %s\\n' \"$*\" >> \"${FRAMEHOUSE_SIGN_TEST_LOG}\"\n",
            encoding="utf-8")
        fake_python.chmod(0o755)
        preserved = subprocess.run([str(SIGNER), str(app), "-"], capture_output=True,
                                   text=True, env=environment, check=False)
        require(preserved.returncode == 0, preserved.stderr)
        new_calls = log.read_text(encoding="utf-8").splitlines()[len(calls):]
        require(new_calls[0].startswith("runtime-check ") and new_calls[-1].startswith("runtime-check "),
                "prepared runtime needs both preflight and post-seal verification")
        require(all(str(runtime) not in line for line in new_calls if not line.startswith("runtime-check ")),
                "prepared runtime leaves and nested frameworks must not be re-signed")
        require(len([line for line in new_calls if "--sign -" in line]) == 4,
                "preserving the search runtime must not skip other app signing")

    print("macOS bundle-signing self-test passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
