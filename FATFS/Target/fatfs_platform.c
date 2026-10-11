/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : fatfs_platform.c
  * @brief          : fatfs_platform source file
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
#include "fatfs_platform.h"

uint8_t	BSP_PlatformIsDetected(void) {
    uint8_t status = SD_PRESENT;
    /* Check SD card detect pin */
    if(HAL_GPIO_ReadPin(SD_DETECT_GPIO_PORT, SD_DETECT_PIN) != GPIO_PIN_RESET)
    {
        status = SD_NOT_PRESENT;
    }
    /* USER CODE BEGIN 1 */
    // Achado em bancada (2026-10): a logica gerada acima assume LOW =
    // cartao presente (convencao comum de soquete SD), mas o switch de
    // deteccao desta placa e' invertido na pratica — medido direto via
    // g_storage_stats.card_detected: PD0 le HIGH com o cartao inserido,
    // LOW sem cartao (ha' pull-up externo na placa, confirmado pelo
    // usuario). Inverte aqui, dentro de USER CODE — sobrevive a
    // regeneracao do CubeMX — em vez de editar a linha gerada acima.
    status = (status == SD_PRESENT) ? SD_NOT_PRESENT : SD_PRESENT;
    /* USER CODE END 1 */
    return status;
}
