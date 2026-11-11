        ////////////////////////////////////////////////////
        //     TFT_eSPI virtual bus driver functions      //
        ////////////////////////////////////////////////////

// Selected by -D TFT_VIRTUAL_BUS on any processor. No SPI port or pin is touched: every byte the library would clock
// out, with the chip select and data/command levels around it, is handed to three functions the application defines.
// The application models the display controller behind them (a frame memory it shows on another panel, or checks in a
// test), so the selected TFT driver's own command stream is kept.

#ifndef _TFT_eSPI_VIRTUALBUSH_
#define _TFT_eSPI_VIRTUALBUSH_

// Processor ID reported by getSetup()
#define PROCESSOR_ID 0x0000

// The display pins of the setup are dropped: the library's own pinMode() and digitalWrite() on chip select, DC, reset
// and backlight are compiled out, and no SPI pin is named.
#undef TFT_CS
#undef TFT_DC
#undef TFT_RST
#undef TFT_BL
#undef TFT_MOSI
#undef TFT_SCLK

// The application's side of the bus
void    tftVirtualBusChipSelect(bool selected); // CS: true while the display is selected
void    tftVirtualBusDataMode(bool data);       // DC: false for a command byte, true for parameters and pixels
uint8_t tftVirtualBusTransfer(uint8_t out);     // one byte out; the return value is the byte read back

// Processor specific code used by SPI bus transaction startWrite and endWrite functions
#define SET_BUS_WRITE_MODE // Not used
#define SET_BUS_READ_MODE  // Not used

// Code to check if DMA is busy, used by SPI bus transaction startWrite and endWrite functions
#define DMA_BUSY_CHECK // Nothing is queued: a DMA push has left the bus when it returns

#if !defined (SUPPORT_TRANSACTIONS)
  #define SUPPORT_TRANSACTIONS
#endif

// Initialise processor specific SPI functions, used by init()
#define INIT_TFT_DATA_BUS

////////////////////////////////////////////////////////////////////////////////////////
// DC, CS and touch chip select
////////////////////////////////////////////////////////////////////////////////////////
#define DC_C tftVirtualBusDataMode(false)
#define DC_D tftVirtualBusDataMode(true)

#define CS_L tftVirtualBusChipSelect(true)
#define CS_H tftVirtualBusChipSelect(false)

#define T_CS_L // The virtual bus carries the display only
#define T_CS_H

#ifndef TFT_RD
  #define TFT_RD -1
#endif

#ifndef TFT_MISO
  #define TFT_MISO -1
#endif

////////////////////////////////////////////////////////////////////////////////////////
// Macros to write commands/pixel colour data, most significant byte first
////////////////////////////////////////////////////////////////////////////////////////
static inline void tftVirtualBusWrite16(uint16_t value)
{
  tftVirtualBusTransfer((uint8_t)(value >> 8));
  tftVirtualBusTransfer((uint8_t)value);
}

#define tft_Write_8(C)     tftVirtualBusTransfer((uint8_t)(C))
#define tft_Write_16(C)    tftVirtualBusWrite16((uint16_t)(C))
#define tft_Write_16S(C)   tftVirtualBusWrite16((uint16_t)(((C) >> 8) | ((C) << 8)))
#define tft_Write_16N      tft_Write_16

#define tft_Write_32(C)    tft_Write_16((uint16_t)((C) >> 16)); tft_Write_16((uint16_t)(C))
#define tft_Write_32C(C,D) tft_Write_16((uint16_t)(C)); tft_Write_16((uint16_t)(D))
#define tft_Write_32D(C)   tft_Write_16((uint16_t)(C)); tft_Write_16((uint16_t)(C))

#define tft_Read_8() tftVirtualBusTransfer(0)

#endif // Header end
