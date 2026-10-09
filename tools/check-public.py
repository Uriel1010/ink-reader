"""Audit tracked public files. Reports file names, never matched secret values."""
from pathlib import Path
import re, subprocess, sys
root=Path(__file__).resolve().parents[1]
paths=subprocess.check_output(['git','ls-files','-z'],cwd=root).decode().split('\0')
if not any(paths):
 print('FAIL: no tracked files to audit. Stage the export first.');sys.exit(1)
bad=[]
patterns=[rb'gh[pousr]_[A-Za-z0-9]{30,}',rb'github_pat_[A-Za-z0-9_]{40,}',rb'AKIA[A-Z0-9]{16}',rb'-----BEGIN (?:RSA |EC |OPENSSH )?PRIVATE KEY-----',rb'[A-Z]:[\\/]Users[\\/][^\\/\r\n]+',rb'OneDrive[\\/]',rb'(?i)(?:ssid|password)\s*=\s*"(?!"|password|ssid)[^"\n]{8,}"']
for name in filter(None,paths):
 p=root/name
 if name.startswith('.crowreader/') or '/serial-' in name or 'validation-results/' in name or name.endswith(('.bin','.zip','.pem','.key')) or (name.endswith(('.epub','.pdf')) and not name.startswith('ebook-reader/test-corpus/')):
  bad.append((name,'prohibited artifact'))
 if name in {'ebook-reader/'+x for x in ['EPD.cpp','EPD.h','EPD_GUI.cpp','EPD_GUI.h','EPD_SPI.cpp','EPD_SPI.h','EPD_font.h']}:
  bad.append((name,'unlicensed vendor source'))
 if p.is_file() and p.suffix not in {'.png','.jpg','.jpeg','.ttf','.otf','.epub','.pdf'} and name!='tools/check-public.py':
  data=p.read_bytes()
  for pat in patterns:
   if re.search(pat,data):bad.append((name,'secret/private-path pattern'));break
for name,reason in bad:print(f'FAIL {name}: {reason}')
print(f'Audited {len(list(filter(None,paths)))} tracked files; {len(bad)} findings.')
sys.exit(bool(bad))
