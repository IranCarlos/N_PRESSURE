/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file   fatfs.c
  * @brief  Code for fatfs applications
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */
#include "fatfs.h"

uint8_t retSD;    /* Return value for SD */
char SDPath[4];   /* SD logical drive path */
FATFS SDFatFS;    /* File system object for SD logical drive */
FIL SDFile;       /* File object for SD */

/* USER CODE BEGIN Variables */

/* Definido em main.c (gerado pelo CubeMX). Declarado aqui em vez de incluir
 * main.h para nao acoplar FATFS/App ao arquivo top-level — mesmo criterio
 * usado nos modulos de Core/Services. Timestamp de arquivo da trilha de
 * auditoria precisa do RTC de verdade (ver get_fattime abaixo). */
extern RTC_HandleTypeDef hrtc;

/* USER CODE END Variables */

void MX_FATFS_Init(void)
{
  /*## FatFS: Link the SD driver ###########################*/
  retSD = FATFS_LinkDriver(&SD_Driver, SDPath);

  /* USER CODE BEGIN Init */
  /* additional user code for init */
  /* USER CODE END Init */
}

/**
  * @brief  Gets Time from RTC
  * @param  None
  * @retval Time in DWORD
  */
DWORD get_fattime(void)
{
  /* USER CODE BEGIN get_fattime */
  // Antes retornava 0 sempre (todo arquivo ficava com data 1980-01-01) —
  // sem valor para a trilha de auditoria. GetTime antes de GetDate: exigido
  // pelo HAL para travar os registradores sombra do calendario ate a
  // leitura de DR (mesmo criterio de RtcService_CheckAtBoot).
  RTC_TimeTypeDef time;
  RTC_DateTypeDef date;
  uint32_t year_origin_1980;

  if ((HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) != HAL_OK) ||
      (HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) != HAL_OK)) {
    return 0; // formato invalido do FatFS = "nao disponivel"; nunca usado como erro fatal
  }

  // RTC_DateTypeDef.Year e' 0-99 (2000+Year); get_fattime() quer origem 1980.
  year_origin_1980 = (uint32_t)(2000 + date.Year - 1980);

  return ((DWORD)year_origin_1980 << 25) |
         ((DWORD)date.Month << 21) |
         ((DWORD)date.Date << 16) |
         ((DWORD)time.Hours << 11) |
         ((DWORD)time.Minutes << 5) |
         ((DWORD)(time.Seconds / 2u));
  /* USER CODE END get_fattime */
}

/* USER CODE BEGIN Application */

/* USER CODE END Application */
