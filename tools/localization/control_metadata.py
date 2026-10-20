"""Refresh canonical preview metadata from actual stable-ID presentation calls."""
import ast,json,re
import inventory
from presentation_arguments import mask,arguments
def update():
    entries=inventory.load();by_id={e['id']:e for e in entries};found={}
    kinds={'Button':'button','ActionButton':'button','IconButton':'button','SmallButton':'button','OperationAction':'button','Checkbox':'checkbox','Toggle':'checkbox','EmphasizedCheckbox':'checkbox','WindowSectionHeader':'header','ComponentHeading':'header','BeginNrCard':'header','SeparatorText':'header','CollapsingHeader':'header','ContentCard':'header','HelpMarker':'tooltip','Description':'tooltip','SetTooltip':'tooltip','TextColored':'warning','Line':'status','Linef':'status','TextWrapped':'paragraph'}
    rank={k:i for i,k in enumerate(['label','paragraph','status','warning','tooltip','header','checkbox','button'])}
    for path in inventory.paths():
        source=path.read_text(encoding='utf-8-sig');code=mask(source)
        for call in re.finditer(r'\b('+ '|'.join(kinds)+r')\s*\(',code):
            _,end=arguments(code,code.index('(',call.start()));kind=kinds[call[1]]
            for binding in re.finditer(r'Neurotic::Ui(?:Literal|Options|Message)\s*\(\s*("(?:[^"\\]|\\.)*")',source[call.start():end]):
                for id in ast.literal_eval(binding[1]).split('|'):
                    if id in by_id and (id not in found or rank[kind]>rank[found[id]]):found[id]=kind
    changes=[]
    for id,kind in found.items():
        entry=by_id[id]
        if entry['control']!=kind:changes.append(dict(id=id,before=entry['control'],after=kind));entry['control']=kind
    inventory.validate(entries);(inventory.DEST/'english.json').write_text(json.dumps(entries,ensure_ascii=False,indent=2)+'\n',encoding='utf-8');inventory.generate()
    (inventory.ROOT/'docs/implementation-evidence/community-languages/CONTROL_METADATA.json').write_text(json.dumps(dict(classified=len(found),changes=changes),indent=2)+'\n',encoding='utf-8')
    print('Production presentation controls classified:',len(found))
if __name__=='__main__':update()
