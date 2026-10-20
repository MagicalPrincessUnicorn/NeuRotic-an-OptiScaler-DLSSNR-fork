"""Run the existing TEST002 framing verifier for a registered construction suite."""
import argparse
import json
import pathlib
from construction_profiles import PROFILES
from supervised_producer import _FRAMING

ROOT = pathlib.Path(__file__).resolve().parents[3]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--suite', choices=PROFILES, required=True)
    parser.add_argument('--log', type=pathlib.Path, required=True)
    parser.add_argument('--run-dir', type=pathlib.Path, required=True)
    parser.add_argument('--run-id', required=True)
    args = parser.parse_args()
    inventory = json.loads((ROOT / PROFILES[args.suite]['inventory']).read_text(encoding='utf-8-sig'))
    _FRAMING.verify_log(inventory, args.suite, args.log, args.run_dir, args.run_id, False)
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
