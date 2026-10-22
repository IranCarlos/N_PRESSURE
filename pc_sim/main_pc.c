/**
 * @file main_pc.c
 * @brief Simulador de PC (SDL2) rodando o MESMO codigo de Core/App/HMI_UI
 *        que vai no firmware, com pc_sim/ui_backend_mock.c no lugar do
 *        backend real.
 *
 * Entrada (so' encoder, como no equipamento):
 *   Seta esquerda / direita  girar o encoder (tambem: roda do mouse)
 *   Enter                    botao do encoder (segurar = long-press;
 *                            tambem: botao do meio do mouse)
 *   Espaco                   pedal / botao do usuario (inicia em IDLE,
 *                            senao aborta/desinfla)
 *   F1..F4, F5               injeta E001..E004, E013
 *   F8                       limpa o erro
 *   S / R                    alterna cartao SD / RTC valido
 *   Esc                      sair
 */

#define SDL_MAIN_HANDLED
#include <SDL2/SDL.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "hmi_ui.h"
#include "ui_backend_mock.h"

#define SIM_HOR_RES   (320)
#define SIM_VER_RES   (240)
#define SIM_ZOOM      (2u)
#define SIM_HMI_PERIOD_MS (20u)

static bool s_key_prev[SDL_NUM_SCANCODES];

static bool key_down(SDL_Scancode sc)
{
    const Uint8 * keys = SDL_GetKeyboardState(NULL);
    return (keys != NULL) && (keys[sc] != 0u);
}

/* true so' na transicao solto -> pressionado */
static bool key_edge(SDL_Scancode sc)
{
    bool now = key_down(sc);
    bool edge = now && !s_key_prev[sc];

    s_key_prev[sc] = now;
    return edge;
}

/* --- Modo roteiro (--shots <dir>): navega sozinho e salva BMPs ----------- */

typedef enum { ACT_ROT, ACT_PRESS, ACT_LONG, ACT_PEDAL, ACT_ERR, ACT_CLEAR, ACT_SHOT } ScriptAct_t;

typedef struct {
    uint32_t delay_ms; /* espera antes desta acao */
    ScriptAct_t act;
    int32_t arg;
    const char * name;
} ScriptStep_t;

static const ScriptStep_t k_script[] = {
    {  500u, ACT_SHOT,  0, "00_boot" },
    { 2000u, ACT_SHOT,  0, "01_home_idle" },
    {  100u, ACT_PEDAL, 0, NULL },
    { 3000u, ACT_SHOT,  0, "02_home_inflating" },
    { 8000u, ACT_SHOT,  0, "03_home_stabilized" },
    {  100u, ACT_PEDAL, 0, NULL },
    { 4800u, ACT_SHOT,  0, "04_result_popup" },
    {  200u, ACT_PRESS, 0, NULL },                 /* Fechar */
    {  500u, ACT_PRESS, 0, NULL },                 /* botao de menu */
    {  500u, ACT_SHOT,  0, "05_menu" },
    {  100u, ACT_PRESS, 0, NULL },                 /* Novo Ensaio */
    {  500u, ACT_SHOT,  0, "06_new_test" },
    {  100u, ACT_PRESS, 0, NULL },                 /* edita Operador */
    {  300u, ACT_ROT,   3, NULL },
    {  400u, ACT_SHOT,  0, "07_new_test_editing" },
    {  100u, ACT_LONG,  0, NULL },                 /* sai da edicao */
    {  900u, ACT_ROT,   4, NULL },                 /* -> Cancelar */
    {  300u, ACT_PRESS, 0, NULL },
    {  500u, ACT_ROT,   1, NULL },                 /* -> Ajustes */
    {  300u, ACT_PRESS, 0, NULL },
    {  600u, ACT_SHOT,  0, "08_settings_top" },
    {  100u, ACT_ROT,   5, NULL },                 /* -> pressao de teste */
    {  600u, ACT_SHOT,  0, "09_settings_calibration" },
    {  100u, ACT_ROT,   6, NULL },                 /* -> dia */
    {  600u, ACT_SHOT,  0, "10_settings_datetime" },
    {  100u, ACT_ROT,   7, NULL },                 /* -> Teste de Vazamento */
    {  600u, ACT_SHOT,  0, "11_settings_bottom" },
    {  100u, ACT_PRESS, 0, NULL },                 /* inicia teste de vazamento */
    { 4000u, ACT_SHOT,  0, "12_settings_leak_running" },
    {  100u, ACT_ROT,   1, NULL },                 /* -> Voltar */
    {  300u, ACT_PRESS, 0, NULL },
    {  500u, ACT_ROT,   2, NULL },                 /* -> Voltar (menu) */
    {  300u, ACT_PRESS, 0, NULL },
    {  500u, ACT_ERR,   1, NULL },
    {  800u, ACT_SHOT,  0, "13_error_popup" },
};

static const char * s_shot_dir;
static size_t s_script_idx;
static uint32_t s_script_next_ms;
static int32_t s_script_diff;
static uint32_t s_script_release_ms;
static lv_display_t * s_disp;

#include "src/display/lv_display_private.h"

/* Salva o framebuffer RGB565 do LVGL (modo DIRECT = tela inteira) em BMP 24 bits. */
static void save_bmp(const char * name)
{
    char path[512];
    FILE * f;
    const lv_draw_buf_t * buf = s_disp->buf_act;
    uint32_t w = (uint32_t)SIM_HOR_RES;
    uint32_t h = (uint32_t)SIM_VER_RES;
    uint32_t row_bytes = w * 3u;
    uint32_t img_size = row_bytes * h;
    uint8_t hdr[54] = { 'B', 'M' };
    uint32_t y;

    (void)snprintf(path, sizeof(path), "%s/%s.bmp", s_shot_dir, name);
    f = fopen(path, "wb");
    if ((f == NULL) || (buf == NULL)) {
        printf("falha ao salvar %s\n", path);
        if (f != NULL) {
            (void)fclose(f);
        }
        return;
    }

#define PUT32(o, v) do { hdr[(o)] = (uint8_t)(v); hdr[(o) + 1] = (uint8_t)((v) >> 8); \
                         hdr[(o) + 2] = (uint8_t)((v) >> 16); hdr[(o) + 3] = (uint8_t)((v) >> 24); } while (0)
    PUT32(2, 54u + img_size);
    PUT32(10, 54u);
    PUT32(14, 40u);
    PUT32(18, w);
    PUT32(22, h);
    hdr[26] = 1u;
    hdr[28] = 24u;
    PUT32(34, img_size);
#undef PUT32
    (void)fwrite(hdr, 1u, sizeof(hdr), f);

    for (y = 0u; y < h; y++) {
        const uint16_t * src = (const uint16_t *)((const uint8_t *)buf->data +
                                                  ((h - 1u - y) * buf->header.stride));
        uint32_t x;

        for (x = 0u; x < w; x++) {
            uint16_t c = src[x];
            uint8_t bgr[3];

            bgr[0] = (uint8_t)((c & 0x1Fu) << 3);          /* B: bits 4..0 */
            bgr[1] = (uint8_t)(((c >> 5) & 0x3Fu) << 2);   /* G: bits 10..5 */
            bgr[2] = (uint8_t)(((c >> 11) & 0x1Fu) << 3);  /* R: bits 15..11 */
            (void)fwrite(bgr, 1u, 3u, f);
        }
    }
    (void)fclose(f);
    printf("captura: %s\n", path);
}

/* Executa o roteiro; devolve false quando terminou. */
static bool script_step(uint32_t now)
{
    const ScriptStep_t * st;

    if (s_script_idx >= (sizeof(k_script) / sizeof(k_script[0]))) {
        return false;
    }
    if (s_script_next_ms == 0u) {
        s_script_next_ms = now + k_script[0].delay_ms;
    }
    if (now < s_script_next_ms) {
        return true;
    }

    st = &k_script[s_script_idx];
    switch (st->act) {
        case ACT_ROT:   s_script_diff += st->arg;            break;
        case ACT_PRESS: s_script_release_ms = now + 100u;    break;
        case ACT_LONG:  s_script_release_ms = now + 800u;    break;
        case ACT_PEDAL: UiMock_UserButton();                 break;
        case ACT_ERR:   UiMock_InjectError((uint16_t)st->arg); break;
        case ACT_CLEAR: UiMock_ClearError();                 break;
        case ACT_SHOT:  save_bmp(st->name);                  break;
        default:                                             break;
    }

    s_script_idx++;
    if (s_script_idx < (sizeof(k_script) / sizeof(k_script[0]))) {
        s_script_next_ms = now + k_script[s_script_idx].delay_ms;
    }
    return true;
}

/* Encoder pelo teclado: cada toque numa seta = 1 detent. */
static void keyboard_encoder_read(lv_indev_t * indev, lv_indev_data_t * data)
{
    LV_UNUSED(indev);

    if (s_shot_dir != NULL) {
        data->enc_diff = (int16_t)s_script_diff;
        s_script_diff = 0;
        data->state = (SDL_GetTicks() < s_script_release_ms) ? LV_INDEV_STATE_PRESSED
                                                             : LV_INDEV_STATE_RELEASED;
        return;
    }

    data->enc_diff = 0;
    if (key_edge(SDL_SCANCODE_RIGHT) || key_edge(SDL_SCANCODE_DOWN)) {
        data->enc_diff++;
    }
    if (key_edge(SDL_SCANCODE_LEFT) || key_edge(SDL_SCANCODE_UP)) {
        data->enc_diff--;
    }
    data->state = key_down(SDL_SCANCODE_RETURN) ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static bool poll_hotkeys(void)
{
    static const struct {
        SDL_Scancode sc;
        uint16_t code;
    } k_errors[] = {
        { SDL_SCANCODE_F1, 1u }, { SDL_SCANCODE_F2, 2u }, { SDL_SCANCODE_F3, 3u },
        { SDL_SCANCODE_F4, 4u }, { SDL_SCANCODE_F5, 13u },
    };
    size_t i;

    if (key_edge(SDL_SCANCODE_ESCAPE)) {
        return false;
    }
    if (key_edge(SDL_SCANCODE_SPACE)) {
        UiMock_UserButton();
    }
    for (i = 0u; i < (sizeof(k_errors) / sizeof(k_errors[0])); i++) {
        if (key_edge(k_errors[i].sc)) {
            UiMock_InjectError(k_errors[i].code);
        }
    }
    if (key_edge(SDL_SCANCODE_F8)) {
        UiMock_ClearError();
    }
    if (key_edge(SDL_SCANCODE_S)) {
        UiMock_ToggleSd();
    }
    if (key_edge(SDL_SCANCODE_R)) {
        UiMock_ToggleRtc();
    }
    return true;
}

int main(int argc, char * argv[])
{
    lv_display_t * disp;
    lv_indev_t * wheel;
    lv_indev_t * kb_encoder;
    uint32_t last_hmi = 0u;

    if ((argc >= 3) && (strcmp(argv[1], "--shots") == 0)) {
        s_shot_dir = argv[2];
    }

    SDL_SetMainReady();
    lv_init();

    disp = lv_sdl_window_create(SIM_HOR_RES, SIM_VER_RES);
    LV_ASSERT_NULL(disp);
    s_disp = disp;
    lv_sdl_window_set_zoom(disp, SIM_ZOOM);
    lv_sdl_window_set_title(disp, "myPRESSURE - simulador 320x240");

    wheel = lv_sdl_mousewheel_create();

    kb_encoder = lv_indev_create();
    lv_indev_set_type(kb_encoder, LV_INDEV_TYPE_ENCODER);
    lv_indev_set_read_cb(kb_encoder, keyboard_encoder_read);

    UiMock_Init();
    HMI_Init(wheel);
    HMI_BindIndev(kb_encoder);

    printf("Setas: girar | Enter: botao (segurar = long-press) | Espaco: pedal\n"
           "F1-F5: E001/E002/E003/E004/E013 | F8: limpa erro | S: SD | R: RTC | Esc: sair\n");

    for (;;) {
        uint32_t wait_ms;
        uint32_t now;

        if (!poll_hotkeys()) {
            break;
        }
        if ((s_shot_dir != NULL) && !script_step(SDL_GetTicks())) {
            break;
        }

        UiMock_Step();

        now = SDL_GetTicks();
        if ((now - last_hmi) >= SIM_HMI_PERIOD_MS) {
            last_hmi = now;
            HMI_Update();
        }

        wait_ms = lv_timer_handler();
        if (wait_ms > 10u) {
            wait_ms = 10u;
        }
        SDL_Delay(wait_ms);
    }

    lv_sdl_quit();
    return 0;
}
