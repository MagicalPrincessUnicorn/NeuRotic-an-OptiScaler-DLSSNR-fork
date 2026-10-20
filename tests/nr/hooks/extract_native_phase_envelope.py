"""Extract the actual native RunBefore try/restore boundary for a CPU fixture.

Only external host, callback, result and restoration dependencies are stubbed by
the fixture. The production control flow and phase annotation are not copied.
"""
import argparse
import hashlib
import json
from pathlib import Path

parser = argparse.ArgumentParser()
parser.add_argument("source", type=Path)
parser.add_argument("output", type=Path)
args = parser.parse_args()
raw = args.source.read_bytes()
text = raw.decode("utf-8").replace("\r\n", "\n")
anchor = "// Creation/preparation can itself record work. Its state belongs"
assert text.count(anchor) == 1, "production envelope anchor changed"
start = text.rfind("        try\n        {", 0, text.index(anchor))
end_marker = "        const bool restored=restore.Restore();"
end = text.index(end_marker, start) + len(end_marker)
assert start >= 0
fragment = text[start:end] + "\n"
args.output.write_bytes(fragment.encode("utf-8"))
print(json.dumps({"source_sha256": hashlib.sha256(raw).hexdigest(),
                  "fragment_sha256": hashlib.sha256(fragment.encode()).hexdigest(),
                  "scope": "actual RunBefore control flow; declared external CPU stubs"}))
