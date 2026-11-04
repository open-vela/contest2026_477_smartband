/* SPDX-License-Identifier: Apache-2.0 */
/* Included by ili9341.c after the resolution helpers. Synchronous ownership:
 * sendgram must finish consuming its source before returning. */
static int ili9341_putarea(FAR struct lcd_dev_s *lcd_dev,
                          fb_coord_t row_start, fb_coord_t row_end,
                          fb_coord_t col_start, fb_coord_t col_end,
                          FAR const uint8_t *buffer, fb_coord_t stride)
{
  FAR struct ili9341_dev_s *dev = (FAR struct ili9341_dev_s *)lcd_dev;
  FAR struct ili9341_lcd_s *lcd = dev->lcd;
  size_t width, rows, row_bytes;
  int ret = 0;
  if (!buffer || ((uintptr_t)buffer & 1) ||
      row_end < row_start || col_end < col_start ||
      row_end >= ili9341_getyres(dev) || col_end >= ili9341_getxres(dev))
    return -EINVAL;
  width = col_end - col_start + 1;
  rows = row_end - row_start + 1;
  row_bytes = width * sizeof(uint16_t);
  if (stride == 0) stride = row_bytes;
  if ((size_t)stride < row_bytes || (stride & 1)) return -EINVAL;
  lcd->select(lcd);
#define AREA_SEND(call) do { ret = (call); if (ret < 0) goto done; } while (0)
  AREA_SEND(lcd->sendcmd(lcd, ILI9341_COLUMN_ADDRESS_SET));
  AREA_SEND(lcd->sendparam(lcd, col_start >> 8));
  AREA_SEND(lcd->sendparam(lcd, col_start & 255));
  AREA_SEND(lcd->sendparam(lcd, col_end >> 8));
  AREA_SEND(lcd->sendparam(lcd, col_end & 255));
  AREA_SEND(lcd->sendcmd(lcd, ILI9341_PAGE_ADDRESS_SET));
  AREA_SEND(lcd->sendparam(lcd, row_start >> 8));
  AREA_SEND(lcd->sendparam(lcd, row_start & 255));
  AREA_SEND(lcd->sendparam(lcd, row_end >> 8));
  AREA_SEND(lcd->sendparam(lcd, row_end & 255));
  AREA_SEND(lcd->sendcmd(lcd, ILI9341_MEMORY_WRITE));
  if ((size_t)stride == row_bytes)
    {
      const uint16_t *pixels = (const uint16_t *)buffer;
      size_t remaining = width * rows;
      while (remaining)
        {
          /* R528 HAL bounces at most 4160 bytes. Keep arbitrary rectangle
           * lengths and malloc-backed conversion buffers within that path. */
          size_t count = remaining > 2048 ? 2048 : remaining;
          AREA_SEND(lcd->sendgram(lcd, pixels, count));
          pixels += count; remaining -= count;
        }
    }
  else
    {
      for (size_t row = 0; row < rows; row++)
        AREA_SEND(lcd->sendgram(lcd, (const uint16_t *)(buffer + row * stride), width));
    }
done:
  lcd->deselect(lcd);
#undef AREA_SEND
  return ret;
}
