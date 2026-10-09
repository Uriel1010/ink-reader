# PDF and manga import

Firmware 1.16 adds offline PDF import to the Wi-Fi file manager. Pages are rendered on your phone/PC and stored as a fixed-layout image EPUB named `Original name [PDF].epub`. The original PDF stays unchanged. The reader does not parse raw PDF files copied directly onto SD.

## Import a manga

1. On the reader, open Settings → File Transfer. Join its hotspot and open `http://192.168.4.1` in a full browser.
2. Expand **PDF / manga import** before choosing or dropping a PDF.
3. Choose a layout and page range, then select the PDF. Preparation progress appears before upload progress. Cancel works during either stage.
4. Leave transfer mode and open the generated book from Library. Your page and bookmarks are saved like other books.

| Option | Use |
|---|---|
| Split wide spreads automatically | Default. Landscape source pages split at the middle; ordinary portrait pages stay whole. |
| Manga: right half first | Japanese spread order. Choose left first for Western comics. Source PDF page order is retained. |
| Whole pages | Preserve each complete PDF page, including spreads. |
| Top / bottom halves | Two views with 5% overlap, useful for small speech bubbles. Try landscape screen rotation for larger lettering. |
| Trim white margins | Remove empty borders, retaining a small safety border around artwork. Turn off for deliberate margins. |
| First / last PDF page | Import a chapter or a smaller volume. Numbers refer to source PDF pages. Empty last means the end. |

Portrait screen rotation usually suits whole manga pages. The reader fits each image without stretching and uses the full screen, hiding normal title/progress decorations for fixed-layout books. Menu → Chapters lists the source page/half labels; bookmarks retain those labels. Changing reading fonts does not change lettering drawn inside PDF artwork.

## Limits and behavior

- Up to **128 MiB** per source PDF, **1000 source pages** per import, and **256 MiB** per resulting book. Split layouts may produce twice as many reading pages. Use a smaller range for large books or phones with limited memory.
- Page images prefer lossless PNG to preserve line art, with JPEG fallback if needed to stay below the decoder limit. They are at most **800 pixels** on the longest side and below **1 MiB** each. The display remains 400×300 black/white; no OCR, text reflow, interactive PDF forms, arbitrary zoom or panning is included.
- PDF.js **6.4.299**, standard fonts, JPEG2000/JBIG2 JavaScript fallbacks, and common Japanese CMaps are bundled in firmware. No CDN or internet is used. Unusual external CMaps/fonts and every PDF codec combination are not qualified.
- Password prompts are local to the browser; passwords are not saved. PDF scripting/XFA and font-code evaluation are disabled. Password-protected documents are not part of the current physical-device qualification matrix.
- Preparation renders sequentially. Legitimate preparation sends activity signals so the hotspot remains open; cancellation stops them. Upload uses the existing temporary-file, CRC32, acknowledgement, and replacement-recovery protocol. An existing destination is replaced only after confirmation and successful commit.
- Use a current full browser. Captive sign-in windows may restrict file selection/workers. Chromium on Windows was tested; Safari/Firefox/phone qualification remains pending.

## Development and validation

Normal firmware builds use committed generated assets. To regenerate them, run `python ebook-reader/prepare-pdf-assets.py --download` (or pass a local `--archive`). The archive and each asset are checked against pinned SHA-256 hashes. PDF.js and its auxiliary component notices are in `ebook-reader/licenses/PDFjs-*.txt`.

Host integration tests: install the pinned Playwright dependency in `ebook-reader/tests`, install Chromium with Playwright, then run `npm test` there. Tests serve the actual packed assets and UI on an ordinary HTTP origin, convert original vector/scanned/spread/rotated fixtures, validate chunks, retry a dropped connection, and check ranges/cancellation/corrupt input. Rebuilding the original PDF fixture requires reportlab, pypdf, and Pillow.

Device serial `M` runs a small authored image-book parser/decoder check in all four rotations. It writes and removes an application-private test file and does not navigate away from the user's book.

Measured qualification results are recorded in the public validation document. Whole-board battery qualification remains unchanged.
