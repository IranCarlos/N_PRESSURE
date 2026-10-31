/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-22
 */
#include "calibration.h"
#include <math.h>
#include <string.h>

float Cal_ApplyMmHg(const Cal_Coeffs_t *c, float d_kpa)
{
    return ((d_kpa * c->scale) + c->offset_kpa) * CAL_MMHG_PER_KPA;
}

uint8_t Cal_CoeffsPlausible(const Cal_Coeffs_t *c)
{
    if ((c->scale != c->scale) || (c->offset_kpa != c->offset_kpa)) { /* NaN */
        return 0U;
    }
    if ((c->scale < CAL_SCALE_MIN) || (c->scale > CAL_SCALE_MAX)) {
        return 0U;
    }
    if (fabsf(c->offset_kpa * CAL_MMHG_PER_KPA) > CAL_OFFSET_MAX_MMHG) {
        return 0U;
    }
    return 1U;
}

uint8_t Cal_IsDuplicatePoint(const Cal_Point_t *pts, uint8_t n, float ref_kpa)
{
    uint8_t i;
    for (i = 0U; i < n; i++) {
        if (fabsf((pts[i].ref_kpa - ref_kpa) * CAL_MMHG_PER_KPA) < CAL_PC_MIN_POINT_GAP_MMHG) {
            return 1U;
        }
    }
    return 0U;
}

Cal_FitResult_t Cal_FitPc(const Cal_Point_t *pts, uint8_t n, float linearity_max_mmhg,
                          Cal_Fit_t *out)
{
    float sd = 0.0f;
    float sr = 0.0f;
    float sdd = 0.0f;
    float sdr = 0.0f;
    float lo;
    float hi;
    float den;
    float fn;
    uint8_t i;

    memset(out, 0, sizeof(*out));
    out->n = n;
    if ((pts == NULL) || (n < CAL_PC_MIN_POINTS) || (n > CAL_MAX_POINTS)) {
        return CAL_FIT_TOO_FEW_POINTS;
    }

    lo = pts[0].ref_kpa;
    hi = pts[0].ref_kpa;
    for (i = 0U; i < n; i++) {
        sd += pts[i].d_kpa;
        sr += pts[i].ref_kpa;
        sdd += pts[i].d_kpa * pts[i].d_kpa;
        sdr += pts[i].d_kpa * pts[i].ref_kpa;
        lo = (pts[i].ref_kpa < lo) ? pts[i].ref_kpa : lo;
        hi = (pts[i].ref_kpa > hi) ? pts[i].ref_kpa : hi;
    }
    out->span_mmhg = (hi - lo) * CAL_MMHG_PER_KPA;

    /* Minimos quadrados de ref = escala * d + offset:
     *   escala = (n*Sdr - Sd*Sr) / (n*Sdd - Sd^2), offset = (Sr - escala*Sd)/n */
    fn = (float)n;
    den = (fn * sdd) - (sd * sd);
    if (fabsf(den) < 1e-6f) {
        return CAL_FIT_DEGENERATE;
    }
    out->coeffs.scale = ((fn * sdr) - (sd * sr)) / den;
    out->coeffs.offset_kpa = (sr - (out->coeffs.scale * sd)) / fn;

    for (i = 0U; i < n; i++) {
        float res = fabsf(Cal_ApplyMmHg(&out->coeffs, pts[i].d_kpa) - (pts[i].ref_kpa * CAL_MMHG_PER_KPA));
        out->max_residual_mmhg = (res > out->max_residual_mmhg) ? res : out->max_residual_mmhg;
    }

    if (out->span_mmhg < CAL_PC_MIN_SPAN_MMHG) {
        return CAL_FIT_SPAN_TOO_SMALL;
    }
    if ((out->coeffs.scale < CAL_SCALE_MIN) || (out->coeffs.scale > CAL_SCALE_MAX)) {
        return CAL_FIT_SCALE_RANGE;
    }
    if (fabsf(out->coeffs.offset_kpa * CAL_MMHG_PER_KPA) > CAL_OFFSET_MAX_MMHG) {
        return CAL_FIT_OFFSET_RANGE;
    }
    if (out->max_residual_mmhg > linearity_max_mmhg) {
        return CAL_FIT_NONLINEAR;
    }
    return CAL_FIT_OK;
}

Cal_VerifyResult_t Cal_Verify(const Cal_Coeffs_t *current, const Cal_Point_t *p1,
                              const Cal_Point_t *p2, float ok_mmhg, float fix_max_mmhg,
                              Cal_Verify_t *out)
{
    float e1;
    float e2;
    float span_mmhg;
    float dd;

    memset(out, 0, sizeof(*out));
    e1 = Cal_ApplyMmHg(current, p1->d_kpa) - (p1->ref_kpa * CAL_MMHG_PER_KPA);
    e2 = Cal_ApplyMmHg(current, p2->d_kpa) - (p2->ref_kpa * CAL_MMHG_PER_KPA);
    out->err1_mmhg = e1;
    out->err2_mmhg = e2;
    out->max_abs_err_mmhg = (fabsf(e1) > fabsf(e2)) ? fabsf(e1) : fabsf(e2);

    if (out->max_abs_err_mmhg <= ok_mmhg) {
        return CAL_VERIFY_OK;
    }
    if (out->max_abs_err_mmhg > fix_max_mmhg) {
        return CAL_VERIFY_ALERT; /* grande demais para o equipamento se corrigir sozinho */
    }

    span_mmhg = fabsf(p2->ref_kpa - p1->ref_kpa) * CAL_MMHG_PER_KPA;
    dd = p2->d_kpa - p1->d_kpa;
    if ((span_mmhg >= CAL_VERIFY_MIN_SLOPE_SPAN_MMHG) && (fabsf(dd) > 1e-3f)) {
        /* Reta pelos 2 pontos (escala e offset). */
        out->new_coeffs.scale = (p2->ref_kpa - p1->ref_kpa) / dd;
        out->new_coeffs.offset_kpa = p1->ref_kpa - (p1->d_kpa * out->new_coeffs.scale);
        out->slope_corrected = 1U;
    } else {
        /* Pontos proximos: mantem a escala, tira o erro medio do offset. */
        out->new_coeffs.scale = current->scale;
        out->new_coeffs.offset_kpa = current->offset_kpa - (((e1 + e2) * 0.5f) / CAL_MMHG_PER_KPA);
        out->slope_corrected = 0U;
    }
    if (Cal_CoeffsPlausible(&out->new_coeffs) == 0U) {
        return CAL_VERIFY_INVALID;
    }
    return CAL_VERIFY_CORRECTED;
}

/* ---- Registro persistente -------------------------------------------------
 *  0 magic u32 "CAL1" | 4 versao u16 | 6 reservado u16 | 8 seq u32
 * 12 escala f32 | 16 offset_kpa f32 | 20 ano u16 | 22 mes,dia,hora,min,seg u8
 * 27 origem u8 | 28 n_pontos u8 | 29..31 reservado | 32 residuo f32
 * 36 crc32 u32 (sobre 0..35)                        = 40 bytes, little-endian
 */
#define CAL_MAGIC   (0x314C4143UL) /* "CAL1" */
#define CAL_VERSION (1U)

static void put16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)(v & 0xFFU); p[1] = (uint8_t)(v >> 8); }
static void put32(uint8_t *p, uint32_t v)
{
    p[0] = (uint8_t)(v & 0xFFU);
    p[1] = (uint8_t)((v >> 8) & 0xFFU);
    p[2] = (uint8_t)((v >> 16) & 0xFFU);
    p[3] = (uint8_t)((v >> 24) & 0xFFU);
}
static void putf(uint8_t *p, float f) { uint32_t u; memcpy(&u, &f, sizeof(u)); put32(p, u); }
static uint16_t get16(const uint8_t *p) { return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8)); }
static uint32_t get32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}
static float getf(const uint8_t *p) { uint32_t u = get32(p); float f; memcpy(&f, &u, sizeof(f)); return f; }

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

void Cal_RecordFactory(Cal_Record_t *r)
{
    memset(r, 0, sizeof(*r));
    r->coeffs.scale = 1.0f;
    r->coeffs.offset_kpa = 0.0f;
    r->source = (uint8_t)CAL_SOURCE_FACTORY;
}

size_t Cal_RecordSerialize(const Cal_Record_t *r, uint32_t seq, uint8_t *out, size_t cap)
{
    if ((r == NULL) || (out == NULL) || (cap < CAL_RECORD_BLOB_LEN)) {
        return 0U;
    }
    memset(out, 0, CAL_RECORD_BLOB_LEN);
    put32(&out[0], CAL_MAGIC);
    put16(&out[4], CAL_VERSION);
    put32(&out[8], seq);
    putf(&out[12], r->coeffs.scale);
    putf(&out[16], r->coeffs.offset_kpa);
    put16(&out[20], r->year);
    out[22] = r->month;
    out[23] = r->day;
    out[24] = r->hour;
    out[25] = r->minute;
    out[26] = r->second;
    out[27] = r->source;
    out[28] = r->n_points;
    putf(&out[32], r->max_residual_mmhg);
    put32(&out[36], crc32_ieee(out, 36U));
    return CAL_RECORD_BLOB_LEN;
}

uint8_t Cal_RecordParse(const uint8_t *in, size_t len, Cal_Record_t *r, uint32_t *seq)
{
    Cal_Record_t tmp;

    if ((in == NULL) || (r == NULL) || (len < CAL_RECORD_BLOB_LEN)) {
        return 0U;
    }
    if ((get32(&in[0]) != CAL_MAGIC) || (get16(&in[4]) != CAL_VERSION) ||
        (crc32_ieee(in, 36U) != get32(&in[36]))) {
        return 0U;
    }
    memset(&tmp, 0, sizeof(tmp));
    tmp.coeffs.scale = getf(&in[12]);
    tmp.coeffs.offset_kpa = getf(&in[16]);
    tmp.year = get16(&in[20]);
    tmp.month = in[22];
    tmp.day = in[23];
    tmp.hour = in[24];
    tmp.minute = in[25];
    tmp.second = in[26];
    tmp.source = in[27];
    tmp.n_points = in[28];
    tmp.max_residual_mmhg = getf(&in[32]);
    if (Cal_CoeffsPlausible(&tmp.coeffs) == 0U) {
        return 0U; /* integro mas absurdo: nao aplica */
    }
    *r = tmp;
    if (seq != NULL) {
        *seq = get32(&in[8]);
    }
    return 1U;
}
