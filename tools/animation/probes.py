"""Validate live engine readback and split it into paired animcheck streams."""
import json
import math


def split_readback(path, directory):
    counts = {"expected": 0, "applied": 0}
    observations = {"expected": [], "applied": []}
    errors = {"position_cm": 0.0, "rotation_deg": 0.0}
    scorer_selftest = False
    with (directory / "expected.jsonl").open("w", encoding="utf-8") as expected, \
         (directory / "applied.jsonl").open("w", encoding="utf-8") as applied:
        outputs = {"expected": expected, "applied": applied}
        for line in path.read_text(encoding="utf-8").splitlines():
            row = json.loads(line)
            machine = row.get("machine")
            scorer_selftest = scorer_selftest or row.get("scorer_selftest") is True
            if machine not in outputs or row.get("source") != "live_engine_readback":
                raise ValueError("expected a live engine readback stream")
            for key in errors:
                value = row.get("readback_error", {}).get(key)
                if type(value) not in (float, int) or not math.isfinite(value) or value < 0:
                    raise ValueError("missing or invalid engine readback error")
                errors[key] = max(errors[key], value)
            outputs[machine].write(json.dumps(row, separators=(",", ":")) + "\n")
            observations[machine].append((row.get("t_ms"), row.get("frame"),
                                           tuple(actor.get("cell") for actor in row.get("actors", []))))
            counts[machine] += len(row.get("actors", []))
    if not all(counts.values()):
        raise ValueError("missing expected/applied samples; zero comparisons cannot pass")
    if observations["expected"] != observations["applied"]:
        raise ValueError("unpaired expected/applied timestamps, frames or worlds")
    return {"samples": counts, "paired_samples": len(observations["expected"]),
            "maximum_errors": errors, "scorer_selftest": scorer_selftest}
