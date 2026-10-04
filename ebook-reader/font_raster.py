"""Pixel-grid hinted monochrome rasterization, without bitmap stretching."""
from pathlib import Path
import io,re,sys,statistics,json
root=Path(__file__).parent
(root/"validation-results").mkdir(parents=True,exist_ok=True)
# Uses installed freetype-py; no local development dependency directory.
import freetype

THRESHOLD=102 # Used only if a fallback face unexpectedly returns coverage pixels.
FLAGS=freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO|freetype.FT_LOAD_MONOCHROME
ADJUSTMENTS=set()
def counter_count(ink,w,h):
 unseen={(x,y) for y in range(h) for x in range(w)}-ink;count=0
 while unseen:
  stack=[unseen.pop()];edge=False
  while stack:
   x,y=stack.pop();edge|=x in (0,w-1) or y in (0,h-1)
   for pt in ((x-1,y),(x+1,y),(x,y-1),(x,y+1)):
    if pt in unseen:unseen.remove(pt);stack.append(pt)
  count+=not edge
 return count
def parse(source,prefix,size):
 return {int(v.split(',')[0]):tuple(map(int,v.split(',')[1:])) for v in re.findall(r'\{([^{}]+)\}',re.search(rf'{prefix}{size}\[\] = \{{(.*?)\}};',source)[1])}
class Raster:
 def __init__(self,name,weight=450):
  self.face=freetype.Face(io.BytesIO((root/'fonts'/name).read_bytes()));self.name=name;self.size=16
  if self.face.has_multiple_masters:self.face.set_var_design_coords([weight if a.name=='Weight' else a.default for a in self.face.get_variation_info().axes])
 def setsize(self,size):self.size=size;self.face.set_char_size(0,round(size*64),72,72)
 def get(self,cp):
  flags=(freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO) if ismark(cp) else FLAGS
  self.face.load_char(chr(cp),flags);g=self.face.glyph;b=g.bitmap
  def mask(threshold):
   ink=set()
   for y in range(b.rows):
    row=(y if b.pitch>=0 else b.rows-1-y)*abs(b.pitch)
    for x in range(b.width):
     covered=(b.buffer[row+x//8]&(128>>(x%8))) if b.pixel_mode==freetype.FT_PIXEL_MODE_MONO else b.buffer[row+x]>=threshold
     if covered:ink.add((x,y))
   return ink
  ink=mask(THRESHOLD)
  required=2 if chr(cp) in 'B8' else 1 if chr(cp) in 'abdo pqADOPQR069םס'.replace(' ','') else 0
  if required and counter_count(ink,b.width,b.rows)<required:
   # Preserve the font's bowl topology using its original coverage samples.
   # No synthetic pixels or horizontal/vertical resizing are introduced.
   for threshold in (96,88,80,72,64,120,128,140,153):
    candidate=mask(threshold)
    if counter_count(candidate,b.width,b.rows)>=required:
     ink=candidate;ADJUSTMENTS.add((self.name,cp,self.size,threshold));break
  if not ink:return 0,0,round(g.advance.x/64),0,0,[]
  x0=min(x for x,y in ink);y0=min(y for x,y in ink);x1=max(x for x,y in ink)+1;y1=max(y for x,y in ink)+1
  w,h=x1-x0,y1-y0
  return w,h,round(g.advance.x/64),g.bitmap_left+x0,-g.bitmap_top+y0,[int((x+x0,y+y0) in ink) for y in range(h) for x in range(w)]
 def match(self,target,sample,nominal):
  options=[]
  for tick in range(max(64,(nominal-6)*8),(nominal+9)*8):
   size=tick/8;self.setsize(size);height=statistics.median(self.get(ord(ch))[1] for ch in sample)
   options.append((abs(height-target),abs(size-nominal),size,height))
  error,_,size,height=min(options);self.setsize(size)
  assert error<=0.5,(self.name,target,height)
  return {'font':self.name,'raster_size':size,'ink_height':height,'target_height':target}

def ismark(cp):return 0x591<=cp<=0x5bd or cp in (0x5bf,0x5c1,0x5c2,0x5c4,0x5c5,0x5c7)
def base(cp):return 65<=cp<=90 or 97<=cp<=122 or 0x5d0<=cp<=0x5ea
def emit(prefix,bprefix,cps,previous,latin,hebrew,fallback,size,reading):
 bits=[];glyphs=[];changes=0;growth=0
 for cp in cps:
  raster=hebrew if 0x590<=cp<=0x5ff else latin
  if ismark(cp):raster=fallback
  if not raster.face.get_char_index(cp):raster=fallback
  w,h,natural,x,y,pixels=raster.get(cp)
  a=previous[cp][3] if reading else natural
  if ismark(cp):a=0
  if (0x200b<=cp<=0x200f) or (0x202a<=cp<=0x202e) or (0x2066<=cp<=0x2069):w=h=0;pixels=[]
  if base(cp):
   assert pixels and any(pixels),(prefix,size,cp)
   # Keep the original advance when possible. Widen a cell instead of destroying
   # an already-rasterized outline; all ink remains the original coverage mask.
   a=max(a,w);x=max(0,min(x,a-w))
  if a!=previous[cp][3]:changes+=1;growth+=a-previous[cp][3]
  glyphs.append((cp,len(bits),w,h,a,x,y))
  bits.extend(sum((128>>j) for j in range(8) if n+j<len(pixels) and pixels[n+j]) for n in range(0,len(pixels),8))
 assert all(0<=w<=255 and 0<=h<=255 and 0<=a<=255 and -128<=x<=127 and -128<=y<=127 for cp,o,w,h,a,x,y in glyphs)
 text=f'static const uint8_t {bprefix}{size}[] = {{'+','.join(map(str,bits))+'};\n'
 text+=f'static const Glyph {prefix}{size}[] = {{'+','.join('{'+','.join(map(str,g))+'}' for g in glyphs)+'};\n'
 return text,{'size':size,'changed_advances':changes,'total_advance_delta':growth,'glyphs':len(cps),'bitmap_bytes':len(bits)}

def generate(which='all'):
 source=(root/'ReaderFonts.h').read_text();family=(root/'ReaderFontFamilies.h').read_text();ui=(root/'ReaderUIFonts.h').read_text()
 cps=sorted(parse(source,'glyphs',12));report={'mode':'hinted_monochrome','fallback_threshold':THRESHOLD,'flags':FLAGS,'bitmap_resampling':False,'tables':[]}
 if which in ('all','noto'):
  out=['#pragma once','#include <stdint.h>','static const int fontProfile = 12;','struct Glyph { uint32_t cp; uint32_t offset; uint8_t w,h,advance; int8_t x,y; };']
  for size in (12,14,16,18,20):
   old=parse(source,'glyphs',size);lf=Raster('NotoSansHebrew.ttf',500);hf=Raster('NotoSansHebrew.ttf',500)
   lm=lf.match(old[ord('H')][2],'H',size);hm=hf.match(statistics.median(old[ord(ch)][2] for ch in 'אבדהחמםסרת'),'אבדהחמםסרת',size)
   text,info=emit('glyphs','fontBits',cps,old,lf,hf,lf,size,True);out.append(text);report['tables'].append({'table':'Noto',**info,'latin':lm,'hebrew':hm})
  out.append(f'static const int glyphCount = {len(cps)};');(root/'ReaderFonts.h').write_text('\n'.join(out)+'\n')
 if which in ('all','families'):
  out=['#pragma once','// Generated by font_raster.py; retained OFL notices in licenses/.']
  for name,latin,hebrew in [('Alef','Alef-Regular.ttf','Alef-Regular.ttf'),('Book','Charis-Regular.ttf','DavidLibre-Medium.ttf')]:
   for size in (14,16,18,20):
    old=parse(family,name+'Glyphs',size);lf=Raster(latin);hf=Raster(hebrew);fallback=Raster('NotoSansHebrew.ttf');fallback.setsize(size)
    lm=lf.match(old[ord('H')][2],'H',size);hm=hf.match(statistics.median(old[ord(ch)][2] for ch in 'אבדהחמםסרת'),'אבדהחמםסרת',size)
    text,info=emit(name+'Glyphs',name+'Bits',cps,old,lf,hf,fallback,size,True);out.append(text);report['tables'].append({'table':name,**info,'latin':lm,'hebrew':hm})
  out.extend(['static const Glyph *familyGlyphs[2][4]={{AlefGlyphs14,AlefGlyphs16,AlefGlyphs18,AlefGlyphs20},{BookGlyphs14,BookGlyphs16,BookGlyphs18,BookGlyphs20}};','static const uint8_t *familyBits[2][4]={{AlefBits14,AlefBits16,AlefBits18,AlefBits20},{BookBits14,BookBits16,BookBits18,BookBits20}};']);(root/'ReaderFontFamilies.h').write_text('\n'.join(out)+'\n')
 if which in ('all','ui'):
  out=['#pragma once','// Atkinson / Alef, generated by font_raster.py; see OFL notices.']
  for size in (12,14,16,18,20):
   old=parse(ui,'uiGlyphs',size);lf=Raster('DejaVuSansCondensed-Bold.ttf' if size==20 else 'DejaVuSansCondensed.ttf');hf=Raster('DejaVuSansCondensed-Bold.ttf' if size==20 else 'DejaVuSansCondensed.ttf');fallback=Raster('NotoSansHebrew.ttf');fallback.setsize(size)
   lm=lf.match(old[ord('H')][2],'H',13 if size==12 else size);hm=hf.match(statistics.median(old[ord(ch)][2] for ch in 'אבדהחמםסרת'),'אבדהחמםסרת',size)
   text,info=emit('uiGlyphs','uiBits',cps,old,lf,hf,fallback,size,False);out.append(text);report['tables'].append({'table':'UI',**info,'latin':lm,'hebrew':hm})
  out.extend(['static const Glyph *uiGlyphs[]={uiGlyphs14,uiGlyphs16,uiGlyphs18,uiGlyphs20,uiGlyphs12};','static const uint8_t *uiBits[]={uiBits14,uiBits16,uiBits18,uiBits20,uiBits12};']);(root/'ReaderUIFonts.h').write_text('\n'.join(out)+'\n')
 if which=='all':
  report['counter_calibration']=sorted(ADJUSTMENTS)
  (root/'validation-results/production-fonts-generation.json').write_text(json.dumps(report,indent=2)+'\n')
 print('Generated',which,'hinted monochrome fonts, without bitmap resizing')
if __name__=='__main__':generate()
