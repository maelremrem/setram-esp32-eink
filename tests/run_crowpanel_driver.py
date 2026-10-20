#!/usr/bin/env python3
"""Compile actual full-only CrowPanel GPIO-SPI tests; no board required."""
from pathlib import Path
import difflib
import os
import shlex
import subprocess
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
PINNED_UPSTREAM = "11291e3e9a868be8943232b065f3e264dfaeeae3"
TESTS = ("mirror", "lifecycle", "noop", "padding", "full-only")
CXX = os.environ.get("CXX", "g++")
FLAGS = shlex.split(os.environ.get("CROWPANEL_TEST_CXXFLAGS", ""))
INCLUDES = ["-I" + str(ROOT / "tests/driver_host"), "-I" + str(ROOT / "lib/CrowPanel")]
BASE = [CXX, "-std=c++17", "-Wall", "-Wextra", "-Werror", "-g", *FLAGS, *INCLUDES]


def verify_vendor(upstream):
    """Compare all eight vendor files to the exact pinned, pristine git blobs."""
    head = subprocess.check_output(["git", "-C", str(upstream), "rev-parse", "HEAD"], text=True).strip()
    if head != PINNED_UPSTREAM:
        raise RuntimeError(f"upstream HEAD {head} is not pinned {PINNED_UPSTREAM}")
    for controller, directory in (
        ("jd79661", "example/arduino-v1.2/main"),
        ("ssd1680", "example/arduino/Example Code/2.13_Global_refresh"),
    ):
        for name in ("EPD_Init.h", "EPD_Init.cpp", "spi.h", "spi.cpp"):
            relative = directory + "/" + name
            pinned = subprocess.check_output(["git", "-C", str(upstream), "show", PINNED_UPSTREAM + ":" + relative])
            local_name = name + ".inc" if name.endswith(".cpp") else name
            local = ROOT / "lib/CrowPanel/vendor" / controller / local_name
            if (upstream / relative).read_bytes() != pinned:
                raise RuntimeError(f"upstream working file differs from pinned blob: {relative}")
            if local.read_bytes() != pinned:
                raise RuntimeError(f"vendor file differs from pinned upstream: {local}")
            print(f"PASS pinned vendor bytes: {controller}/{local_name}", flush=True)


def verify_removed_api(tmp):
    for name in ("supportsPartialRefresh", "partialRefreshLimit", "setPartialRefreshLimit",
                 "partialRefreshEnabled", "setPartialRefresh"):
        probe = Path(tmp) / "removed-api.cpp"
        probe.write_text('#include "CrowPanel.h"\nauto forbidden = &crowpanel::' + name + ';\n')
        result = subprocess.run([*BASE, "-fsyntax-only", str(probe)], capture_output=True, text=True)
        if result.returncode == 0 or name not in result.stderr:
            raise RuntimeError(f"removed API still exposed or probe failed unexpectedly: {name}\n{result.stderr}")
    print("PASS removed partial APIs are absent from public header", flush=True)


def compare_trace(binary, reference, label):
    actual = subprocess.check_output([binary, "trace"])
    expected = subprocess.check_output([reference, "trace"])
    if actual == expected:
        print(f"PASS {label}: JD 20-frame full GC control/LUT trace equals pristine upstream (image lengths included)", flush=True)
        return True
    print("".join(difflib.unified_diff(expected.decode().splitlines(True), actual.decode().splitlines(True),
                                      fromfile="upstream", tofile=label)))
    return False


def main():
    selected = tuple(sys.argv[1:]) or TESTS
    if any(test not in (*TESTS, "trace") for test in selected):
        raise RuntimeError(f"unknown test selection: {selected}")
    default_upstream = Path(os.environ.get("TMPDIR", tempfile.gettempdir())) / "jd-official"
    configured = os.environ.get("CROWPANEL_UPSTREAM")
    upstream = Path(configured) if configured else (default_upstream if default_upstream.is_dir() else None)
    if upstream:
        verify_vendor(upstream)
    else:
        print("SKIP pinned vendor/upstream regression: set CROWPANEL_UPSTREAM to the pinned official clone", flush=True)
    failed = False
    with tempfile.TemporaryDirectory(prefix="crowpanel-driver-", dir=os.environ.get("TMPDIR")) as tmp:
        verify_removed_api(tmp)
        reference = None
        if upstream:
            directory = upstream / "example/arduino-v1.2/main"
            reference = str(Path(tmp) / "official-jd")
            # Compile the unmodified upstream separately from the adapter.
            subprocess.run([CXX, "-std=c++17", *FLAGS, *INCLUDES,
                            "-DEPAPER_JD79661", "-DCROWPANEL_REFERENCE", "-I" + str(directory),
                            str(ROOT / "tests/test_crowpanel_driver.cpp"),
                            str(directory / "EPD_Init.cpp"), str(directory / "spi.cpp"),
                            "-o", reference], check=True)
        for controller in ("JD79661", "SSD1680"):
            for obsolete in (False, True):
                label = f"{controller}, " + ("obsolete CROWPANEL_ENABLE_PARTIAL=1 ignored" if obsolete else "default full-only")
                binary = str(Path(tmp) / (controller + ("-obsolete" if obsolete else "")))
                subprocess.run([*BASE, "-DEPAPER_" + controller,
                                *(["-DCROWPANEL_ENABLE_PARTIAL=1"] if obsolete else []),
                                str(ROOT / "tests/test_crowpanel_driver.cpp"),
                                str(ROOT / "lib/CrowPanel/CrowPanel.cpp"), "-o", binary], check=True)
                for test in selected:
                    print(f"{label}: {test}", flush=True)
                    failed |= subprocess.run([binary, test]).returncode != 0
                if controller == "JD79661" and reference:
                    failed |= not compare_trace(binary, reference, label)
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
