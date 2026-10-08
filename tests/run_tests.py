#!/usr/bin/env python3
"""Host C tests; these do not replace an ESP-IDF build or a hardware test."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
core = root / "components/alarm_core"
app = root / "components/alarm_app"
compiler = shutil.which(os.environ.get("CC", "cc"))
if compiler is None:
    raise SystemExit("A host C compiler is required (cc/gcc/clang, or set CC).")
cases = {
    "protocol": ["tests/test_protocol.c", "components/alarm_core/alarm_protocol.c"],
    "system": ["tests/test_system.c", "components/alarm_core/alarm_logic.c", "components/alarm_core/alarm_protocol.c"],
    "ui": ["tests/test_ui.c", "components/alarm_app/ui_frame.c", "components/alarm_core/alarm_logic.c", "components/alarm_core/alarm_protocol.c"],
    "oled-wire": ["tests/test_oled_wire.c", "components/alarm_app/oled_wire.c"],
    "outputs": ["tests/test_outputs.c", "components/alarm_app/panel_outputs.c", "components/alarm_core/alarm_logic.c", "components/alarm_core/alarm_protocol.c"],
}
with tempfile.TemporaryDirectory(prefix="host-test-", dir=root) as build:
    for name, sources in cases.items():
        output = Path(build) / (name + (".exe" if os.name == "nt" else ""))
        subprocess.run([compiler, "-std=c11", "-Wall", "-Wextra", "-Werror", "-pedantic", "-O2",
                        "-I", str(core / "include"), "-I", str(app), "-I", str(root / "tests/stubs"),
                        *[str(root / s) for s in sources], "-o", str(output)], check=True)
        subprocess.run([str(output)], check=True)
print("All 5 host test programs passed. Radio/I2C/GPIO timing still needs measurement on the boards.")
