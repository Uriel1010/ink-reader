# Build and flash

## Requirements

- Git, PowerShell 5.1+ or PowerShell 7, Arduino CLI.
- ESP32 Arduino core **3.3.7** (install commands in the root README).
- Original SSD1683 CrowPanel 4.2-inch, ESP32-S3 N8R8, FAT32 microSD.

`tools/setup-vendor.ps1` downloads seven display files from a pinned Elecrow example, validates original hashes, applies the minimal display-safety patch, and validates resulting hashes. It never overwrites existing files. Recheck with `-VerifyOnly`. These files stay ignored by Git.

`tools/build.ps1` compiles by default. It locates Arduino CLI on PATH, then falls back to the Arduino IDE Windows installation. Pass `-ArduinoCli /path/to/arduino-cli` to override it. A fresh temporary ASCII sketch directory avoids non-ASCII toolchain path issues.

```powershell
./tools/build.ps1
./tools/build.ps1 -DisableClock
./tools/build.ps1 -Upload -Port COM6
```

FQBN: `esp32:esp32:esp32s3:FlashSize=8M,PSRAM=opi,CDCOnBoot=default,PartitionScheme=default_8MB`.

Application partition limit: **3,342,336 bytes**, checked before upload. Standard 8 MB partition layout; NVS starts at 0x9000. Never select erase-all when preserving settings. Back up the SD before changing firmware or partition layouts. Upload explicitly requires `-Upload` and the correct serial port.

Clock-disabled builds remove clock menus/time synchronization while retaining AP file transfer. Existing clock sessions fall back to their return screen.

## Fonts and test content

Generated bitmap headers are checked in; building firmware requires no Python or font-generation dependencies. The font sources and their notices are retained. Font-generation scripts are advanced development tools; their dependencies are imported explicitly in each script.

`ebook-reader/test-corpus/` contains authored test material, not commercial books. See its manifest. Public CI compiles both clock configurations but does not publish binaries containing the locally fetched vendor dependency.

Optional font asset regeneration requires Python, `freetype-py`, and Pillow (`python -m pip install freetype-py Pillow`). Generated diagnostic previews go to ignored `ebook-reader/validation-results/`. Normal firmware builds use the committed tables.

Firmware 1.16 adds the offline PDF.js bundle. The clock-enabled application is approximately 3.26 MB, close to the existing 3,342,336-byte partition limit; the build size check remains enforced. Do not change partitions or erase NVS for this update. PDF assets are already generated; their regeneration and browser test instructions are in [PDF / manga](PDF-MANGA.md).
