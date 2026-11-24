# TFT_eSPI 2.5.43 - C3 register mapping fix (0.4.1)

The upstream C3 processor header defines SPI_PORT as SPI2_HOST (enum 1).
The soc/spi_reg.h in pinned Arduino-ESP32 2.0.17 maps REG_SPI_BASE(i) only
when the hardware register index is 2. In the 0.4.0 ELF, _spi_user=0x10,
_spi_cmd=0, _spi_mosi_dlen=0x1c, _spi_w=0x98. This matches the user's
Store access fault MTVAL=0x10 at TFT_eSPI::begin_tft_write, reached by tft.init.

Set only the raw register index SPI_PORT to 2. Arduino SPIClass continues
using FSPI (1) and IDF host APIs retain SPI2_HOST (1); no SDK-global edits.
A compile-time assertion verifies the actual SDK mapping. The ELF regression
test checks all four compiled pointer values against DR_REG_SPI2_BASE.

Runtime library source, headers, fonts, processors and drivers are vendored
here; examples/tools are omitted. Original upstream copyright/license retained.
Upstream: https://github.com/Bodmer/TFT_eSPI/tree/v2.5.43
Related report: https://github.com/Bodmer/TFT_eSPI/issues/3670
