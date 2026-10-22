/* Teste de host de params (Fase 7.6, sem HAL). Ver test/host/run.sh. */
#include "params.h"
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)

static SystemConfig_t s_cfg;
static ControlCore_SafetyConfig_t s_safety;
static ControlCore_TuningConfig_t s_tuning;

/* Mesmos defaults do firmware (EEPROM_Storage_LoadDefaults /
 * s_control_safety / ControlCore_DefaultTuning) nos campos da tabela. */
static void load_factory(void)
{
    memset(&s_cfg, 0, sizeof(s_cfg));
    s_cfg.pid.kp = 0.8f;
    s_cfg.pid.ki = 0.015f;
    s_cfg.pid.ff = 1.5f;
    s_cfg.pid.out_max = 40.0f;
    s_cfg.pid.i_min = -8.0f;
    s_cfg.pid.i_max = 8.0f;
    s_cfg.max_setpoint_mmhg = 180.0f;
    s_cfg.setpoint_warn_mmhg = 170.0f;
    s_cfg.stabilization.band_mmhg = 3.0f;
    s_cfg.stabilization.time_ms = 1000U;
    s_cfg.leak_test.target_mmhg = 180.0f;
    s_cfg.leak_test.hold_time_ms = 30000U;
    s_cfg.leak_test.max_drop_mmhg = 5.0f;
    s_cfg.leak_test.reach_timeout_ms = 20000U;

    s_safety.p_max_abs_mmhg = 185.0f;
    s_safety.sensor_fault_debounce_ms = 300U;
    s_safety.t_inflar_max_ms = 180000U;
    s_safety.t_desinflar_max_ms = 15000U;
    s_safety.dp_vazamento_mmhg = 1.0f;
    s_safety.t_janela_ms = 5000U;
    s_safety.p_segura_mmhg = 10.0f;
    s_safety.t_seguro_ms = 200U;

    ControlCore_DefaultTuning(&s_tuning);
}

static void bind(void)
{
    const Params_Targets_t t = { &s_cfg, &s_safety, &s_tuning };
    load_factory();
    Params_Bind(&t);
}

static uint16_t idx_of(uint16_t id)
{
    int32_t i = Params_IndexOf(id);
    CHECK(i >= 0);
    return (uint16_t)((i < 0) ? 0 : i);
}

static void test_table_sane(void)
{
    uint16_t i;
    uint16_t j;

    bind();
    CHECK(Params_Count() >= 30U);
    for (i = 0U; i < Params_Count(); i++) {
        const Param_Desc_t *d = Params_DescAt(i);
        float def = Params_GetDefault(i);
        CHECK(strlen(d->name) <= 23U);
        CHECK(strlen(d->unit) <= 7U);
        CHECK(d->min < d->max);
        /* o default de fabrica tem que caber na propria faixa */
        if ((def < d->min) || (def > d->max)) {
            printf("  default fora da faixa: %s = %g [%g, %g]\n", d->name, (double)def,
                   (double)d->min, (double)d->max);
            CHECK(0);
        }
        for (j = (uint16_t)(i + 1U); j < Params_Count(); j++) {
            CHECK(Params_DescAt(j)->id != d->id); /* ids unicos */
        }
    }
    CHECK(Params_DescAt(Params_Count()) == NULL);
    CHECK(Params_IndexOf(0xFFFFU) == -1);
    CHECK(Params_IsDirty() == 0U);
}

static void test_set_writes_target_live(void)
{
    bind();
    CHECK(Params_Set(idx_of(0x0101U), 1.25f) == PARAMS_OK);     /* pid.kp */
    CHECK(s_cfg.pid.kp == 1.25f);
    CHECK(Params_Set(idx_of(0x0305U), 600.0f) == PARAMS_OK);    /* valv.acomodacao (u32) */
    CHECK(s_tuning.relief_settle_ms == 600U);
    CHECK(Params_Set(idx_of(0x0503U), 90000.0f) == PARAMS_OK);  /* seg.t_inflar_max */
    CHECK(s_safety.t_inflar_max_ms == 90000U);
    CHECK(Params_IsDirty() == 1U);
}

static void test_range_and_type_rejected(void)
{
    float nan;
    uint32_t nan_bits = 0x7FC00000U;
    memcpy(&nan, &nan_bits, sizeof(nan));

    bind();
    CHECK(Params_Set(idx_of(0x0101U), 25.0f) == PARAMS_ERR_RANGE);
    CHECK(Params_Set(idx_of(0x0101U), -0.1f) == PARAMS_ERR_RANGE);
    CHECK(Params_Set(idx_of(0x0101U), nan) == PARAMS_ERR_RANGE);
    CHECK(Params_Set(idx_of(0x0305U), 400.5f) == PARAMS_ERR_RANGE); /* u32 exige inteiro */
    CHECK(s_cfg.pid.kp == 0.8f);
    CHECK(Params_IsDirty() == 0U);
    CHECK(Params_Set(Params_Count(), 1.0f) == PARAMS_ERR_ID);
}

static void test_consistency_rules(void)
{
    bind();
    /* pulso_min > pulso_max (150) */
    CHECK(Params_Set(idx_of(0x0303U), 200.0f) == PARAMS_ERR_CONSISTENCY);
    CHECK(s_tuning.relief_pulse_min_ms == 20U); /* desfeito */
    /* setpoint maximo nao pode alcancar o limite absoluto (185) */
    CHECK(Params_Set(idx_of(0x0501U), 175.0f) == PARAMS_ERR_CONSISTENCY); /* abaixo do setpoint max 180 */
    CHECK(Params_Set(idx_of(0x0701U), 160.0f) == PARAMS_OK);
    CHECK(Params_Set(idx_of(0x0501U), 175.0f) == PARAMS_OK);
    CHECK(Params_Set(idx_of(0x0701U), 175.0f) == PARAMS_ERR_CONSISTENCY);
    /* limites do sensor deste hardware (187,5 mmHg) */
    CHECK(Params_Set(idx_of(0x0501U), 190.0f) == PARAMS_ERR_RANGE);
    CHECK(Params_Set(idx_of(0x0701U), 181.0f) == PARAMS_ERR_RANGE);
    /* out_min >= out_max */
    CHECK(Params_Set(idx_of(0x0105U), 40.0f) == PARAMS_ERR_CONSISTENCY);
}

static void test_bind_clamps_out_of_range_default(void)
{
    const Params_Targets_t t = { &s_cfg, &s_safety, &s_tuning };
    load_factory();
    s_cfg.max_setpoint_mmhg = 300.0f; /* bloco legado antigo */
    Params_Bind(&t);
    CHECK(s_cfg.max_setpoint_mmhg == 180.0f);
    CHECK(Params_GetDefault(idx_of(0x0701U)) == 180.0f);
}

static void test_revert_and_defaults(void)
{
    bind();
    (void)Params_Set(idx_of(0x0101U), 2.0f);
    Params_MarkSaved();
    (void)Params_Set(idx_of(0x0101U), 3.0f);
    CHECK(Params_IsDirty() == 1U);
    Params_RevertToSaved();
    CHECK(s_cfg.pid.kp == 2.0f);
    CHECK(Params_IsDirty() == 0U);
    Params_RestoreDefaults();
    CHECK(s_cfg.pid.kp == 0.8f);
    CHECK(Params_IsDirty() == 1U); /* fabrica != salvo (2.0) ate' gravar */
}

static void test_blob_roundtrip(void)
{
    uint8_t blob[PARAMS_BLOB_MAX];
    size_t len;
    uint32_t seq = 0U;

    bind();
    (void)Params_Set(idx_of(0x0102U), 0.05f);
    (void)Params_Set(idx_of(0x0304U), 300.0f);
    len = Params_Serialize(blob, sizeof(blob), 42U);
    CHECK(len == 12U + (size_t)Params_Count() * 8U + 4U);
    CHECK(Params_ValidateBlob(blob, len, &seq) == 1U);
    CHECK(seq == 42U);

    bind(); /* "reboot": alvos de volta ao default */
    CHECK(s_cfg.pid.ki == 0.015f);
    CHECK(Params_ApplyBlob(blob, len) == Params_Count());
    CHECK(s_cfg.pid.ki == 0.05f);
    CHECK(s_tuning.relief_pulse_max_ms == 300U);
    CHECK(Params_IsDirty() == 0U); /* carregado = salvo */
}

static void test_blob_corruption_rejected(void)
{
    uint8_t blob[PARAMS_BLOB_MAX];
    size_t len;

    bind();
    len = Params_Serialize(blob, sizeof(blob), 1U);
    blob[20] ^= 0x01U; /* um bit num valor */
    CHECK(Params_ValidateBlob(blob, len, NULL) == 0U);
    CHECK(Params_ApplyBlob(blob, len) == 0U);
    blob[20] ^= 0x01U;
    blob[0] = 0x00U;   /* magic */
    CHECK(Params_ValidateBlob(blob, len, NULL) == 0U);
    CHECK(Params_ValidateBlob(blob, 10U, NULL) == 0U);
    CHECK(Params_Serialize(blob, 16U, 1U) == 0U); /* nao cabe */
}

/* Bloco gravado por um firmware que tinha um parametro a menos e um id que
 * nao existe mais: carrega o que reconhece, o resto fica no default. */
static void test_blob_forward_compatible(void)
{
    uint8_t blob[PARAMS_BLOB_MAX];
    size_t len;
    uint16_t n;
    uint8_t *pair;
    uint32_t crc;
    uint32_t c;
    size_t i;
    uint8_t b;

    bind();
    (void)Params_Set(idx_of(0x0101U), 4.0f); /* kp */
    len = Params_Serialize(blob, sizeof(blob), 7U);
    n = (uint16_t)(blob[6] | (blob[7] << 8));
    /* troca o id do 2o par por um desconhecido e tira o ultimo par */
    pair = &blob[12U + 8U];
    pair[0] = 0xEEU;
    pair[1] = 0xEEU;
    n--;
    blob[6] = (uint8_t)(n & 0xFFU);
    blob[7] = (uint8_t)(n >> 8);
    len = 12U + (size_t)n * 8U;
    c = 0xFFFFFFFFU; /* recalcula o CRC32 (mesmo algoritmo de params.c) */
    for (i = 0U; i < len; i++) {
        c ^= blob[i];
        for (b = 0U; b < 8U; b++) {
            c = ((c & 1U) != 0U) ? ((c >> 1) ^ 0xEDB88320U) : (c >> 1);
        }
    }
    crc = c ^ 0xFFFFFFFFU;
    blob[len] = (uint8_t)crc;
    blob[len + 1U] = (uint8_t)(crc >> 8);
    blob[len + 2U] = (uint8_t)(crc >> 16);
    blob[len + 3U] = (uint8_t)(crc >> 24);
    len += 4U;

    bind();
    CHECK(Params_ApplyBlob(blob, len) == (uint16_t)(Params_Count() - 2U));
    CHECK(s_cfg.pid.kp == 4.0f);
    CHECK(s_cfg.pid.ki == 0.015f); /* 2o par (ki) virou id desconhecido -> default */
}

int main(void)
{
    test_table_sane();
    test_set_writes_target_live();
    test_range_and_type_rejected();
    test_consistency_rules();
    test_bind_clamps_out_of_range_default();
    test_revert_and_defaults();
    test_blob_roundtrip();
    test_blob_corruption_rejected();
    test_blob_forward_compatible();
    printf(s_fail ? "params: %d FALHA(S)\n" : "params: OK\n", s_fail);
    return s_fail != 0;
}
