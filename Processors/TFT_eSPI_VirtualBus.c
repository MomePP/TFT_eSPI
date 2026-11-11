        ////////////////////////////////////////////////////
        //     TFT_eSPI virtual bus driver functions      //
        ////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////////////
// Global variables
////////////////////////////////////////////////////////////////////////////////////////

// The library's `spi` object, with no port behind it: the calls that set up, lock or clock a real SPI port do nothing
// or go to the application's bus functions.
namespace
{
struct VirtualBusPort
{
  void begin(int8_t = -1, int8_t = -1, int8_t = -1, int8_t = -1) {}
  void end(void) {}
  void beginTransaction(SPISettings) {}
  void endTransaction(void) {}
  void setFrequency(uint32_t) {}
  uint8_t transfer(uint8_t out) { return tftVirtualBusTransfer(out); }
  uint16_t transfer16(uint16_t out)
  {
    const uint8_t high = tftVirtualBusTransfer((uint8_t)(out >> 8));
    return (uint16_t)(high << 8) | tftVirtualBusTransfer((uint8_t)out);
  }
};

VirtualBusPort spi;
} // namespace

////////////////////////////////////////////////////////////////////////////////////////
//                   Standard SPI 16-bit colour TFT
////////////////////////////////////////////////////////////////////////////////////////

/***************************************************************************************
** Function name:           pushBlock - for the virtual bus
** Description:             Write a block of pixels of the same colour
***************************************************************************************/
void TFT_eSPI::pushBlock(uint16_t color, uint32_t len){

  while ( len-- ) {tft_Write_16(color);}
}

/***************************************************************************************
** Function name:           pushPixels - for the virtual bus
** Description:             Write a sequence of pixels
***************************************************************************************/
void TFT_eSPI::pushPixels(const void* data_in, uint32_t len){

  uint16_t *data = (uint16_t*)data_in;

  if (_swapBytes) while ( len-- ) {tft_Write_16(*data); data++;}
  else while ( len-- ) {tft_Write_16S(*data); data++;}
}

////////////////////////////////////////////////////////////////////////////////////////
//                                DMA FUNCTIONS
////////////////////////////////////////////////////////////////////////////////////////

// Nothing is queued on the virtual bus: the pixels have gone out when a push returns. The pushes keep what the
// ESP32-S3 processor does to the caller's buffer (the byte swap in place, the clip into the buffer), so code written
// for that processor sees the same bytes afterwards.

/***************************************************************************************
** Function name:           initDMA
** Description:             Mark the DMA pushes usable
***************************************************************************************/
bool TFT_eSPI::initDMA(bool ctrl_cs)
{
  (void)ctrl_cs;
  if (DMA_Enabled) return false;
  DMA_Enabled = true;
  return true;
}

/***************************************************************************************
** Function name:           deInitDMA
** Description:             Mark the DMA pushes unusable
***************************************************************************************/
void TFT_eSPI::deInitDMA(void)
{
  DMA_Enabled = false;
}

/***************************************************************************************
** Function name:           dmaBusy
** Description:             Check if DMA is busy
***************************************************************************************/
bool TFT_eSPI::dmaBusy(void)
{
  return false;
}

/***************************************************************************************
** Function name:           dmaWait
** Description:             Wait until DMA is over
***************************************************************************************/
void TFT_eSPI::dmaWait(void)
{
  spiBusyCheck = 0;
}

/***************************************************************************************
** Function name:           pushPixelsDMA
** Description:             Push pixels to TFT (len must be less than 32767)
***************************************************************************************/
// This will byte swap the original image if setSwapBytes(true) was called by sketch.
void TFT_eSPI::pushPixelsDMA(uint16_t* image, uint32_t len)
{
  if ((len == 0) || (!DMA_Enabled)) return;

  if (_swapBytes) {
    for (uint32_t i = 0; i < len; i++) (image[i] = image[i] << 8 | image[i] >> 8);
  }

  const bool swap = _swapBytes;
  _swapBytes = false;
  pushPixels(image, len);
  _swapBytes = swap;
}

/***************************************************************************************
** Function name:           pushImageDMA
** Description:             Push image to a window (w*h must be less than 65536)
***************************************************************************************/
// This will clip and also swap bytes if setSwapBytes(true) was called by sketch
void TFT_eSPI::pushImageDMA(int32_t x, int32_t y, int32_t w, int32_t h, uint16_t* image, uint16_t* buffer)
{
  if ((x >= _vpW) || (y >= _vpH) || (!DMA_Enabled)) return;

  int32_t dx = 0;
  int32_t dy = 0;
  int32_t dw = w;
  int32_t dh = h;

  if (x < _vpX) { dx = _vpX - x; dw -= dx; x = _vpX; }
  if (y < _vpY) { dy = _vpY - y; dh -= dy; y = _vpY; }

  if ((x + dw) > _vpW ) dw = _vpW - x;
  if ((y + dh) > _vpH ) dh = _vpH - y;

  if (dw < 1 || dh < 1) return;

  uint32_t len = dw*dh;

  if (buffer == nullptr) {
    buffer = image;
  }

  // If image is clipped, copy pixels into a contiguous block
  if ( (dw != w) || (dh != h) ) {
    if(_swapBytes) {
      for (int32_t yb = 0; yb < dh; yb++) {
        for (int32_t xb = 0; xb < dw; xb++) {
          uint32_t src = xb + dx + w * (yb + dy);
          (buffer[xb + yb * dw] = image[src] << 8 | image[src] >> 8);
        }
      }
    }
    else {
      for (int32_t yb = 0; yb < dh; yb++) {
        memcpy((uint8_t*) (buffer + yb * dw), (uint8_t*) (image + dx + w * (yb + dy)), dw << 1);
      }
    }
  }
  // else, if a buffer pointer has been provided copy whole image to the buffer
  else if (buffer != image || _swapBytes) {
    if(_swapBytes) {
      for (uint32_t i = 0; i < len; i++) (buffer[i] = image[i] << 8 | image[i] >> 8);
    }
    else {
      memcpy(buffer, image, len*2);
    }
  }

  setAddrWindow(x, y, dw, dh);

  const bool swap = _swapBytes;
  _swapBytes = false;
  pushPixels(buffer, len);
  _swapBytes = swap;
}
