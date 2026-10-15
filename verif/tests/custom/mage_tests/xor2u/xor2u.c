// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: xor1_2.c
// Description: Verification test for Radix-2 NTT conflict-free XOR addressing on Mage CGRA
//              Configured with 2 parallel computing units (U = 2), using 4 SpM banks (B = 4),
//              and testing full 8-stage ping-pong execution with N=256 samples.

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define DRAM_BASE ((volatile uint32_t *)(0x80000000UL)) // DRAM base address

/*=============================================================================
 * BASE ADDRESS AND CONTROL REGISTER DEFINITIONS (MageCtrl @ 0x5000_0000)
 *===========================================================================*/
#define MAGE_CTRL_BASE        0x50000000UL // Control registers base address
#define MAGE_DATA_BASE        0xF0000000UL // SpM base address

#define BANK_SIZE_BYTES       0x1000U      // 1024 32-bit words = 4096 bytes
#define MAGE_BANK(n)          ((volatile uint32_t *)(MAGE_DATA_BASE + (n) * BANK_SIZE_BYTES))

/* Control registers */
#define REG_STATUS            (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x00))
#define REG_GEN_CFG           (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x04))
#define REG_ILB_HWL           (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x08))
#define REG_BLOCK_SIZE        (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x0C))
#define REG_FLB_HWL           (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x10))
#define REG_INC_HWL           (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x14))
#define REG_STREAM_XOR_MODE   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x18))

#define REG_STRIDES(i)        (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x1C + (i) * 4))
#define REG_STRIDE_MODE(i)    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x6C + (i) * 4))

#define REG_ACC_HWLP_SEL_0    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xBC))
#define REG_ACC_HWLP_SEL_1    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xC0))
#define REG_ACC_HWLP_SEL_2    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xC4))

#define REG_CFG_PE(r, c)      (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xC8 + ((r) * 2 + (c)) * 4))

#define REG_OUT_PEA_SEL       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xE8))
#define REG_LOAD_AGE_SEL_0    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xEC))
#define REG_LOAD_AGE_SEL_1    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xF0))
#define REG_PEA_DMEM_SEL_0    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xF4))
#define REG_PEA_DMEM_SEL_1    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xF8))

#define REG_CFG_MAGE_AGE(i)   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0xFC + (i) * 4))

#define N_SAMPLES             256

/*=============================================================================
 * ALGORITHM 7 MAPPING FUNCTIONS FOR U = 2 (B = 4 banks, M = 2)
 *===========================================================================*/
static inline uint32_t get_sn_2u(uint32_t a) {
  uint32_t higher = a >> 2;
  uint32_t sn = 0;
  while (higher) {
    sn ^= (higher & 1);
    higher >>= 1;
  }
  return sn;
}

static inline uint32_t get_bi_2u(uint32_t a) {
  uint32_t b = a & 3U;
  uint32_t sn = get_sn_2u(a);
  return (b ^ (sn << 1));
}

static inline uint32_t get_ba_2u(uint32_t a) {
  return (a >> 2);
}

int main(void) {
  uint32_t inerrors = 0;
  uint32_t outerrors = 0;
  uint32_t vec_in[N_SAMPLES];

  /*==========================================================================
   * STEP 1: Initialize test vector with known unique values
   *========================================================================*/
  for (uint32_t i = 0; i < N_SAMPLES; i++) {
    vec_in[i] = 0x10000000U + (i + 1U);
  }

  /*==========================================================================
   * STEP 2: Pre-load vector into Ping SpM (Stream 0, Banks 0..3)
   *========================================================================*/
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi_2u(a);
    uint32_t ba = get_ba_2u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = vec_in[a];
  }

  asm volatile("fence w, w" ::: "memory");

  /* Verify Ping SpM initialization integrity */
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi_2u(a);
    uint32_t ba = get_ba_2u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    if (bank[ba] != vec_in[a]) {
      inerrors++;
    }
  }

  asm volatile("fence r, r" ::: "memory");

  /*==========================================================================
   * STEP 3: Configure static Mage Control Registers
   *========================================================================*/
  REG_GEN_CFG = 0x00000000U;         // II = 0, fully static mode
  REG_BLOCK_SIZE = 0x00000000U;      // BS = 0 (word-interleaved)
  REG_STREAM_XOR_MODE = 0x00000003U; // Stream 0 (Ping) & Stream 1 (Pong) XOR mode ENABLED

  for (int i = 0; i < 20; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U; // Linear addressing for all AGEs
  }
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_ACC_HWLP_SEL_1 = 0x00000000U;
  REG_ACC_HWLP_SEL_2 = 0x00000000U;

  // PEA Output Muxes (OUT_PEA_SEL):
  // Output 0..1: Col 0 (0), Output 4..5: Col 1 (1)
  // Output 8..9: Col 0 (0), Output 12..13: Col 1 (1)
  REG_OUT_PEA_SEL = 0x55005500U;

  // Crossbar Mapping
  REG_LOAD_AGE_SEL_0 = 0x88FAC688U;
  REG_LOAD_AGE_SEL_1 = 0x0688FAC6U;
  REG_PEA_DMEM_SEL_0 = 0x88FAC688U;
  REG_PEA_DMEM_SEL_1 = 0x0688FAC6U;

  // Inactive AGEs disabled
  for (int i = 0; i < 20; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /*==========================================================================
   * STEP 4: Execute 8 Stages of NTT (p = 7 down to 0) with U = 2
   *========================================================================*/
  for (int p = 7; p >= 0; p--) {
    bool is_ping_load = (p % 2 != 0);

    uint32_t J = 1U << p;
    uint32_t s0 = 0;
    uint32_t s1 = 0;
    uint32_t flb_reg = 0;
    uint32_t b_u0 = 0, b_u1 = 0;
    uint32_t b_l0 = 0, b_l1 = 0;

    if (p == 7) {
      flb_reg = 63U; // L0 = 64 iterations, L1 = 1
      s0 = 2U;
      s1 = 0U;
      b_u0 = 0U; b_u1 = 1U;
      b_l0 = 128U; b_l1 = 129U;
    } else if (p >= 1) {
      uint32_t L0 = J / 2U;
      uint32_t L1 = 128U / J;
      flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);
      s0 = 2U;
      s1 = 2U * J;
      b_u0 = 0U; b_u1 = 1U;
      b_l0 = J; b_l1 = J + 1U;
    } else { // p == 0 (J = 1 < d = 2)
      flb_reg = 63U; // L0 = 64 iterations, L1 = 1
      s0 = 4U;
      s1 = 0U;
      b_u0 = 0U; b_u1 = 2U;
      b_l0 = 1U; b_l1 = 3U;
    }

    // Configure HWLP Loop Bounds (64 total iterations per stage)
    REG_ILB_HWL = 0x00000000U;
    REG_FLB_HWL = flb_reg;
    REG_INC_HWL = 0x01010101U;

    // Configure Strides for active AGEs
    uint32_t stride_val = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
    for (int i = 0; i < 16; i++) {
      REG_STRIDES(i) = stride_val;
    }

    // Configure AGE Instructions:
    // valid=1, n_banks=2 (4 banks, bits [8:7]=2), iv_constraint_sel=4 (unconstrained)
    // hwlp_rf_sel = 0 for LOAD, hwlp_rf_sel = 3 for STORE
    uint32_t load_u0  = (1U << 31) | (b_u0 << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);
    uint32_t load_u1  = (1U << 31) | (b_u1 << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);
    uint32_t load_l0  = (1U << 31) | (b_l0 << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);
    uint32_t load_l1  = (1U << 31) | (b_l1 << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);

    uint32_t store_u0 = (1U << 31) | (b_u0 << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (3U << 0);
    uint32_t store_u1 = (1U << 31) | (b_u1 << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (3U << 0);
    uint32_t store_l0 = (1U << 31) | (b_l0 << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (3U << 0);
    uint32_t store_l1 = (1U << 31) | (b_l1 << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (3U << 0);

    // Disable all AGEs first
    for (int i = 0; i < 16; i++) {
      REG_CFG_MAGE_AGE(i) = 0x00000000U;
    }

    if (is_ping_load) {
      REG_CFG_MAGE_AGE(0)  = load_u0;   // Stream 0 (Ping) LOAD Row 0 upper
      REG_CFG_MAGE_AGE(1)  = load_u1;   // Stream 0 (Ping) LOAD Row 1 upper
      REG_CFG_MAGE_AGE(4)  = load_l0;   // Stream 0 (Ping) LOAD Row 0 lower
      REG_CFG_MAGE_AGE(5)  = load_l1;   // Stream 0 (Ping) LOAD Row 1 lower

      REG_CFG_MAGE_AGE(8)  = store_u0;  // Stream 1 (Pong) STORE Row 0 upper
      REG_CFG_MAGE_AGE(9)  = store_u1;  // Stream 1 (Pong) STORE Row 1 upper
      REG_CFG_MAGE_AGE(12) = store_l0;  // Stream 1 (Pong) STORE Row 0 lower
      REG_CFG_MAGE_AGE(13) = store_l1;  // Stream 1 (Pong) STORE Row 1 lower
    } else {
      REG_CFG_MAGE_AGE(8)  = load_u0;   // Stream 1 (Pong) LOAD Row 0 upper
      REG_CFG_MAGE_AGE(9)  = load_u1;   // Stream 1 (Pong) LOAD Row 1 upper
      REG_CFG_MAGE_AGE(12) = load_l0;   // Stream 1 (Pong) LOAD Row 0 lower
      REG_CFG_MAGE_AGE(13) = load_l1;   // Stream 1 (Pong) LOAD Row 1 lower

      REG_CFG_MAGE_AGE(0)  = store_u0;  // Stream 0 (Ping) STORE Row 0 upper
      REG_CFG_MAGE_AGE(1)  = store_u1;  // Stream 0 (Ping) STORE Row 1 upper
      REG_CFG_MAGE_AGE(4)  = store_l0;  // Stream 0 (Ping) STORE Row 0 lower
      REG_CFG_MAGE_AGE(5)  = store_l1;  // Stream 0 (Ping) STORE Row 1 lower
    }

    // Dynamic PE configuration (Rows 0 and 1 active, Rows 2 and 3 NOP):
    for (int r = 0; r < 2; r++) {
      if (is_ping_load) {
        REG_CFG_PE(r, 0) = (0x0BU << 8) | (1U << 0); // FW_A of MEM_IN_0 (AGE 0/1)
        REG_CFG_PE(r, 1) = (0x0BU << 8) | (2U << 0); // FW_A of MEM_IN_1 (AGE 4/5)
      } else {
        REG_CFG_PE(r, 0) = (0x0BU << 8) | (3U << 0); // FW_A of MEM_IN_2 (AGE 8/9)
        REG_CFG_PE(r, 1) = (0x0BU << 8) | (4U << 0); // FW_A of MEM_IN_3 (AGE 12/13)
      }
    }
    for (int r = 2; r < 4; r++) {
      REG_CFG_PE(r, 0) = 0x00000000U;
      REG_CFG_PE(r, 1) = 0x00000000U;
    }

    asm volatile("fence w, w" ::: "memory");

    // Launch execution for stage p
    REG_STATUS = (1U << 0);

    // Wait until DONE deasserts upon entering EXEC
    while ((REG_STATUS >> 1) & 0x1U) {
      // wait
    }

    // Wait until stage execution completes (DONE bit asserts)
    while (!((REG_STATUS >> 1) & 0x1U)) {
      // poll
    }

    asm volatile("fence r, r" ::: "memory");
  }

  /*==========================================================================
   * STEP 5: Verify results in Ping SpM (Stream 0, Banks 0..3)
   *========================================================================*/
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi_2u(a);
    uint32_t ba = get_ba_2u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    uint32_t val = bank[ba];
    if (val != vec_in[a]) {
      outerrors++;
    }
  }

  asm volatile("fence r, r" ::: "memory");

  /*==========================================================================
   * STEP 6: Dump summary results to DRAM for testbench observation
   *========================================================================*/
  DRAM_BASE[0] = inerrors;
  DRAM_BASE[1] = outerrors;

  for (int i = 0; i < 8; i++) {
    uint32_t bi = get_bi_2u(i);
    uint32_t ba = get_ba_2u(i);
    DRAM_BASE[2 + i] = MAGE_BANK(bi)[ba];
  }
  for (int i = 0; i < 8; i++) {
    uint32_t a = 248 + i;
    uint32_t bi = get_bi_2u(a);
    uint32_t ba = get_ba_2u(a);
    DRAM_BASE[10 + i] = MAGE_BANK(bi)[ba];
  }

  asm volatile("fence w, w" ::: "memory");

  return (inerrors + outerrors);
}

