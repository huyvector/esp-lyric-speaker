# Release validation · 0.4.11

Both firmware roles built successfully with the pinned PlatformIO configuration. The development release passed 31 portable test groups, flash-helper checks, C3 register checks and native UI/font rendering checks. This public packaging does not change application source or binaries.

Audio static RAM: 54,064 bytes; app: 783,152 bytes. Display static RAM: 129,436 bytes; app: 1,272,064 bytes. These are build sizes, not runtime free-heap measurements. Both boards require 4 MB flash and the included partition layout.

The final 0.4.11 build has not yet been flashed and visually/audibly checked on the complete assembly. Lookup success, timing and runtime stability depend on track metadata, Bluetooth source, network services, supply and module variants. Browser installer checks validate packaging and connection UI, not physical flashing or audio quality.

Firmware SHA-256 checksums are included. Browser factory images are merged with DIO / 40 MHz / 4 MB settings at offset 0, including the correct chip-specific bootloader. The factory image spans NVS and will reset configuration. Terminal app-only updates require the existing dual-board partition layout.
