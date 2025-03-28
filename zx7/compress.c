/*
 * (c) Copyright 2012-2016 by Einar Saukas. All rights reserved.
 * Copyright 2017-2025 Matt "MateoConLechuga" Waltz (multithread support)
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
#include <string.h>
#include <limits.h>

#define MAX_OFFSET  2176  /* range 1..2176 */
#define MAX_LEN    65536  /* range 2..65536 */

typedef struct zx7_optimal_t {
    int bits;
    int offset;
    int len;
} zx7_Optimal;

static int count_bits(int offset, int len) {
    return (((sizeof(int)*CHAR_BIT+4) - __builtin_clz(len-1)) << 1) + ((128 - offset) >> (sizeof(int)*CHAR_BIT-1) & 4);
}

static zx7_Optimal *zx7_optimize(unsigned char *input_data, int input_size, int skip) {
    int min[MAX_OFFSET+1];
    int max[MAX_OFFSET+1];
    int matches[256*256];
    int *match_slots = NULL;
    zx7_Optimal *optimal = NULL;
    int *match;
    int match_index;
    int offset;
    int len;
    int best_len;
    int bits;
    int i;

    memset(matches, 0, sizeof matches);
    memset(min, 0, sizeof min);
    memset(max, 0, sizeof max);

    match_slots = calloc(input_size, sizeof(int));
    if (match_slots == NULL) {
        return NULL;
    }

    optimal = calloc(input_size, sizeof(zx7_Optimal));
    if (optimal == NULL) {
        goto free_match_slots;
    }

    /* index skipped bytes */
    for (i = 1; i <= skip; i++) {
        match_index = input_data[i-1] << 8 | input_data[i];
        match_slots[i] = matches[match_index];
        matches[match_index] = i;
    }

    /* first byte is always literal */
    optimal[skip].bits = 8;

    /* process remaining bytes */
    for (; i < input_size; i++) {

        optimal[i].bits = optimal[i-1].bits + 9;
        match_index = input_data[i-1] << 8 | input_data[i];
        best_len = 1;
        for (match = &matches[match_index]; *match != 0 && best_len < MAX_LEN; match = &match_slots[*match]) {
            offset = i - *match;
            if (offset > MAX_OFFSET) {
                *match = 0;
                break;
            }

            for (len = 2; len <= MAX_LEN && i >= skip+len; len++) {
                if (len > best_len) {
                    best_len = len;
                    bits = optimal[i-len].bits + count_bits(offset, len);
                    if (optimal[i].bits > bits) {
                        optimal[i].bits = bits;
                        optimal[i].offset = offset;
                        optimal[i].len = len;
                    }
                } else if (max[offset] != 0 && i+1 == max[offset]+len) {
                    len = i-min[offset];
                    if (len > best_len) {
                        len = best_len;
                    }
                }
                if (i < offset+len || input_data[i-len] != input_data[i-len-offset]) {
                    break;
                }
            }
            min[offset] = i+1-len;
            max[offset] = i;
        }
        match_slots[i] = matches[match_index];
        matches[match_index] = i;
    }

free_match_slots:
    free(match_slots);

    return optimal;
}

#define read_bytes(n, delta) \
do { \
   diff += n; \
   if (diff > delta) \
       delta = diff; \
} while (0)

#define write_byte(v) \
do { \
    int v0 = v; \
    output_data[output_index++] = v0; \
    diff--; \
} while (0)

#define write_bit(v) \
do { \
    int v1 = v; \
    if (bit_mask == 0) { \
        bit_mask = 128; \
        bit_index = output_index; \
        write_byte(0); \
    } \
    if (v1 > 0) { \
        output_data[bit_index] |= bit_mask; \
    } \
    bit_mask >>= 1; \
} while (0)

#define write_elias_gamma(v) \
do { \
    int v2 = v; \
    int j; \
    for (j = 2; j <= v2; j <<= 1) { \
        write_bit(0); \
    } \
    while ((j >>= 1) > 0) { \
        write_bit(v2 & j); \
    } \
} while (0)

unsigned char *zx7_compress(unsigned char *input_data, int input_size, int skip, int *output_size, long *delta)
{
    zx7_Optimal *optimal;
    unsigned char *output_data;
    int output_index;
    int input_index;
    int bit_index;
    int bit_mask;
    int offset1;
    int mask;
    int i;
    long diff;

    optimal = zx7_optimize(input_data, input_size, skip);
    if (optimal == NULL)
    {
        return NULL;
    }

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
        int input_prev = input_index - (optimal[input_index].len > 0 ? optimal[input_index].len : 1);
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

    free(optimal);

    return output_data;
}
