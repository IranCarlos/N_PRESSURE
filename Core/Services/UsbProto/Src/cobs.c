/*
 * Name: Iran Sousa
 * Company: Dynaminds Electronics
 * Contact: dynamindelectronics@gmail.com
 * Created: 2026-10-14
 */
#include "cobs.h"

Cobs_Status_t Cobs_Encode(const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t read_idx = 0U;
    size_t write_idx = 1U; /* out[0] reservado pro 1o byte de codigo */
    size_t code_idx = 0U;
    uint8_t code = 1U;

    if (out_cap < 1U) {
        return COBS_ERR_OUTPUT_OVERFLOW;
    }

    while (read_idx < in_len) {
        if (in[read_idx] == 0x00U) {
            out[code_idx] = code;
            code = 1U;
            if (write_idx >= out_cap) {
                return COBS_ERR_OUTPUT_OVERFLOW;
            }
            code_idx = write_idx;
            write_idx++;
            read_idx++;
        } else {
            if (write_idx >= out_cap) {
                return COBS_ERR_OUTPUT_OVERFLOW;
            }
            out[write_idx] = in[read_idx];
            write_idx++;
            read_idx++;
            code++;
            if (code == 0xFFU) {
                out[code_idx] = code;
                code = 1U;
                if (write_idx >= out_cap) {
                    return COBS_ERR_OUTPUT_OVERFLOW;
                }
                code_idx = write_idx;
                write_idx++;
            }
        }
    }
    out[code_idx] = code;
    *out_len = write_idx;
    return COBS_OK;
}

Cobs_Status_t Cobs_Decode(const uint8_t *in, size_t in_len,
                           uint8_t *out, size_t out_cap, size_t *out_len)
{
    size_t read_idx = 0U;
    size_t write_idx = 0U;

    while (read_idx < in_len) {
        uint8_t code = in[read_idx];
        size_t i;

        if (code == 0x00U) {
            /* 0x00 so' e' valido como delimitador de frame (tratado por fora
             * deste modulo); dentro do payload codificado e' malformado. */
            return COBS_ERR_MALFORMED;
        }
        if ((read_idx + (size_t)code > in_len) && (code != 1U)) {
            /* code aponta alem do buffer de entrada. */
            return COBS_ERR_MALFORMED;
        }
        read_idx++;
        for (i = 1U; i < code; i++) {
            if (write_idx >= out_cap) {
                return COBS_ERR_OUTPUT_OVERFLOW;
            }
            out[write_idx] = in[read_idx];
            write_idx++;
            read_idx++;
        }
        if ((code != 0xFFU) && (read_idx != in_len)) {
            if (write_idx >= out_cap) {
                return COBS_ERR_OUTPUT_OVERFLOW;
            }
            out[write_idx] = 0x00U;
            write_idx++;
        }
    }
    *out_len = write_idx;
    return COBS_OK;
}
