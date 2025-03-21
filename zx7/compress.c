/*
 * (c) Copyright 2012-2016 by Einar Saukas. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * The name of its author may not be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "zx7.h"

#include <stdio.h>
#include <stdlib.h>

#define read_bytes(n, delta) \
do { \
   diff += n; \
   if (diff > delta) \
       delta = diff; \
} while (0)

#define write_byte(v) \
do { \
    int value = v; \
    output_data[output_index++] = value; \
    diff--; \
} while (0)

#define write_bit(v) \
do { \
    int value = v; \
    if (bit_mask == 0) { \
        bit_mask = 128; \
        bit_index = output_index; \
        write_byte(0); \
    } \
    if (value > 0) { \
        output_data[bit_index] |= bit_mask; \
    } \
    bit_mask >>= 1; \
} while (0)

#define write_elias_gamma(v) \
do { \
    int value = v; \
    int j; \
    for (j = 2; j <= value; j <<= 1) { \
        write_bit(0); \
    } \
    while ((j >>= 1) > 0) { \
        write_bit(value & j); \
    } \
} while (0)

unsigned char *zx7_compress(zx7_Optimal *optimal, unsigned char *input_data, size_t input_size, unsigned long skip, size_t *output_size, long *delta)
{
    unsigned char *output_data;
    size_t output_index;
    size_t input_index;
    size_t bit_index;
    int bit_mask;
    int offset1;
    int mask;
    int i;
    long diff;

    /* calculate and allocate output buffer */
    input_index = input_size-1;
    *output_size = (optimal[input_index].bits+18+7)/8;
    output_data = calloc(*output_size, sizeof(unsigned char));
    if (!output_data) {
         return NULL;
    }

    /* initialize delta */
    diff = *output_size - input_size + skip;
    *delta = 0;

    /* un-reverse optimal sequence */
    optimal[input_index].bits = 0;
    while (input_index != skip) {
        size_t input_prev = input_index - (optimal[input_index].len > 0 ? optimal[input_index].len : 1);
        optimal[input_prev].bits = input_index;
        input_index = input_prev;
    }

    output_index = 0;
    bit_mask = 0;

    /* first byte is always literal */
    write_byte(input_data[input_index]);
    read_bytes(1, *delta);

    /* process remaining bytes */
    while ((input_index = optimal[input_index].bits) > 0) {
        if (optimal[input_index].len == 0) {

            /* literal indicator */
            write_bit(0);

            /* literal value */
            write_byte(input_data[input_index]);
            read_bytes(1, *delta);

        } else {

            /* sequence indicator */
            write_bit(1);

            /* sequence length */
            write_elias_gamma(optimal[input_index].len-1);

            /* sequence offset */
            offset1 = optimal[input_index].offset-1;
            if (offset1 < 128) {
                write_byte(offset1);
            } else {
                offset1 -= 128;
                write_byte((offset1 & 127) | 128);
                for (mask = 1024; mask > 127; mask >>= 1) {
                    write_bit(offset1 & mask);
                }
            }
            read_bytes(optimal[input_index].len, *delta);
        }
    }

    /* sequence indicator */
    write_bit(1);

    /* end marker > MAX_LEN */
    for (i = 0; i < 16; i++) {
        write_bit(0);
    }
    write_bit(1);

    return output_data;
}
