"""Wire bound text parameters before printf inserts them into an unbound buffer.

Preserves the compiled ABI and every original label/control ID. User text remains
unbound and unchanged. This is a source migration, not a runtime spelling map.
"""
from pathlib import Path
import ast,re,sys
sys.path.insert(0,str(Path(__file__).parent))
import inventory
def mask(source):
 return inventory.TOKEN.sub(lambda m:' '*len(m[0]),source)
def arguments(code,start):
 depth=0;begin=start+1;parts=[]
 for i in range(start,len(code)):
  c=code[i]
  if c in '({[':depth+=1
  elif c in ')}]':
   depth-=1
   if depth==0:parts.append((begin,i));return parts,i+1
  elif c==',' and depth==1:parts.append((begin,i));begin=i+1
 return [],start
def main():
 count=0
 for path in inventory.paths():
  source=path.read_text(encoding='utf-8-sig');code=mask(source);edits=[]
  for call in re.finditer(r'(?:ImGui::)?(Text(?:Wrapped|Disabled|Colored)?|SetTooltip|StrFmt|DlssNr::StatusPanel::Format)\s*\(',code):
   args,_=arguments(code,code.index('(',call.start()));f=1 if call[1]=='TextColored' else 0
   if len(args)<=f:continue
   start,end=args[f];raw=source[start:end];strings=list(inventory.TOKEN.finditer(raw))
   if not strings:continue
   try:fmt=ast.literal_eval(strings[-1][0])
   except (SyntaxError,ValueError):continue
   tokens=[t for t in inventory.PRINTF.findall(fmt) if t!='%%']
   if len(args)!=f+1+len(tokens):continue
   for token,(a,b) in zip(tokens,args[f+1:]):
    value=source[a:b].strip()
    if token.endswith('s') and not token.endswith('ls') and value and not value.startswith('Neurotic::Translate('):
     edits.append((a,b,'Neurotic::Translate('+value+').c_str()'));count+=1
  # Only disjoint innermost replacements in one pass; a later pass can bind parents.
  chosen=[]
  for edit in sorted(edits,key=lambda e:e[1]-e[0]):
   if not any(edit[0]<other[1] and other[0]<edit[1] for other in chosen):chosen.append(edit)
  for a,b,text in sorted(chosen,reverse=True):source=source[:a]+text+source[b:]
  if edits:path.write_text(source,encoding='utf-8')
 print('Bound presentation text arguments:',count)
if __name__=='__main__':main()
