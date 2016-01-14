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

#ifndef _PICOOS_U_SPIFFS_H
#define _PICOOS_U_SPIFFS_H
/**
 * @file    picoos-u-spiffs.h
 * @brief   Include file of picoos-micro SPIFFS add-on for pico]OS
 * @author  Ari Suutari <ari@stonepile.fi>
 */

/**
 * @mainpage picoos-micro-spiffs - SPIFFS add-on for Pico]OS &mu;-layer
 * <b> Table Of Contents </b>
 * - @ref api
 * - @ref config
 * @section overview Overview
 * This library integrates SPIFFS into Pico]OS &mu;-layer filesystem API.
 */

/** @defgroup api   &mu;-layer API */
/** @defgroup config   Configuration */

#ifdef __cplusplus
extern "C"
{
#endif /* __cplusplus */

#include <stdint.h>
#include <stdbool.h>

#include <spiflash.h>
#include <spiffs.h>

/**
 * @ingroup api
 * @{
 */

typedef struct {

  UosSpiDevConf base;
  struct {
    spiflash_config_t  cf;
    spiflash_cmd_tbl_t cmds;
  } spiflash;
} UosFlashConf;

typedef struct {
  UosSpiDev base;
  spiflash_t spif;
  bool haveCs;
} UosFlashDev;

void uosFlashInit(UosFlashDev* dev, const UosFlashConf* cf, UosSpiBus* bus);

int uosMountSpiffs(const char* mountPoint, UosFlashDev* dev, spiffs_config* cfg);

/** @} */

#ifdef __cplusplus
} // extern "C"
#endif /* __cplusplus */
#endif
