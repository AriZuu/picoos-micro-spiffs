/*
 * Copyright (c) 2016, Ari Suutari <ari@stonepile.fi>.
 * All rights reserved. 
 * 
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 
 *  1. Redistributions of source code must retain the above copyright
 *     notice, this list of conditions and the following disclaimer.
 *  2. Redistributions in binary form must reproduce the above copyright
 *     notice, this list of conditions and the following disclaimer in the
 *     documentation and/or other materials provided with the distribution.
 *  3. The name of the author may not be used to endorse or promote
 *     products derived from this software without specific prior written
 *     permission. 
 * 
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT,  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <picoos.h>
#include <picoos-u.h>
#include <picoos-u-spiffs.h>
#include <spiflash.h>

static int flashTxRx(spiflash_t* spi,
                     const uint8_t* tx_data,
                     uint32_t tx_len,
                     uint8_t* rx_data,
                     uint32_t rx_len);
static void flashCs(spiflash_t * spi, uint8_t cs);
static void flashWait(spiflash_t * spi, uint32_t ms);

static const spiflash_hal_t my_spiflash_hal = {

  ._spiflash_spi_txrx = flashTxRx,
  ._spiflash_spi_cs   = flashCs,
  ._spiflash_wait     = flashWait
};

void uosFlashInit(UosFlashDev* dev, const UosFlashConf* cf, UosSpiBus* bus)
{
  uosSpiDevInit(&dev->base, &cf->base, bus);

  dev->haveCs = false;
  SPIFLASH_init(&dev->spif,
                &cf->spiflash.cf,
                &cf->spiflash.cmds,
                &my_spiflash_hal,
                0,
                SPIFLASH_SYNCHRONOUS,
                dev);
}

static int flashTxRx(spiflash_t* spi,
                     const uint8_t* tx_data,
                     uint32_t tx_len,
                     uint8_t* rx_data,
                     uint32_t rx_len)
{
  UosFlashDev* dev = (UosFlashDev*)spi->user_data;

  if (tx_len > 0)
    uosSpiXmit(&dev->base, tx_data, tx_len);

  if (rx_len > 0)
    uosSpiRcvr(&dev->base, rx_data, rx_len);

  return SPIFLASH_OK;
}

static void flashCs(spiflash_t * spi, uint8_t cs)
{
  UosFlashDev* dev = (UosFlashDev*)spi->user_data;

  if (cs) {

    if (!dev->haveCs) {

      uosSpiBegin(&dev->base);
      dev->haveCs = true;
    }
  }
  else {

    if (dev->haveCs) {

      dev->haveCs = false;
      uosSpiEnd(&dev->base);
    }
  }
}

static void flashWait(spiflash_t * spi, uint32_t ms)
{
  posTaskSleep(MS(ms));
}

