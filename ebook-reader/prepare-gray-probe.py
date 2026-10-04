"""Public calibration asset: solid gray patches and genuine 2-bit text edges."""
from pathlib import Path
import io,re,sys,zlib,json
root=Path(__file__).parent
(root/"validation-results").mkdir(parents=True,exist_ok=True)
# Uses installed freetype-py; no local development dependency directory.
import freetype
from PIL import Image,ImageDraw
vendor=(root/'licenses/Waveshare-4in2-V2-reference.txt').read_text()
notice=vendor[vendor.index(' *  Copyright'):vendor.index(' */')].replace(' * ','').strip()
(root/'licenses/Waveshare-4in2-V2-MIT.txt').write_text(notice+'\n')
lut=re.search(r'const unsigned char LUT_ALL\[233\]\s*=\s*\{(.*?)\};',vendor,re.S)[1]
lut=re.sub(r'/\*.*?\*/|//[^\n]*','',lut,flags=re.S)
values=[int(v.strip(),0) for v in lut.split(',') if v.strip()]
assert len(values)==233
out=['#pragma once','#include <stdint.h>','// Waveshare SSD1683 V2 calibration waveform, MIT notice retained.','static const uint8_t grayProbeLut[233]={'+','.join(map(str,values))+'};']
report=[]
for width,height in ((400,300),(300,400)):
 im=Image.new('L',(width,height),255);draw=ImageDraw.Draw(im)
 def text(s,x,baseline,size=14,face='AtkinsonHyperlegibleNext.ttf',rtl=False):
  ft=freetype.Face(io.BytesIO((root/'fonts'/face).read_bytes()))
  if ft.has_multiple_masters:ft.set_var_design_coords([450 if a.name=='Weight' else a.default for a in ft.get_variation_info().axes])
  ft.set_char_size(0,round(size*64),72,72);glyphs=[]
  for ch in s[::-1] if rtl else s:
   ft.load_char(ch,freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_NORMAL);g=ft.glyph;b=g.bitmap
   mask=Image.new('L',(max(1,b.width),max(1,b.rows)),0)
   if b.width and b.rows:mask.putdata([b.buffer[y*abs(b.pitch)+xx] for y in range(b.rows) for xx in range(b.width)])
   glyphs.append((mask,round(g.advance.x/64),g.bitmap_left,-g.bitmap_top))
  total=sum(g[1] for g in glyphs)
  if rtl:x=width-18-total
  assert x>=0 and x+total<=width-12,(s,total,width)
  for mask,a,left,top in glyphs:im.paste(0,(x+left,baseline+top),mask);x+=a
 text('Gray-level calibration',18,26,16)
 text('4 solid shades, no dithering',18,47,13)
 patch=(width-48)//4
 for n,label in enumerate(('WHITE','LIGHT','DARK','BLACK')):
  x=18+n*(patch+4);draw.rectangle((x,65,x+patch-1,119),fill=255-85*n,outline=0)
  text(label,x,139,11)
 yy=178 if height==300 else 205
 text('The morning light fills the room.',18,yy,16.5,'Charis-Regular.ttf')
 text('A quiet story begins here.',18,yy+25,16.5,'Charis-Regular.ttf')
 text('שלום עולם, סיפור שקט מתחיל.',18,yy+57,18,'DavidLibre-Medium.ttf',True)
 text('Any button: return to book',18,height-17,13)
 levels=[(value*3+127)//255 for value in im.getdata()]
 packed=bytes(sum(levels[i+j]<<(6-2*j) for j in range(4)) for i in range(0,len(levels),4))
 assert len(packed)==30000 and set(levels)=={0,1,2,3}
 compressed=zlib.compress(packed,9);crc=zlib.crc32(packed)
 out.append(f'static const uint8_t grayProbe{width}[]={{'+','.join(map(str,compressed))+'};')
 out.append(f'static const uint32_t grayProbeCRC{width}={crc}u;')
 preview=Image.new('L',(width,height));preview.putdata([v*85 for v in levels]);preview.save(root/f'validation-results/gray-probe-{width}.png')
 report.append({'width':width,'height':height,'bytes':len(packed),'crc32':crc,'shades':4})
(root/'ReaderGrayProbeAssets.h').write_text('\n'.join(out)+'\n')
(root/'validation-results/gray-probe-assets.json').write_text(json.dumps(report,indent=2)+'\n')
print('Generated two four-level calibration frames and exact vendor LUT')
