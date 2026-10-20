# Battery operation

The existing battery profile is enabled by turning Development Mode off in Settings / Advanced Settings. Reading and full-screen images sleep after a completed refresh; dashboard and menus sleep after 60 idle seconds. A released button press wakes and performs one action. File Transfer temporarily keeps the reader awake and shuts Wi-Fi down on exit.

The clock screensaver remains awake to update every minute. Exit it when battery conservation matters; it is not the low-power reading profile.

The reviewed board schematic has a dedicated battery connector and charger, with no connected battery-voltage ADC or USB/battery detection input. Firmware cannot detect a newly connected battery, show a trustworthy percentage, or automatically switch development mode based on USB power. The saved preference applies with either power source. Charging is handled by board hardware, not this firmware. Confirm battery type, polarity and wiring against the board documentation.

Software sleep/wake validation over USB does not establish battery-only operation, whole-board sleep current, charging behavior, or runtime. Battery-only button wake requires a physical unplug-and-press check; current/runtime require measurements.
