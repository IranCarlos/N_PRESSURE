/* Teste de host de calibration (Fase 7.8, sem HAL). Ver test/host/run.sh. */
#include "calibration.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

static int s_fail;
#define CHECK(c) do { if (!(c)) { printf("FALHA %s:%d: %s\n", __FILE__, __LINE__, #c); s_fail++; } } while (0)
#define NEAR(a, b, tol) (fabsf((a) - (b)) <= (tol))

static float k(float mmhg) { return mmhg / CAL_MMHG_PER_KPA; }

/* Sensor "real": d = (ref - offset) / escala, com erro de linearidade opcional. */
static Cal_Point_t pt(float ref_mmhg, float true_scale, float true_offset_mmhg, float nonlin_mmhg)
{
    Cal_Point_t p;
    p.ref_kpa = k(ref_mmhg);
    p.d_kpa = (k(ref_mmhg) - k(true_offset_mmhg)) / true_scale + k(nonlin_mmhg);
    return p;
}

static void test_fit_recovers_line(void)
{
    Cal_Point_t pts[4];
    Cal_Fit_t fit;

    pts[0] = pt(0.0f, 1.05f, -2.0f, 0.0f);
    pts[1] = pt(50.0f, 1.05f, -2.0f, 0.0f);
    pts[2] = pt(100.0f, 1.05f, -2.0f, 0.0f);
    pts[3] = pt(160.0f, 1.05f, -2.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 4U, 2.0f, &fit) == CAL_FIT_OK);
    CHECK(NEAR(fit.coeffs.scale, 1.05f, 1e-4f));
    CHECK(NEAR(fit.coeffs.offset_kpa * CAL_MMHG_PER_KPA, -2.0f, 1e-3f));
    CHECK(fit.max_residual_mmhg < 0.01f);
    CHECK(NEAR(fit.span_mmhg, 160.0f, 0.01f));
    CHECK(NEAR(Cal_ApplyMmHg(&fit.coeffs, pts[2].d_kpa), 100.0f, 0.01f));
}

static void test_fit_rejections(void)
{
    Cal_Point_t pts[4];
    Cal_Fit_t fit;

    pts[0] = pt(0.0f, 1.0f, 0.0f, 0.0f);
    pts[1] = pt(100.0f, 1.0f, 0.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 2U, 2.0f, &fit) == CAL_FIT_TOO_FEW_POINTS);  /* PC exige >= 3 */

    pts[0] = pt(80.0f, 1.0f, 0.0f, 0.0f);
    pts[1] = pt(100.0f, 1.0f, 0.0f, 0.0f);
    pts[2] = pt(120.0f, 1.0f, 0.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 3U, 2.0f, &fit) == CAL_FIT_SPAN_TOO_SMALL);  /* 40 < 50 */

    pts[0] = pt(0.0f, 1.5f, 0.0f, 0.0f);
    pts[1] = pt(80.0f, 1.5f, 0.0f, 0.0f);
    pts[2] = pt(160.0f, 1.5f, 0.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 3U, 2.0f, &fit) == CAL_FIT_SCALE_RANGE);

    pts[0] = pt(0.0f, 1.0f, 30.0f, 0.0f);
    pts[1] = pt(80.0f, 1.0f, 30.0f, 0.0f);
    pts[2] = pt(160.0f, 1.0f, 30.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 3U, 2.0f, &fit) == CAL_FIT_OFFSET_RANGE);

    /* ponto do meio 4 mmHg fora da reta (curva): reprova com limite 2 */
    pts[0] = pt(0.0f, 1.0f, 0.0f, 0.0f);
    pts[1] = pt(80.0f, 1.0f, 0.0f, 4.0f);
    pts[2] = pt(160.0f, 1.0f, 0.0f, 0.0f);
    CHECK(Cal_FitPc(pts, 3U, 2.0f, &fit) == CAL_FIT_NONLINEAR);
    CHECK(fit.max_residual_mmhg > 2.0f);
    CHECK(Cal_FitPc(pts, 3U, 5.0f, &fit) == CAL_FIT_OK);           /* limite mais folgado aceita */

    pts[1] = pts[0];
    pts[2] = pts[0];
    CHECK(Cal_FitPc(pts, 3U, 2.0f, &fit) == CAL_FIT_DEGENERATE);
}

static void test_duplicate_points(void)
{
    Cal_Point_t pts[2];
    pts[0] = pt(0.0f, 1.0f, 0.0f, 0.0f);
    pts[1] = pt(100.0f, 1.0f, 0.0f, 0.0f);
    CHECK(Cal_IsDuplicatePoint(pts, 2U, k(103.0f)) == 1U);
    CHECK(Cal_IsDuplicatePoint(pts, 2U, k(106.0f)) == 0U);
}

static void test_verify_outcomes(void)
{
    const Cal_Coeffs_t cur = { 0.0f, 1.0f };
    Cal_Point_t p1;
    Cal_Point_t p2;
    Cal_Verify_t v;

    /* sensor correto: erro ~0 */
    p1 = pt(50.0f, 1.0f, 0.0f, 0.0f);
    p2 = pt(150.0f, 1.0f, 0.0f, 0.0f);
    CHECK(Cal_Verify(&cur, &p1, &p2, 1.0f, 5.0f, &v) == CAL_VERIFY_OK);

    /* deriva de 3 mmHg de offset: corrige (pontos distantes -> reta pelos 2) */
    p1 = pt(50.0f, 1.0f, 3.0f, 0.0f);
    p2 = pt(150.0f, 1.0f, 3.0f, 0.0f);
    CHECK(Cal_Verify(&cur, &p1, &p2, 1.0f, 5.0f, &v) == CAL_VERIFY_CORRECTED);
    CHECK(v.slope_corrected == 1U);
    CHECK(NEAR(v.max_abs_err_mmhg, 3.0f, 0.01f));
    CHECK(NEAR(Cal_ApplyMmHg(&v.new_coeffs, p1.d_kpa), 50.0f, 0.01f));
    CHECK(NEAR(Cal_ApplyMmHg(&v.new_coeffs, p2.d_kpa), 150.0f, 0.01f));

    /* pontos proximos (20 mmHg): so' offset, escala intacta */
    p1 = pt(100.0f, 1.0f, 2.0f, 0.0f);
    p2 = pt(120.0f, 1.0f, 2.0f, 0.0f);
    CHECK(Cal_Verify(&cur, &p1, &p2, 1.0f, 5.0f, &v) == CAL_VERIFY_CORRECTED);
    CHECK(v.slope_corrected == 0U);
    CHECK(v.new_coeffs.scale == 1.0f);
    CHECK(NEAR(Cal_ApplyMmHg(&v.new_coeffs, p1.d_kpa), 100.0f, 0.01f));

    /* erro de 12 mmHg: alerta, sem correcao */
    p1 = pt(50.0f, 1.0f, 12.0f, 0.0f);
    p2 = pt(150.0f, 1.0f, 12.0f, 0.0f);
    CHECK(Cal_Verify(&cur, &p1, &p2, 1.0f, 5.0f, &v) == CAL_VERIFY_ALERT);
    CHECK(NEAR(v.max_abs_err_mmhg, 12.0f, 0.01f));

    /* referencia igual nos 2 pontos com erro aceitavel: so' offset */
    p1 = pt(100.0f, 1.0f, 2.0f, 0.0f);
    p2 = p1;
    CHECK(Cal_Verify(&cur, &p1, &p2, 1.0f, 5.0f, &v) == CAL_VERIFY_CORRECTED);
    CHECK(v.slope_corrected == 0U);
}

static void test_record_roundtrip(void)
{
    Cal_Record_t r;
    Cal_Record_t back;
    uint8_t blob[CAL_RECORD_BLOB_LEN];
    uint32_t seq = 0U;

    Cal_RecordFactory(&r);
    r.coeffs.scale = 1.02f;
    r.coeffs.offset_kpa = k(-1.5f);
    r.year = 2026U;
    r.month = 10U;
    r.day = 22U;
    r.hour = 15U;
    r.source = (uint8_t)CAL_SOURCE_PC;
    r.n_points = 5U;
    r.max_residual_mmhg = 0.7f;
    CHECK(Cal_RecordSerialize(&r, 9U, blob, sizeof(blob)) == CAL_RECORD_BLOB_LEN);
    CHECK(Cal_RecordParse(blob, sizeof(blob), &back, &seq) == 1U);
    CHECK(seq == 9U);
    CHECK((back.coeffs.scale == 1.02f) && (back.n_points == 5U) && (back.day == 22U));

    blob[13] ^= 0x10U; /* corrompe a escala */
    CHECK(Cal_RecordParse(blob, sizeof(blob), &back, NULL) == 0U);
    CHECK(Cal_RecordSerialize(&r, 1U, blob, 10U) == 0U);

    /* integro mas implausivel: nao aplica */
    r.coeffs.scale = 3.0f;
    (void)Cal_RecordSerialize(&r, 1U, blob, sizeof(blob));
    CHECK(Cal_RecordParse(blob, sizeof(blob), &back, NULL) == 0U);
}

int main(void)
{
    test_fit_recovers_line();
    test_fit_rejections();
    test_duplicate_points();
    test_verify_outcomes();
    test_record_roundtrip();
    printf(s_fail ? "calibration: %d FALHA(S)\n" : "calibration: OK\n", s_fail);
    return s_fail != 0;
}
