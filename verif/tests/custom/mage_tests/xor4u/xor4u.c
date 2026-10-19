// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: xor4u.c
// Description: Verification test for Radix-2 NTT conflict-free XOR addressing on Mage CGRA
//              Configured with 4 parallel computing units (PEA Rows 0..3, U = 4), 8 SpM banks per stream,
//              and testing full 8-stage ping-pong execution with N=256 samples
//              using the official Mage software APIs.
//              See: https://doi.org/10.46586/tches.v2022.i1.94-126 (Algorithm 7)
//              Test for configuration: cfg_ntt_4u_3stream.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"

#define DRAM_BASE  ((volatile uint32_t *)(0x80000000UL))
#define N_SAMPLES  256

/* Function prototypes */
uint32_t *fillInVect(uint32_t *invect, uint32_t len);
uint32_t *goldenModel(uint32_t *invect, uint32_t *outvect, uint32_t len);
uint32_t loadSamplesIntoSpM(uint32_t *invect, uint32_t len);
uint32_t readSamplesFromSpM(uint32_t *outvect, uint32_t len);
uint32_t compareVectors(uint32_t *vect1, uint32_t *vect2, uint32_t len);

void configureStageIndependentRegs(void);
void configureStage4u(int p, bool is_ping_load);

static inline uint32_t get_sn_4u(uint32_t a);
static inline uint32_t get_bi_4u(uint32_t a);
static inline uint32_t get_ba_4u(uint32_t a);

int main(void) {

  uint32_t invect[N_SAMPLES];
  fillInVect(invect, N_SAMPLES);

  uint32_t outvect_gm[N_SAMPLES]; // Output vector from golden model
  goldenModel(invect, outvect_gm, N_SAMPLES);

  uint32_t loaderrs = loadSamplesIntoSpM(invect, N_SAMPLES);
  if (loaderrs == 0) {

    // Stage-independent registers configuration
    configureStageIndependentRegs();

    // Configuration for Stage 0 (p = 7, J = 128, Ping -> Pong)
    /**
     * REG_FLB_HWL = 0x0000001F
     *   FLB_0[7:0]   = 0x1F; Loop 0 bound (32 iterations per stage)
     *   FLB_1[15:8]  = 0x00; Loop 1 bound (1 iteration)
     *   FLB_2[23:16] = 0x00; Unused
     *   FLB_3[31:24] = 0x00; Unused
     * REG_STRIDES = 0x00000004
     *   S0[7:0]      = 0x04; Stride for loop 0 (step = 4 elements across 4 units)
     *   S1[15:8]     = 0x00; Stride for loop 1 (single outer block)
     * AGE Instructions:
     *   Row k: LOAD upper base = k, lower base = 128 + k (delay = 0)
     *   Row k: STORE upper base = k, lower base = 128 + k (delay = 3)
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B01: FW_A, MUX_A = MEM_IN_0 (AGE r)
     *   REG_CFG_PE(r, 1) = 0x00000B02: FW_A, MUX_A = MEM_IN_1 (AGE 4+r)
     */
    configureStage4u(7, true);
    // End of configuration for Stage 0

    mage_start();
    mage_wait_done();

    // Configuration for Stage 1 (p = 6, J = 64, Pong -> Ping)
    /**
     * REG_FLB_HWL = 0x0000010F
     *   FLB_0[7:0]   = 0x0F; Loop 0 bound (16 iterations)
     *   FLB_1[15:8]  = 0x01; Loop 1 bound (2 iterations)
     * REG_STRIDES = 0x00008004
     *   S0[7:0]      = 0x04; Stride for loop 0
     *   S1[15:8]     = 0x80; Stride for loop 1 (2 * J = 128)
     * AGE Instructions:
     *   Row k: upper base = k, lower base = 64 + k
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B03: FW_A, MUX_A = MEM_IN_2 (AGE 8+r)
     *   REG_CFG_PE(r, 1) = 0x00000B04: FW_A, MUX_A = MEM_IN_3 (AGE 12+r)
     */
    configureStage4u(6, false);
    // End of configuration for Stage 1

    mage_start();
    mage_wait_done();

    // Configuration for Stage 2 (p = 5, J = 32, Ping -> Pong)
    /**
     * REG_FLB_HWL = 0x00000307
     *   FLB_0[7:0]   = 0x07; Loop 0 bound (8 iterations)
     *   FLB_1[15:8]  = 0x03; Loop 1 bound (4 iterations)
     * REG_STRIDES = 0x00004004
     *   S0[7:0]      = 0x04; Stride for loop 0
     *   S1[15:8]     = 0x40; Stride for loop 1 (2 * J = 64)
     * AGE Instructions:
     *   Row k: upper base = k, lower base = 32 + k
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B01: FW_A, MUX_A = MEM_IN_0
     *   REG_CFG_PE(r, 1) = 0x00000B02: FW_A, MUX_A = MEM_IN_1
     */
    configureStage4u(5, true);
    // End of configuration for Stage 2

    mage_start();
    mage_wait_done();

    // Configuration for Stage 3 (p = 4, J = 16, Pong -> Ping)
    /**
     * REG_FLB_HWL = 0x00000703
     *   FLB_0[7:0]   = 0x03; Loop 0 bound (4 iterations)
     *   FLB_1[15:8]  = 0x07; Loop 1 bound (8 iterations)
     * REG_STRIDES = 0x00002004
     *   S0[7:0]      = 0x04; Stride for loop 0
     *   S1[15:8]     = 0x20; Stride for loop 1 (2 * J = 32)
     * AGE Instructions:
     *   Row k: upper base = k, lower base = 16 + k
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B03: FW_A, MUX_A = MEM_IN_2
     *   REG_CFG_PE(r, 1) = 0x00000B04: FW_A, MUX_A = MEM_IN_3
     */
    configureStage4u(4, false);
    // End of configuration for Stage 3

    mage_start();
    mage_wait_done();

    // Configuration for Stage 4 (p = 3, J = 8, Ping -> Pong)
    /**
     * REG_FLB_HWL = 0x00000F01
     *   FLB_0[7:0]   = 0x01; Loop 0 bound (2 iterations)
     *   FLB_1[15:8]  = 0x0F; Loop 1 bound (16 iterations)
     * REG_STRIDES = 0x00001004
     *   S0[7:0]      = 0x04; Stride for loop 0
     *   S1[15:8]     = 0x10; Stride for loop 1 (2 * J = 16)
     * AGE Instructions:
     *   Row k: upper base = k, lower base = 8 + k
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B01: FW_A, MUX_A = MEM_IN_0
     *   REG_CFG_PE(r, 1) = 0x00000B02: FW_A, MUX_A = MEM_IN_1
     */
    configureStage4u(3, true);
    // End of configuration for Stage 4

    mage_start();
    mage_wait_done();

    // Configuration for Stage 5 (p = 2, J = 4, Pong -> Ping)
    /**
     * REG_FLB_HWL = 0x00001F00
     *   FLB_0[7:0]   = 0x00; Loop 0 bound (1 iteration)
     *   FLB_1[15:8]  = 0x1F; Loop 1 bound (32 iterations)
     * REG_STRIDES = 0x00000804
     *   S0[7:0]      = 0x04; Stride for loop 0
     *   S1[15:8]     = 0x08; Stride for loop 1 (2 * J = 8)
     * AGE Instructions:
     *   Row k: upper base = k, lower base = 4 + k
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B03: FW_A, MUX_A = MEM_IN_2
     *   REG_CFG_PE(r, 1) = 0x00000B04: FW_A, MUX_A = MEM_IN_3
     */
    configureStage4u(2, false);
    // End of configuration for Stage 5

    mage_start();
    mage_wait_done();

    // Configuration for Stage 6 (p = 1, J = 2 < d = 4, Ping -> Pong)
    /**
     * REG_FLB_HWL = 0x0000001F
     *   FLB_0[7:0]   = 0x1F; Loop 0 bound (32 iterations, exception branch)
     *   FLB_1[15:8]  = 0x00; Loop 1 bound (1 iteration)
     * REG_STRIDES = 0x00000008
     *   S0[7:0]      = 0x08; Stride for loop 0 (step = 8 across 2-element butterfly pairs)
     *   S1[15:8]     = 0x00; Stride for loop 1
     * AGE Instructions:
     *   Row 0: upper base = 0, lower base = 2
     *   Row 1: upper base = 1, lower base = 3
     *   Row 2: upper base = 4, lower base = 6
     *   Row 3: upper base = 5, lower base = 7
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B01: FW_A, MUX_A = MEM_IN_0
     *   REG_CFG_PE(r, 1) = 0x00000B02: FW_A, MUX_A = MEM_IN_1
     */
    configureStage4u(1, true);
    // End of configuration for Stage 6

    mage_start();
    mage_wait_done();

    // Configuration for Stage 7 (p = 0, J = 1 < d = 4, Pong -> Ping)
    /**
     * REG_FLB_HWL = 0x0000001F
     *   FLB_0[7:0]   = 0x1F; Loop 0 bound (32 iterations, exception branch)
     *   FLB_1[15:8]  = 0x00; Loop 1 bound (1 iteration)
     * REG_STRIDES = 0x00000008
     *   S0[7:0]      = 0x08; Stride for loop 0 (step = 8 across 1-element butterfly pairs)
     *   S1[15:8]     = 0x00; Stride for loop 1
     * AGE Instructions:
     *   Row 0: upper base = 0, lower base = 1
     *   Row 1: upper base = 2, lower base = 3
     *   Row 2: upper base = 4, lower base = 5
     *   Row 3: upper base = 6, lower base = 7
     * PE Forwarding:
     *   REG_CFG_PE(r, 0) = 0x00000B03: FW_A, MUX_A = MEM_IN_2
     *   REG_CFG_PE(r, 1) = 0x00000B04: FW_A, MUX_A = MEM_IN_3
     */
    configureStage4u(0, false);
    // End of configuration for Stage 7

    mage_start();
    mage_wait_done();

    // Read from SpM
    uint32_t outvect_hw[N_SAMPLES];
    readSamplesFromSpM(outvect_hw, N_SAMPLES);

    // Check errors
    uint32_t cmperrs = compareVectors(outvect_hw, outvect_gm, N_SAMPLES);

    // Print number of errors into DRAM[1]
    DRAM_BASE[0] = loaderrs;
    DRAM_BASE[1] = cmperrs;

  } else {
    DRAM_BASE[0] = loaderrs;
    DRAM_BASE[1] = 0xFFFFFFFFU;
  }
  
  return 0;
}

/**
 * @brief Configure stage-independent static control registers of Mage
 */
void configureStageIndependentRegs(void) {
  /**
   * REG_GEN_CFG = 0x00000000
   *   II[3:0]                  = 0x0; Initiation Interval - 1 = 0 (1 cycle per loop step)
   *   S_N_T_AGU[4]             = 0; Static AGU configuration
   *   S_N_T_PEA[5]             = 0; Static PEA configuration
   *   S_N_T_PEA_SEL_OUT_REGS[6]= 0; Static output selectors
   *   S_N_T_LOAD_STREAM[7]     = 0; Static load stream crossbar
   *   S_N_T_STREAM_STREAM[8]   = 0; Static store stream crossbar
   *   ACC_VEC_MODE[12:9]       = 0; 32-bit accumulation mode
   */
  mage_set_general_cfg_bits(0x00000000U);

  /**
   * REG_BLOCK_SIZE = 0x00000000
   *   BS[1:0] = 0; Block size = 1 word (4 bytes interleaved)
   */
  mage_set_address_map_blocksize(0x00000000U);

  /**
   * REG_STREAM_XOR_MODE = 0x00000003
   *   STREAM_XOR_MODE[0] = 1; Enable XOR addressing on Stream 0 (Ping)
   *   STREAM_XOR_MODE[1] = 1; Enable XOR addressing on Stream 1 (Pong)
   *   STREAM_XOR_MODE[2] = 0; Disable XOR addressing on Stream 2 (Zetas)
   */
  mage_set_stream_xor_mode(0x00000003U);

  /**
   * REG_INC_HWL = 0x01010101
   *   INC_0[7:0]   = 1; Increment for loop 0
   *   INC_1[15:8]  = 1; Increment for loop 1
   *   INC_2[23:16] = 1; Increment for loop 2
   *   INC_3[31:24] = 1; Increment for loop 3
   */
  REG_INC_HWL = 0x01010101U;

  /**
   * REG_ILB_HWL = 0x00000000
   *   ILB_0..3 = 0; Start all loop indices from 0
   */
  REG_ILB_HWL = 0x00000000U;

  // Set linear addressing mode for all 20 AGEs
  for (int i = 0; i < 20; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U;
  }

  // Clear accumulator loop selector registers
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_ACC_HWLP_SEL_1 = 0x00000000U;
  REG_ACC_HWLP_SEL_2 = 0x00000000U;

  // Program PEA output multiplexers (OUT_PEA_SEL)
  REG_OUT_PEA_SEL = 0x55005500U;

  // Configure crossbar stream routing for Load and Store (4 Computing Units, 8 streams per group)
  REG_LOAD_AGE_SEL_0 = 0x88FAC688U;
  REG_LOAD_AGE_SEL_1 = 0x0688FAC6U;
  REG_PEA_DMEM_SEL_0 = 0x88FAC688U;
  REG_PEA_DMEM_SEL_1 = 0x0688FAC6U;

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure hardware loops, strides, AGEs and PEA for a specific NTT stage (U = 4)
 * 
 * @param p Current stage index (p = 7 down to 0)
 * @param is_ping_load true if loading from Ping (Stream 0) and storing to Pong (Stream 1)
 */
void configureStage4u(int p, bool is_ping_load) {
  uint32_t J = 1U << p;
  uint32_t s0 = 0;
  uint32_t s1 = 0;
  uint32_t flb_reg = 0;
  uint32_t base_upper[4];
  uint32_t base_lower[4];

  if (p >= 2) {
    uint32_t L0 = J / 4U;
    uint32_t L1 = 128U / J;
    flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);
    s0 = 4U;
    s1 = (p == 7) ? 0U : (2U * J);

    for (int k = 0; k < 4; k++) {
      base_upper[k] = (uint32_t)k;
      base_lower[k] = J + (uint32_t)k;
    }
  } else if (p == 1) {
    // Exception branch of Algorithm 6: J = 2 < d = 4
    flb_reg = 31U; // 32 total iterations
    s0 = 8U;
    s1 = 0U;

    base_upper[0] = 0U; base_lower[0] = 2U;
    base_upper[1] = 1U; base_lower[1] = 3U;
    base_upper[2] = 4U; base_lower[2] = 6U;
    base_upper[3] = 5U; base_lower[3] = 7U;
  } else {
    // Exception branch of Algorithm 6: J = 1 < d = 4 (stage p = 0)
    flb_reg = 31U; // 32 total iterations
    s0 = 8U;
    s1 = 0U;

    base_upper[0] = 0U; base_lower[0] = 1U;
    base_upper[1] = 2U; base_lower[1] = 3U;
    base_upper[2] = 4U; base_lower[2] = 5U;
    base_upper[3] = 6U; base_lower[3] = 7U;
  }

  // 1. Program Hardware Loop bounds
  REG_ILB_HWL = 0x00000000U;
  REG_FLB_HWL = flb_reg;

  // 2. Program Strides for active AGEs
  uint32_t stride_val = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
  for (int i = 0; i < 16; i++) {
    REG_STRIDES(i) = stride_val;
  }

  // 3. Clear all AGE configuration registers
  for (int i = 0; i < 16; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  // 4. Configure active AGE instructions (4 read pairs + 4 write pairs)
  // valid=1 [31], base_addr [30:15], is_load [13], n_banks=3 (8 banks) [8:7],
  // iv_constraint_sel=4 (unconstrained) [6:4], hwlp_rf_sel [3:0] (0 for LOAD, 3 for STORE)
  for (int k = 0; k < 4; k++) {
    uint32_t load_u  = (1U << 31) | (base_upper[k] << 15) | (1U << 13) | (3U << 7) | (4U << 4) | (0U << 0);
    uint32_t load_l  = (1U << 31) | (base_lower[k] << 15) | (1U << 13) | (3U << 7) | (4U << 4) | (0U << 0);
    uint32_t store_u = (1U << 31) | (base_upper[k] << 15) | (0U << 13) | (3U << 7) | (4U << 4) | (3U << 0);
    uint32_t store_l = (1U << 31) | (base_lower[k] << 15) | (0U << 13) | (3U << 7) | (4U << 4) | (3U << 0);

    if (is_ping_load) {
      REG_CFG_MAGE_AGE(k)      = load_u;   // Stream 0 (Ping) LOAD upper
      REG_CFG_MAGE_AGE(4 + k)  = load_l;   // Stream 0 (Ping) LOAD lower
      REG_CFG_MAGE_AGE(8 + k)  = store_u;  // Stream 1 (Pong) STORE upper
      REG_CFG_MAGE_AGE(12 + k) = store_l;  // Stream 1 (Pong) STORE lower
    } else {
      REG_CFG_MAGE_AGE(8 + k)  = load_u;   // Stream 1 (Pong) LOAD upper
      REG_CFG_MAGE_AGE(12 + k) = load_l;   // Stream 1 (Pong) LOAD lower
      REG_CFG_MAGE_AGE(k)      = store_u;  // Stream 0 (Ping) STORE upper
      REG_CFG_MAGE_AGE(4 + k)  = store_l;  // Stream 0 (Ping) STORE lower
    }
  }

  // 5. Dynamic PE configuration (All 4 Rows active)
  for (int r = 0; r < 4; r++) {
    if (is_ping_load) {
      REG_CFG_PE(r, 0) = (0x0BU << 8) | (1U << 0); // FW_A of MEM_IN_0 (AGE r)
      REG_CFG_PE(r, 1) = (0x0BU << 8) | (2U << 0); // FW_A of MEM_IN_1 (AGE 4+r)
    } else {
      REG_CFG_PE(r, 0) = (0x0BU << 8) | (3U << 0); // FW_A of MEM_IN_2 (AGE 8+r)
      REG_CFG_PE(r, 1) = (0x0BU << 8) | (4U << 0); // FW_A of MEM_IN_3 (AGE 12+r)
    }
  }

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Calculate Step Number (parity of bank address bits) for U = 4
 * 
 * @param a Flat sample index
 * @retval sn Parity bit (0 or 1)
 */
static inline uint32_t get_sn_4u(uint32_t a) {
  uint32_t higher = a >> 3;
  uint32_t sn = 0;
  while (higher) {
    sn ^= (higher & 1U);
    higher >>= 1;
  }
  return sn;
}

/**
 * @brief Calculate physical Bank Index for U = 4 (B = 8 banks)
 * 
 * @param a Flat sample index
 * @retval bi Physical bank index (0..7)
 */
static inline uint32_t get_bi_4u(uint32_t a) {
  uint32_t b = a & 7U;
  uint32_t sn = get_sn_4u(a);
  return (b ^ (sn << 2));
}

/**
 * @brief Calculate Intra-Bank Word Address for U = 4
 * 
 * @param a Flat sample index
 * @retval ba Word address within the designated bank
 */
static inline uint32_t get_ba_4u(uint32_t a) {
  return (a >> 3);
}

/**
 * @brief Load samples into SpM banks according to XOR pattern and verify readback
 * 
 * @param invect Pointer to ordered input vector
 * @param len Length of input vector
 * @retval errs Number of readback errors
 */
uint32_t loadSamplesIntoSpM(uint32_t *invect, uint32_t len) {
  uint32_t errs = 0;

  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_4u(a);
    uint32_t ba = get_ba_4u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = invect[a];
  }

  asm volatile("fence w, w" ::: "memory");

  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_4u(a);
    uint32_t ba = get_ba_4u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    if (bank[ba] != invect[a]) {
      errs++;
    }
  }

  asm volatile("fence r, r" ::: "memory");

  return errs;
}

/**
 * @brief Read back processed samples from SpM banks according to XOR pattern
 * 
 * @param outvect Pointer to destination output vector
 * @param len Length of output vector
 * @retval len
 */
uint32_t readSamplesFromSpM(uint32_t *outvect, uint32_t len) {
  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_4u(a);
    uint32_t ba = get_ba_4u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    outvect[a] = bank[ba];
  }

  asm volatile("fence r, r" ::: "memory");

  return len;
}

/**
 * @brief Fill input vector with dummy values (0x10000001, ...)
 * 
 * @param invect Pointer to input vector
 * @param len Length of input vector
 * @retval invect
 */
uint32_t *fillInVect(uint32_t *invect, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    invect[i] = 0x10000000U + (i + 1U);
  }
  return invect;
}

/**
 * @brief Compute expected output pattern of the last stage (Golden Model)
 * 
 * @param invect Pointer to input vector
 * @param outvect Pointer to output vector
 * @param len Length of input and output vectors
 * @retval outvect
 */
uint32_t *goldenModel(uint32_t *invect, uint32_t *outvect, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    outvect[i] = invect[i];
  }
  return outvect;
}

/**
 * @brief Check vectors equality element-by-element
 * 
 * @param vect1 Pointer to vector 1
 * @param vect2 Pointer to vector 2
 * @param len Length of input vectors
 * @retval errs Number of different elements
 */
uint32_t compareVectors(uint32_t *vect1, uint32_t *vect2, uint32_t len) {
  uint32_t errs = 0;
  for (uint32_t i = 0; i < len; i++) {
    if (vect1[i] != vect2[i]) {
      errs++;
    }
  }
  return errs;
}
