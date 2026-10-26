// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: gold_ntt.c
// Description: Golden model for CRYSTALS-Dilithium's algorithm NTT.

#include "gold_ntt.h"
#include "encoding.h"

#define DRAM_BASE ((volatile uint32_t *)(0x80000000UL))

uint32_t cycles, instr;
uint32_t start_instret;

int main () {
  clear_csr(mcountinhibit, 1); // Enable counters
  
  int32_t vec_in[N];
  int32_t gold_out[N];

  // 1. Generate deterministic non-trivial polynomial input coefficients
  for (uint32_t i = 0; i < N; i++) {
    vec_in[i] = (int32_t)(i * 17U) - 123;
    gold_out[i] = vec_in[i];
  }

  // 2. Compute reference Golden Model (CRYSTALS-Dilithium official forward NTT)

  write_csr(mcycle, 0);
  start_instret = (uint32_t)read_csr(minstret);

  gold_ntt(gold_out);

  cycles = (uint32_t)read_csr(mcycle);
  instr  = (uint32_t)read_csr(minstret) - start_instret;

  DRAM_BASE[0] = cycles;
  DRAM_BASE[1] = instr;
  
}

/**
 * @brief Montgomery reduction official Dilithium algorithm.
 *        Computes r = a * 2^(-32) mod Q such that -Q < r < Q.
 *
 * @param a 64-bit product operand.
 * @return int32_t Reduced 32-bit result.
 */
int32_t montgomery_reduce(int64_t a) {
  int32_t t;
  t = (int64_t)(int32_t)a * (int64_t)QINV_VAL;
  t = (a - (int64_t)t * (int64_t)Q_VAL) >> 32;
  return t;
}

/**
 * @brief Forward NTT in-place algorithm from official Dilithium repository.
 *
 * @param a Coefficient array of length N = 256.
 */
void gold_ntt(int32_t a[N]) {
  unsigned int len, start, j, k;
  int32_t zeta, t;

  k = 0;
  for (len = 128; len > 0; len >>= 1) {
    for (start = 0; start < N; start = j + len) {
      zeta = zetas[++k];
      for (j = start; j < start + len; ++j) {
        t = montgomery_reduce((int64_t)zeta * a[j + len]);
        a[j + len] = a[j] - t;
        a[j] = a[j] + t;
      }
    }
  }
}