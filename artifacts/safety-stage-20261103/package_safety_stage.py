"""Archive the isolated safety candidate without interacting with hardware."""
from pathlib import Path
import hashlib
import json
import shutil
import zipfile

root = Path(__file__).resolve().parents[1]
repo = root / "my-contest-repo"
save = root / "backup/safety-stage-20261103"
stage = repo / "artifacts/safety-stage-20261103"
evidence = repo / "docs/validation-safety-20261103"
stage.mkdir(parents=True, exist_ok=True)
evidence.mkdir(parents=True, exist_ok=True)

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

for source, name in [(save / "safety.bin", "firmware.bin"),
                     (save / "safety.config", "config"),
                     (save / "safety.patch", "safety.patch"),
                     (root / "exports/mobile/VelaRide-0.1.5-debug.apk", "VelaRide-0.1.5-debug.apk"),
                     (root / "tools/build_safety_stage.sh", "build_safety_stage.sh"),
                     (root / "tools/test_safety_stage_ui.sh", "test_safety_stage_ui.sh"),
                     (Path(__file__), "package_safety_stage.py")]:
    shutil.copy2(source, stage / name)
for source, name in [(save / "ride-session-tests.log", "ride-session-tests.txt"),
                     (save / "android-build.log", "android-build.txt"),
                     (save / "02-apply.log", "patch-application.txt"),
                     (save / "ui/03-sport.log", "sport-ui-tests.txt"),
                     (save / "ui/04-shell.log", "shell-ui-tests.txt")]:
    shutil.copy2(source, evidence / name)
(evidence / "firmware-build-tail.txt").write_text(
    "\n".join((save / "04-build.log").read_text(errors="replace").splitlines()[-24:]) + "\n",
    encoding="utf-8", newline="\n")
assert not (save / "05-config.diff").read_bytes(), "Unexpected configuration changes"
for name in ("08-safety-waiting", "08-alert", "08a-help-confirm", "08a-help-result", "09-safety-summary"):
    from PIL import Image
    Image.open(save / "ui/screens" / (name + ".ppm")).save(evidence / (name + ".png"))
with zipfile.ZipFile(stage / "android-0.1.5-source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    mobile = repo / "mobile/android"
    for path in sorted(mobile.rglob("*")):
        if path.is_file() and ".build" not in path.relative_to(mobile).parts:
            archive.write(path, path.relative_to(mobile).as_posix())
with zipfile.ZipFile(stage / "validation-source.zip", "w", zipfile.ZIP_DEFLATED) as archive:
    for folder in ("tests/ride_session", "tests/sport_ui"):
        for path in sorted((repo / folder).rglob("*")):
            if path.is_file(): archive.write(path, path.relative_to(repo).as_posix())
    archive.write(repo / "tests/test_ble_safety_payload.py", "tests/test_ble_safety_payload.py")
summary = {
    "date": "2026-11-03", "checkpoint_commit": "7f16229",
    "checkpoint_tag": "checkpoint-before-safety-20261103",
    "parent_firmware_sha256": "9041c538529b46f3419c06f46bf70cfdd2570c0d20a58f704b31f9b147f7c0ef",
    "firmware_sha256": sha(stage / "firmware.bin"),
    "firmware_bytes": (stage / "firmware.bin").stat().st_size,
    "application_space_remaining_bytes": 0x990000 - (stage / "firmware.bin").stat().st_size,
    "apk_version": "0.1.5", "apk_sha256": sha(stage / "VelaRide-0.1.5-debug.apk"),
    "passed": {
        "firmware_build": True, "unchanged_parent_config": True, "ride_scenarios_asan_ubsan": 14,
        "actual_stage_lvgl_asan_ubsan": True, "lvgl_render_visually_checked": True,
        "actual_ble_format_and_max_size": True, "android_wire_handshake_gps_history": True,
        "independent_v1_v2_migration": True, "v3_roundtrip_and_csv": True,
        "apk_signature_valid_and_same_signer_as_014": True,
    },
    "hardware_operations_performed": False, "candidate_installed": False,
    "phone_candidate_installed": False, "audio_playback_performed": False,
    "synthetic_device_ride_written": False,
    "pending": ["User physical validation of original GPS candidate", "Safety candidate installation after that checkpoint",
                "Physical UI and baseline regression", "Safe sensor-trigger validation and false positives",
                "New V3 record reboot persistence and Android 0.1.5 sync/export on phone"],
    "limitations": ["Help is intent only; no external message, call or background listener",
                    "Summary counts, not a complete event timeline",
                    "Current ride not recoverable after reboot before finish",
                    "Phone GPS remains foreground-only", "Old firmware/APK cannot read newly saved V3/VRH3 data",
                    "Not a verified clean full-source reproducible build"],
}
(evidence / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
manifest = dict(summary)
manifest["files"] = {p.name: sha(p) for p in sorted(stage.iterdir()) if p.is_file() and p.name != "manifest.json"}
(stage / "manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
sums = repo / "artifacts/SHA256SUMS"
lines = [line for line in sums.read_text().splitlines() if "safety-stage-20261103/" not in line]
for name in ("firmware.bin", "VelaRide-0.1.5-debug.apk"):
    lines.append(f"{sha(stage / name).upper()}  safety-stage-20261103/{name}")
sums.write_text("\n".join(lines) + "\n", encoding="utf-8", newline="\n")
print(json.dumps({key: summary[key] for key in ("firmware_sha256", "firmware_bytes", "apk_sha256", "candidate_installed")}, indent=2))
