"""Create original, redistributable QA books/images; never copy user books."""
from pathlib import Path
import hashlib, json, re, zipfile
from PIL import Image, ImageDraw
root=Path(__file__).parent
out=root/'test-corpus';out.mkdir(parents=True,exist_ok=True)
def fixture(name):
    text=(root/'ImageFixtures.h').read_text()
    return bytes(map(int,re.search(name+r'\[\]=\{([^}]+)\}',text)[1].split(',')))
english='A quiet morning brought a new chapter. Clear text, balanced spaces, and a steady reading rhythm matter more than decoration. '
hebrew='הבוקר התחיל בשקט. הספר נשאר פתוח בדיוק במקום שבו עצרנו. קריאה נעימה דורשת אותיות ברורות ורווחים מאוזנים. '
mixed='שלום world 123 — (English) עברית! שָׁלוֹם; 2026 / 42. '
def epub(name,version,rtl):
    language='he' if rtl else 'en';title='ספר בדיקה מקורי' if rtl else 'Original reading test'
    chapters=[]
    for index in range(3):
        paragraphs=''.join(f'<p>{(hebrew if rtl else english)*5}</p><p dir="rtl">{mixed*3}</p><p dir="ltr">English 123 inside a Hebrew book.</p>' for _ in range(12))
        chapters.append(f'<?xml version="1.0" encoding="utf-8"?><html xmlns="http://www.w3.org/1999/xhtml" dir="{"rtl" if rtl else "ltr"}"><head><title>Chapter {index+1}</title></head><body><h1 id="start">Chapter {index+1}</h1>{paragraphs}<h2 id="details">Details</h2><ul><li>First item</li><li>פריט שני</li></ul><blockquote>A passage set apart from the main text.</blockquote><img src="image.png" alt="Transparent picture"/><p>{hebrew*4}</p><img src="progressive.jpg" alt="Progressive JPEG"/></body></html>')
    manifest=''.join(f'<item id="c{i}" href="c{i}.xhtml" media-type="application/xhtml+xml"/>' for i in range(3))
    nav='<html xmlns="http://www.w3.org/1999/xhtml" xmlns:epub="http://www.idpf.org/2007/ops"><body><nav epub:type="toc"><ol>'+''.join(f'<li><a href="c{i}.xhtml#start">Chapter {i+1}</a><ol><li><a href="c{i}.xhtml#details">Details</a></li></ol></li>' for i in range(3))+'</ol></nav></body></html>'
    ncx='<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><navMap>'+''.join(f'<navPoint id="p{i}" playOrder="{i+1}"><navLabel><text>Chapter {i+1}</text></navLabel><content src="c{i}.xhtml#start"/></navPoint>' for i in range(3))+'</navMap></ncx>'
    navigation='<item id="nav" href="nav.xhtml" media-type="application/xhtml+xml" properties="nav"/>' if version==3 else '<item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>'
    package=f'<package xmlns="http://www.idpf.org/2007/opf" version="{version}.0" unique-identifier="uid"><metadata xmlns:dc="http://purl.org/dc/elements/1.1/"><dc:identifier id="uid">inkreader-original-{version}</dc:identifier><dc:title>{title}</dc:title><dc:creator>Ink Reader QA</dc:creator><dc:language>{language}</dc:language></metadata><manifest>{manifest}{navigation}<item id="img" href="image.png" media-type="image/png"/><item id="jpg" href="progressive.jpg" media-type="image/jpeg"/></manifest><spine toc="ncx">'+''.join(f'<itemref idref="c{i}"/>' for i in range(3))+'</spine></package>'
    with zipfile.ZipFile(out/name,'w',compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr('mimetype','application/epub+zip',compress_type=zipfile.ZIP_STORED)
        z.writestr('META-INF/container.xml','<container xmlns="urn:oasis:names:tc:opendocument:xmlns:container" version="1.0"><rootfiles><rootfile full-path="OPS/book.opf" media-type="application/oebps-package+xml"/></rootfiles></container>')
        z.writestr('OPS/book.opf',package);z.writestr('OPS/nav.xhtml',nav);z.writestr('OPS/toc.ncx',ncx)
        for i,chapter in enumerate(chapters):z.writestr(f'OPS/c{i}.xhtml',chapter)
        z.writestr('OPS/image.png',fixture('fixtureAlphaPng'));z.writestr('OPS/progressive.jpg',fixture('fixtureProgressiveJpeg'))
epub('English EPUB2.epub',2,False);epub('עברית EPUB3.epub',3,True)
text=('Literal <h1> &amp; [[IMG:not-an-image.png]] stays text.\r\n'+english+'\r\n'+hebrew+'\r\n'+mixed+'\r\n\r\n')*50
for name,encoding in [('Mixed UTF8.txt','utf-8-sig'),('עברית UTF16LE.txt','utf-16'),('Mixed UTF16BE.txt','utf-16-be')]:
    data=text.encode(encoding);data=(b'\xfe\xff'+data) if encoding=='utf-16-be' else data;(out/name).write_bytes(data)
for name,key in [('Progressive.jpg','fixtureProgressiveJpeg'),('Transparent.png','fixtureAlphaPng'),('Gray16.png','fixtureWidePng'),('Oversized.png','fixtureOversizedPng')]: (out/name).write_bytes(fixture(key))
(out/'Corrupt.png').write_bytes(b'not an image')
im=Image.new('RGB',(400,300),'white');d=ImageDraw.Draw(im)
for x in range(400):d.line((x,0,x,119),fill=(x*255//399,)*3)
for x in range(10,391,10):d.line((x,140,x,280),fill='black',width=1)
im.save(out/'Fine lines.png')
manifest={'origin':'Original generated QA text and fixture images; third-party decoder notices remain in their source files','files':[]}
for p in sorted(out.iterdir()):
    if p.name=='manifest.json' or not p.is_file():continue
    data=p.read_bytes();manifest['files'].append({'name':p.name,'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()})
(out/'manifest.json').write_text(json.dumps(manifest,ensure_ascii=False,indent=2),encoding='utf-8')
print(f'Created {len(manifest["files"])} QA files in {out.name}')
