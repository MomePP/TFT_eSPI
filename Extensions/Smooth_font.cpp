 // Coded by Bodmer 10/2/18, see license in root directory.
 // This is part of the TFT_eSPI class and is associated with anti-aliased font functions
 

////////////////////////////////////////////////////////////////////////////////////////
// New anti-aliased (smoothed) font functions added below
////////////////////////////////////////////////////////////////////////////////////////

/***************************************************************************************
** Shared glyph metric cache for fonts loaded from a (PROGMEM) VLW array
***************************************************************************************/
// Every TFT_eSPI / TFT_eSprite instance used to parse the 28-byte-per-glyph VLW table
// into seven private malloc()ed arrays on every loadFont() call. The parse result only
// depends on the font array, so it is computed once per distinct array and shared by
// all instances. Entries are never freed: the number of distinct fonts is small and
// bounded, and font arrays are assumed immutable (they live in flash).
namespace
{
  constexpr uint32_t VLW_HEADER_BYTES = 24; // 6 x uint32_t
  constexpr uint32_t VLW_GLYPH_BYTES  = 28; // 7 x int32_t per glyph
  constexpr uint32_t VLW_FIELD_BYTES  = 4;
  constexpr uint32_t VLW_HDR_COUNT    = 0;  // byte offsets inside the header
  constexpr uint32_t VLW_HDR_ASCENT   = 16;
  constexpr uint32_t VLW_HDR_DESCENT  = 20;
  constexpr uint32_t VLW_GLYPH_UNICODE = 0; // byte offsets inside a glyph record
  constexpr uint32_t VLW_GLYPH_HEIGHT  = 4;
  constexpr uint32_t VLW_GLYPH_WIDTH   = 8;
  constexpr uint32_t VLW_GLYPH_ADVANCE = 12;
  constexpr uint32_t VLW_GLYPH_DY      = 16;
  constexpr uint32_t VLW_GLYPH_DX      = 20;
  constexpr uint32_t GLYPH_ARRAY_BYTES = 2 + 2 + 4 + 1 + 1 + 1 + 1; // per-glyph bytes of the metric arrays

  struct FontCache
  {
    FontCache*            next;
    const uint8_t*        array;
    uint8_t               header[VLW_HEADER_BYTES];
    TFT_eSPI::fontMetrics metrics;
    bool                  sorted;
    uint32_t*             bitmap;
    uint16_t*             unicode;
    int16_t*              dY;
    uint8_t*              height;
    uint8_t*              width;
    uint8_t*              xAdvance;
    int8_t*               dX;
  };

  FontCache* s_fontCache = nullptr; // Singly linked, prepend-only; accessed with acquire/release

  uint32_t vlwRead32(const uint8_t* p)
  {
    return (uint32_t)pgm_read_byte(p) << 24 | (uint32_t)pgm_read_byte(p + 1) << 16 |
           (uint32_t)pgm_read_byte(p + 2) << 8 | (uint32_t)pgm_read_byte(p + 3);
  }

  const FontCache* fontCacheFind(const uint8_t* array)
  {
    for (const FontCache* e = __atomic_load_n(&s_fontCache, __ATOMIC_ACQUIRE); e; e = e->next)
      if (e->array == array && memcmp(e->header, array, VLW_HEADER_BYTES) == 0) return e;
    return nullptr;
  }

  // Same parse as the per-instance loadMetrics() used to do, same field truncations
  const FontCache* fontCacheBuild(const uint8_t* array)
  {
    const uint16_t count = (uint16_t)vlwRead32(array + VLW_HDR_COUNT);
    FontCache* e = (FontCache*)malloc(sizeof(FontCache) + (size_t)count * GLYPH_ARRAY_BYTES);
    if (e == nullptr) return nullptr;

    // Arrays follow the header struct, widest element first so each stays naturally aligned
    uint8_t* mem = (uint8_t*)(e + 1);
    e->bitmap   = (uint32_t*)mem; mem += count * 4;
    e->unicode  = (uint16_t*)mem; mem += count * 2;
    e->dY       = (int16_t*)mem;  mem += count * 2;
    e->height   = mem;            mem += count;
    e->width    = mem;            mem += count;
    e->xAdvance = mem;            mem += count;
    e->dX       = (int8_t*)mem;

    e->array = array;
    memcpy(e->header, array, VLW_HEADER_BYTES);

    TFT_eSPI::fontMetrics& m = e->metrics;
    m.gArray   = array;
    m.gCount   = count;
    m.ascent   = (uint16_t)vlwRead32(array + VLW_HDR_ASCENT);
    m.descent  = (uint16_t)vlwRead32(array + VLW_HDR_DESCENT);
    m.maxAscent  = m.ascent;
    m.maxDescent = m.descent;

    uint32_t bitmapPtr = VLW_HEADER_BYTES + (uint32_t)count * VLW_GLYPH_BYTES;
    bool sorted = true;

    for (uint16_t g = 0; g < count; g++)
    {
      const uint8_t* rec = array + VLW_HEADER_BYTES + (uint32_t)g * VLW_GLYPH_BYTES;
      e->unicode[g]  = (uint16_t)vlwRead32(rec + VLW_GLYPH_UNICODE);
      e->height[g]   =  (uint8_t)vlwRead32(rec + VLW_GLYPH_HEIGHT);
      e->width[g]    =  (uint8_t)vlwRead32(rec + VLW_GLYPH_WIDTH);
      e->xAdvance[g] =  (uint8_t)vlwRead32(rec + VLW_GLYPH_ADVANCE);
      e->dY[g]       =  (int16_t)vlwRead32(rec + VLW_GLYPH_DY);
      e->dX[g]       =   (int8_t)vlwRead32(rec + VLW_GLYPH_DX);

      if (g && e->unicode[g] < e->unicode[g - 1]) sorted = false;

      // Different glyph sets have different descent values not always based on "p", so get maximum glyph descent
      if (((int16_t)e->height[g] - (int16_t)e->dY[g]) > m.maxDescent)
      {
        // Avoid UTF coding values and characters that tend to give duff values
        if (((e->unicode[g] > 0x20) && (e->unicode[g] < 0xA0) && (e->unicode[g] != 0x7F)) || (e->unicode[g] > 0xFF))
          m.maxDescent = e->height[g] - e->dY[g];
      }

      e->bitmap[g] = bitmapPtr;
      bitmapPtr += e->width[g] * e->height[g];
    }

    m.yAdvance   = m.maxAscent + m.maxDescent;
    m.spaceWidth = (m.ascent + m.descent) * 2 / 7; // Guess at space width
    e->sorted    = sorted;

    // Publish the finished entry. Two tasks building the same font at the same moment can
    // lose one entry from the list; the loser's block stays valid for whoever loaded it.
    e->next = s_fontCache;
    __atomic_store_n(&s_fontCache, e, __ATOMIC_RELEASE);
    return e;
  }
}

/***************************************************************************************
** Function name:           loadFont
** Description:             loads parameters from a font vlw array in memory
*************************************************************************************x*/
void TFT_eSPI::loadFont(const uint8_t array[])
{
  if (array == nullptr) return;
  if (fontLoaded) unloadFont();

#ifdef FONT_FS_AVAILABLE
  fs_font = false;
#endif
  fontPtr = (uint8_t*) array;

  const FontCache* fc = fontCacheFind(array);
  if (fc == nullptr) fc = fontCacheBuild(array);
  if (fc == nullptr) return; // Out of memory: no font gets loaded

  gFont     = fc->metrics;
  gUnicode  = fc->unicode;
  gHeight   = fc->height;
  gWidth    = fc->width;
  gxAdvance = fc->xAdvance;
  gdY       = fc->dY;
  gdX       = fc->dX;
  gBitmap   = fc->bitmap;
  gSorted   = fc->sorted;
  gShared   = true;
  fontLoaded = true;
}

#ifdef FONT_FS_AVAILABLE
/***************************************************************************************
** Function name:           loadFont
** Description:             loads parameters from a font vlw file
*************************************************************************************x*/
void TFT_eSPI::loadFont(String fontName, fs::FS &ffs)
{
  fontFS = ffs;
  loadFont(fontName, false);
}
#endif

/***************************************************************************************
** Function name:           loadFont
** Description:             loads parameters from a font vlw file
*************************************************************************************x*/
void TFT_eSPI::loadFont(String fontName, bool flash)
{
  /*
    The vlw font format does not appear to be documented anywhere, so some reverse
    engineering has been applied!

    Header of vlw file comprises 6 uint32_t parameters (24 bytes total):
      1. The gCount (number of character glyphs)
      2. A version number (0xB = 11 for the one I am using)
      3. The font size (in points, not pixels)
      4. Deprecated mboxY parameter (typically set to 0)
      5. Ascent in pixels from baseline to top of "d"
      6. Descent in pixels from baseline to bottom of "p"

    Next are gCount sets of values for each glyph, each set comprises 7 int32t parameters (28 bytes):
      1. Glyph Unicode stored as a 32-bit value
      2. Height of bitmap bounding box
      3. Width of bitmap bounding box
      4. gxAdvance for cursor (setWidth in Processing)
      5. dY = distance from cursor baseline to top of glyph bitmap (signed value +ve = up)
      6. dX = distance from cursor to left side of glyph bitmap (signed value -ve = left)
      7. padding value, typically 0

    The bitmaps start next at 24 + (28 * gCount) bytes from the start of the file.
    Each pixel is 1 byte, an 8-bit Alpha value which represents the transparency from
    0xFF foreground colour, 0x00 background. The library uses a linear interpolation
    between the foreground and background RGB component colours. e.g.
        pixelRed = ((fgRed * alpha) + (bgRed * (255 - alpha))/255
    To gain a performance advantage fixed point arithmetic is used with rounding and
    division by 256 (shift right 8 bits is faster).

    After the bitmaps is:
       1 byte for font name string length (excludes null)
       a zero terminated character string giving the font name
       1 byte for Postscript name string length
       a zero/one terminated character string giving the font name
       last byte is 0 for non-anti-aliased and 1 for anti-aliased (smoothed)


    Glyph bitmap example is:
    // Cursor coordinate positions for this and next character are marked by 'C'
    // C<------- gxAdvance ------->C  gxAdvance is how far to move cursor for next glyph cursor position
    // |                           |
    // |                           |   ascent is top of "d", descent is bottom of "p"
    // +-- gdX --+             ascent
    // |         +-- gWidth--+     |   gdX is offset to left edge of glyph bitmap
    // |   +     x@.........@x  +  |   gdX may be negative e.g. italic "y" tail extending to left of
    // |   |     @@.........@@  |  |   cursor position, plot top left corner of bitmap at (cursorX + gdX)
    // |   |     @@.........@@ gdY |   gWidth and gHeight are glyph bitmap dimensions
    // |   |     .@@@.....@@@@  |  |
    // | gHeight ....@@@@@..@@  +  +    <-- baseline
    // |   |     ...........@@     |
    // |   |     ...........@@     |   gdY is the offset to the top edge of the bitmap
    // |   |     .@@.......@@. descent plot top edge of bitmap at (cursorY + ascent - gdY)
    // |   +     x..@@@@@@@..x     |   x marks the corner pixels of the bitmap
    // |                           |
    // +---------------------------+   yAdvance is y delta for the next line, font size or (ascent + descent)
    //                                 some fonts can overlay in y direction so may need a user adjust value

  */

#ifndef FONT_FS_AVAILABLE
  // Only array fonts are available without a file system, see loadFont(array)
  (void)fontName;
  (void)flash;
}
#else
  if (fontLoaded) unloadFont();

  if (fontName == "") fs_font = false;
  else { fontPtr = nullptr; fs_font = true; }

  if (fs_font) {
    spiffs = flash; // true if font is in SPIFFS

    if(spiffs) fontFS = SPIFFS;

    // Avoid a crash on the ESP32 if the file does not exist
    if (fontFS.exists("/" + fontName + ".vlw") == false) {
      Serial.println("Font file " + fontName + " not found!");
      return;
    }

    fontFile = fontFS.open( "/" + fontName + ".vlw", "r");

    if(!fontFile) return;

    fontFile.seek(0, fs::SeekSet);
  }

  gFont.gArray   = (const uint8_t*)fontPtr;

  gFont.gCount   = (uint16_t)readInt32(); // glyph count in file
                             readInt32(); // vlw encoder version - discard
  gFont.yAdvance = (uint16_t)readInt32(); // Font size in points, not pixels
                             readInt32(); // discard
  gFont.ascent   = (uint16_t)readInt32(); // top of "d"
  gFont.descent  = (uint16_t)readInt32(); // bottom of "p"

  // These next gFont values might be updated when the Metrics are fetched
  gFont.maxAscent  = gFont.ascent;   // Determined from metrics
  gFont.maxDescent = gFont.descent;  // Determined from metrics
  gFont.yAdvance   = gFont.ascent + gFont.descent;
  gFont.spaceWidth = gFont.yAdvance / 4;  // Guess at space width

  fontLoaded = true;

  // Fetch the metrics for each glyph
  loadMetrics();
}
#endif


#ifdef FONT_FS_AVAILABLE
/***************************************************************************************
** Function name:           loadMetrics
** Description:             Get the metrics for each glyph and store in RAM
*************************************************************************************x*/
//#define SHOW_ASCENT_DESCENT
void TFT_eSPI::loadMetrics(void)
{
  uint32_t headerPtr = 24;
  uint32_t bitmapPtr = headerPtr + gFont.gCount * 28;

#if defined (ESP32) && defined (CONFIG_SPIRAM_SUPPORT)
  if ( psramFound() )
  {
    gUnicode  = (uint16_t*)ps_malloc( gFont.gCount * 2); // Unicode 16-bit Basic Multilingual Plane (0-FFFF)
    gHeight   =  (uint8_t*)ps_malloc( gFont.gCount );    // Height of glyph
    gWidth    =  (uint8_t*)ps_malloc( gFont.gCount );    // Width of glyph
    gxAdvance =  (uint8_t*)ps_malloc( gFont.gCount );    // xAdvance - to move x cursor
    gdY       =  (int16_t*)ps_malloc( gFont.gCount * 2); // offset from bitmap top edge from lowest point in any character
    gdX       =   (int8_t*)ps_malloc( gFont.gCount );    // offset for bitmap left edge relative to cursor X
    gBitmap   = (uint32_t*)ps_malloc( gFont.gCount * 4); // seek pointer to glyph bitmap in the file
  }
  else
#endif
  {
    gUnicode  = (uint16_t*)malloc( gFont.gCount * 2); // Unicode 16-bit Basic Multilingual Plane (0-FFFF)
    gHeight   =  (uint8_t*)malloc( gFont.gCount );    // Height of glyph
    gWidth    =  (uint8_t*)malloc( gFont.gCount );    // Width of glyph
    gxAdvance =  (uint8_t*)malloc( gFont.gCount );    // xAdvance - to move x cursor
    gdY       =  (int16_t*)malloc( gFont.gCount * 2); // offset from bitmap top edge from lowest point in any character
    gdX       =   (int8_t*)malloc( gFont.gCount );    // offset for bitmap left edge relative to cursor X
    gBitmap   = (uint32_t*)malloc( gFont.gCount * 4); // seek pointer to glyph bitmap in the file
  }

#ifdef SHOW_ASCENT_DESCENT
  Serial.print("ascent  = "); Serial.println(gFont.ascent);
  Serial.print("descent = "); Serial.println(gFont.descent);
#endif

#ifdef FONT_FS_AVAILABLE
  if (fs_font) fontFile.seek(headerPtr, fs::SeekSet);
#endif

  uint16_t gNum = 0;
  gSorted = true;

  while (gNum < gFont.gCount)
  {
    gUnicode[gNum]  = (uint16_t)readInt32(); // Unicode code point value
    gHeight[gNum]   =  (uint8_t)readInt32(); // Height of glyph
    gWidth[gNum]    =  (uint8_t)readInt32(); // Width of glyph
    gxAdvance[gNum] =  (uint8_t)readInt32(); // xAdvance - to move x cursor
    gdY[gNum]       =  (int16_t)readInt32(); // y delta from baseline
    gdX[gNum]       =   (int8_t)readInt32(); // x delta from cursor
    readInt32(); // ignored
    if (gNum && gUnicode[gNum] < gUnicode[gNum - 1]) gSorted = false;

    //Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", gHeight  = "); Serial.println(gHeight[gNum]);
    //Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", gWidth  = "); Serial.println(gWidth[gNum]);
    //Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", gxAdvance  = "); Serial.println(gxAdvance[gNum]);
    //Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", gdY  = "); Serial.println(gdY[gNum]);

    // Different glyph sets have different ascent values not always based on "d", so we could get
    // the maximum glyph ascent by checking all characters. BUT this method can generate bad values
    // for non-existent glyphs, so we will reply on processing for the value and disable this code for now...
    /*
    if (gdY[gNum] > gFont.maxAscent)
    {
      // Try to avoid UTF coding values and characters that tend to give duff values
      if (((gUnicode[gNum] > 0x20) && (gUnicode[gNum] < 0x7F)) || (gUnicode[gNum] > 0xA0))
      {
        gFont.maxAscent   = gdY[gNum];
#ifdef SHOW_ASCENT_DESCENT
        Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", maxAscent  = "); Serial.println(gFont.maxAscent);
#endif
      }
    }
    */

    // Different glyph sets have different descent values not always based on "p", so get maximum glyph descent
    if (((int16_t)gHeight[gNum] - (int16_t)gdY[gNum]) > gFont.maxDescent)
    {
      // Avoid UTF coding values and characters that tend to give duff values
      if (((gUnicode[gNum] > 0x20) && (gUnicode[gNum] < 0xA0) && (gUnicode[gNum] != 0x7F)) || (gUnicode[gNum] > 0xFF))
      {
        gFont.maxDescent   = gHeight[gNum] - gdY[gNum];
#ifdef SHOW_ASCENT_DESCENT
        Serial.print("Unicode = 0x"); Serial.print(gUnicode[gNum], HEX); Serial.print(", maxDescent = "); Serial.println(gHeight[gNum] - gdY[gNum]);
#endif
      }
    }

    gBitmap[gNum] = bitmapPtr;

    bitmapPtr += gWidth[gNum] * gHeight[gNum];

    gNum++;
    yield();
  }

  gFont.yAdvance = gFont.maxAscent + gFont.maxDescent;

  gFont.spaceWidth = (gFont.ascent + gFont.descent) * 2/7;  // Guess at space width
}
#endif


/***************************************************************************************
** Function name:           deleteMetrics
** Description:             Delete the old glyph metrics and free up the memory
*************************************************************************************x*/
void TFT_eSPI::unloadFont( void )
{
  if (gShared)
  {
    // Arrays belong to the shared font cache, only drop the references
    gUnicode  = NULL;
    gHeight   = NULL;
    gWidth    = NULL;
    gxAdvance = NULL;
    gdY       = NULL;
    gdX       = NULL;
    gBitmap   = NULL;
    gShared   = false;
  }
  else
  {
    free(gUnicode);  gUnicode  = NULL;
    free(gHeight);   gHeight   = NULL;
    free(gWidth);    gWidth    = NULL;
    free(gxAdvance); gxAdvance = NULL;
    free(gdY);       gdY       = NULL;
    free(gdX);       gdX       = NULL;
    free(gBitmap);   gBitmap   = NULL;
  }

  gFont.gArray = nullptr;

#ifdef FONT_FS_AVAILABLE
  if (fs_font && fontFile) fontFile.close();
#endif

  fontLoaded = false;
}


#ifdef FONT_FS_AVAILABLE


/***************************************************************************************
** Function name:           readInt32
** Description:             Get a 32-bit integer from the font file
*************************************************************************************x*/
uint32_t TFT_eSPI::readInt32(void)
{
  uint32_t val = 0;

  if (fs_font) {
    val  = (uint32_t)fontFile.read() << 24;
    val |= (uint32_t)fontFile.read() << 16;
    val |= (uint32_t)fontFile.read() << 8;
    val |= (uint32_t)fontFile.read();
  }
  else
  {
    val  = (uint32_t)pgm_read_byte(fontPtr++) << 24;
    val |= (uint32_t)pgm_read_byte(fontPtr++) << 16;
    val |= (uint32_t)pgm_read_byte(fontPtr++) << 8;
    val |= (uint32_t)pgm_read_byte(fontPtr++);
  }

  return val;
}
#endif


/***************************************************************************************
** Function name:           getUnicodeIndex
** Description:             Get the font file index of a Unicode character
*************************************************************************************x*/
bool TFT_eSPI::getUnicodeIndex(uint16_t unicode, uint16_t *index)
{
  uint16_t lo = 0;
  uint16_t hi = gFont.gCount;

  if (gSorted)
  {
    // Lower bound, so duplicated code points resolve to the first record like the linear scan
    while (lo < hi)
    {
      const uint16_t mid = (uint16_t)((lo + hi) >> 1);
      if (gUnicode[mid] < unicode) lo = (uint16_t)(mid + 1);
      else hi = mid;
    }
    if (lo < gFont.gCount && gUnicode[lo] == unicode)
    {
      *index = lo;
      return true;
    }
    return false;
  }

  for (; lo < hi; lo++)
  {
    if (gUnicode[lo] == unicode)
    {
      *index = lo;
      return true;
    }
  }
  return false;
}


/***************************************************************************************
** Function name:           drawGlyph
** Description:             Write a character to the TFT cursor position
*************************************************************************************x*/
// Expects file to be open
void TFT_eSPI::drawGlyph(uint16_t code)
{
  uint16_t fg = textcolor;
  uint16_t bg = textbgcolor;

  // Check if cursor has moved
  if (last_cursor_x != cursor_x)
  {
    bg_cursor_x = cursor_x;
    last_cursor_x = cursor_x;
  }

  if (code < 0x21)
  {
    if (code == 0x20) {
      if (_fillbg) fillRect(bg_cursor_x, cursor_y, (cursor_x + gFont.spaceWidth) - bg_cursor_x, gFont.yAdvance, bg);
      cursor_x += gFont.spaceWidth;
      bg_cursor_x = cursor_x;
      last_cursor_x = cursor_x;
      return;
    }

    if (code == '\n') {
      cursor_x = 0;
      bg_cursor_x = 0;
      last_cursor_x = 0;
      cursor_y += gFont.yAdvance;
      if (textwrapY && (cursor_y >= height())) cursor_y = 0;
      return;
    }
  }

  uint16_t gNum = 0;
  bool found = getUnicodeIndex(code, &gNum);
  
  if (found)
  {

    if (textwrapX && (cursor_x + gWidth[gNum] + gdX[gNum] > width()))
    {
      cursor_y += gFont.yAdvance;
      cursor_x = 0;
      bg_cursor_x = 0;
    }
    if (textwrapY && ((cursor_y + gFont.yAdvance) >= height())) cursor_y = 0;
    if (cursor_x == 0) cursor_x -= gdX[gNum];

    uint8_t* pbuffer = nullptr;
    const uint8_t* gPtr = (const uint8_t*) gFont.gArray;

#ifdef FONT_FS_AVAILABLE
    if (fs_font)
    {
      fontFile.seek(gBitmap[gNum], fs::SeekSet);
      pbuffer =  (uint8_t*)malloc(gWidth[gNum]);
    }
#endif

    int16_t cy = cursor_y + gFont.maxAscent - gdY[gNum];
    int16_t cx = cursor_x + gdX[gNum];

    //  if (cx > width() && bg_cursor_x > width()) return;
    //  if (cursor_y > height()) return;

    int16_t  fxs = cx;
    uint32_t fl = 0;
    int16_t  bxs = cx;
    uint32_t bl = 0;
    int16_t  bx = 0;
    uint8_t pixel;

    startWrite(); // Avoid slow ESP32 transaction overhead for every pixel

    int16_t fillwidth  = 0;
    int16_t fillheight = 0;

    // Fill area above glyph
    if (_fillbg) {
      fillwidth  = (cursor_x + gxAdvance[gNum]) - bg_cursor_x;
      if (fillwidth > 0) {
        fillheight = gFont.maxAscent - gdY[gNum];
        // Could be negative
        if (fillheight > 0) {
          fillRect(bg_cursor_x, cursor_y, fillwidth, fillheight, textbgcolor);
        }
      }
      else {
        // Could be negative
        fillwidth = 0;
      }

      // Fill any area to left of glyph                              
      if (bg_cursor_x < cx) fillRect(bg_cursor_x, cy, cx - bg_cursor_x, gHeight[gNum], textbgcolor);
      // Set x position in glyph area where background starts
      if (bg_cursor_x > cx) bx = bg_cursor_x - cx;
      // Fill any area to right of glyph
      if (cx + gWidth[gNum] < cursor_x + gxAdvance[gNum]) {
        fillRect(cx + gWidth[gNum], cy, (cursor_x + gxAdvance[gNum]) - (cx + gWidth[gNum]), gHeight[gNum], textbgcolor);
      }
    }

    for (int32_t y = 0; y < gHeight[gNum]; y++)
    {
#ifdef FONT_FS_AVAILABLE
      if (fs_font) {
        if (spiffs)
        {
          fontFile.read(pbuffer, gWidth[gNum]);
          //Serial.println("SPIFFS");
        }
        else
        {
          endWrite();    // Release SPI for SD card transaction
          fontFile.read(pbuffer, gWidth[gNum]);
          startWrite();  // Re-start SPI for TFT transaction
          //Serial.println("Not SPIFFS");
        }
      }
#endif

      for (int32_t x = 0; x < gWidth[gNum]; x++)
      {
#ifdef FONT_FS_AVAILABLE
        if (fs_font) pixel = pbuffer[x];
        else
#endif
        pixel = pgm_read_byte(gPtr + gBitmap[gNum] + x + gWidth[gNum] * y);

        if (pixel)
        {
          if (bl) { drawFastHLine( bxs, y + cy, bl, bg); bl = 0; }
          if (pixel != 0xFF)
          {
            if (fl) {
              if (fl==1) drawPixel(fxs, y + cy, fg);
              else drawFastHLine( fxs, y + cy, fl, fg);
              fl = 0;
            }
            if (getColor) bg = getColor(x + cx, y + cy);
            drawPixel(x + cx, y + cy, alphaBlend(pixel, fg, bg));
          }
          else
          {
            if (fl==0) fxs = x + cx;
            fl++;
          }
        }
        else
        {
          if (fl) { drawFastHLine( fxs, y + cy, fl, fg); fl = 0; }
          if (_fillbg) {
            if (x >= bx) {
              if (bl==0) bxs = x + cx;
              bl++;
            }
          }
        }
      }
      if (fl) { drawFastHLine( fxs, y + cy, fl, fg); fl = 0; }
      if (bl) { drawFastHLine( bxs, y + cy, bl, bg); bl = 0; }
    }

    // Fill area below glyph
    if (fillwidth > 0) {
      fillheight = (cursor_y + gFont.yAdvance) - (cy + gHeight[gNum]);
      if (fillheight > 0) {
        fillRect(bg_cursor_x, cy + gHeight[gNum], fillwidth, fillheight, textbgcolor);
      }
    }

    if (pbuffer) free(pbuffer);
    cursor_x += gxAdvance[gNum];
    endWrite();
  }
  else
  {
    // Point code not in font so draw a rectangle and move on cursor
    drawRect(cursor_x, cursor_y + gFont.maxAscent - gFont.ascent, gFont.spaceWidth, gFont.ascent, fg);
    cursor_x += gFont.spaceWidth + 1;
  }
  bg_cursor_x = cursor_x;
  last_cursor_x = cursor_x;
}

/***************************************************************************************
** Function name:           showFont
** Description:             Page through all characters in font, td ms between screens
*************************************************************************************x*/
void TFT_eSPI::showFont(uint32_t td)
{
  if(!fontLoaded) return;

  int16_t cursorX = width(); // Force start of new page to initialise cursor
  int16_t cursorY = height();// for the first character
  uint32_t timeDelay = 0;    // No delay before first page

  fillScreen(textbgcolor);
  
  for (uint16_t i = 0; i < gFont.gCount; i++)
  {
    // Check if this will need a new screen
    if (cursorX + gdX[i] + gWidth[i] >= width())  {
      cursorX = -gdX[i];

      cursorY += gFont.yAdvance;
      if (cursorY + gFont.maxAscent + gFont.descent >= height()) {
        cursorX = -gdX[i];
        cursorY = 0;
        delay(timeDelay);
        timeDelay = td;
        fillScreen(textbgcolor);
      }
    }

    setCursor(cursorX, cursorY);
    drawGlyph(gUnicode[i]);
    cursorX += gxAdvance[i];
    yield();
  }

  delay(timeDelay);
  fillScreen(textbgcolor);
}
