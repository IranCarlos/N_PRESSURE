/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.c
  * @brief          : Main program body
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
/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "FreeRTOS.h"
#include "cmsis_os2.h"
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "adc_acq.h"
#include "valve_gpio.h"
#include "pwm_motor_tim.h"
#include "actuator_bench.h"
#include "control_loop.h"
#include "encoder.h"
#include "pedal.h"
#include "storage_task.h"
#include <stdio.h>
#include "hmi_ui.h"
#include "hmi_encoder_group.h"
#include "st7789.h"
#include "buzzer.h"
#include "eeprom_storage_io.h"
#include "fault_manager.h"
#include "port_dwt.h"
#include "supervisor.h"
#include "rtc_service.h"
#include <stm32h7xx_hal_flash_ex.h>
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

// Bring-up: 1 = pula LVGL/EEPROM/ADC/Control/HMI e testa display, encoder e
// buzzer isoladamente (SPI polling, sem DMA). Objetivo: validar cada
// periferico um a um antes de reativar a pilha completa (LVGL etc.).
// Reverter para 0 quando o bring-up estiver concluido.
#define APP_MODE_BRINGUP            (1)

// Dentro do bring-up, liga/desliga cada periferico individualmente para
// isolar problemas. Com ENCODER=0 o display roda um ciclo automatico de
// cores solidas (nao depende do encoder) — o teste mais direto para
// validar SPI + init do painel.
#define BRINGUP_ENABLE_DISPLAY      (1)
#define BRINGUP_ENABLE_ENCODER      (1)
#define BRINGUP_ENABLE_BUZZER       (1)

// 1 = troca o desenho cru (barra/ciclo de cores) por uma tela de teste LVGL
// renderizada via DMA (lv_timer_handler + flush com HAL_SPI_Transmit_DMA).
// Isola o pipeline LVGL+DMA+encoder ANTES de religar EEPROM/ADC/Control_Loop/
// HMI_UI — mesma filosofia incremental do resto do bring-up. Precisa de
// BRINGUP_ENABLE_DISPLAY=1 e BRINGUP_ENABLE_ENCODER=1 (usa o encoder como
// dispositivo de entrada do LVGL).
#define BRINGUP_ENABLE_LVGL         (1)

// 1 = liga o sensor de pressao (ADC2 disparado pelo TIM15 a 1 kHz + DMA
// circular) no bring-up, sem Control_Loop/EEPROM. A leitura fica em
// g_pressure_diag (Live Watch: pressure_kpa, last_p2p_counts = ruido,
// flags). Estado do sistema fixo em IDLE => baseline atmosferico rastreado.
#define BRINGUP_ENABLE_PRESSURE     (1)

// 1 = bancada de atuadores: bomba (TIM3_CH1) e valvulas V1/V2/V3 comandadas
// pelo Live Watch, SEM a malha de controle (ver actuator_bench.h). Exige
// BRINGUP_ENABLE_PRESSURE=1 (usa a pressao para o intertravamento). Comece com
// a MANGUEIRA DESCONECTADA do manguito. Variaveis: g_act_arm (0/1),
// g_act_pump (0..1, limitada a 0.30), g_act_valves (bit0=V1, bit1=V2, bit2=V3;
// 1 = aberta). Trip por sobrepressao/sensor/tempo de bomba: ver g_act_bench.trip.
#define BRINGUP_ENABLE_ACTUATORS    (0)

// 1 = ensaio automatico de verdade (Fase 4): Control_Loop/Control_Core
// comandam bomba+valvulas pela maquina de estados real (IDLE->INFLATING->
// STABILIZING->STABILIZED->DEFLATING_FAST), com as protecoes de seguranca
// descritas em Docs/dev/sim-report.md (sobrepressao, sensor invalido,
// vazamento, timeouts). Mutuamente exclusivo com BRINGUP_ENABLE_ACTUATORS
// (os dois comandam os mesmos atuadores) e exige BRINGUP_ENABLE_PRESSURE=1.
// NUNCA testado em hardware ainda — ver o aviso de bancada no bloco de
// globais logo abaixo antes de habilitar.
#define BRINGUP_ENABLE_CONTROL      (1)

// 1 = storage_task de verdade (Fase 5): monta o cartao SD, processa a fila
// (StorageTask_Log*/SubmitTestRecord) e grava nos arquivos do Apendice C.
// NUNCA testado em hardware — primeira vez que este projeto escreve no
// SD. Com a flag ligada, appTask tambem enfileira um evento de teste a
// cada ~3 s (string crescente), so' para exercitar o caminho fila ->
// gravacao -> hash encadeado sem depender do ensaio completo. Ver
// checklist de bancada em 00-auditoria.md antes de habilitar. Diagnostico:
// g_storage_stats (Live Watch) — sd_mounted, messages_processed,
// write_errors, etc.
#define BRINGUP_ENABLE_STORAGE      (0)

#define TASK_ENCODER_PERIOD_MS      (1u)
#define TASK_CONTROL_PERIOD_MS      (1u)
#define TASK_HMI_PERIOD_MS          (20u)
#define TASK_LVGL_HANDLER_PERIOD_MS (15u)
#define TASK_IWDG_REFRESH_PERIOD_MS (1u)
// Janela de avaliacao do supervisor (safetyTask). Bem menor que o timeout do
// IWDG (~8,2 s) e maior que o periodo da tarefa critica mais lenta (appTask,
// 20 ms) + o bloqueio do beep de bring-up (15 ms), com folga.
#define SUPERVISOR_WINDOW_MS        (100u)

// Teste de bancada do Fault_Manager (criterio de aceitacao da Fase 1: "hard
// fault forcado e' registrado e sobrevive ao reset"). 0 = desligado (padrao).
// Com N > 0, a safetyTask forca um HardFault N ms apos o boot. Com o
// depurador anexado o GDB para na entrada do HardFault_Handler (vector
// catch): de "Continue" para o handler gravar o registro e resetar. No boot
// seguinte g_boot_fault_pending == 1 e g_boot_fault_record traz r0-r3, r12,
// lr, pc, psr, CFSR, HFSR, MMFAR e BFAR (Live Watch), e o appTask toca 3
// bipes graves. VOLTAR A 0 depois do teste.
#define FAULT_TEST_FORCE_HARDFAULT_MS (0u)

// MX_RTC_Init() (gerado pelo CubeMX, nao editavel fora dos marcadores) chama
// HAL_RTC_SetTime()/SetDate() incondicionalmente a cada boot, com data/hora
// fixas (00:00:00, 01/01) — sem essa guarda, o calendario seria resetado a
// cada ligamento mesmo com o RTC alimentado por VBAT. RTC_BKP_DR13 marca
// "calendario ja inicializado alguma vez" (RTC_BKP_DR0..12 sao usados pelo
// Fault_Manager — ver fault_manager.c). So' e' escrito de novo se a bateria
// de backup for removida/descarregar (perda do dominio de backup).
#define RTC_CALENDAR_INITIALIZED_BKP_REG (RTC_BKP_DR13)
#define RTC_CALENDAR_INITIALIZED_MAGIC   (0x32F2u)

// Trava de seguranca de hardware: esta placa alimenta o nucleo pelo LDO
// interno (H723VGT6 em LQFP100, sem SMPS). PWR_EXTERNAL_SOURCE_SUPPLY liga o
// bypass do LDO — sem tensao de nucleo externa o chip morre, e como o bit so'
// e' limpo por power-on-reset, nem reset nem debug reconectam ate' cortar a
// energia. Se o CubeMX regenerar com essa fonte de novo, o build para aqui em
// vez de gerar um firmware que "mata" a placa.
#if defined(USE_PWR_EXTERNAL_SOURCE_SUPPLY) || defined(USE_PWR_DIRECT_SMPS_SUPPLY)
#error "Fonte de alimentacao do nucleo deve ser PWR_LDO_SUPPLY (CubeMX: Clock Configuration / PWR -> Supply Source = LDO). Ver Docs/dev/00-auditoria.md."
#endif

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc2;

I2C_HandleTypeDef hi2c1;

IWDG_HandleTypeDef hiwdg1;

RTC_HandleTypeDef hrtc;

SD_HandleTypeDef hsd1;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;
TIM_HandleTypeDef htim8;
TIM_HandleTypeDef htim15;

PCD_HandleTypeDef hpcd_USB_OTG_HS;
HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* Definitions for controlTask */
osThreadId_t controlTaskHandle;
uint32_t controlTaskBuffer[ 256 ];
osStaticThreadDef_t controlTaskControlBlock;
const osThreadAttr_t controlTask_attributes = {
  .name = "controlTask",
  .cb_mem = &controlTaskControlBlock,
  .cb_size = sizeof(controlTaskControlBlock),
  .stack_mem = &controlTaskBuffer[0],
  .stack_size = sizeof(controlTaskBuffer),
  .priority = (osPriority_t) osPriorityRealtime,
};
/* Definitions for safetyTask */
osThreadId_t safetyTaskHandle;
uint32_t safetyTaskBuffer[ 128 ];
osStaticThreadDef_t safetyTaskControlBlock;
const osThreadAttr_t safetyTask_attributes = {
  .name = "safetyTask",
  .cb_mem = &safetyTaskControlBlock,
  .cb_size = sizeof(safetyTaskControlBlock),
  .stack_mem = &safetyTaskBuffer[0],
  .stack_size = sizeof(safetyTaskBuffer),
  .priority = (osPriority_t) osPriorityHigh,
};
/* Definitions for inputTask */
osThreadId_t inputTaskHandle;
uint32_t inputTaskBuffer[ 256 ];
osStaticThreadDef_t inputTaskControlBlock;
const osThreadAttr_t inputTask_attributes = {
  .name = "inputTask",
  .cb_mem = &inputTaskControlBlock,
  .cb_size = sizeof(inputTaskControlBlock),
  .stack_mem = &inputTaskBuffer[0],
  .stack_size = sizeof(inputTaskBuffer),
  .priority = (osPriority_t) osPriorityAboveNormal,
};
/* Definitions for appTask */
osThreadId_t appTaskHandle;
uint32_t appTaskBuffer[ 256 ];
osStaticThreadDef_t appTaskControlBlock;
const osThreadAttr_t appTask_attributes = {
  .name = "appTask",
  .cb_mem = &appTaskControlBlock,
  .cb_size = sizeof(appTaskControlBlock),
  .stack_mem = &appTaskBuffer[0],
  .stack_size = sizeof(appTaskBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for usbProtoTask */
osThreadId_t usbProtoTaskHandle;
uint32_t usbProtoTaskBuffer[ 512 ];
osStaticThreadDef_t usbProtoTaskControlBlock;
const osThreadAttr_t usbProtoTask_attributes = {
  .name = "usbProtoTask",
  .cb_mem = &usbProtoTaskControlBlock,
  .cb_size = sizeof(usbProtoTaskControlBlock),
  .stack_mem = &usbProtoTaskBuffer[0],
  .stack_size = sizeof(usbProtoTaskBuffer),
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for storageTask */
osThreadId_t storageTaskHandle;
uint32_t storageTaskBuffer[ 512 ];
osStaticThreadDef_t storageTaskControlBlock;
const osThreadAttr_t storageTask_attributes = {
  .name = "storageTask",
  .cb_mem = &storageTaskControlBlock,
  .cb_size = sizeof(storageTaskControlBlock),
  .stack_mem = &storageTaskBuffer[0],
  .stack_size = sizeof(storageTaskBuffer),
  .priority = (osPriority_t) osPriorityBelowNormal,
};
/* Definitions for uiTask */
osThreadId_t uiTaskHandle;
uint32_t uiTaskBuffer[ 1024 ];
osStaticThreadDef_t uiTaskControlBlock;
const osThreadAttr_t uiTask_attributes = {
  .name = "uiTask",
  .cb_mem = &uiTaskControlBlock,
  .cb_size = sizeof(uiTaskControlBlock),
  .stack_mem = &uiTaskBuffer[0],
  .stack_size = sizeof(uiTaskBuffer),
  .priority = (osPriority_t) osPriorityLow,
};
/* Definitions for audioTask */
osThreadId_t audioTaskHandle;
uint32_t audioTaskBuffer[ 128 ];
osStaticThreadDef_t audioTaskControlBlock;
const osThreadAttr_t audioTask_attributes = {
  .name = "audioTask",
  .cb_mem = &audioTaskControlBlock,
  .cb_size = sizeof(audioTaskControlBlock),
  .stack_mem = &audioTaskBuffer[0],
  .stack_size = sizeof(audioTaskBuffer),
  .priority = (osPriority_t) osPriorityLow,
};
/* USER CODE BEGIN PV */
// Estruturas de dados globais para o estado do sistema. So' o caminho de
// producao (!APP_MODE_BRINGUP) usa — em bring-up ficariam "definidas e nao
// usadas" (-Wunused-variable).
#if !APP_MODE_BRINGUP
static SystemConfig_t g_system_cfg;
static SystemRuntime_t g_runtime_data;
#endif

// Margem minima de pilha (bytes ainda "virgens", o high-water-mark do
// FreeRTOS) de cada tarefa, atualizada pela safetyTask a cada 500 ms. Ordem:
// control, safety, input, app, usbProto, storage, ui, audio. Para diagnosticar
// estouro de pilha, adicione g_task_stack_free_bytes ao Live Watch: valor
// proximo de 0 = pilha daquela tarefa esta no limite.
volatile uint32_t g_task_stack_free_bytes[8];

// Estado do heap do LVGL (LV_MEM_SIZE), atualizado pela uiTask a cada 500 ms.
// Para diagnostico no Live Watch: max_used proximo de LV_MEM_SIZE (65536) ou
// free_biggest_size pequeno = heap esgotando (lv_malloc devolve NULL e o
// LVGL nao checa em varios pontos do desenho -> escrita em NULL -> HardFault).
lv_mem_monitor_t g_lv_mem;

// Estado do supervisor (ver StartSafetyTask): mascara de tarefas que fizeram
// check-in na ultima janela e contador de janelas com falta de check-in.
#if (APP_MODE_BRINGUP && BRINGUP_ENABLE_PRESSURE)
static const SensorConfig_t s_bringup_sensor_cal = { .offset_kpa = 0.0f, .scale = 1.0f };
static SystemRuntime_t s_bringup_runtime = { .state = SYSTEM_STATE_IDLE };
const PressureCore_t *g_pressure_diag; // Live Watch: expandir para ver ruido/flags
#endif
#if (APP_MODE_BRINGUP && BRINGUP_ENABLE_ACTUATORS)
#if !BRINGUP_ENABLE_PRESSURE
#error "BRINGUP_ENABLE_ACTUATORS exige BRINGUP_ENABLE_PRESSURE=1 (intertravamento por pressao)"
#endif
volatile uint8_t g_act_arm;                          // 0 = seguro
volatile float   g_act_pump;                         // 0..1 (limitada em 0.30)
volatile uint8_t g_act_valves = ACTBENCH_ALL_OPEN;   // bit=1 -> aberta
ActuatorBench_t     g_act_bench;
ActuatorBench_Out_t g_act_out = { 0.0f, ACTBENCH_ALL_OPEN };
#endif
#if (APP_MODE_BRINGUP && BRINGUP_ENABLE_CONTROL)
#if !BRINGUP_ENABLE_PRESSURE
#error "BRINGUP_ENABLE_CONTROL exige BRINGUP_ENABLE_PRESSURE=1 (precisa de leitura real de pressao)"
#endif
#if BRINGUP_ENABLE_ACTUATORS
#error "BRINGUP_ENABLE_CONTROL e BRINGUP_ENABLE_ACTUATORS sao mutuamente exclusivos (os dois comandam bomba/valvulas)"
#endif
// Fase 4: ensaio automatico de verdade, pela 1a vez no hardware. Comece com
// o setpoint BAIXO (ver g_bringup_sys_cfg.current_setpoint_mmhg, default 40
// mmHg aqui) e um reservatorio/manguito de volume conhecido — NUNCA o
// manguito direto num paciente ou membro nesta fase. Suba o setpoint aos
// poucos pelo Live Watch so' depois de confirmar que insufla/desinfla e
// estabiliza em baixa pressao. Pedal ou botao do encoder: 1o clique insufla,
// clique durante o ensaio desinfla (mesma logica do caminho de producao).
// Diagnostico: g_control_runtime->state/pressure_display, g_control_last_error
// (ver ControlCore_Error_t em control_core.h), g_control_jitter_us.
SystemConfig_t   g_bringup_sys_cfg;
SystemRuntime_t *g_control_runtime; // aponta para s_bringup_runtime (compartilhado com ADC_Acq)
volatile uint32_t g_control_last_error;
volatile uint32_t g_control_jitter_us;
// HUD de bancada (display+encoder) para nao depender do Live Watch/Debug
// Console (a extensao do VS Code usada neste projeto nao suporta editar
// variavel: stm32cube-ide-debug-core 1.5.0 so' tem addWatchExpr/getCopyValue,
// sem setValue). O arco ja usado no bring-up do LVGL vira o knob do setpoint
// (girar o encoder = sobe/desce o setpoint, ate' ARC_SETPOINT_MAX_MMHG);
// os labels abaixo mostram estado/pressao/erro. Atualizados so' pela uiTask
// (unico lugar que toca LVGL, ja documentado nao-thread-safe).
#define ARC_SETPOINT_MAX_MMHG (150)
static lv_obj_t *s_ctrl_arc;
static lv_obj_t *s_ctrl_label_state;
static lv_obj_t *s_ctrl_label_pressure;
static lv_obj_t *s_ctrl_label_error;
static const char *const s_state_names[] = {
    "BOOT", "IDLE", "INFLATING", "STABILIZING", "STABILIZED",
    "DEFLATING", "LEAK_TEST", "CONFIG", "FAULT"
};

/** @brief 1 casa decimal sem usar %f (newlib-nano/--specs=nano.specs nao
 *         linka printf_float por padrao neste projeto — ver cmake/gcc-arm-
 *         none-eabi.cmake). So' %d/%s, funciona sem mudar o toolchain. */
static void format_1dp(float v, char *buf, size_t n) {
    uint8_t neg = (v < 0.0f) ? 1u : 0u;
    float av = neg ? -v : v;
    int whole = (int)av;
    int frac = (int)(((av - (float)whole) * 10.0f) + 0.5f);
    if (frac >= 10) { frac = 0; whole += 1; }
    snprintf(buf, n, "%s%d.%d", neg ? "-" : "", whole, frac);
}
#endif
#if (APP_MODE_BRINGUP && BRINGUP_ENABLE_STORAGE)
StorageTask_Stats_t g_storage_stats;
static uint32_t s_storage_test_seq;
#endif
volatile uint32_t g_supervisor_last_mask;
volatile uint32_t g_supervisor_missed_windows;

// Fault do boot anterior, recuperado dos registradores de backup do RTC por
// FaultManager_Init(). Valido so' se g_boot_fault_pending != 0.
// RCC->RSR do boot: PORRSTF/PINRSTF (0x00..), BORRSTF (bit 21, brown-out),
// SFTRSTF (bit 24, NVIC_SystemReset), IWDG1RSTF (bit 26, watchdog).
volatile uint32_t g_boot_rsr;

// 1 quando o calendario precisou ser reinicializado neste boot (o backup
// register RTC_CALENDAR_INITIALIZED_BKP_REG nao tinha o magic): 1o boot de
// sempre, OU o dominio de backup foi resetado (perda de VBAT, ou troca da
// fonte de clock do RTC via CubeMX). Setado em MX_RTC_Init() (USER CODE
// BEGIN RTC_Init 2), consumido por RtcService_CheckAtBoot() logo depois.
volatile uint8_t  g_rtc_backup_domain_was_reset;
// Resultado de RtcService_CheckAtBoot (Live Watch): ver RtcValidityFlags_t
// em rtc_validity.h. 0 = calendario confiavel para timestamps de log/ensaio.
volatile uint32_t g_rtc_status;
volatile uint8_t g_boot_fault_pending;
FaultManager_Record_t g_boot_fault_record;
/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
void SystemClock_Config(void);
void PeriphCommonClock_Config(void);
static void MPU_Config(void);
static void MX_GPIO_Init(void);
static void MX_DMA_Init(void);
static void MX_I2C1_Init(void);
static void MX_IWDG1_Init(void);
static void MX_SPI1_Init(void);
static void MX_TIM2_Init(void);
static void MX_TIM3_Init(void);
static void MX_TIM4_Init(void);
static void MX_ADC2_Init(void);
static void MX_SDMMC1_SD_Init(void);
static void MX_USB_OTG_HS_PCD_Init(void);
static void MX_RTC_Init(void);
static void MX_TIM8_Init(void);
static void MX_TIM15_Init(void);
void StartControlTask(void *argument);
void StartSafetyTask(void *argument);
void StartInputTask(void *argument);
void StartAppTask(void *argument);
void StartUsbProtoTask(void *argument);
void StartStorageTask(void *argument);
void StartUiTask(void *argument);
void StartAudioTask(void *argument);

/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/**
  * @brief  The application entry point.
  * @retval int
  */
int main(void)
{

  /* USER CODE BEGIN 1 */
  /* Forca UNALIGN_TRP=0 (tolerar acesso desalinhado em LDR/LDRH/LDRB, valor
   * padrao da arquitetura ARMv7-M). Diagnostico: LVGL v9 usa lv_color_t como
   * struct de 3 bytes {blue,green,red} incondicionalmente (nao depende de
   * LV_COLOR_DEPTH), e lv_palette_main() gera um ldrh a partir de um indice
   * impar (ex.: LV_PALETTE_BLUE=5) contra esse array — isso e' um acesso de
   * 16 bits desalinhado por design da biblioteca, tolerado por hardware com
   * UNALIGN_TRP=0. Nada neste projeto liga esse bit explicitamente; esta
   * linha e' so uma garantia defensiva contra ele estar setado no reset.
   */

#ifdef DEBUG
  /* Somente builds Debug: DISDEFWBUF (ACTLR bit 1 do Cortex-M7; o CMSIS deste
   * projeto nao define a mascara) desliga o write buffer nos acessos ao mapa
   * de memoria padrao. Sem isso, um erro de barramento numa ESCRITA e'
   * "imprecise": e' reportado depois, numa instrucao qualquer, sem endereco
   * (BFAR invalido) — impossivel saber qual escrita falhou. Com o bit ligado
   * o BusFault vira preciso: o PC aponta a propria instrucao e o BFAR traz o
   * endereco invalido. Custa desempenho, por isso nao vai para Release. */
  SCnSCB->ACTLR |= (1UL << 1);
#endif

  __DSB();
  __ISB();
  /* USER CODE END 1 */

  /* MPU Configuration--------------------------------------------------------*/
  MPU_Config();

  /* Enable the CPU Cache */

  /* Enable I-Cache---------------------------------------------------------*/
  SCB_EnableICache();

  /* Enable D-Cache---------------------------------------------------------*/
  SCB_EnableDCache();

  /* MCU Configuration--------------------------------------------------------*/

  /* Reset of all peripherals, Initializes the Flash interface and the Systick. */
  HAL_Init();

  /* USER CODE BEGIN Init */
  // Sem isto, o IWDG continua contando enquanto o core esta parado num
  // breakpoint (o IWDG nao e' um periferico "congelavel" por natureza — so'
  // para se o DBGMCU disser explicitamente). Sem essa configuracao, qualquer
  // pausa no debugger mais longa que o timeout (~8,2 s) reseta a placa no
  // meio da sessao de debug, parecendo reset espontaneo.
  __HAL_DBGMCU_FREEZE_IWDG1();
  /* USER CODE END Init */

  /* Configure the system clock */
  SystemClock_Config();

  /* Configure the peripherals common clocks */
  PeriphCommonClock_Config();

  /* USER CODE BEGIN SysInit */

  /* USER CODE END SysInit */

  /* Initialize all configured peripherals */
  MX_GPIO_Init();
  MX_DMA_Init();
  MX_I2C1_Init();
  MX_IWDG1_Init();
  MX_SPI1_Init();
  MX_TIM2_Init();
  MX_TIM3_Init();
  MX_TIM4_Init();
  MX_ADC2_Init();
  MX_SDMMC1_SD_Init();
  MX_USB_OTG_HS_PCD_Init();
  MX_FATFS_Init();
  MX_RTC_Init();
  MX_TIM8_Init();
  MX_TIM15_Init();
  /* USER CODE BEGIN 2 */
  // Fase 1: captura de fault/reset persistente (RTC ja inicializado acima)
  // e contador de ciclos para medicao de jitter nas Fases 3/4. Rodam antes
  // de qualquer outro modulo para nao perder um fault ocorrido cedo demais.
  // BUG (aberto, 2026-10): RTC esta' em LSI, nao LSE — o LSE voltou a falhar
  // em startup mesmo com LSE Drive Capability = Medium High (2a falha
  // registrada; a 1a, com Low, motivou a troca para Medium High). RTC fica
  // sem persistencia de hora entre quedas de energia ate' isso ser resolvido
  // de verdade (ver Docs/dev/00-auditoria.md, secao LSE, para o historico e
  // proximos passos: inspecao fisica do cristal/capacitores de carga).
  g_boot_rsr = RCC->RSR; // antes de o FaultManager limpar: motivo do ultimo reset
  FaultManager_Init(&hrtc);
  PortDWT_Init();
  g_rtc_status = RtcService_CheckAtBoot(&hrtc, g_rtc_backup_domain_was_reset);
  g_boot_fault_pending = FaultManager_HasPendingFault();
  if (g_boot_fault_pending != 0u) {
    FaultManager_GetLastFault(&g_boot_fault_record);
  }

  /* Liga o backlight do display */
  HAL_GPIO_WritePin(TFT_BACKLIGHT_GPIO_Port, TFT_BACKLIGHT_Pin, GPIO_PIN_SET);

#if APP_MODE_BRINGUP
  // --- MODO BRING-UP: testa display, encoder e buzzer isoladamente ---
#if BRINGUP_ENABLE_BUZZER
  Buzzer_Init(&htim8);
#endif

#if BRINGUP_ENABLE_PRESSURE
  ADC_Acq_Init(&hadc2, &htim15, &s_bringup_sensor_cal, &s_bringup_runtime);
  g_pressure_diag = ADC_Acq_GetDiagnostics();
#endif

#if BRINGUP_ENABLE_CONTROL
  {
    const ControlLoop_ValvePins_t valve_pins = {
        .valve1 = { VALVE_1_GPIO_Port, VALVE_1_Pin },
        .valve2 = { VALVE_2_GPIO_Port, VALVE_2_Pin },
        .valve3 = { VALVE_3_GPIO_Port, VALVE_3_Pin },
    };
    EEPROM_Storage_LoadDefaults(&g_bringup_sys_cfg);
    // 1o teste em hardware de verdade: setpoint baixo de proposito, mesmo
    // com o default da EEPROM sendo 120 mmHg — suba pelo Live Watch
    // (g_bringup_sys_cfg.current_setpoint_mmhg) so' depois de confirmar
    // que o ciclo completo (inflar/estabilizar/desinflar) funciona aqui.
    g_bringup_sys_cfg.current_setpoint_mmhg = 40.0f;
    g_control_runtime = &s_bringup_runtime;
    Pedal_Init(&htim4); // nao chamado em nenhum outro lugar do bring-up
    ControlLoop_Init(&g_bringup_sys_cfg, &htim3, g_control_runtime, &valve_pins);
  }
#endif

#if BRINGUP_ENABLE_STORAGE
  StorageTask_InitQueue(); // cria a fila; storageTask monta o SD sozinha ao rodar
#endif

#if BRINGUP_ENABLE_ACTUATORS
  {
    const Valve_GpioMap_t map = { .valve = {
        { VALVE_1_GPIO_Port, VALVE_1_Pin },
        { VALVE_2_GPIO_Port, VALVE_2_Pin },
        { VALVE_3_GPIO_Port, VALVE_3_Pin },
    } };
    Valve_InitGpio(&map);                       // todas abertas
    PwmMotor_InitTim(&htim3, TIM_CHANNEL_1);    // CCR = 0
    PwmMotor_SetLimit(ACTBENCH_PUMP_MAX);
    ActuatorBench_Init(&g_act_bench);
  }
#endif

#if (BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL)
  ST7789_Init();
  ST7789_FillRect(0, 0, 319, 239, 0x0000); // limpa a tela (preto)

#if BRINGUP_ENABLE_BUZZER
  // Diagnostico: se alguma transmissao SPI falhou durante o init/clear acima,
  // toca um bipe grave e distinto (200Hz) para avisar imediatamente. Se NAO
  // bipar aqui, a SPI/HAL esta ok e o problema esta no painel/fiacao/timing,
  // nao no software de transmissao.
  if (ST7789_GetSpiErrorCount() > 0u) {
    Buzzer_Beep(200u, 500u);
  }
#endif
#endif /* BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL */

#if BRINGUP_ENABLE_ENCODER
  Encoder_Init(&htim2);
#endif

#if (BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL)
  // --- Estagio LVGL+DMA: renderizacao via lv_port_disp (flush por
  //     HAL_SPI_Transmit_DMA) + entrada via encoder, isolado de
  //     EEPROM/ADC_Acq/Control_Loop/HMI_UI. ---
  lv_init();
  lv_port_disp_init(); // chama ST7789_Init() internamente (SPI polling)

#if BRINGUP_ENABLE_BUZZER
  // Mesmo diagnostico do path sem LVGL: bipe de alarme se a sequencia de
  // init do painel (via SPI polling, dentro de ST7789_Init) falhou.
  if (ST7789_GetSpiErrorCount() > 0u) {
    Buzzer_Beep(200u, 500u);
  }
#endif

  lv_port_indev_init();

  {
    lv_obj_t *scr = lv_screen_active();
    lv_obj_t *title = lv_label_create(scr);
    lv_obj_t *arc = lv_arc_create(scr);
    static lv_obj_t *s_bringup_focus_objs[1];
    HMI_EncoderGroupConfig_t group_cfg = {0};

    lv_label_set_text(title, "LVGL + DMA bring-up");
    // Tema padrao do LVGL aqui e' claro (LV_THEME_DEFAULT_DARK=0, fundo
    // branco) — cor de texto preta explicita para garantir contraste em
    // vez de depender do estilo default do tema.
    lv_obj_set_style_text_color(title, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 10);

    lv_obj_set_size(arc, 180, 180);
    lv_obj_align(arc, LV_ALIGN_CENTER, 0, 10);
#if BRINGUP_ENABLE_CONTROL
    // HUD de bancada: o arco vira o knob do setpoint (ver uiTask), em mmHg.
    lv_label_set_text(title, "Fase 4: ensaio de controle");
    lv_arc_set_range(arc, 0, ARC_SETPOINT_MAX_MMHG);
    lv_arc_set_value(arc, (int16_t)g_bringup_sys_cfg.current_setpoint_mmhg);
    s_ctrl_arc = arc;

    s_ctrl_label_state = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ctrl_label_state, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_ctrl_label_state, LV_ALIGN_TOP_MID, 0, 30);

    s_ctrl_label_pressure = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ctrl_label_pressure, lv_color_hex(0xFFFFFF), 0);
    lv_obj_align(s_ctrl_label_pressure, LV_ALIGN_BOTTOM_MID, 0, -30);

    s_ctrl_label_error = lv_label_create(scr);
    lv_obj_set_style_text_color(s_ctrl_label_error, lv_color_hex(0xFF4040), 0);
    lv_obj_align(s_ctrl_label_error, LV_ALIGN_BOTTOM_MID, 0, -10);
#else
    lv_arc_set_range(arc, 0, 50);
    lv_arc_set_value(arc, 25);
#endif

    HMI_EncoderGroup_Init();
    HMI_EncoderGroup_BindIndev(lv_port_indev_get_default());
    s_bringup_focus_objs[0] = arc;
    group_cfg.focus_objs = s_bringup_focus_objs;
    group_cfg.count = 1u;
    group_cfg.initial_focus_idx = 0u;
    group_cfg.wrap = false;
    HMI_EncoderGroup_Load(&group_cfg);

    // So' ha' 1 objeto no grupo, entao o modo normal de navegacao (girar =
    // troca de foco entre objetos) nao teria efeito visivel — forca direto
    // o modo de edicao, assim girar o encoder ja manipula o valor do arco
    // sem precisar apertar o botao antes. E' so' pra esse teste isolado;
    // quando o HMI_UI real entrar (varias telas/widgets), o fluxo padrao
    // (apertar pra entrar/sair de edicao) volta a fazer sentido.
    HMI_EncoderGroup_SetEditing(true);
  }
#endif /* BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL */

#if (BRINGUP_ENABLE_BUZZER && !BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_ENCODER)
  // Buzzer isolado: sem display nem encoder para observar, um bipe de
  // confirmacao na inicializacao e o unico sinal de que o driver rodou.
  Buzzer_Beep(2500u, 200u);
#endif
#else
  // --- INICIALIZACAO DOS MODULOS ---

  // 1. LVGL
  lv_init();
  lv_port_disp_init();

  // 2. Encoder (Hardware e Driver)
  Encoder_Init(&htim2);

  // 3. Pedal (substituto do botao de usuario BT_USER; hardware novo do H723,
  //    sem equivalente no projeto STM32L476 original)
  Pedal_Init(&htim4);

  // 4. Dispositivo de Entrada LVGL (ponte entre Encoder e LVGL)
  lv_port_indev_init();

  // 5. EEPROM (carrega as configuracoes)
  if (EEPROM_Storage_Init(&hi2c1, 0u) == 0u) {
      // Falha na comunicacao, carrega padroes
      EEPROM_Storage_LoadDefaults(&g_system_cfg);
  } else if (EEPROM_Storage_Load(&g_system_cfg) == 0u) {
      // EEPROM vazia ou corrompida, carrega padroes e salva
      EEPROM_Storage_LoadDefaults(&g_system_cfg);
      EEPROM_Storage_Save(&g_system_cfg);
  }

  // 6. Sensor de Pressao
  ADC_Acq_Init(&hadc2, &htim15, &g_system_cfg.sensor_cal, &g_runtime_data);

  // 7. Malha de Controle (pinos das valvulas definidos por esta placa,
  //    injetados aqui em vez do modulo depender de main.h diretamente)
  {
    const ControlLoop_ValvePins_t valve_pins = {
        .valve1 = { VALVE_1_GPIO_Port, VALVE_1_Pin },
        .valve2 = { VALVE_2_GPIO_Port, VALVE_2_Pin },
        .valve3 = { VALVE_3_GPIO_Port, VALVE_3_Pin },
    };
    ControlLoop_Init(&g_system_cfg, &htim3, &g_runtime_data, &valve_pins);
  }

  // 8. Interface Grafica (HMI)
  HMI_Init(&g_runtime_data, &g_system_cfg);
#endif /* APP_MODE_BRINGUP */
  /* USER CODE END 2 */

  /* Init scheduler */
  osKernelInitialize();

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of controlTask */
  controlTaskHandle = osThreadNew(StartControlTask, NULL, &controlTask_attributes);

  /* creation of safetyTask */
  safetyTaskHandle = osThreadNew(StartSafetyTask, NULL, &safetyTask_attributes);

  /* creation of inputTask */
  inputTaskHandle = osThreadNew(StartInputTask, NULL, &inputTask_attributes);

  /* creation of appTask */
  appTaskHandle = osThreadNew(StartAppTask, NULL, &appTask_attributes);

  /* creation of usbProtoTask */
  usbProtoTaskHandle = osThreadNew(StartUsbProtoTask, NULL, &usbProtoTask_attributes);

  /* creation of storageTask */
  storageTaskHandle = osThreadNew(StartStorageTask, NULL, &storageTask_attributes);

  /* creation of uiTask */
  uiTaskHandle = osThreadNew(StartUiTask, NULL, &uiTask_attributes);

  /* creation of audioTask */
  audioTaskHandle = osThreadNew(StartAudioTask, NULL, &audioTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

  /* Start scheduler */
  osKernelStart();

  /* We should never get here as control is now taken by the scheduler */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  // Morto a partir daqui: osKernelStart() acima nao retorna em operacao
  // normal ("We should never get here" logo abaixo, gerado pelo CubeMX). A
  // logica que rodava neste super loop (Encoder_Update, ControlLoop_Update,
  // HMI_Update, lv_timer_handler, HAL_IWDG_Refresh) foi migrada para as
  // tarefas correspondentes (StartInputTask, StartControlTask, StartAppTask,
  // StartUiTask, StartSafetyTask, mais abaixo neste arquivo).
  while (1)
  {
    /* USER CODE END WHILE */

    /* USER CODE BEGIN 3 */
  }
  /* USER CODE END 3 */
}

/**
  * @brief System Clock Configuration
  * @retval None
  */
void SystemClock_Config(void)
{
  RCC_OscInitTypeDef RCC_OscInitStruct = {0};
  RCC_ClkInitTypeDef RCC_ClkInitStruct = {0};

  /** Supply configuration update enable
  */
  HAL_PWREx_ConfigSupply(PWR_LDO_SUPPLY);

  /** Configure the main internal regulator output voltage
  */
  __HAL_PWR_VOLTAGESCALING_CONFIG(PWR_REGULATOR_VOLTAGE_SCALE0);

  while(!__HAL_PWR_GET_FLAG(PWR_FLAG_VOSRDY)) {}

  /** Initializes the RCC Oscillators according to the specified parameters
  * in the RCC_OscInitTypeDef structure.
  */
  RCC_OscInitStruct.OscillatorType = RCC_OSCILLATORTYPE_HSI48|RCC_OSCILLATORTYPE_HSI
                              |RCC_OSCILLATORTYPE_LSI|RCC_OSCILLATORTYPE_HSE;
  RCC_OscInitStruct.HSEState = RCC_HSE_ON;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSE;
  RCC_OscInitStruct.PLL.PLLM = 2;
  RCC_OscInitStruct.PLL.PLLN = 32;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 2;
  RCC_OscInitStruct.PLL.PLLR = 2;
  RCC_OscInitStruct.PLL.PLLRGE = RCC_PLL1VCIRANGE_3;
  RCC_OscInitStruct.PLL.PLLVCOSEL = RCC_PLL1VCOWIDE;
  RCC_OscInitStruct.PLL.PLLFRACN = 0;
  if (HAL_RCC_OscConfig(&RCC_OscInitStruct) != HAL_OK)
  {
    Error_Handler();
  }

  /** Initializes the CPU, AHB and APB buses clocks
  */
  RCC_ClkInitStruct.ClockType = RCC_CLOCKTYPE_HCLK|RCC_CLOCKTYPE_SYSCLK
                              |RCC_CLOCKTYPE_PCLK1|RCC_CLOCKTYPE_PCLK2
                              |RCC_CLOCKTYPE_D3PCLK1|RCC_CLOCKTYPE_D1PCLK1;
  RCC_ClkInitStruct.SYSCLKSource = RCC_SYSCLKSOURCE_PLLCLK;
  RCC_ClkInitStruct.SYSCLKDivider = RCC_SYSCLK_DIV1;
  RCC_ClkInitStruct.AHBCLKDivider = RCC_HCLK_DIV2;
  RCC_ClkInitStruct.APB3CLKDivider = RCC_APB3_DIV2;
  RCC_ClkInitStruct.APB1CLKDivider = RCC_APB1_DIV2;
  RCC_ClkInitStruct.APB2CLKDivider = RCC_APB2_DIV2;
  RCC_ClkInitStruct.APB4CLKDivider = RCC_APB4_DIV2;

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_2) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief Peripherals Common Clock Configuration
  * @retval None
  */
void PeriphCommonClock_Config(void)
{
  RCC_PeriphCLKInitTypeDef PeriphClkInitStruct = {0};

  /** Initializes the peripherals clock
  */
  PeriphClkInitStruct.PeriphClockSelection = RCC_PERIPHCLK_CKPER;
  PeriphClkInitStruct.CkperClockSelection = RCC_CLKPSOURCE_HSI;
  if (HAL_RCCEx_PeriphCLKConfig(&PeriphClkInitStruct) != HAL_OK)
  {
    Error_Handler();
  }
}

/**
  * @brief ADC2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_ADC2_Init(void)
{

  /* USER CODE BEGIN ADC2_Init 0 */

  /* USER CODE END ADC2_Init 0 */

  ADC_ChannelConfTypeDef sConfig = {0};

  /* USER CODE BEGIN ADC2_Init 1 */

  /* USER CODE END ADC2_Init 1 */

  /** Common config
  */
  hadc2.Instance = ADC2;
  hadc2.Init.ClockPrescaler = ADC_CLOCK_ASYNC_DIV4;
  hadc2.Init.Resolution = ADC_RESOLUTION_16B;
  hadc2.Init.ScanConvMode = ADC_SCAN_DISABLE;
  hadc2.Init.EOCSelection = ADC_EOC_SINGLE_CONV;
  hadc2.Init.LowPowerAutoWait = DISABLE;
  hadc2.Init.ContinuousConvMode = DISABLE;
  hadc2.Init.NbrOfConversion = 1;
  hadc2.Init.DiscontinuousConvMode = DISABLE;
  hadc2.Init.ExternalTrigConv = ADC_EXTERNALTRIG_T15_TRGO;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_RISING;
  hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_CIRCULAR;
  hadc2.Init.Overrun = ADC_OVR_DATA_OVERWRITTEN;
  hadc2.Init.LeftBitShift = ADC_LEFTBITSHIFT_NONE;
  hadc2.Init.OversamplingMode = ENABLE;
  hadc2.Init.Oversampling.Ratio = 4;
  hadc2.Init.Oversampling.RightBitShift = ADC_RIGHTBITSHIFT_2;
  hadc2.Init.Oversampling.TriggeredMode = ADC_TRIGGEREDMODE_SINGLE_TRIGGER;
  hadc2.Init.Oversampling.OversamplingStopReset = ADC_REGOVERSAMPLING_CONTINUED_MODE;
  if (HAL_ADC_Init(&hadc2) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Regular Channel
  */
  sConfig.Channel = ADC_CHANNEL_5;
  sConfig.Rank = ADC_REGULAR_RANK_1;
  sConfig.SamplingTime = ADC_SAMPLETIME_64CYCLES_5;
  sConfig.SingleDiff = ADC_SINGLE_ENDED;
  sConfig.OffsetNumber = ADC_OFFSET_NONE;
  sConfig.Offset = 0;
  sConfig.OffsetSignedSaturation = DISABLE;
  if (HAL_ADC_ConfigChannel(&hadc2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN ADC2_Init 2 */

  /* USER CODE END ADC2_Init 2 */

}

/**
  * @brief I2C1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_I2C1_Init(void)
{

  /* USER CODE BEGIN I2C1_Init 0 */

  /* USER CODE END I2C1_Init 0 */

  /* USER CODE BEGIN I2C1_Init 1 */

  /* USER CODE END I2C1_Init 1 */
  hi2c1.Instance = I2C1;
  hi2c1.Init.Timing = 0x00C0216C;
  hi2c1.Init.OwnAddress1 = 0;
  hi2c1.Init.AddressingMode = I2C_ADDRESSINGMODE_7BIT;
  hi2c1.Init.DualAddressMode = I2C_DUALADDRESS_DISABLE;
  hi2c1.Init.OwnAddress2 = 0;
  hi2c1.Init.OwnAddress2Masks = I2C_OA2_NOMASK;
  hi2c1.Init.GeneralCallMode = I2C_GENERALCALL_DISABLE;
  hi2c1.Init.NoStretchMode = I2C_NOSTRETCH_DISABLE;
  if (HAL_I2C_Init(&hi2c1) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Analogue filter
  */
  if (HAL_I2CEx_ConfigAnalogFilter(&hi2c1, I2C_ANALOGFILTER_ENABLE) != HAL_OK)
  {
    Error_Handler();
  }

  /** Configure Digital filter
  */
  if (HAL_I2CEx_ConfigDigitalFilter(&hi2c1, 0) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN I2C1_Init 2 */

  /* USER CODE END I2C1_Init 2 */

}

/**
  * @brief IWDG1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_IWDG1_Init(void)
{

  /* USER CODE BEGIN IWDG1_Init 0 */

  /* USER CODE END IWDG1_Init 0 */

  /* USER CODE BEGIN IWDG1_Init 1 */

  /* USER CODE END IWDG1_Init 1 */
  hiwdg1.Instance = IWDG1;
  hiwdg1.Init.Prescaler = IWDG_PRESCALER_64;
  hiwdg1.Init.Window = 4095;
  hiwdg1.Init.Reload = 4095;
  if (HAL_IWDG_Init(&hiwdg1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN IWDG1_Init 2 */

  /* USER CODE END IWDG1_Init 2 */

}

/**
  * @brief RTC Initialization Function
  * @param None
  * @retval None
  */
static void MX_RTC_Init(void)
{

  /* USER CODE BEGIN RTC_Init 0 */

  /* USER CODE END RTC_Init 0 */

  RTC_TimeTypeDef sTime = {0};
  RTC_DateTypeDef sDate = {0};

  /* USER CODE BEGIN RTC_Init 1 */

  /* USER CODE END RTC_Init 1 */

  /** Initialize RTC Only
  */
  hrtc.Instance = RTC;
  hrtc.Init.HourFormat = RTC_HOURFORMAT_24;
  hrtc.Init.AsynchPrediv = 127;
  hrtc.Init.SynchPrediv = 255;
  hrtc.Init.OutPut = RTC_OUTPUT_DISABLE;
  hrtc.Init.OutPutPolarity = RTC_OUTPUT_POLARITY_HIGH;
  hrtc.Init.OutPutType = RTC_OUTPUT_TYPE_OPENDRAIN;
  hrtc.Init.OutPutRemap = RTC_OUTPUT_REMAP_NONE;
  if (HAL_RTC_Init(&hrtc) != HAL_OK)
  {
    Error_Handler();
  }

  /* USER CODE BEGIN Check_RTC_BKUP */
  if (HAL_RTCEx_BKUPRead(&hrtc, RTC_CALENDAR_INITIALIZED_BKP_REG) == RTC_CALENDAR_INITIALIZED_MAGIC) {
    // Calendario ja foi ajustado antes e o dominio de backup (VBAT) manteve
    // o valor — nao sobrescrever com a data/hora padrao abaixo.
    return;
  }
  /* USER CODE END Check_RTC_BKUP */

  /** Initialize RTC and set the Time and Date
  */
  sTime.Hours = 0x0;
  sTime.Minutes = 0x0;
  sTime.Seconds = 0x0;
  sTime.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
  sTime.StoreOperation = RTC_STOREOPERATION_RESET;
  if (HAL_RTC_SetTime(&hrtc, &sTime, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  sDate.WeekDay = RTC_WEEKDAY_MONDAY;
  sDate.Month = RTC_MONTH_JANUARY;
  sDate.Date = 0x1;
  sDate.Year = 0x0;

  if (HAL_RTC_SetDate(&hrtc, &sDate, RTC_FORMAT_BCD) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN RTC_Init 2 */
  // So' chega aqui na primeira inicializacao real (ver Check_RTC_BKUP acima)
  // — marca para nao repetir o SetTime/SetDate default nos proximos boots.
  HAL_RTCEx_BKUPWrite(&hrtc, RTC_CALENDAR_INITIALIZED_BKP_REG, RTC_CALENDAR_INITIALIZED_MAGIC);
  g_rtc_backup_domain_was_reset = 1u;
  /* USER CODE END RTC_Init 2 */

}

/**
  * @brief SDMMC1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SDMMC1_SD_Init(void)
{

  /* USER CODE BEGIN SDMMC1_Init 0 */

  /* USER CODE END SDMMC1_Init 0 */

  /* USER CODE BEGIN SDMMC1_Init 1 */

  /* USER CODE END SDMMC1_Init 1 */
  hsd1.Instance = SDMMC1;
  hsd1.Init.ClockEdge = SDMMC_CLOCK_EDGE_RISING;
  hsd1.Init.ClockPowerSave = SDMMC_CLOCK_POWER_SAVE_ENABLE;
  hsd1.Init.BusWide = SDMMC_BUS_WIDE_4B;
  hsd1.Init.HardwareFlowControl = SDMMC_HARDWARE_FLOW_CONTROL_DISABLE;
  hsd1.Init.ClockDiv = 5;
  if (HAL_SD_Init(&hsd1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SDMMC1_Init 2 */

  /* USER CODE END SDMMC1_Init 2 */

}

/**
  * @brief SPI1 Initialization Function
  * @param None
  * @retval None
  */
static void MX_SPI1_Init(void)
{

  /* USER CODE BEGIN SPI1_Init 0 */

  /* USER CODE END SPI1_Init 0 */

  /* USER CODE BEGIN SPI1_Init 1 */

  /* USER CODE END SPI1_Init 1 */
  /* SPI1 parameter configuration*/
  hspi1.Instance = SPI1;
  hspi1.Init.Mode = SPI_MODE_MASTER;
  hspi1.Init.Direction = SPI_DIRECTION_2LINES_TXONLY;
  hspi1.Init.DataSize = SPI_DATASIZE_8BIT;
  hspi1.Init.CLKPolarity = SPI_POLARITY_LOW;
  hspi1.Init.CLKPhase = SPI_PHASE_1EDGE;
  hspi1.Init.NSS = SPI_NSS_SOFT;
  hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_2;
  hspi1.Init.FirstBit = SPI_FIRSTBIT_MSB;
  hspi1.Init.TIMode = SPI_TIMODE_DISABLE;
  hspi1.Init.CRCCalculation = SPI_CRCCALCULATION_DISABLE;
  hspi1.Init.CRCPolynomial = 0x0;
  hspi1.Init.NSSPMode = SPI_NSS_PULSE_ENABLE;
  hspi1.Init.NSSPolarity = SPI_NSS_POLARITY_LOW;
  hspi1.Init.FifoThreshold = SPI_FIFO_THRESHOLD_01DATA;
  hspi1.Init.TxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.RxCRCInitializationPattern = SPI_CRC_INITIALIZATION_ALL_ZERO_PATTERN;
  hspi1.Init.MasterSSIdleness = SPI_MASTER_SS_IDLENESS_00CYCLE;
  hspi1.Init.MasterInterDataIdleness = SPI_MASTER_INTERDATA_IDLENESS_00CYCLE;
  hspi1.Init.MasterReceiverAutoSusp = SPI_MASTER_RX_AUTOSUSP_DISABLE;
  hspi1.Init.MasterKeepIOState = SPI_MASTER_KEEP_IO_STATE_DISABLE;
  hspi1.Init.IOSwap = SPI_IO_SWAP_DISABLE;
  if (HAL_SPI_Init(&hspi1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN SPI1_Init 2 */

  /* USER CODE END SPI1_Init 2 */

}

/**
  * @brief TIM2 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM2_Init(void)
{

  /* USER CODE BEGIN TIM2_Init 0 */

  /* USER CODE END TIM2_Init 0 */

  TIM_Encoder_InitTypeDef sConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM2_Init 1 */

  /* USER CODE END TIM2_Init 1 */
  htim2.Instance = TIM2;
  htim2.Init.Prescaler = 0;
  htim2.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim2.Init.Period = 4294967295;
  htim2.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim2.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  sConfig.EncoderMode = TIM_ENCODERMODE_TI1;
  sConfig.IC1Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC1Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC1Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC1Filter = 0;
  sConfig.IC2Polarity = TIM_ICPOLARITY_RISING;
  sConfig.IC2Selection = TIM_ICSELECTION_DIRECTTI;
  sConfig.IC2Prescaler = TIM_ICPSC_DIV1;
  sConfig.IC2Filter = 0;
  if (HAL_TIM_Encoder_Init(&htim2, &sConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim2, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM2_Init 2 */

  /* USER CODE END TIM2_Init 2 */

}

/**
  * @brief TIM3 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM3_Init(void)
{

  /* USER CODE BEGIN TIM3_Init 0 */

  /* USER CODE END TIM3_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};

  /* USER CODE BEGIN TIM3_Init 1 */

  /* USER CODE END TIM3_Init 1 */
  htim3.Instance = TIM3;
  htim3.Init.Prescaler = 0;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 13750-1;
  htim3.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim3.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_PWM_Init(&htim3) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim3, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  if (HAL_TIM_PWM_ConfigChannel(&htim3, &sConfigOC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM3_Init 2 */

  /* USER CODE END TIM3_Init 2 */
  HAL_TIM_MspPostInit(&htim3);

}

/**
  * @brief TIM4 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM4_Init(void)
{

  /* USER CODE BEGIN TIM4_Init 0 */

  /* USER CODE END TIM4_Init 0 */

  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_IC_InitTypeDef sConfigIC = {0};

  /* USER CODE BEGIN TIM4_Init 1 */

  /* USER CODE END TIM4_Init 1 */
  htim4.Instance = TIM4;
  htim4.Init.Prescaler = 200-1;
  htim4.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim4.Init.Period = 65535;
  htim4.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim4.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_IC_Init(&htim4) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim4, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigIC.ICPolarity = TIM_INPUTCHANNELPOLARITY_RISING;
  sConfigIC.ICSelection = TIM_ICSELECTION_DIRECTTI;
  sConfigIC.ICPrescaler = TIM_ICPSC_DIV1;
  sConfigIC.ICFilter = 10;
  if (HAL_TIM_IC_ConfigChannel(&htim4, &sConfigIC, TIM_CHANNEL_1) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM4_Init 2 */

  /* USER CODE END TIM4_Init 2 */

}

/**
  * @brief TIM8 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM8_Init(void)
{

  /* USER CODE BEGIN TIM8_Init 0 */

  /* USER CODE END TIM8_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};
  TIM_OC_InitTypeDef sConfigOC = {0};
  TIM_BreakDeadTimeConfigTypeDef sBreakDeadTimeConfig = {0};

  /* USER CODE BEGIN TIM8_Init 1 */

  /* USER CODE END TIM8_Init 1 */
  htim8.Instance = TIM8;
  htim8.Init.Prescaler = 0;
  htim8.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim8.Init.Period = 65535;
  htim8.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim8.Init.RepetitionCounter = 0;
  htim8.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_DISABLE;
  if (HAL_TIM_Base_Init(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim8, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  if (HAL_TIM_PWM_Init(&htim8) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_RESET;
  sMasterConfig.MasterOutputTrigger2 = TIM_TRGO2_RESET;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim8, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sConfigOC.OCMode = TIM_OCMODE_PWM1;
  sConfigOC.Pulse = 0;
  sConfigOC.OCPolarity = TIM_OCPOLARITY_HIGH;
  sConfigOC.OCNPolarity = TIM_OCNPOLARITY_HIGH;
  sConfigOC.OCFastMode = TIM_OCFAST_DISABLE;
  sConfigOC.OCIdleState = TIM_OCIDLESTATE_RESET;
  sConfigOC.OCNIdleState = TIM_OCNIDLESTATE_RESET;
  if (HAL_TIM_PWM_ConfigChannel(&htim8, &sConfigOC, TIM_CHANNEL_2) != HAL_OK)
  {
    Error_Handler();
  }
  sBreakDeadTimeConfig.OffStateRunMode = TIM_OSSR_DISABLE;
  sBreakDeadTimeConfig.OffStateIDLEMode = TIM_OSSI_DISABLE;
  sBreakDeadTimeConfig.LockLevel = TIM_LOCKLEVEL_OFF;
  sBreakDeadTimeConfig.DeadTime = 0;
  sBreakDeadTimeConfig.BreakState = TIM_BREAK_DISABLE;
  sBreakDeadTimeConfig.BreakPolarity = TIM_BREAKPOLARITY_HIGH;
  sBreakDeadTimeConfig.BreakFilter = 0;
  sBreakDeadTimeConfig.Break2State = TIM_BREAK2_DISABLE;
  sBreakDeadTimeConfig.Break2Polarity = TIM_BREAK2POLARITY_HIGH;
  sBreakDeadTimeConfig.Break2Filter = 0;
  sBreakDeadTimeConfig.AutomaticOutput = TIM_AUTOMATICOUTPUT_DISABLE;
  if (HAL_TIMEx_ConfigBreakDeadTime(&htim8, &sBreakDeadTimeConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM8_Init 2 */

  /* USER CODE END TIM8_Init 2 */
  HAL_TIM_MspPostInit(&htim8);

}

/**
  * @brief TIM15 Initialization Function
  * @param None
  * @retval None
  */
static void MX_TIM15_Init(void)
{

  /* USER CODE BEGIN TIM15_Init 0 */

  /* USER CODE END TIM15_Init 0 */

  TIM_ClockConfigTypeDef sClockSourceConfig = {0};
  TIM_MasterConfigTypeDef sMasterConfig = {0};

  /* USER CODE BEGIN TIM15_Init 1 */

  /* USER CODE END TIM15_Init 1 */
  htim15.Instance = TIM15;
  htim15.Init.Prescaler = 275-1;
  htim15.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim15.Init.Period = 1000-1;
  htim15.Init.ClockDivision = TIM_CLOCKDIVISION_DIV1;
  htim15.Init.RepetitionCounter = 0;
  htim15.Init.AutoReloadPreload = TIM_AUTORELOAD_PRELOAD_ENABLE;
  if (HAL_TIM_Base_Init(&htim15) != HAL_OK)
  {
    Error_Handler();
  }
  sClockSourceConfig.ClockSource = TIM_CLOCKSOURCE_INTERNAL;
  if (HAL_TIM_ConfigClockSource(&htim15, &sClockSourceConfig) != HAL_OK)
  {
    Error_Handler();
  }
  sMasterConfig.MasterOutputTrigger = TIM_TRGO_UPDATE;
  sMasterConfig.MasterSlaveMode = TIM_MASTERSLAVEMODE_DISABLE;
  if (HAL_TIMEx_MasterConfigSynchronization(&htim15, &sMasterConfig) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN TIM15_Init 2 */

  /* USER CODE END TIM15_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
static void MX_USB_OTG_HS_PCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 0 */

  /* USER CODE END USB_OTG_HS_PCD_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 1 */

  /* USER CODE END USB_OTG_HS_PCD_Init 1 */
  hpcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hpcd_USB_OTG_HS.Init.dev_endpoints = 9;
  hpcd_USB_OTG_HS.Init.speed = PCD_SPEED_FULL;
  hpcd_USB_OTG_HS.Init.dma_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hpcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  hpcd_USB_OTG_HS.Init.lpm_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.vbus_sensing_enable = ENABLE;
  hpcd_USB_OTG_HS.Init.use_dedicated_ep1 = DISABLE;
  hpcd_USB_OTG_HS.Init.use_external_vbus = DISABLE;
  if (HAL_PCD_Init(&hpcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_PCD_Init 2 */

  /* USER CODE END USB_OTG_HS_PCD_Init 2 */

}

/**
  * @brief USB_OTG_HS Initialization Function
  * @param None
  * @retval None
  */
void MX_USB_OTG_HS_HCD_Init(void)
{

  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 0 */

  /* USER CODE END USB_OTG_HS_HCD_Init 0 */

  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 1 */

  /* USER CODE END USB_OTG_HS_HCD_Init 1 */
  hhcd_USB_OTG_HS.Instance = USB_OTG_HS;
  hhcd_USB_OTG_HS.Init.Host_channels = 16;
  hhcd_USB_OTG_HS.Init.speed = HCD_SPEED_FULL;
  hhcd_USB_OTG_HS.Init.dma_enable = ENABLE;
  hhcd_USB_OTG_HS.Init.phy_itface = USB_OTG_EMBEDDED_PHY;
  hhcd_USB_OTG_HS.Init.Sof_enable = DISABLE;
  hhcd_USB_OTG_HS.Init.low_power_enable = DISABLE;
  if (HAL_HCD_Init(&hhcd_USB_OTG_HS) != HAL_OK)
  {
    Error_Handler();
  }
  /* USER CODE BEGIN USB_OTG_HS_HCD_Init 2 */

  /* USER CODE END USB_OTG_HS_HCD_Init 2 */

}

/**
  * Enable DMA controller clock
  */
static void MX_DMA_Init(void)
{

  /* DMA controller clock enable */
  __HAL_RCC_DMA1_CLK_ENABLE();

  /* DMA interrupt init */
  /* DMA1_Stream0_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 5, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 10, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream1_IRQn);

}

/**
  * @brief GPIO Initialization Function
  * @param None
  * @retval None
  */
static void MX_GPIO_Init(void)
{
  GPIO_InitTypeDef GPIO_InitStruct = {0};
  /* USER CODE BEGIN MX_GPIO_Init_1 */

  /* USER CODE END MX_GPIO_Init_1 */

  /* GPIO Ports Clock Enable */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOH_CLK_ENABLE();
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_BACKLIGHT_GPIO_Port, TFT_BACKLIGHT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, TFT_CS_Pin|TFT_DC_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_RES_GPIO_Port, TFT_RES_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOD, LED_2_Pin|VALVE_3_Pin|VALVE_2_Pin|VALVE_1_Pin
                          |EN_VBUS_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pins : ENCODER_BT_Pin BT_USER_Pin */
  GPIO_InitStruct.Pin = ENCODER_BT_Pin|BT_USER_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOA, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_BACKLIGHT_Pin */
  GPIO_InitStruct.Pin = TFT_BACKLIGHT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TFT_BACKLIGHT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : TFT_CS_Pin TFT_DC_Pin */
  GPIO_InitStruct.Pin = TFT_CS_Pin|TFT_DC_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOC, &GPIO_InitStruct);

  /*Configure GPIO pin : TFT_RES_Pin */
  GPIO_InitStruct.Pin = TFT_RES_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(TFT_RES_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_2_Pin VALVE_3_Pin VALVE_2_Pin VALVE_1_Pin
                           EN_VBUS_Pin */
  GPIO_InitStruct.Pin = LED_2_Pin|VALVE_3_Pin|VALVE_2_Pin|VALVE_1_Pin
                          |EN_VBUS_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_OUTPUT_PP;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  GPIO_InitStruct.Speed = GPIO_SPEED_FREQ_LOW;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pins : LED_1_Pin OC_USB_Pin */
  GPIO_InitStruct.Pin = LED_1_Pin|OC_USB_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(GPIOD, &GPIO_InitStruct);

  /*Configure GPIO pin : USB_HS_INT_Pin */
  GPIO_InitStruct.Pin = USB_HS_INT_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_IT_RISING;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(USB_HS_INT_GPIO_Port, &GPIO_InitStruct);

  /*Configure GPIO pin : SDMMC1_CD_Pin */
  GPIO_InitStruct.Pin = SDMMC1_CD_Pin;
  GPIO_InitStruct.Mode = GPIO_MODE_INPUT;
  GPIO_InitStruct.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SDMMC1_CD_GPIO_Port, &GPIO_InitStruct);

  /* USER CODE BEGIN MX_GPIO_Init_2 */

  /* USER CODE END MX_GPIO_Init_2 */
}

/* USER CODE BEGIN 4 */

/* USER CODE END 4 */

/* USER CODE BEGIN Header_StartControlTask */
/**
  * @brief  Function implementing the controlTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartControlTask */
void StartControlTask(void *argument)
{
  /* USER CODE BEGIN 5 */
  // Prioridade mais alta (osPriorityRealtime) — reservado para o loop de
  // controle de pressao a 1 ms (Fase 3/4). Hoje inerte: em APP_MODE_BRINGUP=1
  // nem ControlLoop_Init() e' chamado (ver main(), USER CODE BEGIN 2), entao
  // nao ha nada para ControlLoop_Update() atualizar ainda.
  for(;;)
  {
#if !APP_MODE_BRINGUP
    ControlLoop_Update();
#elif BRINGUP_ENABLE_CONTROL
    ControlLoop_Update();
    g_control_last_error = (uint32_t)ControlLoop_GetLastError();
    g_control_jitter_us = ControlLoop_GetMaxJitterCycles() / (SystemCoreClock / 1000000u);
#elif BRINGUP_ENABLE_PRESSURE
    ADC_Acq_Update();
#if BRINGUP_ENABLE_ACTUATORS
    {
      const ActuatorBench_Cmd_t cmd = { g_act_arm, g_act_pump, g_act_valves };
      const uint8_t sensor_ok =
          ((g_pressure_diag->flags != 0u) || (g_pressure_diag->atmospheric_valid == 0u)) ? 0u : 1u;

      ActuatorBench_Step(&g_act_bench, &cmd, g_pressure_diag->pressure_kpa, sensor_ok,
                         TASK_CONTROL_PERIOD_MS, &g_act_out);
      // Valvulas antes da bomba ao ligar; bomba antes das valvulas ao desligar
      // nao importa aqui: o estado seguro abre tudo E zera a bomba no mesmo passo.
      Valve_SetAll((g_act_out.valve_open_mask & ACTBENCH_V1) != 0u,
                   (g_act_out.valve_open_mask & ACTBENCH_V2) != 0u,
                   (g_act_out.valve_open_mask & ACTBENCH_V3) != 0u);
      (void)PwmMotor_SetPower(g_act_out.pump_power);
    }
#endif
#endif
    Supervisor_CheckIn(SUPERVISOR_TASK_CONTROL);
    osDelay(TASK_CONTROL_PERIOD_MS);
  }
  /* USER CODE END 5 */
}

/* USER CODE BEGIN Header_StartSafetyTask */
/**
* @brief Function implementing the safetyTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSafetyTask */
void StartSafetyTask(void *argument)
{
  /* USER CODE BEGIN StartSafetyTask */
  // Supervisor (Parte A/B do prompt de projeto): a cada janela de
  // SUPERVISOR_WINDOW_MS, so' alimenta o IWDG se TODAS as tarefas criticas
  // (control, input, app — ver supervisor.h) fizeram check-in nessa janela.
  // Se alguma travar, o IWDG (~8 s) nao e' alimentado e a MCU reseta, caindo
  // no estado seguro por hardware. UI/USB/storage/audio nao entram de
  // proposito (nao podem derrubar o controle). Inspecionar no Live Watch:
  // g_supervisor_last_mask (quem fez check-in na ultima janela) e
  // g_supervisor_missed_windows (janelas em que faltou alguem).
  static osThreadId_t s_task_ids[8];
  uint32_t stack_probe_ticks = 0u;
  uint32_t supervisor_window_ms = 0u;
#if (FAULT_TEST_FORCE_HARDFAULT_MS > 0u)
  uint32_t fault_test_ms = 0u;
#endif

  s_task_ids[0] = controlTaskHandle;
  s_task_ids[1] = safetyTaskHandle;
  s_task_ids[2] = inputTaskHandle;
  s_task_ids[3] = appTaskHandle;
  s_task_ids[4] = usbProtoTaskHandle;
  s_task_ids[5] = storageTaskHandle;
  s_task_ids[6] = uiTaskHandle;
  s_task_ids[7] = audioTaskHandle;

  for(;;)
  {
    supervisor_window_ms += TASK_IWDG_REFRESH_PERIOD_MS;
    if (supervisor_window_ms >= SUPERVISOR_WINDOW_MS) {
      uint32_t collected = Supervisor_CollectCheckIns();

      supervisor_window_ms = 0u;
      g_supervisor_last_mask = collected;
      if (Supervisor_AllCheckedIn(collected) != 0u) {
        HAL_IWDG_Refresh(&hiwdg1);
      } else {
        g_supervisor_missed_windows++;
      }
    }

#if (FAULT_TEST_FORCE_HARDFAULT_MS > 0u)
    fault_test_ms += TASK_IWDG_REFRESH_PERIOD_MS;
    if (fault_test_ms >= FAULT_TEST_FORCE_HARDFAULT_MS) {
      FaultManager_ForceHardFault();
    }
#endif

    stack_probe_ticks += TASK_IWDG_REFRESH_PERIOD_MS;
    if (stack_probe_ticks >= 500u) {
      uint32_t i;
      stack_probe_ticks = 0u;
      for (i = 0u; i < 8u; i++) {
        g_task_stack_free_bytes[i] = osThreadGetStackSpace(s_task_ids[i]);
      }
    }

    osDelay(TASK_IWDG_REFRESH_PERIOD_MS);
  }
  /* USER CODE END StartSafetyTask */
}

/* USER CODE BEGIN Header_StartInputTask */
/**
* @brief Function implementing the inputTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartInputTask */
void StartInputTask(void *argument)
{
  /* USER CODE BEGIN StartInputTask */
  // Migrado do super loop cooperativo (main(), antes do FreeRTOS entrar):
  // debounce/leitura do encoder a cada 1 ms, mais o feedback sonoro de
  // bring-up. O caminho de producao (pedal/bt_user -> ControlLoop) fica
  // atras de !APP_MODE_BRINGUP, mesmo criterio de antes.
#if (BRINGUP_ENABLE_ENCODER && BRINGUP_ENABLE_BUZZER)
  static int32_t s_last_steps_buzz = 0;
#endif

  for(;;)
  {
#if APP_MODE_BRINGUP
#if BRINGUP_ENABLE_ENCODER
    Encoder_Update();
#if BRINGUP_ENABLE_BUZZER
    {
      Encoder_State_t *enc_now = Encoder_GetState();
      // Bipe curto (tom de 2.5kHz por 15ms) a cada passo (detent) do encoder.
      if (enc_now->steps != s_last_steps_buzz) {
        s_last_steps_buzz = enc_now->steps;
        Buzzer_Beep(2500u, 15u);
      }
    }
#endif /* BRINGUP_ENABLE_BUZZER */
#endif /* BRINGUP_ENABLE_ENCODER */
    // Nota: os ramos de bring-up "desenho cru" (BRINGUP_ENABLE_LVGL=0) que
    // liam Encoder_GetEncBtnEvent()/GetUserBtnEvent() para piscar a barra no
    // display ainda nao foram portados para tarefas — so' o caminho ativo
    // hoje (LVGL=1) foi migrado. Reativar aquele modo exigiria portar
    // tambem esse pedaco (era so' escrita de timestamp, sem estado
    // complexo).
#if BRINGUP_ENABLE_CONTROL
    // Mesma logica do caminho de producao (ver #else logo abaixo), usando o
    // runtime de bring-up em vez de g_runtime_data.
    if ((Encoder_GetUserBtnEvent() != 0u) || (Pedal_GetEvent() != 0u)) {
      if (g_control_runtime->state == SYSTEM_STATE_IDLE) {
        ControlLoop_StartInflation();
      } else {
        g_control_runtime->bt_user_pressed = 1u;
      }
    }
#endif
#else
    Encoder_Update();
    if ((Encoder_GetUserBtnEvent() != 0u) || (Pedal_GetEvent() != 0u)) {
      if (g_runtime_data.state == SYSTEM_STATE_IDLE) {
        ControlLoop_StartInflation();
      } else {
        g_runtime_data.bt_user_pressed = 1u;
      }
    }
#endif /* APP_MODE_BRINGUP */

    Supervisor_CheckIn(SUPERVISOR_TASK_INPUT);
    osDelay(TASK_ENCODER_PERIOD_MS);
  }
  /* USER CODE END StartInputTask */
}

/* USER CODE BEGIN Header_StartAppTask */
/**
* @brief Function implementing the appTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAppTask */
void StartAppTask(void *argument)
{
  /* USER CODE BEGIN StartAppTask */
  // Reservado para a orquestracao de ensaio / HMI_Update() (Fase 6). Inerte
  // em APP_MODE_BRINGUP=1 (HMI_Init() nao e' chamado ainda).
#if (APP_MODE_BRINGUP && BRINGUP_ENABLE_BUZZER)
  // Sinaliza sonoramente que o boot anterior terminou em HardFault (o
  // registro completo esta em g_boot_fault_record). Aqui e' seguro chamar
  // Buzzer_Beep(): usa osDelay, entao so' pode rodar com o escalonador ativo.
  if (g_boot_fault_pending != 0u) {
    uint32_t beep;
    for (beep = 0u; beep < 3u; beep++) {
      Buzzer_Beep(400u, 250u);
      osDelay(150u);
    }
  }
#endif
#if BRINGUP_ENABLE_STORAGE
  uint32_t storage_test_ms = 0u;
#endif
  for(;;)
  {
#if !APP_MODE_BRINGUP
    HMI_Update();
#endif
#if BRINGUP_ENABLE_STORAGE
    // Exercita fila -> gravacao -> hash encadeado sem depender do ensaio
    // completo: 1 evento de teste a cada ~3 s, mensagem crescente (da'
    // pra' conferir a sequencia/encadeamento abrindo o arquivo no PC).
    storage_test_ms += TASK_HMI_PERIOD_MS;
    if (storage_test_ms >= 3000u) {
      RTC_TimeTypeDef rtc_time;
      RTC_DateTypeDef rtc_date;
      StorageFormat_LogEntry_t entry;
      char msg[48];

      storage_test_ms = 0u;
      (void)HAL_RTC_GetTime(&hrtc, &rtc_time, RTC_FORMAT_BIN);
      (void)HAL_RTC_GetDate(&hrtc, &rtc_date, RTC_FORMAT_BIN);

      memset(&entry, 0, sizeof(entry));
      entry.datetime.year = (uint16_t)(2000 + rtc_date.Year);
      entry.datetime.month = rtc_date.Month;
      entry.datetime.day = rtc_date.Date;
      entry.datetime.hour = rtc_time.Hours;
      entry.datetime.minute = rtc_time.Minutes;
      entry.datetime.second = rtc_time.Seconds;
      entry.tz_offset_hours = -3; // America/Sao_Paulo; sem suporte a horario de verao aqui
      entry.seq = s_storage_test_seq++;
      entry.code = "";
      entry.state_name = "BRINGUP";
      entry.pressure_mmhg = 0.0f;
      snprintf(msg, sizeof(msg), "Teste de bancada #%lu", (unsigned long)entry.seq);
      entry.message = msg;

      (void)StorageTask_LogEvent(&entry);
      g_storage_stats = StorageTask_GetStats();
    }
#endif
    Supervisor_CheckIn(SUPERVISOR_TASK_APP);
    osDelay(TASK_HMI_PERIOD_MS);
  }
  /* USER CODE END StartAppTask */
}

/* USER CODE BEGIN Header_StartUsbProtoTask */
/**
* @brief Function implementing the usbProtoTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUsbProtoTask */
void StartUsbProtoTask(void *argument)
{
  /* USER CODE BEGIN StartUsbProtoTask */
  // Reservado para o parser do protocolo USB CDC (Fase 7). Nada ainda.
  for(;;)
  {
    osDelay(100);
  }
  /* USER CODE END StartUsbProtoTask */
}

/* USER CODE BEGIN Header_StartStorageTask */
/**
* @brief Function implementing the storageTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartStorageTask */
void StartStorageTask(void *argument)
{
  /* USER CODE BEGIN StartStorageTask */
#if BRINGUP_ENABLE_STORAGE
  // StorageTask_Run() e' o loop (for(;;) — nunca retorna): monta o SD,
  // tenta de novo periodicamente se falhar (E005), processa a fila.
  StorageTask_Run();
#else
  // Reservado para FatFS/log/ensaio (Fase 5). Nada ainda.
  for(;;)
  {
    osDelay(100);
  }
#endif
  /* USER CODE END StartStorageTask */
}

/* USER CODE BEGIN Header_StartUiTask */
/**
* @brief Function implementing the uiTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUiTask */
void StartUiTask(void *argument)
{
  /* USER CODE BEGIN StartUiTask */
  // Migrado do super loop cooperativo: bombeia o lv_timer_handler() no
  // mesmo periodo de antes (TASK_LVGL_HANDLER_PERIOD_MS). Nota: os ramos de
  // bring-up sem LVGL (barra/ciclo de cores cru) nao foram portados para
  // tarefa — so' o caminho ativo hoje (BRINGUP_ENABLE_LVGL=1) foi migrado;
  // reativar aqueles exigiria portar tambem esse desenho para ca.
  //
  // lv_tick_inc() precisa ser chamado daqui agora: o SysTick_Handler() que
  // fazia isso foi removido inteiro de stm32h7xx_it.c quando o timebase do
  // HAL mudou para TIM6 (SysTick agora e' do FreeRTOS) — a funcao sumiu e
  // levou o lv_tick_inc(1) que estava dentro dela junto. Sem isso o LVGL
  // nunca via o tempo passar (desenhava uma vez e travava). Chamar aqui
  // evita depender de qual IRQ o CubeMX decidir usar para o timebase no
  // futuro — soh essa tarefa precisa saber quanto tempo passou entre
  // chamadas de lv_timer_handler().
  uint32_t mem_probe_ms = 0u;

  for(;;)
  {
#if (BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL)
    lv_tick_inc(TASK_LVGL_HANDLER_PERIOD_MS);
    (void)lv_timer_handler();

    mem_probe_ms += TASK_LVGL_HANDLER_PERIOD_MS;
    if (mem_probe_ms >= 500u) {
      mem_probe_ms = 0u;
      lv_mem_monitor(&g_lv_mem); // na propria uiTask: o LVGL nao e' thread-safe
    }

#if BRINGUP_ENABLE_CONTROL
    // HUD de bancada (ver globais acima, USER CODE 2): o arco e' o knob do
    // setpoint, os labels mostram estado/pressao/erro.
    //
    // BUG corrigido (2026-10): escrevia em
    // g_bringup_sys_cfg.current_setpoint_mmhg, mas o Control_Core so' le
    // g_control_runtime->setpoint_mmhg (copiado de current_setpoint_mmhg
    // UMA VEZ, em ControlLoop_Init — nunca mais depois) — girar o encoder
    // nao tinha efeito nenhum no ensaio, o label ficava preso em 40 mmHg.
    // ControlLoop_SetSetpointMmHg() e' a API publica certa: atualiza os
    // dois campos (runtime + cfg) com o clamping devido.
    {
      int16_t setpoint_knob = lv_arc_get_value(s_ctrl_arc);
      char line[48];
      uint32_t state_idx = (uint32_t)g_control_runtime->state;

      ControlLoop_SetSetpointMmHg((float)setpoint_knob);

      {
        char pres_str[16];
        char sp_str[16];
        format_1dp(g_control_runtime->pressure_display, pres_str, sizeof(pres_str));
        format_1dp(g_control_runtime->setpoint_mmhg, sp_str, sizeof(sp_str));
        snprintf(line, sizeof(line), "%s",
                (state_idx < (sizeof(s_state_names) / sizeof(s_state_names[0])))
                    ? s_state_names[state_idx] : "?");
        lv_label_set_text(s_ctrl_label_state, line);
        snprintf(line, sizeof(line), "%s / %s mmHg", pres_str, sp_str);
        lv_label_set_text(s_ctrl_label_pressure, line);
      }

      if (g_control_last_error != 0u) {
        snprintf(line, sizeof(line), "ERRO E%03u", (unsigned)g_control_last_error);
      } else {
        snprintf(line, sizeof(line), " ");
      }
      lv_label_set_text(s_ctrl_label_error, line);
    }
#endif
#endif
    osDelay(TASK_LVGL_HANDLER_PERIOD_MS);
  }
  /* USER CODE END StartUiTask */
}

/* USER CODE BEGIN Header_StartAudioTask */
/**
* @brief Function implementing the audioTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument)
{
  /* USER CODE BEGIN StartAudioTask */
  // Reservado para sequencias de tons do produto final (boot/conclusao/erro,
  // Fase 6). O beep de bring-up por passo do encoder continua em
  // StartInputTask (dispara no mesmo lugar de antes, sem fila). Nada ainda.
  for(;;)
  {
    osDelay(100);
  }
  /* USER CODE END StartAudioTask */
}

 /* MPU Configuration */

void MPU_Config(void)
{
  MPU_Region_InitTypeDef MPU_InitStruct = {0};

  /* Disables the MPU */
  HAL_MPU_Disable();

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Enable = MPU_REGION_ENABLE;
  MPU_InitStruct.Number = MPU_REGION_NUMBER0;
  MPU_InitStruct.BaseAddress = 0x0;
  MPU_InitStruct.Size = MPU_REGION_SIZE_4GB;
  MPU_InitStruct.SubRegionDisable = 0x87;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL0;
  MPU_InitStruct.AccessPermission = MPU_REGION_NO_ACCESS;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_DISABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_NOT_CACHEABLE;
  MPU_InitStruct.IsBufferable = MPU_ACCESS_NOT_BUFFERABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);

  /** Initializes and configures the Region and the memory to be protected
  */
  MPU_InitStruct.Number = MPU_REGION_NUMBER1;
  MPU_InitStruct.BaseAddress = 0x08000000;
  MPU_InitStruct.Size = MPU_REGION_SIZE_1MB;
  MPU_InitStruct.SubRegionDisable = 0x0;
  MPU_InitStruct.TypeExtField = MPU_TEX_LEVEL1;
  MPU_InitStruct.AccessPermission = MPU_REGION_PRIV_RO;
  MPU_InitStruct.DisableExec = MPU_INSTRUCTION_ACCESS_ENABLE;
  MPU_InitStruct.IsShareable = MPU_ACCESS_NOT_SHAREABLE;
  MPU_InitStruct.IsCacheable = MPU_ACCESS_CACHEABLE;

  HAL_MPU_ConfigRegion(&MPU_InitStruct);
  /* Enables the MPU */
  HAL_MPU_Enable(MPU_PRIVILEGED_DEFAULT);

}

/**
  * @brief  Period elapsed callback in non blocking mode
  * @note   This function is called  when TIM6 interrupt took place, inside
  * HAL_TIM_IRQHandler(). It makes a direct call to HAL_IncTick() to increment
  * a global variable "uwTick" used as application time base.
  * @param  htim : TIM handle
  * @retval None
  */
void HAL_TIM_PeriodElapsedCallback(TIM_HandleTypeDef *htim)
{
  /* USER CODE BEGIN Callback 0 */

  /* USER CODE END Callback 0 */
  if (htim->Instance == TIM6)
  {
    HAL_IncTick();
  }
  /* USER CODE BEGIN Callback 1 */

  /* USER CODE END Callback 1 */
}

/**
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
  /* Error_Handler() e' chamado por QUALQUER MX_*_Init()/HAL_*Init() que
   * falhe em main() — inclusive ANTES de MX_GPIO_Init() rodar (ex.:
   * SystemClock_Config() travando porque o LSE nao partiu a tempo, 2026-10:
   * bomba ficou ligada direto porque o pino nunca saiu do estado de reset,
   * flutuante — mesmo achado da bancada de atuadores da Fase 2, so' que
   * permanente desta vez porque o while(1) abaixo nunca devolve o controle).
   * Forca bomba off + valvulas abertas (NA) AQUI, direto, sem depender de
   * nenhum init anterior ja ter rodado — so' entao trava. Nao resolve a
   * causa raiz de cada chamador, mas garante que falhar bem cedo no boot
   * nunca deixa o atuador em estado indefinido. */
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  HAL_GPIO_WritePin(PUMP_CTRL_GPIO_Port, PUMP_CTRL_Pin, GPIO_PIN_RESET); // bomba off (PWM LOW = 0%)
  HAL_GPIO_WritePin(VALVE_1_GPIO_Port, VALVE_1_Pin, GPIO_PIN_RESET);     // valvulas NA: LOW = aberta
  HAL_GPIO_WritePin(VALVE_2_GPIO_Port, VALVE_2_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(VALVE_3_GPIO_Port, VALVE_3_Pin, GPIO_PIN_RESET);
  {
    GPIO_InitTypeDef gpio_init = {0};
    gpio_init.Mode = GPIO_MODE_OUTPUT_PP;
    gpio_init.Pull = GPIO_NOPULL;
    gpio_init.Speed = GPIO_SPEED_FREQ_LOW;

    gpio_init.Pin = PUMP_CTRL_Pin;
    HAL_GPIO_Init(PUMP_CTRL_GPIO_Port, &gpio_init);

    gpio_init.Pin = VALVE_1_Pin | VALVE_2_Pin | VALVE_3_Pin; // mesma porta (GPIOD)
    HAL_GPIO_Init(VALVE_1_GPIO_Port, &gpio_init);
  }

  /* User can add his own implementation to report the HAL error return state */
  __disable_irq();
  while (1)
  {
  }
  /* USER CODE END Error_Handler_Debug */
}
#ifdef USE_FULL_ASSERT
/**
  * @brief  Reports the name of the source file and the source line number
  *         where the assert_param error has occurred.
  * @param  file: pointer to the source file name
  * @param  line: assert_param error line source number
  * @retval None
  */
void assert_failed(uint8_t *file, uint32_t line)
{
  /* USER CODE BEGIN 6 */
  /* User can add his own implementation to report the file name and line number,
     ex: printf("Wrong parameters value: file %s on line %d\r\n", file, line) */
  /* USER CODE END 6 */
}
#endif /* USE_FULL_ASSERT */
