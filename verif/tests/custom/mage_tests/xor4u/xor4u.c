// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: xor1.c
// Description: Verification test for Radix-2 NTT conflict-free XOR addressing on Mage CGRA
//              Configured with 4 parallel computing units (PEA 4x2), 3 streams [8, 8, 4],
//              and 20 SpM banks. Tests full 8-stage ping-pong execution with N=256 samples.

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
 * ALGORITHM 7 MAPPING FUNCTIONS
 *===========================================================================*/
static inline uint32_t get_sn(uint32_t a) {
  uint32_t higher = a >> 3;
  uint32_t sn = 0;
  while (higher) {
    sn ^= (higher & 1);
    higher >>= 1;
  }
  return sn;
}

static inline uint32_t get_bi(uint32_t a) {
  uint32_t b = a & 7;
  uint32_t sn = get_sn(a);
  return (b ^ (sn << 2));
}

static inline uint32_t get_ba(uint32_t a) {
  return (a >> 3);
}

int main(void) {
  uint32_t inerrors = 0;
  uint32_t outerrors = 0;
  uint32_t vec_in[N_SAMPLES];

  /*==========================================================================
   * STEP 1: Generate input vector
   *========================================================================*/
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    vec_in[a] = 0x10000000U + (a + 1);
  }

  /*==========================================================================
   * STEP 2: Load input vector into Ping SpM (Stream 0, Banks 0..7)
   *========================================================================*/
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi(a);
    uint32_t ba = get_ba(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = vec_in[a];
  }

  asm volatile("fence w, w" ::: "memory");

  /* Verify Ping SpM initialization integrity */
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi(a);
    uint32_t ba = get_ba(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    if (bank[ba] != vec_in[a]) {
      inerrors++;
    }
  }

  asm volatile("fence r, r" ::: "memory");

  /*==========================================================================
   * STEP 3: Configure static Mage Control Registers
   *========================================================================*/

  // --- 3.1 General Configuration & XOR Mode ---
  REG_GEN_CFG = 0x00000000U;         // II = 0, fully static mode
  REG_BLOCK_SIZE = 0x00000000U;      // BS = 0 (word-interleaved)
  REG_STREAM_XOR_MODE = 0x00000003U; // Stream 0 (Ping) & Stream 1 (Pong) XOR mode ENABLED

  // --- 3.2 Stride Modes & Accumulation Selectors ---
  for (int i = 0; i < 20; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U; // Linear addressing for all AGEs
  }
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_ACC_HWLP_SEL_1 = 0x00000000U;
  REG_ACC_HWLP_SEL_2 = 0x00000000U;

  // --- 3.3 PEA Configuration (4x2 Array, Forwarding Mode) ---
  // PE instruction: [12:8] opcode (FW_A = 0x0B), [7:4] input B (0), [3:0] input A
  // Col 0: FW_A of MEM_IN_0 (1) -> 0x0B01
  // Col 1: FW_A of MEM_IN_1 (2) -> 0x0B02
  for (int r = 0; r < 4; r++) {
    REG_CFG_PE(r, 0) = (0x0BU << 8) | (1U << 0);
    REG_CFG_PE(r, 1) = (0x0BU << 8) | (2U << 0);
  }

  // --- 3.4 PEA Output Muxes (OUT_PEA_SEL) ---
  // Outputs 0..3: Col 0 (0), Outputs 4..7: Col 1 (1)
  // Outputs 8..11: Col 0 (0), Outputs 12..15: Col 1 (1)
  REG_OUT_PEA_SEL = 0x55005500U;

  // --- 3.5 Crossbar Mapping (LOAD_AGE_SEL & PEA_DMEM_SEL) ---
  // 3 bits per AGE / Bank, mapped across 2 multiregs:
  REG_LOAD_AGE_SEL_0 = 0x88FAC688U;
  REG_LOAD_AGE_SEL_1 = 0x0688FAC6U;
  REG_PEA_DMEM_SEL_0 = 0x88FAC688U;
  REG_PEA_DMEM_SEL_1 = 0x0688FAC6U;

  // Stream 2 (Zetas) unused for this test
  for (int i = 16; i < 20; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /*==========================================================================
   * STEP 4: Execute 8 Stages of NTT (p = 7 down to 0)
   *========================================================================*/
  for (int p = 7; p >= 0; p--) {
    // Ping-pong buffer selection:
    // Odd stages (7, 5, 3, 1): Stream 0 (Ping) LOAD -> Stream 1 (Pong) STORE
    // Even stages (6, 4, 2, 0): Stream 1 (Pong) LOAD -> Stream 0 (Ping) STORE
    bool is_ping_load = (p % 2 != 0);

    uint32_t s0 = 0;
    uint32_t s1 = 0;
    uint32_t flb_reg = 0;
    uint32_t starts[8];

    int J = 1 << p;
    if (p == 7) {
      flb_reg = 31U;
      s0 = 4U;
      s1 = 0U;
      for (int u = 0; u < 4; u++) {
        starts[u] = (uint32_t)u;
        starts[u + 4] = 128U + (uint32_t)u;
      }
    } else if (p >= 3) {
      uint32_t L0 = (uint32_t)(J / 4);
      uint32_t L1 = (uint32_t)(N_SAMPLES / (2 * J));
      flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);
      s0 = 4U;
      s1 = (uint32_t)(2 * J);
      for (int u = 0; u < 4; u++) {
        starts[u] = (uint32_t)u;
        starts[u + 4] = (uint32_t)(J + u);
      }
    } else if (p == 2) {
      flb_reg = 31U;
      s0 = 8U;
      s1 = 0U;
      for (int u = 0; u < 8; u++) {
        starts[u] = (uint32_t)u;
      }
    } else if (p == 1) {
      flb_reg = 31U;
      s0 = 8U;
      s1 = 0U;
      starts[0] = 0U; starts[1] = 1U; starts[2] = 4U; starts[3] = 5U;
      starts[4] = 2U; starts[5] = 3U; starts[6] = 6U; starts[7] = 7U;
    } else { // p == 0
      flb_reg = 31U;
      s0 = 8U;
      s1 = 0U;
      starts[0] = 0U; starts[1] = 2U; starts[2] = 4U; starts[3] = 6U;
      starts[4] = 1U; starts[5] = 3U; starts[6] = 5U; starts[7] = 7U;
    }

    // Configure HWLP Loop Bounds
    REG_ILB_HWL = 0x00000000U;
    REG_FLB_HWL = flb_reg;
    REG_INC_HWL = 0x01010101U;

    // Configure Strides for Stream 0 and Stream 1 AGEs
    uint32_t stride_val = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
    for (int i = 0; i < 16; i++) {
      REG_STRIDES(i) = stride_val;
    }

    // Configure AGE Instructions (valid=1, n_banks=3 for 8 banks, iv_constraint_sel=4)
    for (int u = 0; u < 8; u++) {
      uint32_t base = starts[u] & 0xFFU;
      uint32_t load_inst  = (1U << 31) | (base << 15) | (1U << 13) | (3U << 7) | (4U << 4) | (0U << 0);
      uint32_t store_inst = (1U << 31) | (base << 15) | (0U << 13) | (3U << 7) | (4U << 4) | (3U << 0);

      if (is_ping_load) {
        REG_CFG_MAGE_AGE(u)     = load_inst;  // Stream 0 (Ping) LOAD
        REG_CFG_MAGE_AGE(8 + u) = store_inst; // Stream 1 (Pong) STORE
      } else {
        REG_CFG_MAGE_AGE(8 + u) = load_inst;  // Stream 1 (Pong) LOAD
        REG_CFG_MAGE_AGE(u)     = store_inst; // Stream 0 (Ping) STORE
      }
    }

    // Dynamic PE configuration:
    // When Ping (Stream 0) is LOAD: Col 0 reads MEM_IN_0 (1), Col 1 reads MEM_IN_1 (2)
    // When Pong (Stream 1) is LOAD: Col 0 reads MEM_IN_2 (3), Col 1 reads MEM_IN_3 (4)
    for (int r = 0; r < 4; r++) {
      if (is_ping_load) {
        REG_CFG_PE(r, 0) = (0x0BU << 8) | (1U << 0);
        REG_CFG_PE(r, 1) = (0x0BU << 8) | (2U << 0);
      } else {
        REG_CFG_PE(r, 0) = (0x0BU << 8) | (3U << 0);
        REG_CFG_PE(r, 1) = (0x0BU << 8) | (4U << 0);
      }
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
   * STEP 5: Verify results in Ping SpM (Stream 0, Banks 0..7)
   *========================================================================*/
  // After an even number of stages (8 stages), data has returned to Ping SpM.
  // With FW_A forwarders, all 256 samples must match their original values!
  for (uint32_t a = 0; a < N_SAMPLES; a++) {
    uint32_t bi = get_bi(a);
    uint32_t ba = get_ba(a);
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

  // Dump first 8 samples and last 8 samples for trace inspection
  for (int i = 0; i < 8; i++) {
    uint32_t bi = get_bi(i);
    uint32_t ba = get_ba(i);
    DRAM_BASE[2 + i] = MAGE_BANK(bi)[ba];
  }
  for (int i = 0; i < 8; i++) {
    uint32_t a = 248 + i;
    uint32_t bi = get_bi(a);
    uint32_t ba = get_ba(a);
    DRAM_BASE[10 + i] = MAGE_BANK(bi)[ba];
  }

  asm volatile("fence w, w" ::: "memory");

  return (inerrors + outerrors);
}
