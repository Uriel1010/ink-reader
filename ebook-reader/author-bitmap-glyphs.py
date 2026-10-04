"""Author explicit pixel grids for difficult glyphs; run once to edit source banks.
This is a design tool, not the production generator. No outline resampling.
"""
from pathlib import Path
import re,json
r=Path(__file__).parent

def read(file,prefix,bprefix,size):
 s=(r/file).read_text();bits=bytes(map(int,re.search(rf'{bprefix}{size}\[\] = \{{(.*?)\}};',s)[1].split(',')));g={}
 for row in re.findall(r'\{([^{}]+)\}',re.search(rf'{prefix}{size}\[\] = \{{(.*?)\}};',s)[1]):
  cp,o,w,h,a,x,y=map(int,row.split(','));g[cp]=dict(advance=a,x=x,y=y,rows=[''.join('#' if bits[o+(yy*w+xx)//8]&(128>>((yy*w+xx)%8)) else '.' for xx in range(w)) for yy in range(h)])
 return g

def design(c,old,size):
 rows=old['rows'];w=len(rows[0]);h=len(rows);x=old['x'];a=old['advance']
 if c in 'Il':w=min(3,a);x=(a-w)//2
 p=set()
 def dot(xx,yy):
  if 0<=xx<w and 0<=yy<h:p.add((xx,yy))
 def hor(yy,left=0,right=None):
  for xx in range(left,w if right is None else right+1):dot(xx,yy)
 def ver(xx,top=0,bottom=None):
  for yy in range(top,h if bottom is None else bottom+1):dot(xx,yy)
 def oval():
  corner=2 if size==20 and w>=9 else 1
  hor(0,corner,w-1-corner);hor(h-1,corner,w-1-corner)
  if corner==2:
   dot(1,1);dot(w-2,1);dot(1,h-2);dot(w-2,h-2)
  ver(0,corner,h-1-corner);ver(w-1,corner,h-1-corner)
 def line(x0,y0,x1,y1):
  dx=abs(x1-x0);dy=-abs(y1-y0);sx=1 if x0<x1 else -1;sy=1 if y0<y1 else -1;error=dx+dy
  while True:
   dot(x0,y0)
   if (x0,y0)==(x1,y1):break
   e=2*error
   if e>=dy:error+=dy;x0+=sx
   if e<=dx:error+=dx;y0+=sy
 def bowl(top,bottom,left=0,right=None):
  right=w-1 if right is None else right
  hor(top,left+1,right-1);hor(bottom,left+1,right-1)
  ver(left,top+1,bottom-1);ver(right,top+1,bottom-1)
 lower={14:8,16:9,18:10,20:11}.get(size,min(h,7))
 if c in 'bdpq':
  top=h-lower if c in 'bd' else 0;bottom=h-1 if c in 'bd' else lower-1
  bowl(top,bottom);ver(0 if c in 'bp' else w-1)
 elif c=='h':
  top=h-lower;ver(0);hor(top,2,w-2);dot(1,top+1);ver(w-1,top+1)
 elif c=='u':bowl(0,h-1);p={v for v in p if v[1]!=0};dot(0,0);dot(w-1,0);dot(w-1,h-1)
 elif c=='g':
  bottom=min(lower-1,h-4);bowl(0,bottom);ver(w-1,0,h-2);hor(h-1,1,w-2);dot(0,h-2)
 elif c=='f':
  stem=max(1,w//2-1);ver(stem,1);hor(0,stem+1);hor(max(2,h-lower),0)
 elif c=='t':
  stem=max(1,w//2-1);ver(stem,0,h-2);hor(2);hor(h-1,stem+1);dot(w-1,h-2)
 elif c in 'ij':
  stem=w-1 if c=='j' else w//2;dot(stem,0);ver(stem,3,h-2 if c=='j' else h-1)
  if c=='j':hor(h-1,1,w-2);dot(0,h-2)
 elif c=='k':
  ver(0);mid=h-lower//2-1;line(w-1,h-lower,1,mid);line(1,mid,w-1,h-1)
 elif c=='v':line(0,0,w//2,h-1);line(w-1,0,w//2,h-1)
 elif c=='w':
  mid=w//2;line(0,0,w//4,h-1);line(w//4,h-1,mid,1);line(mid,1,3*w//4,h-1);line(3*w//4,h-1,w-1,0)
 elif c=='x':line(0,0,w-1,h-1);line(w-1,0,0,h-1)
 elif c=='y':
  mid=min(lower-1,h-4);line(0,0,w//2,mid);line(w-1,0,1,h-1);dot(0,h-1)
 elif c=='z':hor(0);hor(h-1);line(w-1,1,0,h-2)
 elif c in 'oce':
  oval()
  if c=='c':p={v for v in p if not(v[0]==w-1 and 2<=v[1]<=h-3)}
  if c=='e':
   mid=h//2;hor(mid);p={v for v in p if not(v[0]==w-1 and mid< v[1]<h-2)}
 elif c=='a':
  right=w-2;mid=h//2-1;hor(0,1,right-1);dot(right,1);ver(right,1);hor(mid,1,right);ver(0,mid+1,h-2);hor(h-1,1,right-2);dot(right-1,h-2);dot(right,h-1)
 elif c=='s':
  mid=h//2;hor(0,1,w-1);ver(0,1,mid-1);hor(mid,1,w-2);ver(w-1,mid+1,h-2);hor(h-1,0,w-2)
 elif c in 'rn':
  ver(0);hor(0,2,w-2);dot(1,1);dot(w-1,1)
  if c=='n':ver(w-1,2)
 elif c=='m':
  middle=w//2;ver(0);hor(0,2,middle-2);dot(1,1);dot(middle-1,1);ver(middle,2);hor(0,middle+2,w-2);dot(middle+1,1);dot(w-1,1);ver(w-1,2)
 elif c=='I':hor(0);hor(h-1);ver(w//2)
 elif c=='l':ver(0);hor(h-1)
 elif c=='1':
  stem=w//2;dot(stem-1,1);dot(stem-2,2);ver(stem);hor(h-1)
 elif c=='א':
  mid=h//2;line(0,0,w-1,h-1);ver(w-1,0,mid-1);line(w-1,mid-1,w//2,mid);ver(0,mid+1);line(0,mid+1,w//2,mid)
 elif c=='ג':
  hor(0,max(0,w-3));ver(w-1);line(w-1,h//2,0,h-1)
 elif c in 'וזיןך':
  hor(0);ver(w//2 if c=='ז' else w-1)
 elif c=='ח':hor(0);ver(0);ver(w-1)
 elif c=='ט':
  ver(0,0,h-3);ver(w-1,1,h-3);hor(0,w//2+1,w-2);dot(w//2,1);hor(2,1,w//2);dot(1,h-2);dot(w-2,h-2);hor(h-1,2,w-3)
 elif c=='ל':
  top=max(2,h-lower);ver(0,0,top);hor(top);ver(w-1,top+1,h-3);dot(w-2,h-2);hor(h-1,1,w-3)
 elif c=='נ':hor(0);ver(w-1);hor(h-1)
 elif c=='ע':
  line(0,0,w//2,h-2);line(w-1,0,w-2,h//2);line(w-2,h//2,0,h-1)
 elif c in 'פף':
  hor(0);ver(w-1);ver(0,1,lower//2);hor(lower//2,0,w//2)
  if c=='פ':hor(h-1)
 elif c in 'צץ':
  mid=min(lower//2,h//2);line(0,0,w-1,lower-1 if c=='צ' else h-1);line(w-1,0,w//2,mid)
  if c=='צ':hor(h-1)
 elif c=='ק':
  hor(0);ver(w-1,1,h-4);ver(0,3)
 elif c=='ם':hor(0);hor(h-1);ver(0);ver(w-1)
 elif c=='ס':oval()
 elif c=='כ':
  hor(0,0,w-2);dot(w-1,1);ver(w-1,2,h-3);dot(w-1,h-2);hor(h-1,0,w-2)
 elif c in 'רד':
  hor(0);ver(w-1 if c=='ר' else w-2,1)
 elif c=='ב':hor(0,0,w-3);dot(w-2,1);ver(w-2,2);hor(h-1)
 elif c=='ה':hor(0);ver(w-1,1);ver(0,3)
 elif c=='ת':hor(0,1);ver(w-1,1);ver(1,1);hor(h-1,0,2)
 elif c=='מ':
  ver(0,0,1);dot(1,2);ver(1,3,h-2);dot(0,h-1);hor(0,3,w-2);dot(2,1);dot(2,2);ver(w-1,1);hor(h-1,w//2)
 elif c=='ש':
  mid=w//2;ver(0,0,h-3);ver(mid,0,h//2-1);ver(w-1,0,h-3);hor(h//2,0,mid-1);dot(w-2,h-2);dot(1,h-2);hor(h-1,2,w-3)
 else:raise ValueError(c)
 return dict(advance=a,x=x,y=old['y'],rows=[''.join('#' if (xx,yy) in p else '.' for xx in range(w)) for yy in range(h)])

out={'format':1,'name':'Ink Bitmap','license':'Derived from DejaVu 2.37; licenses/DejaVu-LICENSE.txt','design':'Explicit size-specific black/white grids; unchanged advances and baseline metrics','tables':{}}
for bank,file,prefix,bprefix,sizes in [('Screen','ReaderScreenFonts.h','ScreenGlyphs','ScreenBits',(14,16,18,20)),('UI','ReaderUIFonts.h','uiGlyphs','uiBits',(12,14,16,18))]:
 for size in sizes:
  old=read(file,prefix,bprefix,size);new={}
  for c in 'abcdefghijklmnopqrstuvwxyzI1אבגדהוזחטיךכלםמןנסעףפץצקרשת':new[f'{ord(c):04X}']={'character':c,**design(c,old[ord(c)],size)}
  out['tables'][bank+str(size)]=new
(r/'fonts/ink-bitmap-overrides.json').write_text(json.dumps(out,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Authored',sum(len(v) for v in out['tables'].values()),'explicit glyph grids across',len(out['tables']),'size/bank combinations')
