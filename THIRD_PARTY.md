# Third-party components

The root MIT license covers original Ink Reader work only. Component notices are retained in `ebook-reader/licenses/` and source headers. Parser/decoder adaptations remain subject to those notices; original font files are unchanged, while derived bitmap tables and authored overlays are project-specific output.

| Component | Retained terms |
|---|---|
| Atomic14 EPUB/ZIP adaptation | atomic14-MIT.txt |
| miniz | miniz.txt (MIT) |
| zlib / TinyXML2 | zlib.txt / tinyxml2.txt (zlib terms) |
| PNGdec | PNGdec.txt (Apache 2.0) |
| TJpgDec R0.03 | TJpgDec.txt (ChaN notice) |
| stb_image | stb_image-MIT.txt (MIT alternative) |
| MiniBidi adaptation and tables | MiniBidi-MIT.txt, CrossPoint-MIT.txt; source author notices |
| Noto, Alef, David Libre, Atkinson, Charis, Liberation | respective SIL Open Font License notices |
| DejaVu | DejaVu-LICENSE.txt (Bitstream Vera / DejaVu) |
| Cozette optional comparison | Cozette-MIT.txt |
| Waveshare gray experiment reference | Waveshare-4in2-V2-MIT.txt and reference notice |

MiniBidi attribution: Ahmad Khalifa's original implementation is identified as MIT in the [upstream component](https://github.com/mintty/mintty/blob/master/src/minibidi.c); Thomas Wolff's changes are retained by name. The ESP32 adaptation/tables originate from [CrossPoint's MIT project](https://github.com/crosspoint-reader/crosspoint-reader/tree/a094609753106e36eeef05187dba862299855f98/lib/MiniBidi). Both source author notices and the MIT permission text are included. The mintty application's separate license is not substituted for this component's stated MIT terms.

## Locally fetched vendor dependency

Elecrow's [official CrowPanel example](https://github.com/Elecrow-RD/CrowPanel-ESP32-4.2-E-paper-HMI-Display-with-400-300) supplies seven display files. No explicit redistribution license was established for these files, so they are excluded and fetched locally from commit `cb6d6b41249051890456d3a311d22925c8f26ebe`. The manifest records original/patched hashes. The patch contains only Ink Reader's safety changes. Source availability does not imply an MIT grant; obtain appropriate rights before redistributing vendor sources or linked binaries.

ESP32 Arduino core and toolchains are external build dependencies; this repository does not bundle them. Review their terms separately when distributing a compiled product.
