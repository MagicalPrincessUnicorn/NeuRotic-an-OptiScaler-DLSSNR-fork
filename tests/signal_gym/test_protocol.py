import subprocess
import sys
import unittest
from unittest.mock import patch
from test_storage import CORPUS
from adapter_process import AdapterProcess
from storage import GymError
REAL_POPEN=subprocess.Popen
class ProtocolTests(unittest.TestCase):
    def child(self,script,timeout=1):
        def launch(*args,**kwargs):return REAL_POPEN([sys.executable,'-u','-c',script],**kwargs)
        with patch('adapter_process.subprocess.Popen',side_effect=launch):return AdapterProcess(sys.executable,timeout)
    def test_watchdog_includes_blocked_write(self):
        s=self.child('import time;time.sleep(10)',.1)
        try:
            with self.assertRaisesRegex(GymError,'WATCHDOG_TIMEOUT'):s.request('invoke',{'padding':'x'*1000000})
        finally:s.abort()
    def test_crash_cannot_be_refusal(self):
        s=self.child('raise SystemExit(3)')
        try:
            with self.assertRaisesRegex(GymError,'PROTOCOL_ERROR'):s.request('describe',None)
        finally:s.abort()
    def test_malformed_trailing_stdout_is_error(self):
        script="import sys,struct,json; n=struct.unpack('<I',sys.stdin.buffer.read(4))[0]; q=json.loads(sys.stdin.buffer.read(n)); r=json.dumps({'interface_version':'2.0.0','request_id':q['request_id'],'status':'ok','payload':None}).encode(); sys.stdout.buffer.write(struct.pack('<I',len(r))+r+b'x'); sys.stdout.buffer.flush(); sys.stdin.buffer.read()"
        s=self.child(script)
        try:
            self.assertIsNone(s.request('describe',None))
            with self.assertRaisesRegex(GymError,'PROTOCOL_ERROR'):s.finish()
        finally:s.abort()
if __name__=='__main__':unittest.main()
