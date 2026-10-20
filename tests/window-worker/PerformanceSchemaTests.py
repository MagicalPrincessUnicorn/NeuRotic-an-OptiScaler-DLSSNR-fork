"""Validate a production serializer's synthetic fixture against the repository schema."""
import json, subprocess
from pathlib import Path
import jsonschema
root=Path(__file__).resolve().parents[2]
run=subprocess.run([str(root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'),'--test-control'],capture_output=True,text=True,encoding='utf-8',check=True)
schema=json.loads((root/'tools/window-worker/performance.schema.json').read_text())
jsonschema.Draft202012Validator.check_schema(schema)
record=json.loads(next(s.removeprefix('PERFORMANCE_RECORD ') for s in run.stdout.splitlines() if s.startswith('PERFORMANCE_RECORD ')))
jsonschema.validate(record,schema)
record["scheduling"]={"wait_since_previous_frame_ms":2.5,"completion_interval_ms":None}
jsonschema.validate(record,schema)
for mutate in [lambda r:r.update(nr_submitted=False),lambda r:r['gpu'].update(nr_ms=-1),lambda r:r.update(extra='wrong'),lambda r:r['scheduling'].update(wait_since_previous_frame_ms=-1)]:
    bad=json.loads(json.dumps(record));mutate(bad)
    try:jsonschema.validate(bad,schema)
    except jsonschema.ValidationError:pass
    else:raise AssertionError('schema admitted invalid frame')
print('PASS performance schema: production serializer synthetic frame and 4 negative cases')
