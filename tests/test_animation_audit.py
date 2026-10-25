"""Check that live pose readback rejects absent or mismatched observations."""
import json
from pathlib import Path
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from tools.animation.probes import split_readback


class AnimationAuditTests(unittest.TestCase):
    def test_missing_engine_observation_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            probe = root / "input.jsonl"
            probe.write_text(json.dumps({"machine": "expected", "source": "live_engine_readback", "actors": [{}],
                                         "readback_error": {"position_cm": 0, "rotation_deg": 0}}) + "\n")
            with self.assertRaisesRegex(ValueError, "missing expected/applied"):
                split_readback(probe, root)

    def test_unpaired_engine_frames_cannot_pass(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            rows = [{"machine": machine, "source": "live_engine_readback", "actors": [{"cell": 1}],
                     "t_ms": time, "readback_error": {"position_cm": 0, "rotation_deg": 0}}
                    for machine, time in (("expected", 100), ("applied", 200))]
            probe = root/"input.jsonl"
            probe.write_text("\n".join(map(json.dumps, rows))+"\n")
            with self.assertRaisesRegex(ValueError, "unpaired"):
                split_readback(probe, root)



if __name__ == "__main__":
    unittest.main()
