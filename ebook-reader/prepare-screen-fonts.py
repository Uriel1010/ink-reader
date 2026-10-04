"""Generate native one-bit screen/UI fonts. DejaVu license retained in licenses/."""
from font_raster import *
from bitmap_glyphs import apply
import ctypes
version=ctypes.c_uint(35)
assert freetype.FT_Property_Set(freetype.get_handle(),b'truetype',b'interpreter-version',ctypes.byref(version))==0
actual=ctypes.c_uint()
assert freetype.FT_Property_Get(freetype.get_handle(),b'truetype',b'interpreter-version',ctypes.byref(actual))==0 and actual.value==35
source=(root/'ReaderFonts.h').read_text();screen=(root/'ReaderScreenFonts.h').read_text();ui=(root/'ReaderUIFonts.h').read_text()
cps=sorted(parse(source,'glyphs',12));report={'mode':'custom_bitmap','interpreter':actual.value,'bitmap_resampling':False,'tables':[]}
for bank,sizes,previous,prefix,bprefix,filename in [('Screen',(14,16,18,20),screen,'ScreenGlyphs','ScreenBits','ReaderScreenFonts.h'),('UI',(12,14,16,18,20),ui,'uiGlyphs','uiBits','ReaderUIFonts.h')]:
 out=['#pragma once','// DejaVu Sans Condensed 2.37; custom per-size grids over native mono base; see fonts/ink-bitmap-overrides.json.']
 for size in sizes:
  old=parse(previous,prefix,size);name='DejaVuSansCondensed-Bold.ttf' if bank=='UI' and size==20 else 'DejaVuSansCondensed.ttf'
  lf=Raster(name);hf=Raster(name);fallback=Raster('NotoSansHebrew.ttf');fallback.setsize(size)
  lm=lf.match(old[ord('H')][2],'H',13 if size==12 else size);hm=hf.match(statistics.median(old[ord(c)][2] for c in 'אבדהחמםסרת'),'אבדהחמםסרת',size)
  text,info=emit(prefix,bprefix,cps,old,lf,hf,fallback,size,False);text,custom=apply(text,prefix,bprefix,size,bank);out.append(text);info.update(custom)
  report['tables'].append(dict(bank=bank,**info,latin=lm,hebrew=hm,xheight=lf.get(ord('x'))[1],line_pitch_changed=False))
 ordered=(14,16,18,20) if bank=='Screen' else (14,16,18,20,12)
 out+=['static const Glyph *'+('screenGlyphs' if bank=='Screen' else 'uiGlyphs')+'[]={'+','.join(prefix+str(s) for s in ordered)+'};','static const uint8_t *'+('screenBits' if bank=='Screen' else 'uiBits')+'[]={'+','.join(bprefix+str(s) for s in ordered)+'};']
 (root/filename).write_text('\n'.join(out)+'\n')
(root/'validation-results/screen-fonts-generation.json').write_text(json.dumps(report,indent=2)+'\n')
print('Generated 9 native monochrome tables, verified TrueType interpreter 35')
