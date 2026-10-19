// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: pe_mulh.c
// Description: Functional verification test for signed 32-bit multiplication
//              (MUL and MULH) in Mage Processing Element Array (PEA).
//              PE(0,0) computes the low 32 bits (MUL: A * B mod 2^32).
//              PE(0,1) computes the high 32 bits (MULH: (int64_t)A * B >> 32).
//              Outputs are validated against an equivalent CPU golden model.

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"

#define DRAM_BASE       ((volatile uint32_t *)(0x80000000UL))
#define N_PAIRS         128U
#define TOTAL_SAMPLES   (2U * N_PAIRS) // 256 samples total in SpM

/* Function prototypes */
void generateInputPairs(int32_t *vec_a, int32_t *vec_b, uint32_t num_pairs);
void computeGoldenModel(const int32_t *vec_a, const int32_t *vec_b,
                        int32_t *gold_mul, int32_t *gold_mulh, uint32_t num_pairs);
uint32_t loadOperandsIntoSpM(const int32_t *vec_a, const int32_t *vec_b, uint32_t num_pairs);
uint32_t verifyMageOutputs(const int32_t *gold_mul, const int32_t *gold_mulh, uint32_t num_pairs);

void configureMageCommon(void);
void configureMageMulhStage(void);

static inline uint32_t get_sn_1u(uint32_t a);
static inline uint32_t get_bi_1u(uint32_t a);
static inline uint32_t get_ba_1u(uint32_t a);

int main(void) {
  int32_t vec_a[N_PAIRS];
  int32_t vec_b[N_PAIRS];
  int32_t gold_mul[N_PAIRS];
  int32_t gold_mulh[N_PAIRS];

  // 1. Generate diverse signed 32-bit test vectors
  generateInputPairs(vec_a, vec_b, N_PAIRS);

  // 2. Compute 64-bit golden model for MUL and MULH
  computeGoldenModel(vec_a, vec_b, gold_mul, gold_mulh, N_PAIRS);

  // 3. Load operands into Mage SpM Ping banks
  uint32_t load_errors = loadOperandsIntoSpM(vec_a, vec_b, N_PAIRS);
  if (load_errors != 0) {
    DRAM_BASE[0] = 0xDEAD0001U;
    DRAM_BASE[1] = load_errors;
    return (int)load_errors;
  }

  // 4. Configure Mage common control registers
  configureMageCommon();

  // 5. Configure Mage AGEs and PEs for MUL and MULH execution
  configureMageMulhStage();

  // 6. Start Mage execution and poll until completion
  mage_start();
  mage_wait_done();

  // 7. Verify outputs from SpM Pong banks against Golden Model
  uint32_t mismatch_count = verifyMageOutputs(gold_mul, gold_mulh, N_PAIRS);

  // 8. Report test outcome
  if (mismatch_count == 0) {
    DRAM_BASE[0] = 0xCAFE0001U; // Success marker
    DRAM_BASE[1] = 0x00000000U; // Zero mismatches
    return 0;
  } else {
    DRAM_BASE[0] = 0xDEADBEEFU; // Failure marker
    DRAM_BASE[1] = mismatch_count;
    return (int)mismatch_count;
  }

  return 0;
}

/**
 * @brief Generate diverse test operand pairs including PQC twiddles and signed corner cases
 * 
 * @param vec_a Pointer to destination buffer for operand A
 * @param vec_b Pointer to destination buffer for operand B
 * @param num_pairs Number of operand pairs to generate (128)
 */
void generateInputPairs(int32_t *vec_a, int32_t *vec_b, uint32_t num_pairs) {
  // Test Dilithium twiddle factors table (subset)
  const int32_t test_zetas[8] = {
    25847, -2608894, -518909, 237124, -777960, -876248, 466468, 1826347
  };

  for (uint32_t i = 0; i < num_pairs; i++) {
    if (i < 16) {
      // Group 1: Positive numbers with 64-bit non-zero upper word
      vec_a[i] = (int32_t)(100000 + i * 15000);
      vec_b[i] = (int32_t)(100000 + i * 20000);
    } else if (i < 32) {
      // Group 2: Negative x Positive (negative 64-bit product)
      vec_a[i] = -(int32_t)(50000 + i * 5000);
      vec_b[i] = (int32_t)(120000 + i * 10000);
    } else if (i < 48) {
      // Group 3: Negative x Negative (positive 64-bit product)
      vec_a[i] = -(int32_t)(80000 + i * 3000);
      vec_b[i] = -(int32_t)(90000 + i * 4000);
    } else if (i < 80) {
      // Group 4: Dilithium coefficients and twiddle factors
      vec_a[i] = (int32_t)(5000000 - i * 12345);
      vec_b[i] = test_zetas[i % 8];
    } else if (i < 96) {
      // Group 5: Corner cases (0, 1, -1, power-of-two, MAX_INT)
      switch (i - 80) {
        case 0:  vec_a[i] = 0;           vec_b[i] = 12345678;    break;
        case 1:  vec_a[i] = 12345678;    vec_b[i] = 0;           break;
        case 2:  vec_a[i] = 1;           vec_b[i] = -98765432;   break;
        case 3:  vec_a[i] = -1;          vec_b[i] = -1;          break;
        case 4:  vec_a[i] = -1;          vec_b[i] = 0x7FFFFFFF;  break;
        case 5:  vec_a[i] = 0x7FFFFFFF;  vec_b[i] = 2;           break;
        case 6:  vec_a[i] = 0x40000000;  vec_b[i] = 4;           break;
        case 7:  vec_a[i] = (int32_t)0x80000000; vec_b[i] = 1;  break;
        default: vec_a[i] = (int32_t)(i * 1000000); vec_b[i] = (int32_t)(-i * 500000); break;
      }
    } else {
      // Group 6: Reproducible pseudo-random signed 32-bit values
      uint32_t seed_a = 0x12345678U + i * 0x9E3779B9U;
      uint32_t seed_b = 0x87654321U + i * 0x6B8718E3U;
      vec_a[i] = (int32_t)seed_a;
      vec_b[i] = (int32_t)seed_b;
    }
  }
}

/**
 * @brief Compute expected 32-bit lower product and upper signed product using 64-bit arithmetic
 * 
 * @param vec_a Pointer to operand A array
 * @param vec_b Pointer to operand B array
 * @param gold_mul Output array for low 32 bits (MUL)
 * @param gold_mulh Output array for high 32 bits (MULH)
 * @param num_pairs Number of operand pairs
 */
void computeGoldenModel(const int32_t *vec_a, const int32_t *vec_b,
                        int32_t *gold_mul, int32_t *gold_mulh, uint32_t num_pairs) {
  for (uint32_t i = 0; i < num_pairs; i++) {
    int64_t full_prod = (int64_t)vec_a[i] * (int64_t)vec_b[i];
    gold_mul[i]  = (int32_t)(full_prod & 0xFFFFFFFFLL);
    gold_mulh[i] = (int32_t)((full_prod >> 32) & 0xFFFFFFFFLL);
  }
}

/**
 * @brief Calculate Step Number for 1 computing unit (XOR address mapping)
 * 
 * @param a Flat sample index (0..255)
 * @retval sn Step number
 */
static inline uint32_t get_sn_1u(uint32_t a) {
  return ((a >> 7) & 1U);
}

/**
 * @brief Calculate Bank Index for 1 computing unit (XOR address mapping)
 * 
 * @param a Flat sample index (0..255)
 * @retval bi Physical SpM bank index (0 or 1 for Ping)
 */
static inline uint32_t get_bi_1u(uint32_t a) {
  uint32_t sn = get_sn_1u(a);
  return ((a ^ sn) & 1U);
}

/**
 * @brief Calculate Word Bank Address for 1 computing unit
 * 
 * @param a Flat sample index (0..255)
 * @retval ba Word address within designated bank
 */
static inline uint32_t get_ba_1u(uint32_t a) {
  return (a >> 1);
}

/**
 * @brief Load operand vectors into SpM Ping banks and verify integrity
 * 
 * @param vec_a Operands A loaded to indices [0..127]
 * @param vec_b Operands B loaded to indices [128..255]
 * @param num_pairs Number of pairs (128)
 * @retval errs Number of write/readback mismatches
 */
uint32_t loadOperandsIntoSpM(const int32_t *vec_a, const int32_t *vec_b, uint32_t num_pairs) {
  uint32_t errs = 0;

  // Operands A are mapped to flat sample indices [0..127]
  for (uint32_t i = 0; i < num_pairs; i++) {
    uint32_t flat_idx = i;
    uint32_t bi = get_bi_1u(flat_idx);
    uint32_t ba = get_ba_1u(flat_idx);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = (uint32_t)vec_a[i];
  }

  // Operands B are mapped to flat sample indices [128..255]
  for (uint32_t i = 0; i < num_pairs; i++) {
    uint32_t flat_idx = i + 128U;
    uint32_t bi = get_bi_1u(flat_idx);
    uint32_t ba = get_ba_1u(flat_idx);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = (uint32_t)vec_b[i];
  }

  asm volatile("fence w, w" ::: "memory");

  // Verify write integrity
  for (uint32_t i = 0; i < num_pairs; i++) {
    uint32_t flat_idx_a = i;
    uint32_t bi_a = get_bi_1u(flat_idx_a);
    uint32_t ba_a = get_ba_1u(flat_idx_a);
    if (MAGE_BANK(bi_a)[ba_a] != (uint32_t)vec_a[i]) {
      errs++;
    }

    uint32_t flat_idx_b = i + 128U;
    uint32_t bi_b = get_bi_1u(flat_idx_b);
    uint32_t ba_b = get_ba_1u(flat_idx_b);
    if (MAGE_BANK(bi_b)[ba_b] != (uint32_t)vec_b[i]) {
      errs++;
    }
  }

  asm volatile("fence r, r" ::: "memory");
  return errs;
}

/**
 * @brief Configure common Mage registers for static DAE execution
 */
void configureMageCommon(void) {
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

  /**
   * REG_OUT_PEA_SEL = 0x55005500
   *   Route Row 0, col 0 (PE 0,0) to pea_data_o[0] (port 0 of store crossbar)
   *   Route Row 0, col 1 (PE 0,1) to pea_data_o[4] (port 1 of store crossbar)
   */
  REG_OUT_PEA_SEL = 0x55005500U;

  /**
   * REG_LOAD_AGE_SEL / REG_PEA_DMEM_SEL
   *   Connect AGE 0 to MEM_IN_0, AGE 4 to MEM_IN_1
   *   Connect pea_data_o[0] to AGE 8 (Store upper), pea_data_o[4] to AGE 12 (Store lower)
   */
  REG_LOAD_AGE_SEL_0 = 0x88FAC688U;
  REG_LOAD_AGE_SEL_1 = 0x0688FAC6U;
  REG_PEA_DMEM_SEL_0 = 0x88FAC688U;
  REG_PEA_DMEM_SEL_1 = 0x0688FAC6U;

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure hardware loops, AGE instructions, and PE opcodes for MUL and MULH
 */
void configureMageMulhStage(void) {
  // 1. Hardware Loop 0 bound = 127 (128 iterations)
  REG_ILB_HWL = 0x00000000U;
  REG_FLB_HWL = 127U;

  // 2. Loop 0 stride = 1 element
  for (int i = 0; i < 16; i++) {
    REG_STRIDES(i) = 0x00000001U;
  }

  // 3. Clear AGE configuration registers
  for (int i = 0; i < 16; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  // 4. Configure active AGE instructions
  // AGE 0:  LOAD from Stream 0 (Ping), base = 0 (operands A), unconstrained (4), hwlp_rf_sel = 0
  // AGE 4:  LOAD from Stream 0 (Ping), base = 128 (operands B), unconstrained (4), hwlp_rf_sel = 0
  // AGE 8:  STORE to Stream 1 (Pong), base = 0 (MUL output), unconstrained (4), hwlp_rf_sel = 3
  // AGE 12: STORE to Stream 1 (Pong), base = 128 (MULH output), unconstrained (4), hwlp_rf_sel = 3
  uint32_t load_upper  = (1U << 31) | (0U << 15)   | (1U << 13) | (1U << 7) | (4U << 4) | (0U << 0);
  uint32_t load_lower  = (1U << 31) | (128U << 15) | (1U << 13) | (1U << 7) | (4U << 4) | (0U << 0);
  uint32_t store_upper = (1U << 31) | (0U << 15)   | (0U << 13) | (1U << 7) | (4U << 4) | (3U << 0);
  uint32_t store_lower = (1U << 31) | (128U << 15) | (0U << 13) | (1U << 7) | (4U << 4) | (3U << 0);

  REG_CFG_MAGE_AGE(0)  = load_upper;
  REG_CFG_MAGE_AGE(4)  = load_lower;
  REG_CFG_MAGE_AGE(8)  = store_upper;
  REG_CFG_MAGE_AGE(12) = store_lower;

  /**
   * 5. Dynamic PE Configuration
   * 
   * PE(0, 0): MUL
   *   REG_CFG_PE(0, 0) = 0x00000121
   *     MUX_A[3:0]   = 0x1 (MEM_IN_0: Operand A from AGE 0)
   *     MUX_B[7:4]   = 0x2 (MEM_IN_1: Operand B from AGE 4)
   *     OPCODE[12:8] = 0x01 (MUL: Low 32 bits of signed product)
   * 
   * PE(0, 1): MULH
   *   REG_CFG_PE(0, 1) = 0x00000D21
   *     MUX_A[3:0]   = 0x1 (MEM_IN_0: Operand A from AGE 0)
   *     MUX_B[7:4]   = 0x2 (MEM_IN_1: Operand B from AGE 4)
   *     OPCODE[12:8] = 0x0D (MULH: High 32 bits of signed product)
   */
  REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG(MAGE_PE_OP_MUL,  MAGE_PE_MUX_MEM_IN_0, MAGE_PE_MUX_MEM_IN_1);
  REG_CFG_PE(0, 1) = MAGE_BUILD_PE_CFG(MAGE_PE_OP_MULH, MAGE_PE_MUX_MEM_IN_0, MAGE_PE_MUX_MEM_IN_1);

  // Disable remaining PE rows (Rows 1..3)
  for (int r = 1; r < 4; r++) {
    REG_CFG_PE(r, 0) = 0x00000000U;
    REG_CFG_PE(r, 1) = 0x00000000U;
  }

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Verify SpM Pong bank outputs against Golden Model for MUL and MULH
 * 
 * @param gold_mul Expected low 32 bits array
 * @param gold_mulh Expected high 32 bits array
 * @param num_pairs Number of pairs (128)
 * @retval mismatches Number of mismatches detected
 */
uint32_t verifyMageOutputs(const int32_t *gold_mul, const int32_t *gold_mulh, uint32_t num_pairs) {
  uint32_t mismatches = 0;

  asm volatile("fence r, r" ::: "memory");

  // MUL results are written to Stream 1 (Pong Bank 8 / 9) at flat indices [0..127]
  for (uint32_t i = 0; i < num_pairs; i++) {
    uint32_t flat_idx = i;
    uint32_t bi = 8U + get_bi_1u(flat_idx); // Pong banks: Bank 8 or Bank 9
    uint32_t ba = get_ba_1u(flat_idx);
    volatile uint32_t *bank = MAGE_BANK(bi);
    int32_t actual_mul = (int32_t)bank[ba];

    if (actual_mul != gold_mul[i]) {
      mismatches++;
      if (mismatches <= 8) {
        // Log first few errors to DRAM for easy debugging
        DRAM_BASE[2 + (mismatches - 1) * 3 + 0] = (uint32_t)i;
        DRAM_BASE[2 + (mismatches - 1) * 3 + 1] = (uint32_t)actual_mul;
        DRAM_BASE[2 + (mismatches - 1) * 3 + 2] = (uint32_t)gold_mul[i];
      }
    }
  }

  // MULH results are written to Stream 1 (Pong Bank 8 / 9) at flat indices [128..255]
  for (uint32_t i = 0; i < num_pairs; i++) {
    uint32_t flat_idx = i + 128U;
    uint32_t bi = 8U + get_bi_1u(flat_idx); // Pong banks: Bank 8 or Bank 9
    uint32_t ba = get_ba_1u(flat_idx);
    volatile uint32_t *bank = MAGE_BANK(bi);
    int32_t actual_mulh = (int32_t)bank[ba];

    if (actual_mulh != gold_mulh[i]) {
      mismatches++;
      if (mismatches <= 8) {
        DRAM_BASE[30 + (mismatches - 1) * 3 + 0] = (uint32_t)i;
        DRAM_BASE[30 + (mismatches - 1) * 3 + 1] = (uint32_t)actual_mulh;
        DRAM_BASE[30 + (mismatches - 1) * 3 + 2] = (uint32_t)gold_mulh[i];
      }
    }
  }

  return mismatches;
}

