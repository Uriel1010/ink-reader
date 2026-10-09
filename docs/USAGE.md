# Using Ink Reader

## Controls

Center selects. Menu opens actions. Exit returns to the previous screen. Up/Down select or turn pages; at 180°/270° their navigation direction reverses. At 0°/90°, physical Up means previous and Down means next. Serial `p`/`n` remain logical previous/next.

The dashboard offers Continue Reading, Library, Bookmarks, Images, and Settings. Reading Menu includes chapters, bookmarking, reading settings, dashboard, sleep, and the optional screensaver. Font sizes are 14/16/18/20 px, with 16 px the default. Layout changes retain the first visible anchor rather than rounding down to an old page boundary.

## Storage and recovery

Use a FAT32 microSD with EPUB/TXT books and JPEG/PNG pictures. Image gallery scanning is bounded to five directory levels and 128 entries. Large/invalid images display a placeholder. Color images become black-and-white, fitted without stretching. Browser uploads prepare images to at most 800 px on the longest side and below the 1 MiB decoder input cap, preserving originals.

The hidden `/.crowreader` folder stores settings, bookmarks, positions, caches, and recovery data. Do not edit it during operation. JSON updates use temporary files and previous-copy recovery; internal-memory fallbacks cover some missing-card cases. Settings and reading sessions are also persisted for restart recovery. Never remove the card during a write.

## File Transfer

Settings → File Transfer saves the reading position and opens the reader's local password-protected hotspot. Join using the displayed credentials or Wi-Fi QR. Menu toggles the QR to the website address. Open `http://192.168.4.1`; captive-portal opening varies by OS/browser. Keep the connection even if your phone says it has no internet.

Upload books/pictures, browse folders, download individual files, create folders, or delete after confirmation. Replacement also asks for confirmation. Browser clipboard support varies, so file selection is always available. The protected application directory cannot be browsed or mutated.

Exit cancels safely, shuts down HTTP/DNS/Wi-Fi, rescans changed content, and restores the previous screen/anchor. Five idle minutes end transfer mode; background probes do not count as activity. Stalled transfers time out after 30 seconds. Keep your own backups: this is removable storage.

## Clock and power

The optional screensaver shows 24-hour time and an English date in Israel's time zone with daylight-saving rules. Configure its separate Wi-Fi credentials in the browser manager; clear them there when desired. Clock entry synchronizes time, then turns Wi-Fi off. Reading startup does not enable Wi-Fi. A normal button exits the clock without also navigating; Menu retries when time is unavailable.

Development Mode disables automatic sleep for USB testing. With it disabled, reading/full-screen image completion may enter deep sleep, while menus sleep after inactivity. A button wakes and acts once. Battery electrical qualification is still pending; no battery percentage or runtime is promised.

PDF / manga import is available in File Transfer: choose PDF options, then select/drop a PDF. It becomes a fixed-layout image EPUB, retaining per-book recovery. See [PDF / manga guide](PDF-MANGA.md).
