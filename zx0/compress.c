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

#define MAX_SCALE 10
#define INITIAL_OFFSET 1
#define ZX0_MAX_OFFSET 32640
#define MAX_ALLOCS 50000

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

#define QTY_BLOCKS 10000

#define zx0_allocate(output, a, b, c, d) \
do \
{ \
    zx0_BLOCK *next = d; \
    zx0_BLOCK *ptr; \
    if (ghost_root) { \
        ptr = ghost_root; \
        ghost_root = ptr->ghost_chain; \
        if (ptr->chain && !--ptr->chain->references) { \
            ptr->chain->ghost_chain = ghost_root; \
            ghost_root = ptr->chain; \
        } \
    } else { \
        if (!dead_array_size) { \
            if ((*nr_allocs) == MAX_ALLOCS) { \
                output = NULL; \
                break; \
            } \
            dead_array = malloc(QTY_BLOCKS * sizeof(zx0_BLOCK)); \
            if (dead_array == NULL) { \
                output = NULL; \
                break; \
            } \
            allocated_mem[(*nr_allocs)] = dead_array; \
            (*nr_allocs)++; \
            dead_array_size = QTY_BLOCKS; \
        } \
        ptr = &dead_array[--dead_array_size]; \
    } \
    ptr->bits = a; \
    ptr->index = b; \
    ptr->offset = c; \
    if (next) \
        next->references++; \
    ptr->chain = next; \
    ptr->references = 0; \
    output = ptr; \
} while (0)

static void zx0_assign(zx0_BLOCK **ptr, zx0_BLOCK *chain, zx0_BLOCK **ghost_root) {
    chain->references++;
    if (*ptr && !--(*ptr)->references) {
        (*ptr)->ghost_chain = *ghost_root;
        *ghost_root = *ptr;
    }
    *ptr = chain;
}

static zx0_BLOCK *zx0_optimize(unsigned char *input_data, int input_size, int skip, int offset_limit, void (*progress)(int), void *allocated_mem[MAX_ALLOCS], size_t *nr_allocs)
{
    zx0_BLOCK *last_literal[ZX0_MAX_OFFSET+1];
    zx0_BLOCK *last_match[ZX0_MAX_OFFSET+1];
    int match_length[ZX0_MAX_OFFSET+1];
    int best_length_size;
    int bits;
    int index;
    int offset;
    int length;
    int bits2;
    int dots = 2;
    int max_offset;
    zx0_BLOCK **optimal = NULL;
    int *best_length = NULL;
    zx0_BLOCK *chain;
    zx0_BLOCK *ghost_root;
    zx0_BLOCK *dead_array;
    int dead_array_size;

    memset(last_literal, 0, sizeof last_literal);
    memset(last_match, 0, sizeof last_match);
    memset(match_length, 0, sizeof match_length);

    ghost_root = NULL;
    dead_array = NULL;
    dead_array_size = 0;

    best_length = malloc(input_size * sizeof(int));
    if (best_length == NULL)
    {
        goto fail;
    }
    allocated_mem[(*nr_allocs)] = best_length;
    (*nr_allocs)++;

    optimal = calloc(input_size, sizeof(zx0_BLOCK *));
    if (optimal == NULL)
    {
        goto fail;
    }
    allocated_mem[(*nr_allocs)] = optimal;
    (*nr_allocs)++;

    if (input_size > 2)
    {
        best_length[2] = 2;
    }

    if (progress)
    {
        progress(1);
    }

    /* start with fake block */
    zx0_allocate(chain, -1, skip-1, INITIAL_OFFSET, NULL);
    if (!chain) {
        goto fail;   
    }
    zx0_assign(&last_match[INITIAL_OFFSET], chain, &ghost_root);

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
                    zx0_allocate(chain, bits, index, offset, last_literal[offset]);
                    if (!chain) {
                        goto fail;
                    }
                    zx0_assign(&last_match[offset], chain, &ghost_root);
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
                        zx0_allocate(chain, bits, index, offset, optimal[index-length]);
                        if (!chain) {
                            goto fail;
                        }
                        zx0_assign(&last_match[offset], chain, &ghost_root);
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
                    zx0_allocate(chain, bits, index, 0, last_match[offset]);
                    if (!chain) {
                        goto fail;
                    }
                    zx0_assign(&last_literal[offset], chain, &ghost_root);
                    if (!optimal[index] || optimal[index]->bits > bits)
                        zx0_assign(&optimal[index], last_literal[offset], &ghost_root);
                }
            }
        }

        if (progress && (((index * MAX_SCALE) / input_size) > dots))
        {
            dots++;
            progress(dots);
        }
    }

    if (progress)
    {
        progress(MAX_SCALE);    
    }

    return optimal[input_size-1];

fail:
    return NULL;
}



/* compression */



#define read_bytes(n) \
do { \
    input_index += n; \
    diff += n; \
    if (*delta < diff) \
        *delta = diff; \
} while (0)

#define write_byte(n) \
do { \
    output_data[output_index++] = n; \
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

#define write_interlaced_elias_gamma(v, h) \
do { \
    int v1 = v; \
    int i; \
    for (i = 2; i <= v1; i <<= 1); \
    i >>= 1; \
    while (i >>= 1) { \
        write_bit(backwards_mode); \
        write_bit((h) ? !(v1 & i) : (v1 & i)); \
    } \
    write_bit(!backwards_mode); \
} while (0)

unsigned char *zx0_compress(unsigned char *input_data, int input_size, int skip, int backwards_mode, int invert_mode, int *output_size, int *delta, void (*progress)(int))
{
    void *allocated_mem[MAX_ALLOCS];
    size_t nr_allocs;
    unsigned char *output_data;
    int output_index;
    int input_index;
    int bit_index;
    int bit_mask;
    int diff;
    int backtrack;
    zx0_BLOCK *prev;
    zx0_BLOCK *next;
    int last_offset = INITIAL_OFFSET;
    int length;
    zx0_BLOCK *optimal;

    nr_allocs = 0;

    optimal = zx0_optimize(input_data, input_size, skip, ZX0_MAX_OFFSET, progress, allocated_mem, &nr_allocs);
    if (!optimal) {
        return NULL;
    }

    /* calculate and allocate output buffer */
    *output_size = (optimal->bits+25)/8;
    output_data = calloc(*output_size, sizeof(unsigned char));
    if (!output_data) {
        return NULL;
    }

    bit_index = 0;

    /* un-reverse optimal sequence */
    prev = NULL;
    next = NULL;
    while (optimal) {
        next = optimal->chain;
        optimal->chain = prev;
        prev = optimal;
        optimal = next;
    }

    /* initialize data */
    diff = *output_size-input_size+skip;
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
            write_interlaced_elias_gamma(length, 0);

            /* copy literals values */
            for (int j = 0; j < length; j++) {
                write_byte(input_data[input_index]);
                read_bytes(1);
            }
        } else if (optimal->offset == last_offset) {
            /* copy from last offset indicator */
            write_bit(0);

            /* copy from last offset length */
            write_interlaced_elias_gamma(length, 0);
            read_bytes(length);
        } else {
            /* copy from new offset indicator */
            write_bit(1);

            /* copy from new offset MSB */
            write_interlaced_elias_gamma((optimal->offset-1)/128+1, invert_mode);

            /* copy from new offset LSB */
            if (backwards_mode)
                write_byte(((optimal->offset-1)%128)<<1);
            else
                write_byte((127-(optimal->offset-1)%128)<<1);

            /* copy from new offset length */
            backtrack = 1;
            write_interlaced_elias_gamma(length-1, 0);
            read_bytes(length);

            last_offset = optimal->offset;
        }
    }

    /* end marker */
    write_bit(1);
    write_interlaced_elias_gamma(256, invert_mode);

    for (size_t j = 0; j < nr_allocs; ++j) {
        free(allocated_mem[j]);
        allocated_mem[j] = NULL;
    }

    /* done! */
    return output_data;
}
