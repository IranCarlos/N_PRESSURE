/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-07-28
 * Modified: 2026-08-03
 */

/**
 * @file hmi_ui.h
 * @brief Interface para a Interface Homem-Máquina (IHM) usando LVGL.
 *
 * @copyright Copyright (c) 2026
 *
 * @details Este módulo é responsável por criar todas as telas, widgets e
 * animações da interface gráfica. Ele também gerencia a navegação via
 * encoder rotativo e a máquina de estados da UI, que reage às mudanças
 * no estado do sistema.
 */

#ifndef HMI_UI_H
#define HMI_UI_H

#include "system_types.h"
#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Inicializa a IHM.
 *
 * Configura o LVGL, o driver de input (encoder), cria o grupo de input e
 * inicia a primeira tela (tela de boot).
 *
 * @param p_runtime_data Ponteiro para a estrutura de dados de tempo de execução.
 */
void HMI_Init(SystemRuntime_t* p_runtime_data, SystemConfig_t* p_system_cfg);

/**
 * @brief Atualiza a IHM.
 *
 * Esta função deve ser chamada continuamente no super loop (while(1)).
 * Ela é responsável por atualizar os valores dos widgets na tela (ex: pressão)
 * e gerenciar as transições de tela com base no estado do sistema.
 */
void HMI_Update(void);

#ifdef __cplusplus
}
#endif

#endif /* HMI_UI_H */