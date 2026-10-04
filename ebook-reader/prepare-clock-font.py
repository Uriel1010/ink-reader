"""Regenerate the optional clock's date font from the included OFL font."""
from pathlib import Path
import zlib
from PIL import Image, ImageDraw, ImageFont

root = Path(__file__).parent
# Larger outline master avoids magnifying stair steps when fitting date widths.
font = ImageFont.truetype(str(root / 'fonts/NotoSansHebrew.ttf'), 64)
font.set_variation_by_axes([400,100])
bits, glyphs = [], []
characters = ''.join(sorted(set('JanuaryFebruaryMarchAprilMayJuneJulyAugustSeptemberOctoberNovemberDecember0123456789, Setting clockTime unavailableAny button: returnMenu: retry   Exit: return?')))
for ch in characters:
    x0, y0, x1, y1 = font.getbbox(ch, anchor='ls')
    width, height = x1 - x0, y1 - y0
    image = Image.new('L', (max(1, width), max(1, height)))
    ImageDraw.Draw(image).text((-x0, -y0), ch, font=font, fill=255, anchor='ls')
    offset, value, count = len(bits), 0, 0
    for y in range(height):
        for x in range(width):
            value = (value << 1) | (image.getpixel((x, y)) >= 128)
            count += 1
            if count == 8:
                bits.append(value)
                value, count = 0, 0
    if count:
        bits.append(value << (8 - count))
    glyphs.append((offset, width, height, round(font.getlength(ch)), x0, y0))
header = '// Generated from Noto Sans Hebrew, OFL: licenses/NotoSansHebrew-OFL.txt.\n#pragma once\n'
header += '#include <stdint.h>\nstruct ClockGlyph {uint16_t offset;uint8_t w,h,advance;int8_t x,y;};\n'
header += 'static const char clockCharacters[]=' + repr(characters).replace("'", '"') + ';\n'
header += 'static constexpr unsigned clockFontSize=' + str(len(bits)) + ';\n'
header += 'static const unsigned char clockFontBits[]={' + ','.join(map(str, zlib.compress(bytes(bits), 9))) + '};\n'
header += 'static const ClockGlyph clockGlyphs[]={' + ','.join('{' + ','.join(map(str, g)) + '}' for g in glyphs) + '};\n'
(root / 'ReaderClockFont.h').write_text(header, encoding='utf-8')
print('Generated date font:', len(bits), 'bitmap bytes')
