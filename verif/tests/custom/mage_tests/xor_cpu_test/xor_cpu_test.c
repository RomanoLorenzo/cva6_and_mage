// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: xor_cpu_test.c
// Description: Functional verification test for Mage SpM CPU Linear Read/Write
//              with transparent XOR conflict-free bank mapping.
//
// Test Phases:
//   Phase 1: CPU Linear Write -> Verify samples stored XOR-like in raw banks.
//   Phase 2: CPU Linear Read  -> Verify samples read linearly from XOR-mapped banks.
//   Phase 3: Raw Bank Write   -> Verify CPU linear read reconstructs linear order.
//   Phase 4: Non-XOR Stream 2 (Zetas, 1 bank) -> Verify unaffected by XOR mode.

// MAGE_CFG_HJSON = ips/Mage-and-Rogue/config/dae/cfg_ntt_1u_3stream.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"
#include "mage_regs.h"

#define DRAM_BASE             ((volatile uint32_t *)(0x80000000UL))
#define TEST_LEN_STREAM       256U
#define TEST_LEN_ZETA         128U

/**
 * @brief Calculate Step Number (parity of bank address bits) for U = 1.
 *
 * @param a Flat sample index (0 to 255).
 * @return uint32_t Parity bit (0 or 1).
 */
static inline uint32_t get_sn_1u(uint32_t a) {
  uint32_t higher = a >> 1;
  uint32_t sn = 0;
  while (higher) {
    sn ^= (higher & 1U);
    higher >>= 1;
  }
  return sn;
}

/**
 * @brief Calculate physical Bank Index for U = 1 (B = 2 banks).
 *
 * @param a Flat sample index.
 * @return uint32_t Bank index (0 or 1).
 */
static inline uint32_t get_bi_1u(uint32_t a) {
  uint32_t b = a & 1U;
  uint32_t sn = get_sn_1u(a);
  return (b ^ sn);
}

/**
 * @brief Calculate Intra-Bank Word Address for U = 1.
 *
 * @param a Flat sample index.
 * @return uint32_t Word offset within designated bank (0 to 127).
 */
static inline uint32_t get_ba_1u(uint32_t a) {
  return (a >> 1);
}

/**
 * @brief Enable CPU XOR-like interleaving mode in Mage.
 */
static inline void enable_cpu_xor_mode(void) {
  // STREAM_XOR_MODE: Stream 0 (Ping) = 1, Stream 1 (Pong) = 1, Stream 2 (Zeta) = 0
  mage_set_stream_xor_mode(0x3U);
  // BLOCK_SIZE: BS = 1 (interleave degree 2), CPU_XOR_MODE = 1 (bit 2) -> (1 << 2) | 1 = 0x5
  mage_set_address_map_blocksize((1U << MAGE_BLOCK_SIZE_CPU_XOR_MODE_BIT) | 1U);
  asm volatile("fence rw, rw" ::: "memory");
}

/**
 * @brief Disable CPU XOR mode (restore legacy raw bank access mode).
 */
static inline void disable_cpu_xor_mode(void) {
  mage_set_address_map_blocksize(0x0U);
  asm volatile("fence rw, rw" ::: "memory");
}

int main(void) {
  uint32_t total_errors = 0;
  uint32_t phase1_errors = 0;
  uint32_t phase2_errors = 0;
  uint32_t phase3_errors = 0;
  uint32_t phase4_errors = 0;

  volatile uint32_t *spm_stream0 = (volatile uint32_t *)MAGE_DATA_BASE;
  volatile uint32_t *spm_stream1 = (volatile uint32_t *)(MAGE_DATA_BASE + 2 * BANK_SIZE_BYTES);
  volatile uint32_t *spm_stream2 = (volatile uint32_t *)(MAGE_DATA_BASE + 4 * BANK_SIZE_BYTES);

  // Clear all 5 banks to poison value 0xDEAD0000 in raw bank mode
  disable_cpu_xor_mode();
  for (uint32_t b = 0; b < 5; b++) {
    volatile uint32_t *bank = MAGE_BANK(b);
    for (uint32_t i = 0; i < TEST_LEN_STREAM; i++) {
      bank[i] = 0xDEAD0000U | (b << 12) | i;
    }
  }
  asm volatile("fence rw, rw" ::: "memory");

  // =========================================================================
  // Phase 1: Point 1 - CPU Linear Write with XOR mode enabled
  // =========================================================================
  enable_cpu_xor_mode();

  // Linear write to Stream 0: a = 0..255, data = 0xA0000000 | a
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    spm_stream0[a] = 0xA0000000U | a;
  }

  // Linear write to Stream 1: a = 0..255, data = 0xB0000000 | a
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    spm_stream1[a] = 0xB0000000U | a;
  }

  // Linear write to Stream 2 (Zetas): a = 0..127, data = 0xC0000000 | a
  for (uint32_t a = 0; a < TEST_LEN_ZETA; a++) {
    spm_stream2[a] = 0xC0000000U | a;
  }
  asm volatile("fence rw, rw" ::: "memory");

  // Switch to RAW bank mode to verify physical storage layout
  disable_cpu_xor_mode();

  // Check Stream 0 (Banks 0 and 1)
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    uint32_t expected = 0xA0000000U | a;
    uint32_t actual = bank[ba];
    if (actual != expected) {
      phase1_errors++;
    }
  }

  // Check Stream 1 (Banks 2 and 3)
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(2 + bi);
    uint32_t expected = 0xB0000000U | a;
    uint32_t actual = bank[ba];
    if (actual != expected) {
      phase1_errors++;
    }
  }

  // =========================================================================
  // Phase 2: Point 2 - CPU Linear Read with XOR mode enabled
  // =========================================================================
  enable_cpu_xor_mode();

  // Linear read from Stream 0
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t expected = 0xA0000000U | a;
    uint32_t actual = spm_stream0[a];
    if (actual != expected) {
      phase2_errors++;
    }
  }

  // Linear read from Stream 1
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t expected = 0xB0000000U | a;
    uint32_t actual = spm_stream1[a];
    if (actual != expected) {
      phase2_errors++;
    }
  }

  // =========================================================================
  // Phase 3: Raw Bank Write -> Linear Read under XOR Mode (Symmetry Check)
  // =========================================================================
  disable_cpu_xor_mode();

  // Populate raw banks according to golden XOR distribution for Stream 0
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = 0x55000000U | (a * 7U);
  }
  asm volatile("fence rw, rw" ::: "memory");

  // Re-enable XOR mode and read back linearly
  enable_cpu_xor_mode();
  for (uint32_t a = 0; a < TEST_LEN_STREAM; a++) {
    uint32_t expected = 0x55000000U | (a * 7U);
    uint32_t actual = spm_stream0[a];
    if (actual != expected) {
      phase3_errors++;
    }
  }

  // =========================================================================
  // Phase 4: Stream 2 (Zetas, 1 bank) non-XOR verification
  // =========================================================================
  // Stream 2 should read back linearly under both XOR mode and RAW mode
  // Under XOR mode:
  for (uint32_t a = 0; a < TEST_LEN_ZETA; a++) {
    uint32_t expected = 0xC0000000U | a;
    uint32_t actual = spm_stream2[a];
    if (actual != expected) {
      phase4_errors++;
    }
  }

  // Under RAW mode:
  disable_cpu_xor_mode();
  volatile uint32_t *bank4 = MAGE_BANK(4);
  for (uint32_t a = 0; a < TEST_LEN_ZETA; a++) {
    uint32_t expected = 0xC0000000U | a;
    uint32_t actual = bank4[a];
    if (actual != expected) {
      phase4_errors++;
    }
  }

  // =========================================================================
  // Reporting
  // =========================================================================
  total_errors = phase1_errors + phase2_errors + phase3_errors + phase4_errors;

  if (total_errors == 0) {
    DRAM_BASE[0] = 0xCAFE0001UL;
    DRAM_BASE[1] = 0x0UL;
  } else {
    DRAM_BASE[0] = 0xDEADBEEFUL;
    DRAM_BASE[1] = total_errors;
  }

  return (int)total_errors;
}

