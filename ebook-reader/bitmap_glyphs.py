"""Apply editable, size-specific black/white grids to compiled glyph tables."""
from pathlib import Path
import json,re
root=Path(__file__).parent

def apply(text,prefix,bprefix,size,bank):
 overrides=json.loads((root/'fonts/ink-bitmap-overrides.json').read_text(encoding='utf-8'))['tables'].get(bank+str(size),{})
 bitmap=re.search(rf'{bprefix}{size}\[\] = \{{(.*?)\}};',text);bits=bytes(map(int,bitmap[1].split(',')))
 glyphs=re.search(rf'{prefix}{size}\[\] = \{{(.*?)\}};',text);output=[];data=[];changed=0
 for row in re.findall(r'\{([^{}]+)\}',glyphs[1]):
  cp,o,w,h,a,x,y=map(int,row.split(','));pixels=[int(bool(bits[o+n//8]&(128>>(n%8)))) for n in range(w*h)]
  override=overrides.get(f'{cp:04X}')
  if override:
   rows=override['rows'];assert rows and len({len(row) for row in rows})==1 and all(set(row)<={'.','#'} for row in rows)
   assert override['advance']==a and override['y']==y and len(rows)==h,(bank,size,cp,'metrics drift')
   nw=len(rows[0]);nx=override['x'];assert 0<=nx and nx+nw<=a
   custom=[int(v=='#') for row in rows for v in row];changed+=pixels!=custom or w!=nw or x!=nx
   pixels=custom;w=nw;x=nx
  output.append((cp,len(data),w,h,a,x,y))
  data.extend(sum(128>>j for j in range(8) if n+j<len(pixels) and pixels[n+j]) for n in range(0,len(pixels),8))
 result=f'static const uint8_t {bprefix}{size}[] = {{'+','.join(map(str,data))+'};\n'
 result+=f'static const Glyph {prefix}{size}[] = {{'+','.join('{'+','.join(map(str,g))+'}' for g in output)+'};\n'
 return result,dict(authored_glyphs=len(overrides),changed_glyphs=changed)
