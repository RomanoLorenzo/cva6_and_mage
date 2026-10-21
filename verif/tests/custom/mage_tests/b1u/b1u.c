// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: b1u.c
// Description: Complete functional verification test for Radix-2 NTT Butterfly
//              with Montgomery modular reduction on Mage CGRA (1 Computing Unit, 2x4 PEA).
//              Step 6: Full Radix-2 NTT Butterfly computation for 8 butterfly iterations.
//                      - PE(0,0): Ingests a(j) from Ping Bank 0 into delay pipeline.
//                      - PE(1,0): Computes MUL0 = a(j+k) * zeta mod 2^32, forwards a(j) East.
//                      - PE(1,1): Computes MULH0 = (a(j+k) *H zeta), forwards a(j) East.
//                      - PE(0,1): Computes MUL1 = MUL0 * QINV mod 2^32, ingests MULH0 into delay.
//                      - PE(0,2): Computes MULH1 = MUL1 *H Q, forwards MULH0 South.
//                      - PE(1,2): Computes SUB0 = MULH0 - MULH1 (Montgomery reduce W), forwards a(j) East.
//                      - PE(0,3): Computes b(j) = a(j) + SUB0 (Radix-2 butterfly upper output).
//                      - PE(1,3): Computes b(j+k) = a(j) - SUB0 (Radix-2 butterfly lower output).
//                      - Store AGE 2: Captures b(j) from PE(0,3) into Pong Bank 2 (delay = 7).
//                      - Store AGE 3: Captures b(j+k) from PE(1,3) into Pong Bank 3 (delay = 7).

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"

#define DRAM_BASE             ((volatile uint32_t *)(0x80000000UL))
#define NUM_BUTTERFLIES       8U
#define Q_VAL                 8380417U  // Modulus q
#define QINV_VAL              58728449U // q^(-1) mod 2^32

/* Test vectors */
static const int32_t vec_aj[NUM_BUTTERFLIES] = {
    100, 200, 300, 400, 500, 600, 700, 800
};

static const int32_t vec_ajk[NUM_BUTTERFLIES] = {
    15, -25, 35, -45, 55, -65, 75, -85
};

static const int32_t vec_zeta[NUM_BUTTERFLIES] = {
    1234, 5678, -9101, 1121, -3141, 5161, 7181, -9202
};

/* Function prototypes */
void loadInputsIntoSpM(const int32_t *aj, const int32_t *ajk, const int32_t *zeta, uint32_t len);
void clearPongBanks(uint32_t len);
void configureMageCommon(void);
void configureStep6_FullButterfly(void);
void computeGoldenModelStep6(const int32_t *aj, const int32_t *ajk, const int32_t *zeta,
                             int32_t *gold_bj, int32_t *gold_bjk, uint32_t len);
uint32_t verifyOutputsStep6(const int32_t *gold_bj, const int32_t *gold_bjk, uint32_t len);

int main(void) {
  int32_t gold_bj[NUM_BUTTERFLIES];
  int32_t gold_bjk[NUM_BUTTERFLIES];

  // 1. Calculate golden model for Step 6: Full butterfly b(j) and b(j+k)
  computeGoldenModelStep6(vec_aj, vec_ajk, vec_zeta, gold_bj, gold_bjk, NUM_BUTTERFLIES);

  // 2. Clear destination SpM Pong banks with poison pattern to avoid false positives
  clearPongBanks(NUM_BUTTERFLIES);

  // 3. Load input samples into SpM Ping banks and twiddle factors into Zeta bank
  loadInputsIntoSpM(vec_aj, vec_ajk, vec_zeta, NUM_BUTTERFLIES);

  // 4. Configure Mage common control registers (HWLP loop bounds, linear mode)
  configureMageCommon();

  // 5. Configure Mage Step 6: Full Butterfly PEA (PE 0,0 - 0,3 and PE 1,0 - 1,3)
  configureStep6_FullButterfly();

  // 6. Start Mage execution and poll until completion
  mage_start();
  mage_wait_done();

  // 7. Verify outputs from SpM Pong Banks 2 and 3 against golden model
  uint32_t cmperrs = verifyOutputsStep6(gold_bj, gold_bjk, NUM_BUTTERFLIES);

  // 8. Report test outcome to DRAM and return error count
  DRAM_BASE[0] = (cmperrs == 0) ? 0xCAFE0001U : 0xDEADBEEFU;
  DRAM_BASE[1] = cmperrs;

  return (int)cmperrs;
}

/**
 * @brief Load input operands into SpM Ping banks (Banks 0, 1) and Zeta bank (Bank 4).
 *
 * @param aj Pointer to array of a(j) samples (upper half of butterfly inputs).
 * @param ajk Pointer to array of a(j+k) samples (lower half of butterfly inputs).
 * @param zeta Pointer to array of twiddle factors.
 * @param len Number of butterfly operations (8).
 */
void loadInputsIntoSpM(const int32_t *aj, const int32_t *ajk, const int32_t *zeta, uint32_t len) {
  volatile uint32_t *bank0 = MAGE_BANK(0); // Stream 0 Ping: a(j)
  volatile uint32_t *bank1 = MAGE_BANK(1); // Stream 0 Ping: a(j+k)
  volatile uint32_t *bank4 = MAGE_BANK(4); // Stream 2 Zeta: zeta

  for (uint32_t i = 0; i < len; i++) {
    bank0[i] = (uint32_t)aj[i];
    bank1[i] = (uint32_t)ajk[i];
    bank4[i] = (uint32_t)zeta[i];
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Clear destination SpM Pong banks with a known poison value to prevent false positives.
 *
 * @param len Number of words to poison.
 */
void clearPongBanks(uint32_t len) {
  volatile uint32_t *bank2 = MAGE_BANK(2); // Stream 1 Pong: b(j)
  volatile uint32_t *bank3 = MAGE_BANK(3); // Stream 1 Pong: b(j+k)

  for (uint32_t i = 0; i < len; i++) {
    bank2[i] = 0x55AA55AAU;
    bank3[i] = 0x55AA55AAU;
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Compute reference CPU golden model for Step 6: Full Radix-2 NTT Butterfly.
 *
 * @param aj Input array a(j).
 * @param ajk Input array a(j+k).
 * @param zeta Input array zeta.
 * @param gold_bj Output array for expected b(j) = a(j) + W.
 * @param gold_bjk Output array for expected b(j+k) = a(j) - W.
 * @param len Number of samples.
 */
void computeGoldenModelStep6(const int32_t *aj, const int32_t *ajk, const int32_t *zeta,
                             int32_t *gold_bj, int32_t *gold_bjk, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    int64_t prod = (int64_t)ajk[i] * (int64_t)zeta[i];
    uint32_t mul0 = (uint32_t)prod;
    int32_t mulh0 = (int32_t)(prod >> 32);
    int32_t mul1 = (int32_t)(mul0 * QINV_VAL);
    int32_t mulh1 = (int32_t)(((int64_t)mul1 * (int64_t)Q_VAL) >> 32);
    int32_t w = mulh0 - mulh1; // Montgomery reduction result W
    gold_bj[i]  = aj[i] + w;
    gold_bjk[i] = aj[i] - w;
  }
}

/**
 * @brief Configure common Mage registers for 8-iteration linear execution.
 */
void configureMageCommon(void) {
  /**
   * REG_GEN_CFG: DAE Mode, Iteration Interval (II) = 1
   *   REG_GEN_CFG[18] = 1 (DAE mode enable)
   *   REG_GEN_CFG[3:0] = 1 (II = 1 cycle per iteration)
   */
  REG_GEN_CFG = (1U << 18) | 1U;

  /**
   * REG_BLOCK_SIZE = 0x00000000
   *   BLOCK_SIZE[1:0] = 0; Linear 1 word per block
   */
  REG_BLOCK_SIZE = 0x00000000U;

  /**
   * REG_STREAM_XOR_MODE = 0x00000000
   *   STREAM_XOR_MODE[2:0] = 0; Linear addressing on all streams
   */
  REG_STREAM_XOR_MODE = 0x00000000U;

  /**
   * REG_ILB_HWL = 0x00000000
   *   ILB_0..3 = 0; Start iteration from 0
   */
  REG_ILB_HWL = 0x00000000U;

  /**
   * REG_FLB_HWL = 0x00000007
   *   FLB_0 = 7; 8 iterations (0 to 7)
   */
  REG_FLB_HWL = (NUM_BUTTERFLIES - 1U);

  /**
   * REG_INC_HWL = 0x01010101
   *   INC_0..3 = 1; Stride increment of 1 per iteration
   */
  REG_INC_HWL = 0x01010101U;

  // Set linear strides and linear stride mode for all 5 AGEs
  for (int i = 0; i < 5; i++) {
    REG_STRIDES(i) = 1U;
    REG_STRIDE_MODE(i) = 0x00000000U;
  }

  // Clear accumulator loop selectors
  REG_ACC_HWLP_SEL_0 = 0x00000000U;

  /**
   * Load crossbar routing:
   *   Stream 0 Bank 0 -> AGE 0 (sel = 0)
   *   Stream 0 Bank 1 -> AGE 1 (sel = 1)
   *   Stream 1 Bank 2 -> AGE 2 (sel = 0)
   *   Stream 1 Bank 3 -> AGE 3 (sel = 1)
   *   Stream 2 Bank 4 -> AGE 4 (sel = 0)
   *   REG_LOAD_AGE_SEL(0) = (1U << 6) | (0U << 4) | (1U << 2) | (0U << 0) = 0x00000044
   */
  REG_LOAD_AGE_SEL(0) = 0x00000044U;
  REG_PEA_DMEM_SEL(0) = 0x00000044U;

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure Mage for Step 6: Full Radix-2 NTT Butterfly.
 *        - PE(0,0): Ingests a(j) from MEM_IN_0 into its delay unit.
 *        - PE(1,0): Computes MUL0 = a(j+k) * zeta mod 2^32, forwards a(j) from D_UP Eastward.
 *        - PE(1,1): Computes MULH0 = (a(j+k) *H zeta), forwards a(j) from D_LEFT Eastward.
 *        - PE(0,1): Computes MUL1 = MUL0 * QINV mod 2^32, ingests MULH0 from DOWN into delay.
 *        - PE(0,2): Computes MULH1 = MUL1 *H Q, forwards MULH0 from D_LEFT Southward.
 *        - PE(1,2): Computes SUB0 = MULH0 - MULH1 (Montgomery reduce result W),
 *                   while concurrently forwarding a(j) from D_LEFT into its delay pipeline.
 *        - PE(0,3): Computes b(j) = a(j) + SUB0 (Radix-2 butterfly upper result).
 *        - PE(1,3): Computes b(j+k) = a(j) - SUB0 (Radix-2 butterfly lower result).
 *        - Store AGE 2: captures Row 0 Col 3 (PE 0,3 b(j))   -> Bank 2 (Pong), delay = 7.
 *        - Store AGE 3: captures Row 1 Col 3 (PE 1,3 b(j+k)) -> Bank 3 (Pong), delay = 7.
 */
void configureStep6_FullButterfly(void) {
  // Clear AGE configuration registers
  for (int i = 0; i < 5; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /**
   * AGE Instructions:
   *   AGE 0:  LOAD from Stream 0 Bank 0 (Ping a(j)), base = 0, delay = 0, start_bank = 0
   */
  REG_CFG_MAGE_AGE(0) = (1U << 31) | (0U << 15) | (1U << 13) | (0U << 9) | (0U << 7) | (4U << 4) | (0U << 0);

  /**
   *   AGE 1:  LOAD from Stream 0 Bank 1 (Ping a(j+k)), base = 0, delay = 0, start_bank = 1
   */
  REG_CFG_MAGE_AGE(1) = (1U << 31) | (0U << 15) | (1U << 13) | (1U << 9) | (0U << 7) | (4U << 4) | (0U << 0);

  /**
   *   AGE 4:  LOAD from Stream 2 Bank 4 (Zeta twiddles), base = 0, delay = 0, start_bank = 0
   */
  REG_CFG_MAGE_AGE(4) = (1U << 31) | (0U << 15) | (1U << 13) | (0U << 9) | (0U << 7) | (4U << 4) | (0U << 0);

  /**
   *   AGE 2:  STORE to Stream 1 Bank 2 (Pong), base = 0, delay = 7, start_bank = 0
   *     HWLP_RF_SEL[3:0] = 7 (7 cycles: read + PE(1,0) + PE(0,1) + PE(0,2) + PE(1,2) + PE(0,3) + write)
   */
  REG_CFG_MAGE_AGE(2) = (1U << 31) | (0U << 15) | (0U << 13) | (0U << 9) | (0U << 7) | (4U << 4) | (7U << 0);

  /**
   *   AGE 3:  STORE to Stream 1 Bank 3 (Pong), base = 0, delay = 7, start_bank = 1
   *     HWLP_RF_SEL[3:0] = 7 (7 cycles: read + PE(1,0) + PE(0,1) + PE(0,2) + PE(1,2) + PE(1,3) + write)
   */
  REG_CFG_MAGE_AGE(3) = (1U << 31) | (0U << 15) | (0U << 13) | (1U << 9) | (0U << 7) | (4U << 4) | (7U << 0);

  /**
   * Output multiplexer selection (REG_OUT_PEA_SEL):
   *   Bits [5:4]: AGE 2 output selector (Row 0). Select Col 3 (PE 0,3: b(j))   -> 2'b11
   *   Bits [7:6]: AGE 3 output selector (Row 1). Select Col 3 (PE 1,3: b(j+k)) -> 2'b11
   *   REG_OUT_PEA_SEL = (3U << 6) | (3U << 4) = 0x000000F0
   */
  REG_OUT_PEA_SEL = (3U << 6) | (3U << 4);

  // Set constants:
  // PE(0,1): QINV = 58728449
  mage_set_pe_constant(QINV_VAL, 0, 1);
  // PE(0,2): Q = 8380417
  mage_set_pe_constant(Q_VAL, 0, 2);

  // Clear all PE configurations
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 4; c++) {
      REG_CFG_PE(r, c) = 0x00000000U;
    }
  }

  /**
   * PE(0,0) Configuration:
   *   OPCODE             = MAGE_PE_OP_NOP (0x00)
   *   MUX_A              = MAGE_PE_MUX_MEM_IN_0 (0x1) -> a(j) from Ping Bank 0
   *   MUX_B              = 0
   *   DELAY_PE_MUX_SEL   = 0
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_INPUT (0x5)       -> Ingest MEM_IN_0 into delay pipeline
   *   DELAY_FWD_SEL      = 0
   *   DELAY_IN_PE_SEL    = MAGE_PE_MUX_MEM_IN_0 (0x1)  -> Ingest a(j) from MEM_IN_0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_NOP,
      MAGE_PE_MUX_MEM_IN_0,
      0,
      0,
      MAGE_D_PE_INPUT,
      0,
      MAGE_PE_MUX_MEM_IN_0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(1,0) Configuration:
   *   OPCODE             = MAGE_PE_OP_MUL (0x01)
   *   MUX_A              = MAGE_PE_MUX_MEM_IN_0 (0x1) -> a(j+k) from Ping Bank 1
   *   MUX_B              = MAGE_PE_MUX_MEM_IN_1 (0x2) -> zeta from Zeta Bank 4
   *   DELAY_PE_MUX_SEL   = 0
   *   DELAY_PE_OP_MUX    = MAGE_D_DELAY_OP (0x4)       -> Forward delay operand
   *   DELAY_FWD_SEL      = MAGE_D_UP (0x0)             -> Forward a(j) received from UP neighbor PE(0,0)
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(1, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MUL,
      MAGE_PE_MUX_MEM_IN_0,
      MAGE_PE_MUX_MEM_IN_1,
      0,
      MAGE_D_DELAY_OP,
      MAGE_D_UP,
      0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(1,1) Configuration:
   *   OPCODE             = MAGE_PE_OP_MULH (0x0D)
   *   MUX_A              = MAGE_PE_MUX_MEM_IN_0 (0x1) -> a(j+k) from Ping Bank 1
   *   MUX_B              = MAGE_PE_MUX_MEM_IN_1 (0x2) -> zeta from Zeta Bank 4
   *   DELAY_PE_MUX_SEL   = 0
   *   DELAY_PE_OP_MUX    = MAGE_D_DELAY_OP (0x4)       -> Forward delay operand
   *   DELAY_FWD_SEL      = MAGE_D_LEFT (0x1)           -> Forward a(j) received from LEFT neighbor PE(1,0)
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(1, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MULH,
      MAGE_PE_MUX_MEM_IN_0,
      MAGE_PE_MUX_MEM_IN_1,
      0,
      MAGE_D_DELAY_OP,
      MAGE_D_LEFT,
      0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,1) Configuration:
   *   OPCODE             = MAGE_PE_OP_MUL (0x01)
   *   MUX_A              = MAGE_PE_MUX_SW (0xA)        -> Consume MUL0 from PE(1,0)
   *   MUX_B              = MAGE_PE_MUX_CONST (0x0)     -> QINV
   *   DELAY_PE_MUX_SEL   = 0
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_INPUT (0x5)       -> Ingest neighbor input into delay pipeline
   *   DELAY_FWD_SEL      = 0
   *   DELAY_IN_PE_SEL    = MAGE_PE_MUX_DOWN (0x6)      -> Ingest MULH0 from neighbor DOWN (PE 1,1)
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(0, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MUL,
      MAGE_PE_MUX_SW,
      MAGE_PE_MUX_CONST,
      0,
      MAGE_D_PE_INPUT,
      0,
      MAGE_PE_MUX_DOWN,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,2) Configuration:
   *   OPCODE             = MAGE_PE_OP_MULH (0x0D)
   *   MUX_A              = MAGE_PE_MUX_LEFT (0x4)      -> Consume MUL1 from PE(0,1)
   *   MUX_B              = MAGE_PE_MUX_CONST (0x0)     -> Q
   *   DELAY_PE_MUX_SEL   = 0
   *   DELAY_PE_OP_MUX    = MAGE_D_DELAY_OP (0x4)       -> Forward delayed operand
   *   DELAY_FWD_SEL      = MAGE_D_LEFT (0x1)           -> Forward MULH0 received from LEFT neighbor
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(0, 2) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MULH,
      MAGE_PE_MUX_LEFT,
      MAGE_PE_MUX_CONST,
      0,
      MAGE_D_DELAY_OP,
      MAGE_D_LEFT,
      0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(1,2) Configuration:
   *   OPCODE             = MAGE_PE_OP_SUB (0x02)       -> Compute SUB0 = op_a - op_b = MULH0 - MULH1
   *   MUX_A              = MAGE_PE_MUX_DELAY_OP (0xC)  -> MULH0 from delay port D_UP
   *   MUX_B              = MAGE_PE_MUX_UP (0x3)        -> MULH1 from regular neighbor UP PE(0,2)
   *   DELAY_PE_MUX_SEL   = MAGE_D_UP (0x0)             -> Select D_UP (MULH0 from PE 0,2 delay out) to FU
   *   DELAY_PE_OP_MUX    = MAGE_D_DELAY_OP (0x4)       -> Forward delayed operand
   *   DELAY_FWD_SEL      = MAGE_D_LEFT (0x1)           -> Forward a(j) received from LEFT neighbor PE(1,1)
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)    -> 1 cycle delay
   */
  REG_CFG_PE(1, 2) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_SUB,
      MAGE_PE_MUX_DELAY_OP,
      MAGE_PE_MUX_UP,
      MAGE_D_UP,
      MAGE_D_DELAY_OP,
      MAGE_D_LEFT,
      0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,3) Configuration:
   *   OPCODE             = MAGE_PE_OP_ADD (0x0A)       -> Compute b(j) = a(j) + SUB0
   *   MUX_A              = MAGE_PE_MUX_DELAY_OP (0xC)  -> a(j) from delay port D_SW
   *   MUX_B              = MAGE_PE_MUX_SW (0xA)        -> SUB0 from SW neighbor PE(1,2)
   *   DELAY_PE_MUX_SEL   = MAGE_D_SW (0x7)             -> Select D_SW (a(j) from PE 1,2 delay out) to FU
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_OP_NONE (0x0)
   *   DELAY_FWD_SEL      = 0
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)
   */
  REG_CFG_PE(0, 3) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_ADD,
      MAGE_PE_MUX_DELAY_OP,
      MAGE_PE_MUX_SW,
      MAGE_D_SW,
      MAGE_D_PE_OP_NONE,
      0,
      0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(1,3) Configuration:
   *   OPCODE             = MAGE_PE_OP_SUB (0x02)       -> Compute b(j+k) = a(j) - SUB0
   *   MUX_A              = MAGE_PE_MUX_DELAY_OP (0xC)  -> a(j) from delay port D_LEFT
   *   MUX_B              = MAGE_PE_MUX_LEFT (0x4)      -> SUB0 from LEFT neighbor PE(1,2)
   *   DELAY_PE_MUX_SEL   = MAGE_D_LEFT (0x1)           -> Select D_LEFT (a(j) from PE 1,2 delay out) to FU
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_OP_NONE (0x0)
   *   DELAY_FWD_SEL      = 0
   *   DELAY_IN_PE_SEL    = 0
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)
   */
  REG_CFG_PE(1, 3) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_SUB,
      MAGE_PE_MUX_DELAY_OP,
      MAGE_PE_MUX_LEFT,
      MAGE_D_LEFT,
      MAGE_D_PE_OP_NONE,
      0,
      0,
      MAGE_DELAY_DEPTH_1
  );

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Verify SpM Pong Banks 2 and 3 outputs against Golden Model for Step 6.
 *
 * @param gold_bj Expected b(j) values array (Bank 2).
 * @param gold_bjk Expected b(j+k) values array (Bank 3).
 * @param len Number of elements to compare.
 * @return uint32_t Number of mismatches.
 */
uint32_t verifyOutputsStep6(const int32_t *gold_bj, const int32_t *gold_bjk, uint32_t len) {
  volatile uint32_t *bank2 = MAGE_BANK(2); // b(j)
  volatile uint32_t *bank3 = MAGE_BANK(3); // b(j+k)
  uint32_t errs = 0;

  for (uint32_t i = 0; i < len; i++) {
    uint32_t hw_bj = bank2[i];
    uint32_t exp_bj = (uint32_t)gold_bj[i];

    uint32_t hw_bjk = bank3[i];
    uint32_t exp_bjk = (uint32_t)gold_bjk[i];

    if (hw_bj != exp_bj) {
      errs++;
    }
    if (hw_bjk != exp_bjk) {
      errs++;
    }
  }

  return errs;
}
