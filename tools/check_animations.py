"""Compare requested poses with actual engine transform readback using animcheck."""
import argparse
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from tools.animation.probes import split_readback


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True, help="native_pose_probe.jsonl from a QA run")
    parser.add_argument("--animcheck-root", type=Path, required=True, help="pinned tools/animcheck directory")
    parser.add_argument("--out", type=Path, required=True)
    args = parser.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    observations = split_readback(args.probe, args.out)
    readback_ok = all(value <= .1 for value in observations["maximum_errors"].values())
    scope = ("synthetic scorer self-test; no engine observations" if observations["scorer_selftest"] else
             "live requested pose versus engine transform readback; not two-client sync")
    command = [sys.executable, "-I", str(args.animcheck_root / "cli.py"), "analyze",
               "expected=" + str(args.out / "expected.jsonl"), "applied=" + str(args.out / "applied.jsonl"),
               "--delay-ms", "0", "--latency-ms", "1", "--avatar", "body=expected:1>applied:2",
               "--assert", "avatar.compared>0", "--assert", "avatar.pose_err_p95<=0.1",
               "--assert", "avatar.pos_err_p95<=0.1",
               "--profile", str(ROOT / "tools/animation/atomic-heart.profile.json"),
               "--json", str(args.out / "animcheck.json"), "--html", str(args.out / "animcheck.html")]
    result = subprocess.run(command, cwd=ROOT, check=False)
    metadata = {"scope": scope, **observations, "readback_limits_passed": readback_ok,
                "animcheck_exit_code": result.returncode}
    (args.out / "scope.json").write_text(json.dumps(metadata, indent=2) + "\n", encoding="utf-8")
    passed = readback_ok and result.returncode == 0
    print("Animation audit:", "PASS" if passed else "FAIL", "|", scope)
    return 0 if passed else 1


if __name__ == "__main__":
    raise SystemExit(main())
