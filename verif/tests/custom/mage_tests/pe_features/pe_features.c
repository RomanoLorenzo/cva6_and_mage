// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: pe_features.c
// Description: Unit and integration verification test for enhanced Mage PE delay features:
//              Feature 1: Concurrent ALU arithmetic and arbitrary PE input ingestion (D_PE_INPUT)
//              Feature 2: Configurable shift-register delay pipeline depth (1, 2, 3, 4 cycles)
//              Feature 3: Decoupled delay selectors (delay_fwd_sel vs delay_pe_mux_sel)
//              Target hardware configuration: cfg_ntt_4u_3stream.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"

#define DRAM_BASE       ((volatile uint32_t *)(0x80000000UL))
#define N_SAMPLES       64U
#define CONST_ADD_VAL   1000U
#define CONST_PE11_VAL  0xCAFEBABEUL

/* Function prototypes */
void generateInputData(uint32_t *vec_a, uint32_t *vec_b, uint32_t len);
uint32_t loadPingOperands(const uint32_t *vec_a, const uint32_t *vec_b, uint32_t len);
void clearPongBanks(uint32_t len);
void configureCommonRegisters(void);

uint32_t testFeature1_ConcurrentAluAndInputIngest(const uint32_t *vec_a, const uint32_t *vec_b, uint32_t len);
uint32_t testFeature2_ConfigurableDelayDepth(const uint32_t *vec_b, uint32_t len);
uint32_t testFeature3_DecoupledDelaySelectors(const uint32_t *vec_a, uint32_t len);

static inline uint32_t get_sn_1u(uint32_t a);
static inline uint32_t get_bi_1u(uint32_t a);
static inline uint32_t get_ba_1u(uint32_t a);

int main(void) {
  uint32_t vec_a[N_SAMPLES];
  uint32_t vec_b[N_SAMPLES];
  uint32_t total_errors = 0;

  // 1. Generate distinct deterministic test datasets
  generateInputData(vec_a, vec_b, N_SAMPLES);

  // 2. Load operands into Ping SpM banks (Banks 0 and 1)
  uint32_t load_errs = loadPingOperands(vec_a, vec_b, N_SAMPLES);
  if (load_errs != 0) {
    DRAM_BASE[0] = 0xDEAD0001U;
    DRAM_BASE[1] = load_errs;
    return (int)load_errs;
  }

  // 3. Configure shared control registers (linear strides, static crossbars, XOR addressing)
  configureCommonRegisters();

  // 4. Test Feature 1: Concurrent ALU Execution & Arbitrary Input Ingestion (D_PE_INPUT)
  uint32_t err_f1 = testFeature1_ConcurrentAluAndInputIngest(vec_a, vec_b, N_SAMPLES);
  if (err_f1 != 0) {
    DRAM_BASE[0] = 0xDEADF100U;
    DRAM_BASE[1] = err_f1;
    return (int)err_f1;
  }

  // 5. Test Feature 2: Configurable Delay Depth (1 to 4 cycles)
  uint32_t err_f2 = testFeature2_ConfigurableDelayDepth(vec_b, N_SAMPLES);
  if (err_f2 != 0) {
    DRAM_BASE[0] = 0xDEADF200U;
    DRAM_BASE[1] = err_f2;
    return 20;
  }

  // 6. Test Feature 3: Decoupled Delay Selectors (delay_fwd_sel vs delay_pe_mux_sel)
  uint32_t err_f3 = testFeature3_DecoupledDelaySelectors(vec_a, N_SAMPLES);
  if (err_f3 != 0) {
    DRAM_BASE[0] = 0xDEADF300U;
    DRAM_BASE[1] = err_f3;
    return 30;
  }

  // 7. Report overall test result
  if (total_errors == 0) {
    DRAM_BASE[0] = 0xCAFE0001U; // Success marker
    DRAM_BASE[1] = 0x00000000U; // 0 errors
    return 0;
  } else {
    DRAM_BASE[0] = 0xDEADBEEFU; // Failure marker
    DRAM_BASE[1] = total_errors;
    return (int)total_errors;
  }
}

/**
 * @brief Populate deterministic non-trivial operands
 *
 * @param vec_a Destination array for operand vector A
 * @param vec_b Destination array for operand vector B
 * @param len Number of elements
 */
void generateInputData(uint32_t *vec_a, uint32_t *vec_b, uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    vec_a[i] = 100U + i * 7U;
    vec_b[i] = 5000U + i * 13U;
  }
}

/**
 * @brief Calculate Step Number for 1 computing unit (XOR address mapping)
 */
static inline uint32_t get_sn_1u(uint32_t a) {
  return ((a >> 7) & 1U);
}

/**
 * @brief Calculate Bank Index for 1 computing unit (XOR address mapping)
 */
static inline uint32_t get_bi_1u(uint32_t a) {
  uint32_t sn = get_sn_1u(a);
  return ((a ^ sn) & 1U);
}

/**
 * @brief Calculate Word Bank Address for 1 computing unit
 */
static inline uint32_t get_ba_1u(uint32_t a) {
  return (a >> 1);
}

/**
 * @brief Load operand vectors into SpM Ping banks (Bank 0 and Bank 1)
 */
uint32_t loadPingOperands(const uint32_t *vec_a, const uint32_t *vec_b, uint32_t len) {
  uint32_t errs = 0;

  // Load vector A to base 0 (flat indices 0..len-1)
  for (uint32_t i = 0; i < len; i++) {
    uint32_t bi = get_bi_1u(i);
    uint32_t ba = get_ba_1u(i);
    MAGE_BANK(bi)[ba] = vec_a[i];
  }

  // Load vector B to base 128 (flat indices 128..128+len-1)
  for (uint32_t i = 0; i < len; i++) {
    uint32_t flat_idx = 128U + i;
    uint32_t bi = get_bi_1u(flat_idx);
    uint32_t ba = get_ba_1u(flat_idx);
    MAGE_BANK(bi)[ba] = vec_b[i];
  }

  asm volatile("fence w, w" ::: "memory");

  // Verify write integrity
  for (uint32_t i = 0; i < len; i++) {
    uint32_t bi_a = get_bi_1u(i);
    uint32_t ba_a = get_ba_1u(i);
    if (MAGE_BANK(bi_a)[ba_a] != vec_a[i]) {
      errs++;
    }

    uint32_t flat_idx_b = 128U + i;
    uint32_t bi_b = get_bi_1u(flat_idx_b);
    uint32_t ba_b = get_ba_1u(flat_idx_b);
    if (MAGE_BANK(bi_b)[ba_b] != vec_b[i]) {
      errs++;
    }
  }

  asm volatile("fence r, r" ::: "memory");
  return errs;
}

/**
 * @brief Zero out destination Pong banks to ensure fresh test results
 */
void clearPongBanks(uint32_t len) {
  for (uint32_t i = 0; i < len; i++) {
    uint32_t bi0 = 8U + get_bi_1u(i);
    uint32_t ba0 = get_ba_1u(i);
    MAGE_BANK(bi0)[ba0] = 0x55AA55AAU;

    uint32_t bi1 = 10U + get_bi_1u(i);
    uint32_t ba1 = get_ba_1u(i);
    MAGE_BANK(bi1)[ba1] = 0x55AA55AAU;
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure common Mage registers for static DAE execution
 */
void configureCommonRegisters(void) {
  /**
   * REG_GEN_CFG = 0x00000000
   *   II[3:0]   = 0; 1 cycle per hardware loop iteration
   *   S_N_T_*   = 0; Fully static configuration mode for AGU, PEA, Xbars
   */
  mage_set_general_cfg_bits(0x00000000U);

  /**
   * REG_BLOCK_SIZE = 0x00000000
   *   BS[1:0] = 0; 1 word per block
   */
  mage_set_address_map_blocksize(0x00000000U);

  /**
   * REG_STREAM_XOR_MODE = 0x00000003
   *   STREAM_XOR_MODE[0] = 1; Ping stream XOR addressing
   *   STREAM_XOR_MODE[1] = 1; Pong stream XOR addressing
   *   STREAM_XOR_MODE[2] = 0; Zeta stream linear addressing
   */
  mage_set_stream_xor_mode(0x00000003U);

  /**
   * REG_INC_HWL = 0x01010101
   *   INC_0..3 = 1; Step by 1 element
   */
  REG_INC_HWL = 0x01010101U;

  /**
   * REG_ILB_HWL = 0x00000000
   *   ILB_0..3 = 0; Start iteration from 0
   */
  REG_ILB_HWL = 0x00000000U;

  // Stride mode linear for all AGEs
  for (int i = 0; i < 20; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U;
  }

  // Clear accumulator selectors
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_ACC_HWLP_SEL_1 = 0x00000000U;
  REG_ACC_HWLP_SEL_2 = 0x00000000U;

  /**
   * REG_OUT_PEA_SEL = 0x55005500
   *   Route Row 0, Col 0 (PE 0,0) to pea_data_o[0]
   *   Route Row 0, Col 1 (PE 0,1) to pea_data_o[4]
   */
  REG_OUT_PEA_SEL = 0x55005500U;

  /**
   * Load crossbar and store crossbar routing:
   *   AGE 0  -> in_data_pea[0] -> PE(0,0) MEM_IN_0
   *   AGE 4  -> in_data_pea[4] -> PE(0,0) MEM_IN_1
   *   pea_data_o[0] -> AGE 8  (Store upper)
   *   pea_data_o[4] -> AGE 12 (Store lower)
   */
  REG_LOAD_AGE_SEL_0 = 0x88FAC688U;
  REG_LOAD_AGE_SEL_1 = 0x0688FAC6U;
  REG_PEA_DMEM_SEL_0 = 0x88FAC688U;
  REG_PEA_DMEM_SEL_1 = 0x0688FAC6U;

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Test Feature 1: Concurrent ALU arithmetic and arbitrary PE input ingestion (D_PE_INPUT)
 *
 * In this test, PE(0,0)'s FU executes:
 *   res = ADD(MEM_IN_0, CONSTANT) -> vec_a[i] + CONST_ADD_VAL
 * and sends it directly to Store AGE 8 (Pong Bank 8/9 base 0).
 * Simultaneously, PE(0,0)'s delay unit captures MEM_IN_1 (vec_b[i]) via D_PE_INPUT:
 *   delay_op_out = delay_op_pe_in (MEM_IN_1)
 * and transmits it across the horizontal delay mesh to PE(0,1)'s LEFT delay port.
 * PE(0,1) forwards the delay operand:
 *   res = FW_A(DELAY_OP) with delay_pe_mux_sel = MAGE_D_LEFT
 * and sends it to Store AGE 12 (Pong Bank 8/9 base 128).
 *
 * @param vec_a Operands A
 * @param vec_b Operands B
 * @param len Number of elements
 * @return Number of mismatches
 */
uint32_t testFeature1_ConcurrentAluAndInputIngest(const uint32_t *vec_a, const uint32_t *vec_b, uint32_t len) {
  clearPongBanks(len);

  // Set PE(0,0) constant register
  mage_set_pe_constant(CONST_ADD_VAL, 0, 0);

  // Configure loop bounds: len iterations
  REG_FLB_HWL = (len - 1U);
  for (int i = 0; i < 16; i++) {
    REG_STRIDES(i) = 0x00000001U;
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /**
   * AGE Instructions:
   *   AGE 0:  LOAD from Stream 0 (Ping), base = 0,   delay = 0, start_bank = 0 (Banks 0/1) (vec_a)
   *   AGE 4:  LOAD from Stream 0 (Ping), base = 128, delay = 0, start_bank = 0 (Banks 0/1) (vec_b)
   *   AGE 8:  STORE to Stream 1 (Pong),  base = 0,   delay = 3, start_bank = 0 (Banks 8/9) (ALU output)
   *   AGE 12: STORE to Stream 1 (Pong),  base = 0,   delay = 4, start_bank = 2 (Banks 10/11) (Delay channel)
   *   Note: Setting start_bank = 2 for AGE 12 routes it to Banks 10/11, eliminating bank conflicts with AGE 8.
   */
  REG_CFG_MAGE_AGE(0)  = (1U << 31) | (0U << 15)   | (1U << 13) | (0U << 9) | (1U << 7) | (4U << 4) | (0U << 0);
  REG_CFG_MAGE_AGE(4)  = (1U << 31) | (128U << 15) | (1U << 13) | (0U << 9) | (1U << 7) | (4U << 4) | (0U << 0);
  REG_CFG_MAGE_AGE(8)  = (1U << 31) | (0U << 15)   | (0U << 13) | (0U << 9) | (1U << 7) | (4U << 4) | (3U << 0);
  REG_CFG_MAGE_AGE(12) = (1U << 31) | (0U << 15)   | (0U << 13) | (2U << 9) | (1U << 7) | (4U << 4) | (4U << 0);

  /**
   * PE(0,0) Configuration:
   *   OPCODE             = MAGE_PE_OP_ADD (0x0A)
   *   MUX_A              = MAGE_PE_MUX_MEM_IN_0 (0x1) -> vec_a
   *   MUX_B              = MAGE_PE_MUX_CONST (0x0)    -> 1000
   *   DELAY_PE_MUX_SEL   = 0 (unused by FU)
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_INPUT (0x5)      -> Ingest regular input
   *   DELAY_FWD_SEL      = 0 (unused)
   *   DELAY_IN_PE_SEL    = MAGE_PE_MUX_MEM_IN_1 (0x2) -> vec_b
   *   DELAY_DEPTH        = MAGE_DELAY_DEPTH_1 (0x0)   -> 1 cycle delay
   */
  REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_ADD,
      MAGE_PE_MUX_MEM_IN_0,
      MAGE_PE_MUX_CONST,
      0,
      MAGE_D_PE_INPUT,
      0,
      MAGE_PE_MUX_MEM_IN_1,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,1) Configuration:
   *   OPCODE             = MAGE_PE_OP_FW_A (0x0B)
   *   MUX_A              = MAGE_PE_MUX_DELAY_OP (0xF) -> consume delay operand
   *   MUX_B              = 0
   *   DELAY_PE_MUX_SEL   = MAGE_D_LEFT (0x1)          -> read LEFT neighbour PE(0,0)
   *   DELAY_PE_OP_MUX    = MAGE_D_PE_OP_NONE (0x0)
   */
  REG_CFG_PE(0, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_FW_A,
      MAGE_PE_MUX_DELAY_OP,
      0,
      MAGE_D_LEFT,
      MAGE_D_PE_OP_NONE,
      0,
      0,
      MAGE_DELAY_DEPTH_1
  );

  // Disable other PEs
  for (int r = 1; r < 4; r++) {
    REG_CFG_PE(r, 0) = 0U;
    REG_CFG_PE(r, 1) = 0U;
  }

  asm volatile("fence w, w" ::: "memory");

  mage_start();
  mage_wait_done();

  asm volatile("fence r, r" ::: "memory");

  // Validate results
  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < len; i++) {
    // Check ALU output in Bank 8/9 base 0: expected vec_a[i] + CONST_ADD_VAL
    uint32_t bi_alu = 8U + get_bi_1u(i);
    uint32_t ba_alu = get_ba_1u(i);
    uint32_t act_alu = MAGE_BANK(bi_alu)[ba_alu];
    uint32_t exp_alu = vec_a[i] + CONST_ADD_VAL;
    if (act_alu != exp_alu) {
      mismatches++;
    }

    // Check Delay output in Bank 10/11 base 0: expected vec_b[i]
    uint32_t bi_del = 10U + get_bi_1u(i);
    uint32_t ba_del = get_ba_1u(i);
    uint32_t act_del = MAGE_BANK(bi_del)[ba_del];
    uint32_t exp_del = vec_b[i];
    if (act_del != exp_del) {
      mismatches++;
    }
  }

  return mismatches;
}

/**
 * @brief Test Feature 2: Configurable shift-register delay pipeline depth (1, 2, 3, 4 cycles)
 *
 * Exercises all 4 delay depth settings (MAGE_DELAY_DEPTH_1, _2, _3, _4).
 * For each depth D in {1, 2, 3, 4}, the store AGE capture timing is adjusted to
 * hwlp_rf_sel = 3 + D. Data correctness verifies that the internal shift-register tap
 * is precisely active at D cycles latency.
 *
 * @param vec_b Test input vector
 * @param len Number of elements
 * @return Number of mismatches across all depths
 */
uint32_t testFeature2_ConfigurableDelayDepth(const uint32_t *vec_b, uint32_t len) {
  uint32_t total_mismatches = 0;

  const uint32_t depth_settings[4] = {
    MAGE_DELAY_DEPTH_1,
    MAGE_DELAY_DEPTH_2,
    MAGE_DELAY_DEPTH_3,
    MAGE_DELAY_DEPTH_4
  };

  for (uint32_t d_idx = 0; d_idx < 4; d_idx++) {
    uint32_t depth_cfg = depth_settings[d_idx];
    uint32_t cycle_delay = d_idx + 1U;
    uint32_t store_hwlp_sel = 3U + cycle_delay;

    clearPongBanks(len);

    REG_FLB_HWL = (len - 1U);
    for (int i = 0; i < 16; i++) {
      REG_STRIDES(i) = 0x00000001U;
      REG_CFG_MAGE_AGE(i) = 0x00000000U;
    }

    // AGE 4 loads vec_b from Ping base 128
    // AGE 12 stores forwarded vec_b to Pong base 128 with programmed cycle delay
    REG_CFG_MAGE_AGE(4)  = (1U << 31) | (128U << 15) | (1U << 13) | (1U << 7) | (4U << 4) | (0U << 0);
    REG_CFG_MAGE_AGE(12) = (1U << 31) | (128U << 15) | (0U << 13) | (1U << 7) | (4U << 4) | (store_hwlp_sel << 0);

    // PE(0,0): Ingests vec_b into delay channel with configurable depth
    REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_NOP,
        0,
        0,
        0,
        MAGE_D_PE_INPUT,
        0,
        MAGE_PE_MUX_MEM_IN_1,
        depth_cfg
    );

    // PE(0,1): Reads LEFT neighbour and forwards to Store AGE 12
    REG_CFG_PE(0, 1) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_FW_A,
        MAGE_PE_MUX_DELAY_OP,
        0,
        MAGE_D_LEFT,
        MAGE_D_PE_OP_NONE,
        0,
        0,
        MAGE_DELAY_DEPTH_1
    );

    for (int r = 1; r < 4; r++) {
      REG_CFG_PE(r, 0) = 0U;
      REG_CFG_PE(r, 1) = 0U;
    }

    asm volatile("fence w, w" ::: "memory");

    mage_start();
    mage_wait_done();

    asm volatile("fence r, r" ::: "memory");

    // Verify received values in Pong Bank 8/9 base 128
    for (uint32_t i = 0; i < len; i++) {
      uint32_t bi = 8U + get_bi_1u(128U + i);
      uint32_t ba = get_ba_1u(128U + i);
      uint32_t actual = MAGE_BANK(bi)[ba];
      uint32_t expected = vec_b[i];
      if (actual != expected) {
        total_mismatches++;
      }
    }
  }

  return total_mismatches;
}

/**
 * @brief Test Feature 3: Decoupled delay selectors (delay_fwd_sel vs delay_pe_mux_sel)
 *
 * Verifies that a single PE (PE 0,1) can simultaneously:
 *   1. Consume a delayed operand from its LEFT neighbour (PE 0,0) into its FU via delay_pe_mux_sel = MAGE_D_LEFT.
 *   2. Forward an independent delayed operand from its DOWN neighbour (PE 1,1) via delay_fwd_sel = MAGE_D_DOWN.
 *
 * Setup:
 *   - PE(0,0) ingests vec_a[i] into delay output to PE(0,1)'s LEFT port.
 *   - PE(1,1) ingests CONST_PE11_VAL into delay output to PE(0,1)'s DOWN port.
 *   - PE(0,1) FU selects DELAY_OP with delay_pe_mux_sel = MAGE_D_LEFT -> outputs vec_a[i] to Store AGE 12.
 *   - PE(0,1) delay forwarder selects delay_fwd_sel = MAGE_D_DOWN -> outputs CONST_PE11_VAL to PE(0,0)'s RIGHT port.
 *   - PE(0,0) FU reads DELAY_OP with delay_pe_mux_sel = MAGE_D_RIGHT -> outputs CONST_PE11_VAL to Store AGE 8.
 *
 * @param vec_a Input vector
 * @param len Number of elements
 * @return Number of mismatches
 */
uint32_t testFeature3_DecoupledDelaySelectors(const uint32_t *vec_a, uint32_t len) {
  clearPongBanks(len);

  // Set PE(1,1) constant register
  mage_set_pe_constant(CONST_PE11_VAL, 1, 1);

  REG_FLB_HWL = (len - 1U);
  for (int i = 0; i < 16; i++) {
    REG_STRIDES(i) = 0x00000001U;
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /**
   * AGE Instructions:
   *   AGE 0:  LOAD from Ping base 0, delay = 0, start_bank = 0 (Banks 0/1) (vec_a)
   *   AGE 8:  STORE to Pong base 0, delay = 5, start_bank = 0 (Banks 8/9) (CONST_PE11_VAL via 2-hop delay forward)
   *   AGE 12: STORE to Pong base 0, delay = 4, start_bank = 2 (Banks 10/11) (vec_a via 1-hop delay forward)
   *   Note: Routing AGE 12 to Banks 10/11 eliminates bank conflicts with AGE 8.
   */
  REG_CFG_MAGE_AGE(0)  = (1U << 31) | (0U << 15)   | (1U << 13) | (0U << 9) | (1U << 7) | (4U << 4) | (0U << 0);
  REG_CFG_MAGE_AGE(8)  = (1U << 31) | (0U << 15)   | (0U << 13) | (0U << 9) | (1U << 7) | (4U << 4) | (5U << 0);
  REG_CFG_MAGE_AGE(12) = (1U << 31) | (0U << 15)   | (0U << 13) | (2U << 9) | (1U << 7) | (4U << 4) | (4U << 0);

  /**
   * PE(1,1) Configuration:
   *   Delay unit ingests CONSTANT (CONST_PE11_VAL) and transmits UP to PE(0,1)'s DOWN port.
   */
  REG_CFG_PE(1, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_NOP,
      0,
      0,
      0,
      MAGE_D_PE_INPUT,
      0,
      MAGE_PE_MUX_CONST,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,0) Configuration:
   *   FU: Reads DELAY_OP from RIGHT neighbour PE(0,1) and writes to Store AGE 8.
   *   Delay: Ingests MEM_IN_0 (vec_a) and transmits to PE(0,1)'s LEFT port.
   */
  REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_FW_A,
      MAGE_PE_MUX_DELAY_OP,
      0,
      MAGE_D_RIGHT,
      MAGE_D_PE_INPUT,
      0,
      MAGE_PE_MUX_MEM_IN_0,
      MAGE_DELAY_DEPTH_1
  );

  /**
   * PE(0,1) Configuration:
   *   FU: Consumes DELAY_OP from LEFT neighbour PE(0,0) -> delay_pe_mux_sel = MAGE_D_LEFT.
   *   Delay Forwarder: Forwards from DOWN neighbour PE(1,1) -> delay_fwd_sel = MAGE_D_DOWN.
   *   Notice: delay_pe_mux_sel != delay_fwd_sel! (Decoupled operation)
   */
  REG_CFG_PE(0, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_FW_A,
      MAGE_PE_MUX_DELAY_OP,
      0,
      MAGE_D_LEFT,
      MAGE_D_DELAY_OP,
      MAGE_D_DOWN,
      0,
      MAGE_DELAY_DEPTH_1
  );

  // Disable remaining PEs
  REG_CFG_PE(1, 0) = 0U;
  for (int r = 2; r < 4; r++) {
    REG_CFG_PE(r, 0) = 0U;
    REG_CFG_PE(r, 1) = 0U;
  }

  asm volatile("fence w, w" ::: "memory");

  mage_start();
  mage_wait_done();

  asm volatile("fence r, r" ::: "memory");

  uint32_t mismatches = 0;
  for (uint32_t i = 0; i < len; i++) {
    // Check Pong base 0 (Store AGE 8): expected CONST_PE11_VAL
    uint32_t bi_0 = 8U + get_bi_1u(i);
    uint32_t ba_0 = get_ba_1u(i);
    uint32_t act_0 = MAGE_BANK(bi_0)[ba_0];
    if (act_0 != CONST_PE11_VAL) {
      mismatches++;
    }

    // Check Pong base 0 (Store AGE 12): expected vec_a[i] in Banks 10/11
    uint32_t bi_1 = 10U + get_bi_1u(i);
    uint32_t ba_1 = get_ba_1u(i);
    uint32_t act_1 = MAGE_BANK(bi_1)[ba_1];
    if (act_1 != vec_a[i]) {
      mismatches++;
    }
  }

  return mismatches;
}

