"""Regenerate the bundled offline renderer from the pinned, hash-verified PDF.js archive."""
from pathlib import Path
import argparse,gzip,hashlib,json,urllib.request,tarfile
root=Path(__file__).resolve().parent
parser=argparse.ArgumentParser();parser.add_argument('--archive',type=Path,default=root/'validation-fixtures/pdfjs/pdfjs.tgz');parser.add_argument('--download',action='store_true');args=parser.parse_args()
manifest=json.loads((root/'pdf-assets-manifest.json').read_text(encoding='utf-8'))
if args.download:
 args.archive.parent.mkdir(parents=True,exist_ok=True)
 with urllib.request.urlopen(manifest['source'],timeout=60) as response:args.archive.write_bytes(response.read())
if not args.archive.exists():raise SystemExit('Pass --archive /path/to/pdfjs.tgz or --download. Normal firmware builds need neither.')
if hashlib.sha256(args.archive.read_bytes()).hexdigest()!=manifest['archive_sha256']:raise SystemExit('PDF.js archive checksum mismatch')
version='/pdfjs/'+manifest['version']+'/'
lines=['// Generated from PDF.js '+manifest['version']+'; see licenses/PDFjs-*.txt and pdf-assets-manifest.json.','#include "ReaderPdfAssets.h"','#include <Arduino.h>']
with tarfile.open(args.archive) as archive:
 for i,entry in enumerate(manifest['assets']):
  relative=entry['path'].removeprefix(version)
  upstream='build/'+relative.replace('.mjs','.min.mjs') if relative in ['pdf.mjs','pdf.worker.mjs'] else relative
  with archive.extractfile('package/'+upstream) as stream:data=stream.read()
  if hashlib.sha256(data).hexdigest()!=entry['sha256']:raise SystemExit('PDF.js asset checksum mismatch: '+relative)
  compressed=gzip.compress(data,9,mtime=0)
  if len(compressed)!=entry['gzip_bytes']:raise SystemExit('Compressed size changed: check Python/zlib version for '+relative)
  lines.append(f'static const uint8_t asset{i}[] PROGMEM={{'+','.join(map(str,compressed))+'};')
 for member in archive.getmembers():
  parts=member.name.split('/')
  if not member.isfile() or len(parts) not in [2,3] or not parts[-1].startswith('LICENSE'):continue
  group='main' if len(parts)==2 else parts[-2]
  if group not in ['main','wasm','standard_fonts','cmaps']:continue
  with archive.extractfile(member) as stream:(root/'licenses'/('PDFjs-'+group+'-'+parts[-1]+'.txt')).write_bytes(stream.read())
lines.append('const ReaderPdfAsset readerPdfAssets[]={')
for i,entry in enumerate(manifest['assets']):lines.append('{"'+entry['path']+'","'+entry['mime']+'",asset'+str(i)+',sizeof(asset'+str(i)+')},')
lines+=['};','const size_t readerPdfAssetCount=sizeof(readerPdfAssets)/sizeof(readerPdfAssets[0]);']
(root/'ReaderPdfAssets.cpp').write_text('\n'.join(lines)+'\n',encoding='utf-8')
source=(root/'pdf-import.js').read_text(encoding='utf-8-sig')
(root/'ReaderPdfImport.h').write_text('#pragma once\nstatic const char readerPdfImport[]=R"INKPDF('+source+')INKPDF";\n',encoding='utf-8')
print('Regenerated verified offline PDF assets and import script.')
