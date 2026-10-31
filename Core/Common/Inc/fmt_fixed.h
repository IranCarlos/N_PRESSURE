/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-31
 *
 * Numero com casas decimais fixas, sem %f: o firmware linka nano.specs sem
 * _printf_float e %f sai vazio, sem erro nenhum (achado 2026-10-31 no log de
 * auditoria). Ligar _printf_float traria malloc em tarefas concorrentes.
 */
#ifndef FMT_FIXED_H
#define FMT_FIXED_H

#include <stdint.h>
#include <stdio.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FMT_FIXED_LEN     (16u)
#define FMT_FIXED_MAX_DEC (6u)

/* decimals <= FMT_FIXED_MAX_DEC; perde casas se o numero nao couber em 32 bits.
 * trim != 0 tira zeros a direita ("1000.0000" -> "1000"). */
static inline void Fmt_Fixed(char *out, size_t out_size, float value, uint8_t decimals, uint8_t trim)
{
    double mag = (value < 0.0f) ? -(double)value : (double)value;
    uint32_t scale = 1u;
    uint8_t dec = (decimals > FMT_FIXED_MAX_DEC) ? (uint8_t)FMT_FIXED_MAX_DEC : decimals;
    uint8_t i;
    uint32_t scaled;
    size_t len;

    for (i = 0u; i < dec; i++) {
        scale *= 10u;
    }
    while ((dec > 0u) && ((mag * (double)scale) >= 4.0e9)) {
        scale /= 10u;
        dec--;
    }
    if ((mag * (double)scale) >= 4.0e9) {
        (void)snprintf(out, out_size, "%sinf", (value < 0.0f) ? "-" : "");
        return;
    }
    scaled = (uint32_t)((mag * (double)scale) + 0.5);
    if (dec == 0u) {
        (void)snprintf(out, out_size, "%s%lu", ((value < 0.0f) && (scaled != 0u)) ? "-" : "", (unsigned long)scaled);
        return;
    }
    (void)snprintf(out, out_size, "%s%lu.%0*lu", ((value < 0.0f) && (scaled != 0u)) ? "-" : "",
                   (unsigned long)(scaled / scale), (int)dec, (unsigned long)(scaled % scale));
    if (trim != 0u) {
        for (len = 0u; out[len] != '\0'; len++) {
        }
        while ((len > 0u) && (out[len - 1u] == '0')) {
            len--;
        }
        if ((len > 0u) && (out[len - 1u] == '.')) {
            len--;
        }
        out[len] = '\0';
    }
}

#ifdef __cplusplus
}
#endif

#endif /* FMT_FIXED_H */
