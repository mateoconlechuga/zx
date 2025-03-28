/*
 * (c) Copyright 2021 by Einar Saukas. All rights reserved.
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

#include "zx0.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

#define MAX_SCALE 10
#define QTY_BLOCKS 10000
#define MAX_ALLOCS 4096

#define ZX0_MAX_OFFSET 32640
#define INITIAL_OFFSET 1

typedef struct zx0_block_t {
    struct zx0_block_t *chain;
    struct zx0_block_t *ghost_chain;
    int bits;
    int index;
    int offset;
    int references;
} zx0_BLOCK;

static int offset_ceiling(int index, int offset_limit) {
    return index > offset_limit ? offset_limit : index < INITIAL_OFFSET ? INITIAL_OFFSET : index;
}

static int elias_gamma_bits(int value) {
    int bits = 1;
    while (value >>= 1)
        bits += 2;
    return bits;
}

static zx0_BLOCK *zx0_allocate(int bits,
                               int index,
                               int offset,
                               zx0_BLOCK *chain,
                               zx0_BLOCK **ghost_root,
                               zx0_BLOCK **dead_array,
                               int *dead_array_size,
                               int *nr_allocs,
                               void *allocs[MAX_ALLOCS]) {
    zx0_BLOCK *ptr;

    if (*ghost_root) {
        ptr = *ghost_root;
        *ghost_root = ptr->ghost_chain;
        if (ptr->chain && !--ptr->chain->references) {
            ptr->chain->ghost_chain = *ghost_root;
            *ghost_root = ptr->chain;
        }
    } else {
        int size = *dead_array_size;
        if ((size % QTY_BLOCKS) == 0)
        {
            *dead_array = calloc(QTY_BLOCKS, sizeof(zx0_BLOCK));
            if (*dead_array == NULL) {
                return NULL;
            }
            allocs[*nr_allocs] = *dead_array;
            *nr_allocs = *nr_allocs + 1;
        }
        ptr = &(*dead_array)[size % QTY_BLOCKS];
        *dead_array_size = size + 1;
    }
    ptr->bits = bits;
    ptr->index = index;
    ptr->offset = offset;
    if (chain)
        chain->references++;
    ptr->chain = chain;
    ptr->references = 0;
    return ptr;
}

static void zx0_assign(zx0_BLOCK **ptr, zx0_BLOCK *chain, zx0_BLOCK **ghost_root) {
    chain->references++;
    if (*ptr && !--(*ptr)->references) {
        (*ptr)->ghost_chain = *ghost_root;
        *ghost_root = *ptr;
    }
    *ptr = chain;
}

static bool zx0_optimize(zx0_BLOCK **opt,
                         unsigned char *input_data,
                         int input_size,
                         int skip,
                         int offset_limit,
                         void (*progress)(int),
                         int *nr_allocs,
                         void *allocs[MAX_ALLOCS])
{
    zx0_BLOCK **optimal;
    zx0_BLOCK *last_literal[ZX0_MAX_OFFSET+1];
    zx0_BLOCK *last_match[ZX0_MAX_OFFSET+1];
    zx0_BLOCK *dead_array;
    zx0_BLOCK *ghost_root;
    int match_length[ZX0_MAX_OFFSET+1];
    int best_length_size;
    int dead_array_size;
    int bits;
    int index;
    int offset;
    int length;
    int bits2;
    int max_offset;
    int *best_length;
    int dots = 2;
    zx0_BLOCK *ptr;

    memset(last_literal, 0, sizeof last_literal);
    memset(last_match, 0, sizeof last_match);
    memset(match_length, 0, sizeof match_length);

    ghost_root = NULL;
    dead_array = NULL;
    dead_array_size = 0;

    best_length = calloc(input_size, sizeof(int));
    if (best_length == NULL)
    {
        return false;
    }

    optimal = calloc(input_size, sizeof(zx0_BLOCK *));
    if (optimal == NULL)
    {
        return false;
    }

    if (input_size > 2)
    {
        best_length[2] = 2;
    }

    if (progress)
    {
        progress(1);
    }

    /* start with fake block */
    ptr = zx0_allocate(-1, skip-1, INITIAL_OFFSET, NULL, &ghost_root, &dead_array, &dead_array_size, nr_allocs, allocs);
    zx0_assign(&last_match[INITIAL_OFFSET], ptr, &ghost_root);

    if (progress)
    {
        progress(2);
    }

    /* process remaining bytes */
    for (index = skip; index < input_size; index++) {
        best_length_size = 2;
        max_offset = offset_ceiling(index, offset_limit);
        for (offset = 1; offset <= max_offset; offset++) {
            if (index != skip && index >= offset && input_data[index] == input_data[index-offset]) {
                /* copy from last offset */
                if (last_literal[offset]) {
                    length = index-last_literal[offset]->index;
                    bits = last_literal[offset]->bits + 1 + elias_gamma_bits(length);
                    ptr = zx0_allocate(bits, index, offset, last_literal[offset], &ghost_root, &dead_array, &dead_array_size, nr_allocs, allocs);
                    zx0_assign(&last_match[offset], ptr, &ghost_root);
                    if (!optimal[index] || optimal[index]->bits > bits)
                        zx0_assign(&optimal[index], last_match[offset], &ghost_root);
                }
                /* copy from new offset */
                if (++match_length[offset] > 1) {
                    if (best_length_size < match_length[offset]) {
                        bits = optimal[index-best_length[best_length_size]]->bits + elias_gamma_bits(best_length[best_length_size]-1);
                        do {
                            best_length_size++;
                            bits2 = optimal[index-best_length_size]->bits + elias_gamma_bits(best_length_size-1);
                            if (bits2 <= bits) {
                                best_length[best_length_size] = best_length_size;
                                bits = bits2;
                            } else {
                                best_length[best_length_size] = best_length[best_length_size-1];
                            }
                        } while(best_length_size < match_length[offset]);
                    }
                    length = best_length[match_length[offset]];
                    bits = optimal[index-length]->bits + 8 + elias_gamma_bits((offset-1)/128+1) + elias_gamma_bits(length-1);
                    if (!last_match[offset] || last_match[offset]->index != index || last_match[offset]->bits > bits) {
                        ptr = zx0_allocate(bits, index, offset, optimal[index-length], &ghost_root, &dead_array, &dead_array_size, nr_allocs, allocs);
                        zx0_assign(&last_match[offset], ptr, &ghost_root);
                        if (!optimal[index] || optimal[index]->bits > bits)
                            zx0_assign(&optimal[index], last_match[offset], &ghost_root);
                    }
                }
            } else {
                /* copy literals */
                match_length[offset] = 0;
                if (last_match[offset]) {
                    length = index-last_match[offset]->index;
                    bits = last_match[offset]->bits + 1 + elias_gamma_bits(length) + length*8;
                    ptr = zx0_allocate(bits, index, 0, last_match[offset], &ghost_root, &dead_array, &dead_array_size, nr_allocs, allocs);
                    zx0_assign(&last_literal[offset], ptr, &ghost_root);
                    if (!optimal[index] || optimal[index]->bits > bits)
                        zx0_assign(&optimal[index], last_literal[offset], &ghost_root);
                }
            }
        }

         /* indicate progress */
        if (progress)
        {
            if (((index * MAX_SCALE) / input_size) > dots)
            {
                dots++;
                progress(dots);
            }
        }
    }

    if (progress)
    {
        progress(MAX_SCALE);
    }

    *opt = optimal[input_size-1];

    free(optimal);
    free(best_length);

    return true;
}

#define read_bytes(n, delta) \
do { \
    input_index += n; \
    diff += n; \
    if (delta < diff) \
        delta = diff; \
} while (0)

#define write_byte(v) \
do { \
    output_data[output_index++] = (v); \
    diff--; \
} while (0)

#define write_bit(v) \
do { \
    int v0 = v; \
    if (backtrack) { \
        if (v0 && output_index) \
            output_data[output_index-1] |= 1; \
        backtrack = 0; \
    } else { \
        if (!bit_mask) { \
            bit_mask = 128; \
            bit_index = output_index; \
            write_byte(0); \
        } \
        if (v0) \
            output_data[bit_index] |= bit_mask; \
        bit_mask >>= 1; \
    } \
} while (0)

#define write_interlaced_elias_gamma(v, backwards_mode, invert_mode) \
do { \
    int v1 = v; \
    int i = 2; \
    for (; i <= v1; i <<= 1) \
        ; \
    i >>= 1; \
    while (i >>= 1) { \
        int nb = v1 & i; \
        write_bit(backwards_mode); \
        write_bit(invert_mode ? !nb : nb); \
    } \
    write_bit(0); \
} while (0)

unsigned char *zx0_compress(unsigned char *input_data,
                            int input_size,
                            int skip,
                            int backwards_mode,
                            int invert_mode,
                            int *output_size,
                            int *delta,
                            void (*progress)(int))
{
    void *allocs[MAX_ALLOCS];
    int nr_allocs = 0;
    unsigned char *output_data;
    zx0_BLOCK *prev;
    zx0_BLOCK *next;
    int output_index;
    int input_index;
    int bit_index;
    int bit_mask;
    int diff;
    int backtrack;
    int last_offset;
    int length;

    zx0_BLOCK *optimal;
    if (!zx0_optimize(&optimal, input_data, input_size, skip, ZX0_MAX_OFFSET, progress, &nr_allocs, allocs)) {
        return NULL;
    }
    last_offset = INITIAL_OFFSET;

    /* calculate and allocate output buffer */
    *output_size = (optimal->bits+25)/8;
    output_data = calloc(*output_size, sizeof(unsigned char));
    if (!output_data) {
        return NULL;
    }

    /* un-reverse optimal sequence */
    prev = NULL;
    while (optimal) {
        next = optimal->chain;
        optimal->chain = prev;
        prev = optimal;
        optimal = next;
    }

    diff = *output_size - input_size + skip;
    *delta = 0;
    input_index = skip;
    output_index = 0;
    bit_mask = 0;
    backtrack = 1;

    /* generate output */
    for (optimal = prev->chain; optimal; prev=optimal, optimal = optimal->chain) {
        length = optimal->index-prev->index;

        if (!optimal->offset) {
            /* copy literals indicator */
            write_bit(0);

            /* copy literals length */
            write_interlaced_elias_gamma(length, backwards_mode, 0);

            /* copy literals values */
            for (int j = 0; j < length; j++) {
                write_byte(input_data[input_index]);
                read_bytes(1, *delta);
            }
        } else if (optimal->offset == last_offset) {
            /* copy from last offset indicator */
            write_bit(0);

            /* copy from last offset length */
            write_interlaced_elias_gamma(length, backwards_mode, 0);
            read_bytes(length, *delta);
        } else {
            /* copy from new offset indicator */
            write_bit(1);

            /* copy from new offset MSB */
            write_interlaced_elias_gamma((optimal->offset-1)/128+1, backwards_mode, invert_mode);

            /* copy from new offset LSB */
            if (backwards_mode)
                write_byte(((optimal->offset-1)%128)<<1);
            else
                write_byte((127-(optimal->offset-1)%128)<<1);

            /* copy from new offset length */
            backtrack = 1;
            write_interlaced_elias_gamma(length-1, backwards_mode, 0);
            read_bytes(length, *delta);

            last_offset = optimal->offset;
        }
    }

    /* end marker */
    write_bit(1);
    write_interlaced_elias_gamma(256, backwards_mode, invert_mode);

    for (int i = 0; i < nr_allocs; ++i)
    {
        free(allocs[i]);
    }

    /* done! */
    return output_data;
}
