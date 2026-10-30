"""Worker replies must honor the complete on-wire JSONL byte limit."""
import io
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from xytron.host_process import MAX_LINE, _reply


class ReplyFrameLimit(unittest.TestCase):
    def test_reply_limit_includes_newline_delimiter(self):
        payload = {"id": 7, "ok": True, "result": ""}
        overhead = len(json.dumps(payload, ensure_ascii=True,
                                  allow_nan=False, separators=(",", ":")).encode("utf-8"))
        for extra, expected_ok in ((0, True), (1, False)):
            with self.subTest(extra=extra):
                payload["result"] = "x" * (MAX_LINE - overhead - 1 + extra)
                output = io.BytesIO()
                _reply(output, payload)
                data = output.getvalue()
                self.assertLessEqual(len(data), MAX_LINE)
                self.assertTrue(data.endswith(b"\n"))
                response = json.loads(data)
                self.assertEqual(response["ok"], expected_ok)
                if not expected_ok:
                    self.assertEqual(response["error"], "ResultTooLarge")


if __name__ == "__main__":
    unittest.main()
