"""Stage explicitly selected inherited dependencies for internal development only."""
import argparse
import json
from pathlib import Path
import sys
ROOT=Path(__file__).resolve().parents[2]
sys.path.insert(0,str(ROOT/'tools/packaging/customer'))
from OfflineDependencies import DependencyArchive, ACCEPTED_ARCHIVE_SHA256, ACCEPTED_INVENTORY_SHA256, require, owned_output as checked_output, write_owned

def owned_output(path):
    return checked_output(ROOT,path,tree=True)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--inventory',type=Path,required=True)
    parser.add_argument('--selection',type=Path,required=True,help='JSON array of exact dependency member paths')
    parser.add_argument('--output-root',type=Path,required=True)
    parser.add_argument('--allow-inspector-models',action='store_true')
    mode=parser.add_mutually_exclusive_group(required=True);mode.add_argument('--plan-only',action='store_true');mode.add_argument('--stage',action='store_true')
    args=parser.parse_args();out=owned_output(args.output_root)
    reader=DependencyArchive(args.archive,ACCEPTED_ARCHIVE_SHA256,args.inventory,ACCEPTED_INVENTORY_SHA256)
    names=json.loads(args.selection.read_bytes());require(isinstance(names,list) and all(isinstance(n,str) for n in names),'Expected exact path selection array')
    rows=reader.plan(names,allow_models=args.allow_inspector_models)
    receipt={'qualification':'INTERNAL_DEVELOPMENT_DEPENDENCIES_ONLY','archive_sha256':reader.archive_sha256,'inventory_sha256':reader.inventory_sha256,'files':rows,'firstparty_reused':False,'executed':False,'publication_authorized':False}
    if args.plan_only:print(json.dumps(receipt,indent=2));return
    files=reader.read(names,allow_models=args.allow_inspector_models)
    receipt_path=owned_output(out/'dependency-receipt.json')
    owned_output(receipt_path.with_name(receipt_path.name+'.nr-write.tmp'))
    # Validate every destination before any write; never overwrite divergent data.
    for name,raw in files.items():
        target=owned_output(out/name)
        require(not target.exists() or (target.is_file() and target.read_bytes()==raw),'Existing staging input differs: '+name)
    for name,raw in files.items():
        target=owned_output(out/name);target.parent.mkdir(parents=True,exist_ok=True)
        if not target.exists():
            with target.open('xb') as stream:stream.write(raw)
    reader.stable();out.mkdir(parents=True,exist_ok=True)
    write_owned(ROOT,receipt_path,(json.dumps(receipt,indent=2)+'\n').encode('utf-8'))
    print(json.dumps({'status':'INTERNAL_DEPENDENCIES_STAGED','files':len(files)}))

if __name__=='__main__':main()
