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
#include "fatfs.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include "lvgl.h"
#include "lv_port_disp.h"
#include "lv_port_indev.h"
#include "adc_acq.h"
#include "control_loop.h"
#include "encoder.h"
#include "pedal.h"
#include "hmi_ui.h"
#include "hmi_encoder_group.h"
#include "st7789.h"
#include "buzzer.h"
#include "eeprom_storage_io.h"
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
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

#define TASK_ENCODER_PERIOD_MS      (1u)
#define TASK_CONTROL_PERIOD_MS      (1u)
#define TASK_HMI_PERIOD_MS          (20u)
#define TASK_LVGL_HANDLER_PERIOD_MS (15u)
#define TASK_IWDG_REFRESH_PERIOD_MS (1u)

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
ADC_HandleTypeDef hadc2;
DMA_HandleTypeDef hdma_adc2;

I2C_HandleTypeDef hi2c1;

IWDG_HandleTypeDef hiwdg1;

SD_HandleTypeDef hsd1;

SPI_HandleTypeDef hspi1;
DMA_HandleTypeDef hdma_spi1_tx;

TIM_HandleTypeDef htim2;
TIM_HandleTypeDef htim3;
TIM_HandleTypeDef htim4;

PCD_HandleTypeDef hpcd_USB_OTG_HS;
HCD_HandleTypeDef hhcd_USB_OTG_HS;

/* USER CODE BEGIN PV */
// Estruturas de dados globais para o estado do sistema
static SystemConfig_t g_system_cfg;
static SystemRuntime_t g_runtime_data;

static uint32_t g_last_encoder_ms;
static uint32_t g_last_control_ms;
static uint32_t g_last_hmi_ms;
static uint32_t g_last_lvgl_ms;
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



   SCB->CCR &= ~SCB_CCR_UNALIGN_TRP_Msk;
  __DSB();
  __ISB();
  /* USER CODE END 1 */

  /* MPU Configuration---------------------------------
  -----------------------*/
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
  /* USER CODE BEGIN 2 */
  /* Liga o backlight do display */
  HAL_GPIO_WritePin(TFT_BACKLIGHT_GPIO_Port, TFT_BACKLIGHT_Pin, GPIO_PIN_SET);

#if APP_MODE_BRINGUP
  // --- MODO BRING-UP: testa display, encoder e buzzer isoladamente ---
#if BRINGUP_ENABLE_BUZZER
  Buzzer_Init();
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
    lv_arc_set_range(arc, 0, 50);
    lv_arc_set_value(arc, 25);

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

  g_last_lvgl_ms = HAL_GetTick();
#endif /* BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL */

#if (BRINGUP_ENABLE_BUZZER && !BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_ENCODER)
  // Buzzer isolado: sem display nem encoder para observar, um bipe de
  // confirmacao na inicializacao e o unico sinal de que o driver rodou.
  Buzzer_Beep(2500u, 200u);
#endif

  g_last_encoder_ms = HAL_GetTick();
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
  ADC_Acq_Init(&hadc2, &g_system_cfg.sensor_cal, &g_runtime_data);

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

  // 9. Inicializacao dos marcos de agendamento cooperativo
  g_last_encoder_ms = HAL_GetTick();
  g_last_control_ms = g_last_encoder_ms;
  g_last_hmi_ms = g_last_encoder_ms;
  g_last_lvgl_ms = g_last_encoder_ms;
#endif /* APP_MODE_BRINGUP */
  /* USER CODE END 2 */

  /* Infinite loop */
  /* USER CODE BEGIN WHILE */
  while (1)
  {
    // --- SUPER LOOP COOPERATIVO (nao bloqueante) ---
    uint32_t now = HAL_GetTick();
    uint8_t ran_task = 0u;

#if APP_MODE_BRINGUP
    // --- MODO BRING-UP: testa display, encoder e buzzer isoladamente ---
#if (BRINGUP_ENABLE_ENCODER && BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL)
    static uint32_t s_last_demo_draw_ms = 0u;
    static int32_t  s_last_bar_px = -1;
    static uint16_t s_last_bar_color = 0xFFFFu;
    static uint32_t s_btn_flash_until_ms = 0u;
#endif
#if (BRINGUP_ENABLE_ENCODER && BRINGUP_ENABLE_BUZZER)
    static int32_t  s_last_steps_buzz = 0;
#endif
#if (BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_ENCODER)
    static uint32_t s_last_color_ms = 0u;
    static uint8_t  s_color_idx = 0u;
    static const uint16_t s_colors[] = {
      0xF800u, /* vermelho */
      0x07E0u, /* verde    */
      0x001Fu, /* azul     */
      0xFFFFu, /* branco   */
      0x0000u, /* preto    */
    };
#endif

#if BRINGUP_ENABLE_ENCODER
    if ((uint32_t)(now - g_last_encoder_ms) >= TASK_ENCODER_PERIOD_MS) {
      g_last_encoder_ms = now;
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
#endif

#if (BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL)
      if ((Encoder_GetEncBtnEvent() != 0u) || (Encoder_GetUserBtnEvent() != 0u)) {
        s_btn_flash_until_ms = now + 300u;
      }
#endif

      ran_task = 1u;
    }
#endif /* BRINGUP_ENABLE_ENCODER */

#if (BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL)
#if BRINGUP_ENABLE_ENCODER
    // --- Barra no display acompanhando o encoder ---
    if ((uint32_t)(now - s_last_demo_draw_ms) >= 50u) {
      Encoder_State_t *enc_state;
      int32_t steps;
      uint16_t color;

      s_last_demo_draw_ms = now;
      enc_state = Encoder_GetState();

      steps = enc_state->steps % 320;
      if (steps < 0) {
        steps += 320;
      }

      color = (now < s_btn_flash_until_ms) ? 0xF800u /* vermelho: botao pressionado */
                                            : 0x07E0u /* verde: normal */;

      if ((steps != s_last_bar_px) || (color != s_last_bar_color)) {
        ST7789_FillRect(0, 100, 319, 139, 0x0000);
        if (steps > 0) {
          ST7789_FillRect(0, 100, (uint16_t)(steps - 1), 139, color);
        }
        s_last_bar_px = steps;
        s_last_bar_color = color;
      }

      ran_task = 1u;
    }
#else
    // --- Display isolado: ciclo automatico de cores solidas ---
    // Valida SPI + init do painel sem depender do encoder. Se as cores
    // aparecerem corretas nessa ordem, o driver ST7789 esta ok.
    if ((uint32_t)(now - s_last_color_ms) >= 1000u) {
      s_last_color_ms = now;
      ST7789_FillRect(0, 0, 319, 239, s_colors[s_color_idx]);
      s_color_idx = (uint8_t)((s_color_idx + 1u) % (sizeof(s_colors) / sizeof(s_colors[0])));
      ran_task = 1u;
    }
#endif /* BRINGUP_ENABLE_ENCODER */
#endif /* BRINGUP_ENABLE_DISPLAY && !BRINGUP_ENABLE_LVGL */

#if (BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL)
    // --- Estagio LVGL+DMA: bombeia o timer handler (renderiza + dispara o
    //     flush por HAL_SPI_Transmit_DMA quando algo mudou na tela). ---
    if ((uint32_t)(now - g_last_lvgl_ms) >= TASK_LVGL_HANDLER_PERIOD_MS) {
      g_last_lvgl_ms = now;
      (void)lv_timer_handler();
      ran_task = 1u;
    }
#endif /* BRINGUP_ENABLE_DISPLAY && BRINGUP_ENABLE_LVGL */
#else
    if ((uint32_t)(now - g_last_encoder_ms) >= TASK_ENCODER_PERIOD_MS) {
      g_last_encoder_ms = now;
      Encoder_Update();

      if ((Encoder_GetUserBtnEvent() != 0u) || (Pedal_GetEvent() != 0u)) {
        if (g_runtime_data.state == SYSTEM_STATE_IDLE) {
          ControlLoop_StartInflation();
        } else {
          g_runtime_data.bt_user_pressed = 1u;
        }
      }

      ran_task = 1u;
    }

    if ((uint32_t)(now - g_last_control_ms) >= TASK_CONTROL_PERIOD_MS) {
      g_last_control_ms = now;
      ControlLoop_Update();
      ran_task = 1u;
    }

    if ((uint32_t)(now - g_last_hmi_ms) >= TASK_HMI_PERIOD_MS) {
      g_last_hmi_ms = now;
      HMI_Update();
      ran_task = 1u;
    }

    if ((uint32_t)(now - g_last_lvgl_ms) >= TASK_LVGL_HANDLER_PERIOD_MS) {
      g_last_lvgl_ms = now;
      (void)lv_timer_handler();
      ran_task = 1u;
    }
#endif /* APP_MODE_BRINGUP */

    // Alimenta o watchdog (IWDG1, ~8.2s de timeout) a cada volta do loop.
    // Novo no H723: o L476 original nao tinha watchdog configurado.
    HAL_IWDG_Refresh(&hiwdg1);

    // Reduz consumo quando nao ha tarefas pendentes.
    if (ran_task == 0u) {
      __WFI();
    }
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
                              |RCC_OSCILLATORTYPE_LSI;
  RCC_OscInitStruct.HSIState = RCC_HSI_DIV1;
  RCC_OscInitStruct.HSICalibrationValue = 64;
  RCC_OscInitStruct.LSIState = RCC_LSI_ON;
  RCC_OscInitStruct.HSI48State = RCC_HSI48_ON;
  RCC_OscInitStruct.PLL.PLLState = RCC_PLL_ON;
  RCC_OscInitStruct.PLL.PLLSource = RCC_PLLSOURCE_HSI;
  RCC_OscInitStruct.PLL.PLLM = 4;
  RCC_OscInitStruct.PLL.PLLN = 34;
  RCC_OscInitStruct.PLL.PLLP = 1;
  RCC_OscInitStruct.PLL.PLLQ = 10;
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

  if (HAL_RCC_ClockConfig(&RCC_ClkInitStruct, FLASH_LATENCY_3) != HAL_OK)
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
  hadc2.Init.ExternalTrigConv = ADC_SOFTWARE_START;
  hadc2.Init.ExternalTrigConvEdge = ADC_EXTERNALTRIGCONVEDGE_NONE;
  hadc2.Init.ConversionDataManagement = ADC_CONVERSIONDATA_DMA_ONESHOT;
  hadc2.Init.Overrun = ADC_OVR_DATA_PRESERVED;
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
  hsd1.Init.ClockDiv = 0;
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
  htim3.Init.Prescaler = 200-1;
  htim3.Init.CounterMode = TIM_COUNTERMODE_UP;
  htim3.Init.Period = 1000-1;
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
  hpcd_USB_OTG_HS.Init.lpm_enable = DISABLE;
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
  hhcd_USB_OTG_HS.Init.dma_enable = DISABLE;
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
  HAL_NVIC_SetPriority(DMA1_Stream0_IRQn, 0, 0);
  HAL_NVIC_EnableIRQ(DMA1_Stream0_IRQn);
  /* DMA1_Stream1_IRQn interrupt configuration */
  HAL_NVIC_SetPriority(DMA1_Stream1_IRQn, 0, 0);
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
  __HAL_RCC_GPIOA_CLK_ENABLE();
  __HAL_RCC_GPIOC_CLK_ENABLE();
  __HAL_RCC_GPIOB_CLK_ENABLE();
  __HAL_RCC_GPIOD_CLK_ENABLE();

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(TFT_BACKLIGHT_GPIO_Port, TFT_BACKLIGHT_Pin, GPIO_PIN_RESET);

  /*Configure GPIO pin Output Level */
  HAL_GPIO_WritePin(GPIOC, TFT_CS_Pin|TFT_DC_Pin|BUZZER_Pin, GPIO_PIN_RESET);

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

  /*Configure GPIO pins : TFT_CS_Pin TFT_DC_Pin BUZZER_Pin */
  GPIO_InitStruct.Pin = TFT_CS_Pin|TFT_DC_Pin|BUZZER_Pin;
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
  * @brief  This function is executed in case of error occurrence.
  * @retval None
  */
void Error_Handler(void)
{
  /* USER CODE BEGIN Error_Handler_Debug */
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
