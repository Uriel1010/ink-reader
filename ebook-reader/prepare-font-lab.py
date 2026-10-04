"""Reproducible one-bit on-panel comparisons; no private book content."""
from pathlib import Path
import io,json,re,sys,zlib,statistics,hashlib
root=Path(__file__).parent
(root/"validation-results").mkdir(parents=True,exist_ok=True)
# Uses installed freetype-py; no local development dependency directory.
import freetype
from PIL import Image,ImageDraw

def table(file,prefix,bits,size):
    source=(root/file).read_text()
    data=bytes(map(int,re.search(rf'{bits}{size}\[\] = \{{(.*?)\}};',source,re.S)[1].split(',')))
    result={}
    for entry in re.findall(r'\{([^{}]+)\}',re.search(rf'{prefix}{size}\[\] = \{{(.*?)\}};',source,re.S)[1]):
        cp,offset,w,h,a,x,y=map(int,entry.split(','));image=Image.new('1',(max(1,w),max(1,h)),0)
        for pos in range(w*h):image.putpixel((pos%w,pos//w),255 if data[offset+pos//8]&(128>>(pos%8)) else 0)
        result[cp]=(image,a,x,y)
    return lambda ch:result[ord(ch)]

current=table('ReaderFonts.h','glyphs','fontBits',18)
common=table('ReaderUIFonts.h','uiGlyphs','uiBits',12)
mono=freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_MONO
auto=mono|freetype.FT_LOAD_FORCE_AUTOHINT
gray=freetype.FT_LOAD_RENDER|freetype.FT_LOAD_TARGET_NORMAL
faces={}
def ftfont(name,flags,target,probe,weight=450):
    face=freetype.Face(io.BytesIO((root/'fonts'/name).read_bytes()))
    if face.has_multiple_masters:face.set_var_design_coords([weight if a.name=='Weight' else a.default for a in face.get_variation_info().axes])
    def glyph(ch):
        face.load_char(ch,flags);g=face.glyph;b=g.bitmap;im=Image.new('1',(max(1,b.width),max(1,b.rows)),0)
        for y in range(b.rows):
            for x in range(b.width):
                row=(y if b.pitch>=0 else b.rows-1-y)*abs(b.pitch)
                ink=(b.buffer[row+x//8]&(128>>(x%8))) if b.pixel_mode==freetype.FT_PIXEL_MODE_MONO else b.buffer[row+x]>=128
                if ink:im.putpixel((x,y),255)
        box=im.getbbox()
        if box:im=im.crop(box);left=g.bitmap_left+box[0];top=-g.bitmap_top+box[1]
        else:left=top=0
        return im,round(g.advance.x/64),left,top
    choices=[]
    for tick in range(64,256):
        size=tick/8
        face.set_char_size(0,tick*8,72,72);height=statistics.median(glyph(ch)[0].getbbox()[3] for ch in probe)
        choices.append((abs(height-target),size,height))
    error,size,height=min(choices)
    if error and not flags&freetype.FT_LOAD_NO_HINTING:
        return ftfont(name,flags|freetype.FT_LOAD_NO_HINTING,target,probe,weight)
    assert error==0,(name,target,min(choices))
    face.set_char_size(0,round(size*64),72,72)
    return glyph,{'font':name,'ppem':size,'ink_height':height,'flags':flags}

def pair(latin,hebrew):return lambda ch:hebrew(ch) if '\u0590'<=ch<='\u05ff' else latin(ch)
target_l=current('H')[0].getbbox()[3];target_h=statistics.median(current(ch)[0].getbbox()[3] for ch in 'אבדהחמםסרת')
styles=[('Noto current',current,{'production':True,'cap_height':target_l,'hebrew_height':target_h})]
for name,ln,hn,flags in [('Noto auto','NotoSansHebrew.ttf','NotoSansHebrew.ttf',auto),('Noto threshold','NotoSansHebrew.ttf','NotoSansHebrew.ttf',gray),('Alef unhinted','Alef-Regular.ttf','Alef-Regular.ttf',mono|freetype.FT_LOAD_NO_HINTING),('Charis / David','Charis-Regular.ttf','DavidLibre-Medium.ttf',auto),('Noto serif / David','NotoSerif-Regular.ttf','DavidLibre-Medium.ttf',mono)]:
    lf,ld=ftfont(ln,flags,target_l,'H');hf,hd=ftfont(hn,flags,target_h,'אבדהחמםסרת');styles.append((name,pair(lf,hf),{'latin':ld,'hebrew':hd}))

bdf={}
for block in (root/'fonts/Cozette.bdf').read_text().split('STARTCHAR ')[1:]:
    cp=int(re.search(r'ENCODING (-?\d+)',block)[1]);a=int(re.search(r'DWIDTH (\d+)',block)[1]);w,h,x,y=map(int,re.search(r'BBX ([-\d ]+)',block)[1].split())
    rows=re.search(r'BITMAP\n(.*?)\nENDCHAR',block,re.S)[1].splitlines();im=Image.new('1',(max(1,w),max(1,h)),0)
    for yy,row in enumerate(rows):
        value=int(row,16)
        for xx in range(w):
            if value&(1<<(len(row)*4-1-xx)):im.putpixel((xx,yy),255)
    box=im.getbbox()
    if box:im=im.crop(box);x+=box[0];y=-y-h+box[1]
    else:y=0
    bdf[cp]=(im,a,x,y)
uis=[]
for label,font,flags in [('Atkinson matched','AtkinsonHyperlegibleNext.ttf',auto),('Noto matched','NotoSansHebrew.ttf',mono),('Alef matched','Alef-Regular.ttf',auto)]:
    lf,ld=ftfont(font,flags,8,'H',500);hf,hd=ftfont('Alef-Regular.ttf',auto,8,'אבדהחמםסרת');uis.append((label,pair(lf,hf),{'latin':ld,'hebrew':hd}))
hf,hd=ftfont('Alef-Regular.ttf',auto,8,'אבדהחמםסרת')
uis.append(('Cozette bitmap',pair(lambda ch:bdf[ord(ch)],hf),{'latin':'Cozette 1.30.0 native 8px capitals','hebrew':hd}))
english=['The room was quiet and bright.','A small book lay on the table.','I opened it and began to read.']
hebrew=['החדר היה שקט ומלא אור.','ספר קטן חיכה על השולחן.','פתחתי אותו והתחלתי לקרוא.']
report={'reading_targets':{'capital':target_l,'hebrew_median':target_h,'line_pitch':21},'reading':[],'ui':[],'frames':[]}
out=['#pragma once','#include <stdint.h>','#include <stddef.h>','// Generated public samples; see FONT-COMPARISON.md and font licenses.','struct FontLabFrame {const uint8_t *data;size_t length,decoded;uint32_t crc;};']
descriptors=[]
preview=root/'validation-results/font-comparison';preview.mkdir(parents=True,exist_ok=True)
for bank,variants in enumerate((styles,uis)):
    for index,(name,font,metadata) in enumerate(variants):
        report['ui' if bank else 'reading'].append({'name':name,**metadata})
        for width,height in ((400,300),(300,400)):
            image=Image.new('1',(width,height),255);draw=ImageDraw.Draw(image);geometry=[]
            def text(s,x,y,getfont=font,fixed=False,rtl=False):
                cells=[]
                for ch in (s[::-1] if rtl else s):
                    im,a,left,top=getfont(ch);ink=im.getbbox()
                    if fixed:
                        a=current(ch)[1]
                        if ink and im.width>a and a>0:im=im.convert('L').resize((a,im.height),Image.Resampling.BOX).point(lambda v:255 if v>=128 else 0,'1')
                        left=max(0,min(left,a-im.width)) if a else left
                    cells.append((ch,im,a,left,top))
                total=sum(c[2] for c in cells)
                if rtl:x=width-18-total
                assert x>=0 and x+total<=width-8,(name,s,total,width)
                geometry.append({'text':s,'baseline':y,'width':total})
                for ch,im,a,left,top in cells:
                    if im.getbbox():
                        assert 0<=x+left and x+left+im.width<=width and 0<=y+top and y+top+im.height<height
                        image.paste(0,(x+left,y+top),im)
                    x+=a
            text(('Reading' if not bank else 'Interface')+f' {index+1}/{len(variants)}: '+name,18,24,common)
            text('Same height and spacing' if not bank else 'Matched 8px capitals / native pixels',18,44,common)
            draw.line((18,55,width-18,55),fill=0)
            shift=25 if height==400 else 0
            if not bank:
                for row,s in enumerate(english):text(s,18,85+shift+21*row,fixed=True)
                for row,s in enumerate(hebrew):text(s,18,172+shift+21*row,fixed=True,rtl=True)
            else:
                for row,s in enumerate(['Library','Reading settings','Text size: 18 px','Bookmarks and chapters','1 I l i   0 O   5 S   8 B','Saved automatically']):
                    y=83+shift+23*row;draw.rounded_rectangle((18,y-15,width-18,y+6),radius=3,outline=0);text(s,28,y)
                text('הגדרות קריאה',18,244+shift,rtl=True)
            draw.line((18,height-38,width-18,height-38),fill=0)
            text('Up/Down: style   Center: baseline',18,height-22,common)
            text('Menu: reading / UI   Exit: return',18,height-7,common)
            raw=image.tobytes();compressed=zlib.compress(raw,9);ident=f'fontLab{len(descriptors)}'
            assert len(raw)==((width+7)//8)*height and zlib.decompress(compressed)==raw
            crc=zlib.crc32(raw);out.append(f'static const uint8_t {ident}[]={{'+','.join(map(str,compressed))+'};');descriptors.append(f'{{{ident},sizeof({ident}),{len(raw)},{crc}u}}')
            filename=f'{"ui" if bank else "reading"}-{index+1}-{width}.png';image.save(preview/filename)
            report['frames'].append({'name':name,'bank':bank,'index':index,'width':width,'height':height,'crc':crc,'sha256':hashlib.sha256(raw).hexdigest(),'geometry':geometry})
out.append('static const FontLabFrame fontLabFrames[]={'+','.join(descriptors)+'};')
(root/'ReaderFontLabFrames.h').write_text('\n'.join(out)+'\n')
(root/'validation-results/font-comparison.json').write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8')
print('Generated',len(descriptors),'CRC-checked comparison frames; production font tables unchanged')
