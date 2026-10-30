#!/usr/bin/env python3
"""Private integration fixture: production Python worker, plus fault modes."""
import json
from pathlib import Path
import sys
import time

root = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(root / 'bindings/python'))
sys.path.insert(0, str(root / 'tests/ipc'))

mode = sys.argv[1]
if mode == 'normal':
    from xytron.host_process import serve
    serve('example_py', 'python_fixture', ['Player'], 'session_1')
elif mode == 'echo':
    for line in sys.stdin.buffer:
        request = json.loads(line)
        answer = {'id': request['id'], 'ok': True,
                  'result': request['args'][0] if request['args'] else None}
        sys.stdout.write(json.dumps(answer, separators=(',', ':')) + '\n')
        sys.stdout.flush()
elif mode == 'badid':
    sys.stdin.buffer.readline()
    sys.stdout.write('{"id":999,"ok":true,"result":17}\n')
    sys.stdout.flush()
elif mode == 'duplicate':
    sys.stdin.buffer.readline()
    sys.stdout.write('{"id":1,"id":1,"ok":true,"result":17}\n')
    sys.stdout.flush()
elif mode == 'wrongtype':
    sys.stdin.buffer.readline()
    sys.stdout.write('{"id":1,"ok":true,"result":{}}\n')
    sys.stdout.flush()
elif mode == 'no_read':
    # Verify the broker's input write deadline when stdin is never drained.
    time.sleep(3)
elif mode == 'stall':
    sys.stdin.buffer.readline()
    time.sleep(3)
elif mode == 'exit':
    sys.exit(0)
else:
    raise SystemExit('unknown fixture mode')
