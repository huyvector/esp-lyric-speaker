# Sources and licenses

Application code in audio/src, display/src, shared, tools and test is MIT; see LICENSE. ESP32-A2DP 1.8.8 is vendored with patches documented in audio/lib/ESP32-A2DP/PATCHES.md. TFT_eSPI 2.5.43 is also vendored in display/lib/TFT_eSPI, with the C3-only raw register index patch described in its PATCHES.md. Pinned external libraries: U8g2 2.36.5, ArduinoJson 6.21.5 and JPEGDEC 1.8.2. License texts are in licenses/. U8g2's Vietnamese Unifont subset retains its font copyright, SIL OFL 1.1 and GPLv2+ with the GNU Font Embedding Exception; see [Unifont licensing](https://unifoundry.com/unifont/index.html).

Firmware links Arduino-ESP32 2.0.17 ([source, LGPL-2.1](https://github.com/espressif/arduino-esp32/tree/2.0.17)) and ESP-IDF 4.4.7 ([source](https://github.com/espressif/esp-idf/tree/v4.4.7)); component licenses remain upstream. The entire application and pinned build configuration are included for rebuilding with a modified core. The AVRCP status-response patch is generated in the build directory by audio/tools/patch_avrc.py, leaving installed SDK source unchanged.

shared/trust_anchors.h is derived from the certifi Mozilla CA store, MPL 2.0; see included certifi license. The procedural record artwork and graphics do not include the user's reference image. Cache images/lyrics are fetched at runtime and are not distributed with this package. No SD library, Helix decoder, music or previous Pi services are included.

ST7789 window mapping was checked against [atoomnetmarc's example](https://github.com/atoomnetmarc/ST7789-76x284) and [TFT_eSPI PR3769](https://github.com/Bodmer/TFT_eSPI/pull/3769/files). Register initialization/gamma follows the ST7789 sequence used by TFT_eSPI; its license is included. Our small write-only SPI driver selects the second panel separately from GC9A01.

## UI 0.4.7

The curved carousel and compose-before-present approach were informed by the user's local NWatch ESP32-C3 project (stage6.7.2-back400). No NWatch source, icon bitmaps or font assets were copied. `ui_canvas.h`, procedural palettes/rim geometry and their generator are new project code. The existing U8g2 Vietnamese font remains under its bundled license.

## UI 0.4.8

The new neon ribbons, orbital curves, starfield and waves are original procedural animations inspired by desktop music-player visualizations. No Microsoft/Windows Media Player source, presets, textures or artwork are included. They respond to peak telemetry and are not FFT spectra or sample-accurate audio waveforms.
