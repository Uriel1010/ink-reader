# Validation and limits

## Public snapshot checks

The clock-enabled public snapshot compiled locally with ESP32 core 3.3.7: **1,512,307 bytes** of application storage and **82,812 bytes** of static RAM. The clock-disabled variant also compiled: **1,491,611 bytes** of application storage and **81,380 bytes** of static RAM. A clean local clone successfully fetched and patched all seven pinned display dependencies. All 12 authored corpus files matched their manifest hashes; Python and PowerShell scripts passed syntax checks, and local Markdown links resolved. No firmware was flashed during publication.

This repository starts from a curated source snapshot of firmware 1.15. It does not publish private development Git history. See PUBLICATION.md for the export checks and GitHub Actions for reproducible build results.

The physical reader supplied the original-text reading and reading-settings framebuffer screenshots in the README. Capture used the development recovery guard, then restored the pre-capture session. The browser screenshot uses the actual embedded UI and synthetic filenames; it is not evidence of an end-to-end transfer.

## Existing hardware observations

On the development CrowPanel, previous sessions exercised EPUB/TXT reading, Hebrew/English layout, font changes, rotation navigation, image browsing, SD transfer, and power-loss recovery. The user confirmed clock readability, return without a second action after a held button, and clock recovery after unplug/replug. They also confirmed button wake with one action and little flashing. These are observations on one board, not a compatibility matrix.

Recent normal monochrome partial updates measured approximately **1.45 seconds**; cold full updates were approximately **3 seconds**. Timing varies with waveform, image, and panel state. Gray-level font experiments were rejected for appearance/speed; normal reading is black-and-white.

## Remaining qualification

- Battery hardware, whole-board sleep current, charging, and runtime.
- More CrowPanel revisions, phones, browsers, and SD-card models.
- Systematic optical font comparisons and long-run ghosting checks.
- Expanded failure-injection testing for storage removal, out-of-space, and power loss at every replacement step.
- A complete accessibility review of the dashboard and browser interface.

Unit/CI compilation cannot validate electrical behavior or e-paper appearance. Do not interpret framebuffer screenshots as physical optical measurements. Report issues with firmware version, board revision, rotation, font/size, and an original or freely redistributable minimal sample.
