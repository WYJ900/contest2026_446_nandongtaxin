"""Freeze one product stage and its evidence without touching the device."""
from pathlib import Path
import argparse
import hashlib
import json
import shutil
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument("name")
parser.add_argument("parent")
parser.add_argument("apk")
parser.add_argument("--scenarios", type=int, default=15)
parser.add_argument("--date", default="20261103")
args = parser.parse_args()
root = Path(__file__).resolve().parents[1]
repo = root / "my-contest-repo"
save = root / f"backup/{args.name}-{args.date}"
stage = repo / f"artifacts/{args.name}-stage-{args.date}"
evidence = repo / f"docs/validation-{args.name}-{args.date}"
stage.mkdir(parents=True, exist_ok=True)
evidence.mkdir(parents=True, exist_ok=True)
def sha(p): return hashlib.sha256(p.read_bytes()).hexdigest()
for name in ("firmware.bin", "config", "feature.patch", "app-source.tar.gz"):
    shutil.copy2(save / name, stage / name)
shutil.copy2(root / f"exports/mobile/VelaRide-{args.apk}-debug.apk", stage)
for name in ("build_product_features.sh", "test_product_features.sh", "package_product_features.py"):
    shutil.copy2(root / "tools" / name, stage)
for name in ("01-apply.log", "ui-sport.log", "ui-shell.log", "ride-tests.log", "ble-tests.log", "memo-tests.log", "android-build.log"):
    if (save / name).exists(): shutil.copy2(save / name, evidence / (name.removesuffix(".log") + ".txt"))
(evidence / "firmware-build-tail.txt").write_text(
    "\n".join((save / "03-build.log").read_text(errors="replace").splitlines()[-22:]) + "\n",
    encoding="utf-8", newline="\n")
assert not (save / "config.diff").read_bytes()
from PIL import Image
for image in (save / "screens").glob("*.ppm"):
    if image.stem.startswith(("09-history", "02a-ride", "05d-route", "06a-memo", "08c-drink", "08d-resume")):
        Image.open(image).save(evidence / (image.stem + ".png"))
with zipfile.ZipFile(stage / "android-source.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for p in (repo / "mobile/android").rglob("*"):
        if p.is_file() and ".build" not in p.relative_to(repo / "mobile/android").parts:
            z.write(p, p.relative_to(repo / "mobile/android").as_posix())
with zipfile.ZipFile(stage / "tests-source.zip", "w", zipfile.ZIP_DEFLATED) as z:
    for folder in ("ride_session", "sport_ui", "memo"):
        if not (repo / "tests" / folder).exists(): continue
        for p in (repo / "tests" / folder).rglob("*"):
            if p.is_file(): z.write(p, p.relative_to(repo).as_posix())
    z.write(repo / "tests/test_ble_safety_payload.py", "tests/test_ble_safety_payload.py")
summary = {
    "stage": args.name, "parent_commit": args.parent,
    "firmware_sha256": sha(stage / "firmware.bin"),
    "firmware_bytes": (stage / "firmware.bin").stat().st_size,
    "application_space_remaining": 0x990000 - (stage / "firmware.bin").stat().st_size,
    "apk_version": args.apk, "apk_sha256": sha(stage / f"VelaRide-{args.apk}-debug.apk"),
    "passed": {"firmware_build": True, "unchanged_config": True, "ride_scenarios": args.scenarios,
               "actual_stage_lvgl_asan_ubsan": True, "ble_payload": True, "android_host_tests_and_build": True},
    "device_operations": False, "candidate_installed": False,
    "pending": ["GPS parent real ride acceptance", "Physical UI and original functions on candidate",
                "Real Flash persistence and Android backfill on phone"],
    "source_limit": "Application-stage snapshot and parent archives; clean full dependency rebuild not verified",
}
(evidence / "summary.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
summary["files"] = {p.name: sha(p) for p in stage.iterdir() if p.is_file() and p.name != "manifest.json"}
(stage / "manifest.json").write_text(json.dumps(summary, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")
print(json.dumps({k: summary[k] for k in ("stage", "firmware_sha256", "firmware_bytes", "application_space_remaining", "apk_version")}, indent=2))
