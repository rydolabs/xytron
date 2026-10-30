"""Offline Python worker JSONL contract; NOT actual WebView/CEF transport."""
import contextlib
import io
import json
import sys
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))

from xytron import export
from xytron.host_process import (
    MAX_ID, MAX_LINE, ProtocolError, _reply, decode_request, serve,
)


@export
class WorkerProbe:
    def __init__(self):
        self.value = 0

    @export
    def add(self, n: int) -> int:
        self.value += n
        return self.value

    def hidden(self) -> str:
        return "secret"


class DecodeContract(unittest.TestCase):
    def test_valid_primitives_and_ids(self):
        ident, operation, args = decode_request(
            b'{"id":1,"operation":"probe.WorkerProbe.create","args":[2,true,null,"ok",1.5]}\n',
            last_id=0,
        )
        self.assertEqual((ident, operation), (1, "probe.WorkerProbe.create"))
        self.assertEqual(args, [2, True, None, "ok", 1.5])
        self.assertEqual(decode_request(
            b'{"id":2147483647,"operation":"a","args":[]}\n', MAX_ID-1)[0], MAX_ID)

    def test_malformed_and_replayed_requests_fail_closed(self):
        invalid = [
            b'not json',
            b'[]',
            b'null',
            b'{}',
            b'{"id":1,"operation":"a","args":[],"caller":"forged"}',
            b'{"id":1,"id":2,"operation":"a","args":[]}',
            b'{"id":1,"operation":"a","operation":"b","args":[]}',
            b'{"id":true,"operation":"a","args":[]}',
            b'{"id":0,"operation":"a","args":[]}',
            b'{"id":-1,"operation":"a","args":[]}',
            b'{"id":2147483648,"operation":"a","args":[]}',
            b'{"id":1.0,"operation":"a","args":[]}',
            b'{"id":1,"operation":"","args":[]}',
            b'{"id":1,"operation":12,"args":[]}',
            b'{"id":1,"operation":"a","args":{}}',
            b'{"id":1,"operation":"a","args":[{}]}',
            b'{"id":1,"operation":"a","args":[[]]}',
            b'{"id":1,"operation":"a","args":[NaN]}',
            b'{"id":1,"operation":"a","args":[Infinity]}',
            b'{"id":1,"operation":"a","args":[1e999]}',
            b'{"id":1,"operation":"a","args":["x"]}extra',
            b'{"id":1,"operation":"a","args":[' + b'0,'*32 + b'0]}',
        ]
        for payload in invalid:
            with self.subTest(payload=payload[:70]), self.assertRaises(ProtocolError):
                decode_request(payload, 0)
        with self.assertRaises(ProtocolError):
            decode_request(b'{"id":2,"operation":"a","args":[]}', 2)
        with self.assertRaises(ProtocolError):
            decode_request(b'{"id":1,"operation":"a","args":[]}', 2)
        with self.assertRaises(ProtocolError):
            decode_request(json.dumps({"id":1,"operation":"x"*257,"args":[]}), 0)
        with self.assertRaises(ProtocolError):
            decode_request(json.dumps({"id":1,"operation":"x","args":["a"*(MAX_LINE+1)]}), 0)

    def test_oversized_result_never_exceeds_protocol_limit(self):
        out = io.BytesIO()
        _reply(out, {"id":7,"ok":True,"result":"x"*(MAX_LINE+100)})
        answer = json.loads(out.getvalue())
        self.assertEqual(answer, {"id":7,"ok":False,"error":"ResultTooLarge"})
        self.assertLessEqual(len(out.getvalue()),MAX_LINE)


class WorkerStreamContract(unittest.TestCase):
    @staticmethod
    def serve_bytes(payload):
        output = io.BytesIO()
        stderr = io.StringIO()
        with contextlib.redirect_stderr(stderr):
            serve("probe", __name__, ["WorkerProbe"], "trusted_owner",
                  reader=io.BytesIO(payload), writer=output)
        return [json.loads(line) for line in output.getvalue().splitlines()]

    def test_valid_create_then_replay_and_unexported_denial(self):
        payload = (
            b'{"id":1,"operation":"probe.WorkerProbe.create","args":[]}\n'
            b'{"id":1,"operation":"probe.WorkerProbe.create","args":[]}\n'
            b'{"id":2,"operation":"probe.WorkerProbe.method.hidden","args":["invalid"]}\n'
            b'{"id":3,"operation":"probe.WorkerProbe.create","args":[]}\n'
        )
        results = self.serve_bytes(payload)
        self.assertEqual([entry["id"] for entry in results], [1, None, 2, 3])
        self.assertTrue(results[0]["ok"])
        self.assertEqual(results[1]["error"], "InvalidRequest")
        self.assertEqual(results[2]["error"], "Rejected")
        self.assertTrue(results[3]["ok"])
        self.assertNotEqual(results[0]["result"], results[3]["result"])

    def test_deep_json_is_denied_without_worker_crash(self):
        # A <64KiB JSON payload may exceed Python JSON decoder nesting
        # depth and raise RecursionError rather than ValueError. The worker
        # must report InvalidRequest and continue to a healthy next frame.
        nested = (b'{"id":1,"operation":"probe.WorkerProbe.create","args":'
                  + b'['*10000 + b']'*10000 + b'}\n')
        healthy = b'{"id":1,"operation":"probe.WorkerProbe.create","args":[]}\n'
        results = self.serve_bytes(nested + healthy)
        self.assertEqual(len(results), 2)
        self.assertEqual(results[0], {"id":None,"ok":False,"error":"InvalidRequest"})
        self.assertEqual(results[1]["id"], 1)
        self.assertTrue(results[1]["ok"])

    def test_unrecoverable_frame_stops_after_error(self):
        oversize = b'x' * (MAX_LINE + 4) + b'\n'
        next_valid = b'{"id":1,"operation":"probe.WorkerProbe.create","args":[]}\n'
        self.assertEqual(self.serve_bytes(oversize + next_valid),
                         [{"id":None,"ok":False,"error":"FrameTooLarge"}])

    def test_bad_bootstrap_identity_rejected(self):
        with self.assertRaises(ValueError):
            serve("bad-id!", __name__, ["WorkerProbe"], "owner",
                  reader=io.BytesIO(), writer=io.BytesIO())
        with self.assertRaises(ValueError):
            serve("probe", __name__, ["WorkerProbe"], "owner/forged",
                  reader=io.BytesIO(), writer=io.BytesIO())


if __name__ == '__main__':
    unittest.main()
