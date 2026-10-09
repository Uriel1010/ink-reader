"""Rebuild the original PDF import fixture (reportlab, pypdf, Pillow)."""
from pathlib import Path
from io import BytesIO
from reportlab.pdfgen import canvas
from pypdf import PdfReader,PdfWriter
from pypdf.generic import NameObject,NumberObject,DictionaryObject,DecodedStreamObject,EncodedStreamObject
from PIL import Image,ImageDraw
root=Path(__file__).resolve().parents[1]
raw=BytesIO();c=canvas.Canvas(raw,pagesize=(420,600),invariant=1);c.setTitle('Original manga-style PDF test');c.setAuthor('Ink Reader tests')
for page in range(5):
 w,h=(840,600) if page==1 else (420,600);c.setPageSize((w,h))
 if page<3:
  c.setLineWidth(2);c.setFont('Helvetica-Bold',16);c.drawString(25,h-32,'ORIGINAL PDF TEST - PAGE '+str(page+1))
  for side in range(2 if page==1 else 1):
   x=20+side*420;c.rect(x,60,380,470);c.setFont('Helvetica-Bold',24);c.drawString(x+30,470,('LEFT' if side==0 else 'RIGHT') if page==1 else 'A quiet story')
   c.circle(x+190,300,65);c.line(x+190,235,x+190,150);c.line(x+190,210,x+120,175);c.line(x+190,210,x+260,175);c.setFont('Helvetica',16);c.drawString(x+30,110,'The page remembers its place.')
   for k in range(6):c.setLineWidth(.5+k*.35);c.line(x+30,440-k*12,x+350,440-k*12)
 c.showPage()
c.save();writer=PdfWriter();writer.append(PdfReader(raw));writer.add_metadata({'/Title':'Original manga-style PDF test','/Author':'Ink Reader tests'})
for number,kind in [(3,'JPEG2000'),(4,'JPEG')]:
 im=Image.new('RGB',(480,720),'white');d=ImageDraw.Draw(im);d.rectangle((20,20,459,699),outline='black',width=4);d.text((40,45),'ORIGINAL SCANNED PDF TEST',fill='black',font_size=22);d.text((40,90),kind,fill='black',font_size=24);d.ellipse((80,170,400,490),outline='black',width=5);d.line((80,330,400,330),fill='black',width=3);d.rectangle((80,550,400,620),fill='#80a0d0');d.text((95,570),'Colors become monochrome',fill='black',font_size=18)
 data=BytesIO();im.save(data,kind,**({'progressive':True,'quality':90} if kind=='JPEG' else {}))
 stream=EncodedStreamObject();stream._data=data.getvalue();stream.update({NameObject('/Type'):NameObject('/XObject'),NameObject('/Subtype'):NameObject('/Image'),NameObject('/Width'):NumberObject(480),NameObject('/Height'):NumberObject(720),NameObject('/ColorSpace'):NameObject('/DeviceRGB'),NameObject('/BitsPerComponent'):NumberObject(8),NameObject('/Filter'):NameObject('/JPXDecode' if kind=='JPEG2000' else '/DCTDecode')})
 page=writer.pages[number];page[NameObject('/Resources')]=DictionaryObject({NameObject('/XObject'):DictionaryObject({NameObject('/Scan'):writer._add_object(stream)})});content=DecodedStreamObject();content.set_data(b'q 360 0 0 540 30 30 cm /Scan Do Q');page[NameObject('/Contents')]=writer._add_object(content)
 if kind=='JPEG':page[NameObject('/Rotate')]=NumberObject(90)
with open(root/'test-corpus/Manga demo.pdf','wb') as out:writer.write(out)
print('Created five-page original fixture: vector, wide spread, JPEG2000, rotated progressive JPEG.')

import json,hashlib
manifest=root/"test-corpus/manifest.json"
if manifest.exists():
 doc=json.loads(manifest.read_text(encoding="utf-8-sig"));pdf=root/"test-corpus/Manga demo.pdf";doc["files"]=[e for e in doc["files"] if e["name"]!=pdf.name]+[dict(name=pdf.name,bytes=pdf.stat().st_size,sha256=hashlib.sha256(pdf.read_bytes()).hexdigest())];manifest.write_text(json.dumps(doc,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
