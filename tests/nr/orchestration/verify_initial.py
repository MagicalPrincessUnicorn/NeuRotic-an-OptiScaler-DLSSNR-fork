"""Use the frozen TEST002 verifier; never turn compilation or missing rows into PASS."""
import argparse,importlib.util,json,pathlib,sys
sys.dont_write_bytecode=True
here=pathlib.Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('frozen',here.parent/'contracts/test002_source_and_evidence.py')
frozen=importlib.util.module_from_spec(spec);spec.loader.exec_module(frozen)
parser=argparse.ArgumentParser();parser.add_argument('--log',type=pathlib.Path,required=True)
parser.add_argument('--suite',required=True);parser.add_argument('--run-id',required=True)
args=parser.parse_args()
frozen.verify_log(json.loads((here/'initial_inventory.json').read_text()),args.suite,args.log,args.log.parent,args.run_id,False)
