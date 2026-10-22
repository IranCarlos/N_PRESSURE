/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "params.h"
#include <string.h>

/* Onde cada parametro mora: qual struct (base) e em que posicao (offsetof). */
typedef enum { BASE_CFG = 0, BASE_SAFETY, BASE_TUNING } Base_t;

typedef struct {
    Param_Desc_t d;
    uint8_t      base;   /* Base_t */
    uint16_t     offset; /* offsetof() dentro da struct base */
} Entry_t;

/* Fundo de escala do sensor DESTE hardware: MPXV7025DP, 25 kPa = 187,5 mmHg.
 * Limite absoluto, setpoint e alvo do teste de vazamento nao podem passar
 * daqui (o sensor satura e nada acima e' medido). Trocar junto com o sensor
 * de 300 mmHg do proximo hardware. */
#define PARAMS_SENSOR_FULL_SCALE_MMHG (187.0f)
#define PARAMS_SETPOINT_MAX_MMHG      (180.0f)

#define F32 ((uint8_t)PARAM_TYPE_F32)
#define U32 ((uint8_t)PARAM_TYPE_U32)
#define CFG(m)    BASE_CFG,    (uint16_t)offsetof(SystemConfig_t, m)
#define SAFETY(m) BASE_SAFETY, (uint16_t)offsetof(ControlCore_SafetyConfig_t, m)
#define TUNING(m) BASE_TUNING, (uint16_t)offsetof(ControlCore_TuningConfig_t, m)

/* IDs: grupo no byte alto. Estaveis entre versoes - nunca reaproveitar um
 * id removido (o bloco gravado na EEPROM e' por id). Faixas: limites
 * fisicos/de sanidade, nao recomendacao de ajuste. */
static const Entry_t k_table[] = {
    /* PID (malha de regulacao perto do setpoint) */
    { { 0x0101U, F32, PARAM_GROUP_PID, "pid.kp",      "",    0.0f,    20.0f }, CFG(pid.kp) },
    { { 0x0102U, F32, PARAM_GROUP_PID, "pid.ki",      "",    0.0f,     5.0f }, CFG(pid.ki) },
    { { 0x0103U, F32, PARAM_GROUP_PID, "pid.kd",      "",    0.0f,    20.0f }, CFG(pid.kd) },
    { { 0x0104U, F32, PARAM_GROUP_PID, "pid.ff",      "",    0.0f,    20.0f }, CFG(pid.ff) },
    { { 0x0105U, F32, PARAM_GROUP_PID, "pid.out_min", "kPa", 0.0f,   100.0f }, CFG(pid.out_min) },
    { { 0x0106U, F32, PARAM_GROUP_PID, "pid.out_max", "kPa", 0.1f,   200.0f }, CFG(pid.out_max) },
    { { 0x0107U, F32, PARAM_GROUP_PID, "pid.i_min",   "kPa", -200.0f,  0.0f }, CFG(pid.i_min) },
    { { 0x0108U, F32, PARAM_GROUP_PID, "pid.i_max",   "kPa", 0.0f,   200.0f }, CFG(pid.i_max) },

    /* Estabilizacao */
    { { 0x0201U, F32, PARAM_GROUP_ESTABILIZACAO, "estab.banda",    "mmHg", 0.5f,    30.0f }, CFG(stabilization.band_mmhg) },
    { { 0x0202U, U32, PARAM_GROUP_ESTABILIZACAO, "estab.tempo",    "ms",   0.0f, 60000.0f }, CFG(stabilization.time_ms) },
    { { 0x0203U, U32, PARAM_GROUP_ESTABILIZACAO, "estab.saida",    "ms",   0.0f,  5000.0f }, TUNING(stabilizing_exit_ms) },

    /* Valvula de alivio (V1) - pulso + acomodacao */
    { { 0x0301U, F32, PARAM_GROUP_VALVULA, "valv.fracao_abre", "",        0.0f,     2.0f }, TUNING(relief_on_fraction) },
    { { 0x0302U, F32, PARAM_GROUP_VALVULA, "valv.ms_por_mmhg", "ms/mmHg", 0.0f,   200.0f }, TUNING(relief_ms_per_mmhg) },
    { { 0x0303U, U32, PARAM_GROUP_VALVULA, "valv.pulso_min",   "ms",      0.0f,  2000.0f }, TUNING(relief_pulse_min_ms) },
    { { 0x0304U, U32, PARAM_GROUP_VALVULA, "valv.pulso_max",   "ms",      1.0f,  5000.0f }, TUNING(relief_pulse_max_ms) },
    { { 0x0305U, U32, PARAM_GROUP_VALVULA, "valv.acomodacao",  "ms",      0.0f, 10000.0f }, TUNING(relief_settle_ms) },

    /* Aproximacao rapida (bomba a 100% ate' a zona do PID) */
    { { 0x0401U, F32, PARAM_GROUP_APROXIMACAO, "aprox.antecipacao", "s",    0.0f,     5.0f }, TUNING(approach_lookahead_s) },
    { { 0x0402U, F32, PARAM_GROUP_APROXIMACAO, "aprox.zona",        "mmHg", 0.0f,   100.0f }, TUNING(approach_zone_mmhg) },
    { { 0x0403U, F32, PARAM_GROUP_APROXIMACAO, "aprox.rearme",      "mmHg", 0.0f,   200.0f }, TUNING(approach_rearm_mmhg) },
    { { 0x0404U, U32, PARAM_GROUP_APROXIMACAO, "aprox.rearme_t",    "ms",   0.0f, 10000.0f }, TUNING(approach_rearm_ms) },
    { { 0x0405U, F32, PARAM_GROUP_APROXIMACAO, "aprox.tau_pressao", "s",    0.001f,   2.0f }, TUNING(rate_p_tau_s) },
    { { 0x0406U, F32, PARAM_GROUP_APROXIMACAO, "aprox.tau_taxa",    "s",    0.001f,   2.0f }, TUNING(rate_tau_s) },
    { { 0x0407U, F32, PARAM_GROUP_APROXIMACAO, "bomba.minimo",      "",     0.0f,     1.0f }, TUNING(pump_min_effective) },

    /* Seguranca (Apendice D/E) */
    { { 0x0501U, F32, PARAM_GROUP_SEGURANCA, "seg.p_max_abs",       "mmHg",   10.0f, PARAMS_SENSOR_FULL_SCALE_MMHG }, SAFETY(p_max_abs_mmhg) },
    { { 0x0502U, U32, PARAM_GROUP_SEGURANCA, "seg.sensor_debounce", "ms",     10.0f,   5000.0f }, SAFETY(sensor_fault_debounce_ms) },
    { { 0x0503U, U32, PARAM_GROUP_SEGURANCA, "seg.t_inflar_max",    "ms",   1000.0f, 600000.0f }, SAFETY(t_inflar_max_ms) },
    { { 0x0504U, U32, PARAM_GROUP_SEGURANCA, "seg.t_desinflar_max", "ms",   1000.0f, 120000.0f }, SAFETY(t_desinflar_max_ms) },
    { { 0x0505U, F32, PARAM_GROUP_SEGURANCA, "seg.p_segura",        "mmHg",    0.0f,     50.0f }, SAFETY(p_segura_mmhg) },
    { { 0x0506U, U32, PARAM_GROUP_SEGURANCA, "seg.t_seguro",        "ms",      0.0f,   5000.0f }, SAFETY(t_seguro_ms) },

    /* Vazamento: deteccao em manutencao (E013) e teste de vazamento */
    { { 0x0601U, F32, PARAM_GROUP_VAZAMENTO, "vaz.queda",       "mmHg",   0.0f,    100.0f }, SAFETY(dp_vazamento_mmhg) },
    { { 0x0602U, U32, PARAM_GROUP_VAZAMENTO, "vaz.janela",      "ms",   100.0f, 600000.0f }, SAFETY(t_janela_ms) },
    { { 0x0603U, F32, PARAM_GROUP_VAZAMENTO, "teste.alvo",      "mmHg",   0.0f, PARAMS_SETPOINT_MAX_MMHG }, CFG(leak_test.target_mmhg) },
    { { 0x0604U, U32, PARAM_GROUP_VAZAMENTO, "teste.manter",    "ms",  1000.0f, 600000.0f }, CFG(leak_test.hold_time_ms) },
    { { 0x0605U, F32, PARAM_GROUP_VAZAMENTO, "teste.queda_max", "mmHg",   0.0f,    100.0f }, CFG(leak_test.max_drop_mmhg) },
    { { 0x0606U, U32, PARAM_GROUP_VAZAMENTO, "teste.timeout",   "ms",  1000.0f, 600000.0f }, CFG(leak_test.reach_timeout_ms) },

    /* Limites do usuario */
    { { 0x0701U, F32, PARAM_GROUP_LIMITES, "limite.setpoint_max",    "mmHg", 0.0f, PARAMS_SETPOINT_MAX_MMHG }, CFG(max_setpoint_mmhg) },
    { { 0x0702U, F32, PARAM_GROUP_LIMITES, "limite.setpoint_alerta", "mmHg", 0.0f, PARAMS_SETPOINT_MAX_MMHG }, CFG(setpoint_warn_mmhg) },
};

#define N_PARAMS ((uint16_t)(sizeof(k_table) / sizeof(k_table[0])))

static Params_Targets_t s_t;
static float s_default[N_PARAMS];
static float s_saved[N_PARAMS];

/* ---- acesso aos alvos ---------------------------------------------------- */

static uint8_t *target_ptr(const Entry_t *e)
{
    uint8_t *base;
    switch ((Base_t)e->base) {
        case BASE_CFG:    base = (uint8_t *)s_t.cfg;    break;
        case BASE_SAFETY: base = (uint8_t *)s_t.safety; break;
        case BASE_TUNING: base = (uint8_t *)s_t.tuning; break;
        default:          base = NULL;                  break;
    }
    return (base != NULL) ? &base[e->offset] : NULL;
}

/* memcpy em vez de cast de ponteiro: sem strict-aliasing/alinhamento. */
static float read_raw(const Entry_t *e)
{
    const uint8_t *p = target_ptr(e);
    float f = 0.0f;
    uint32_t u = 0U;

    if (p == NULL) {
        return 0.0f;
    }
    if (e->d.type == U32) {
        memcpy(&u, p, sizeof(u));
        return (float)u; /* faixas <= 600000 < 2^24: conversao exata */
    }
    memcpy(&f, p, sizeof(f));
    return f;
}

static void write_raw(const Entry_t *e, float v)
{
    uint8_t *p = target_ptr(e);
    uint32_t u;

    if (p == NULL) {
        return;
    }
    if (e->d.type == U32) {
        u = (uint32_t)v;
        memcpy(p, &u, sizeof(u)); /* 32 bits alinhados: escrita atomica no M7 */
    } else {
        memcpy(p, &v, sizeof(v));
    }
}

static uint8_t in_range(const Entry_t *e, float v)
{
    if ((v != v) || (v < e->d.min) || (v > e->d.max)) { /* (v != v): NaN */
        return 0U;
    }
    if ((e->d.type == U32) && ((float)(uint32_t)v != v)) {
        return 0U; /* inteiro exigido */
    }
    return 1U;
}

/* Regras entre parametros (faixa individual nao basta). */
static uint8_t consistent(void)
{
    if ((s_t.cfg == NULL) || (s_t.safety == NULL) || (s_t.tuning == NULL)) {
        return 0U;
    }
    if (s_t.cfg->pid.out_min >= s_t.cfg->pid.out_max) {
        return 0U;
    }
    if (s_t.tuning->relief_pulse_min_ms > s_t.tuning->relief_pulse_max_ms) {
        return 0U;
    }
    /* O limite absoluto tem que ficar ACIMA de qualquer setpoint permitido,
     * senao todo ensaio no topo da faixa terminaria em E001. */
    if (s_t.cfg->max_setpoint_mmhg >= s_t.safety->p_max_abs_mmhg) {
        return 0U;
    }
    return 1U;
}

/* ---- API ----------------------------------------------------------------- */

void Params_Bind(const Params_Targets_t *t)
{
    uint16_t i;

    s_t = *t;
    for (i = 0U; i < N_PARAMS; i++) {
        /* Um default fora da faixa (ex.: bloco legado da EEPROM com o 300
         * antigo) e' trazido para dentro dela: o controle nunca roda com um
         * valor que a tabela nao aceitaria. */
        float v = read_raw(&k_table[i]);
        if (v < k_table[i].d.min) {
            write_raw(&k_table[i], k_table[i].d.min);
        } else if (v > k_table[i].d.max) {
            write_raw(&k_table[i], k_table[i].d.max);
        } else {
            /* dentro da faixa */
        }
        s_default[i] = read_raw(&k_table[i]);
        s_saved[i] = s_default[i];
    }
}

uint16_t Params_Count(void)
{
    return N_PARAMS;
}

const Param_Desc_t *Params_DescAt(uint16_t index)
{
    return (index < N_PARAMS) ? &k_table[index].d : NULL;
}

int32_t Params_IndexOf(uint16_t id)
{
    uint16_t i;
    for (i = 0U; i < N_PARAMS; i++) {
        if (k_table[i].d.id == id) {
            return (int32_t)i;
        }
    }
    return -1;
}

float Params_Get(uint16_t index)
{
    return (index < N_PARAMS) ? read_raw(&k_table[index]) : 0.0f;
}

float Params_GetDefault(uint16_t index)
{
    return (index < N_PARAMS) ? s_default[index] : 0.0f;
}

Params_Result_t Params_Set(uint16_t index, float value)
{
    const Entry_t *e;
    float old;

    if (index >= N_PARAMS) {
        return PARAMS_ERR_ID;
    }
    e = &k_table[index];
    if (in_range(e, value) == 0U) {
        return PARAMS_ERR_RANGE;
    }
    old = read_raw(e);
    write_raw(e, value);
    if (consistent() == 0U) {
        write_raw(e, old); /* desfaz: o controle nunca roda com o par incoerente
                            * por mais que este instante */
        return PARAMS_ERR_CONSISTENCY;
    }
    return PARAMS_OK;
}

uint8_t Params_IsDirty(void)
{
    uint16_t i;
    for (i = 0U; i < N_PARAMS; i++) {
        if (read_raw(&k_table[i]) != s_saved[i]) {
            return 1U;
        }
    }
    return 0U;
}

void Params_MarkSaved(void)
{
    uint16_t i;
    for (i = 0U; i < N_PARAMS; i++) {
        s_saved[i] = read_raw(&k_table[i]);
    }
}

void Params_RevertToSaved(void)
{
    uint16_t i;
    for (i = 0U; i < N_PARAMS; i++) {
        write_raw(&k_table[i], s_saved[i]);
    }
}

void Params_RestoreDefaults(void)
{
    uint16_t i;
    for (i = 0U; i < N_PARAMS; i++) {
        write_raw(&k_table[i], s_default[i]);
    }
}

/* ---- bloco persistente ----------------------------------------------------
 *
 *   0  magic   u32  "PRM1"
 *   4  versao  u16  (1)
 *   6  n       u16  pares
 *   8  seq     u32  maior = mais novo (copias A/B)
 *  12  n x { id u16, reservado u16, valor f32 }
 *  ..  crc32   u32  sobre tudo antes (IEEE 802.3 refletido, init/xor 0xFFFFFFFF)
 * Little-endian.
 */
#define BLOB_MAGIC   (0x314D5250UL) /* "PRM1" em little-endian */
#define BLOB_VERSION (1U)
#define BLOB_HDR_LEN (12U)
#define BLOB_PAIR_LEN (8U)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

/* CRC32 local (bit a bit) - este servico nao depende do UsbProto. */
static uint32_t crc32_ieee(const uint8_t *d, size_t n)
{
    uint32_t c = 0xFFFFFFFFUL;
    size_t i;
    uint8_t b;
    for (i = 0U; i < n; i++) {
        c ^= d[i];
        for (b = 0U; b < 8U; b++) {
            /* bit 0 em 1: desloca e aplica o polinomio refletido 0xEDB88320 */
            c = ((c & 1UL) != 0UL) ? ((c >> 1) ^ 0xEDB88320UL) : (c >> 1);
        }
    }
    return c ^ 0xFFFFFFFFUL;
}

size_t Params_Serialize(uint8_t *out, size_t cap, uint32_t seq)
{
    size_t len = BLOB_HDR_LEN + ((size_t)N_PARAMS * BLOB_PAIR_LEN) + 4U;
    size_t idx;
    uint16_t i;
    uint32_t u;
    float v;

    if ((out == NULL) || (cap < len)) {
        return 0U;
    }
    put32(&out[0], BLOB_MAGIC);
    put16(&out[4], BLOB_VERSION);
    put16(&out[6], N_PARAMS);
    put32(&out[8], seq);
    idx = BLOB_HDR_LEN;
    for (i = 0U; i < N_PARAMS; i++) {
        v = read_raw(&k_table[i]);
        memcpy(&u, &v, sizeof(u));
        put16(&out[idx], k_table[i].d.id);
        put16(&out[idx + 2U], 0U);
        put32(&out[idx + 4U], u);
        idx += BLOB_PAIR_LEN;
    }
    put32(&out[idx], crc32_ieee(out, idx));
    return len;
}

uint8_t Params_ValidateBlob(const uint8_t *in, size_t len, uint32_t *seq)
{
    size_t body;
    uint16_t n;

    if ((in == NULL) || (len < (BLOB_HDR_LEN + 4U))) {
        return 0U;
    }
    if ((get32(&in[0]) != BLOB_MAGIC) || (get16(&in[4]) != BLOB_VERSION)) {
        return 0U;
    }
    n = get16(&in[6]);
    body = BLOB_HDR_LEN + ((size_t)n * BLOB_PAIR_LEN);
    if ((n > 64U) || ((body + 4U) > len)) {
        return 0U;
    }
    if (crc32_ieee(in, body) != get32(&in[body])) {
        return 0U;
    }
    if (seq != NULL) {
        *seq = get32(&in[8]);
    }
    return 1U;
}

uint16_t Params_ApplyBlob(const uint8_t *in, size_t len)
{
    uint16_t n;
    uint16_t k;
    uint16_t applied = 0U;
    int32_t idx;
    uint32_t u;
    float v;
    const uint8_t *pair;

    if (Params_ValidateBlob(in, len, NULL) == 0U) {
        return 0U;
    }
    Params_RestoreDefaults(); /* o que nao vier no bloco fica no default */
    n = get16(&in[6]);
    for (k = 0U; k < n; k++) {
        pair = &in[BLOB_HDR_LEN + ((size_t)k * BLOB_PAIR_LEN)];
        idx = Params_IndexOf(get16(pair));
        u = get32(&pair[4]);
        memcpy(&v, &u, sizeof(v));
        if ((idx >= 0) && (in_range(&k_table[idx], v) != 0U)) {
            write_raw(&k_table[idx], v);
            applied++;
        }
    }
    if (consistent() == 0U) {
        /* Bloco integro mas incoerente com as regras deste firmware (ex.:
         * regra nova): nao arrisca - fica todo no default. */
        Params_RestoreDefaults();
        applied = 0U;
    }
    Params_MarkSaved();
    return applied;
}
