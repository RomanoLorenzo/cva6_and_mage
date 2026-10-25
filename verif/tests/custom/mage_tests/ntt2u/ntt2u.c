// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: ntt2u.c
// Description: Full 8-stage Radix-2 NTT verification test on Mage CGRA
//              using 2 parallel Computing Units (4x4 PEA, U = 2) with conflict-free XOR addressing
//              and Montgomery modular reduction.
//              Outputs are validated against the official CRYSTALS-Dilithium CPU golden model.

// MAGE_CFG_HJSON = ips/Mage-and-Rogue/config/dae/cfg_ntt_2u_3stream.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"
#include "mage_regs.h"

#define DRAM_BASE             ((volatile uint32_t *)(0x80000000UL))
#define N                     256U
#define Q_VAL                 8380417U  // Modulus q
#define QINV_VAL              58728449U // q^(-1) mod 2^32
#define N_UNITS               2U        // 2 Computing Units

/* Twiddle factors from official CRYSTALS-Dilithium repository */
static const int32_t zetas[N] = {
         0,    25847, -2608894,  -518909,   237124,  -777960,  -876248,   466468,
   1826347,  2353451,  -359251, -2091905,  3119733, -2884855,  3111497,  2680103,
   2725464,  1024112, -1079900,  3585928,  -549488, -1119584,  2619752, -2108549,
  -2118186, -3859737, -1399561, -3277672,  1757237,   -19422,  4010497,   280005,
   2706023,    95776,  3077325,  3530437, -1661693, -3592148, -2537516,  3915439,
  -3861115, -3043716,  3574422, -2867647,  3539968,  -300467,  2348700,  -539299,
  -1699267, -1643818,  3505694, -3821735,  3507263, -2140649, -1600420,  3699596,
    811944,   531354,   954230,  3881043,  3900724, -2556880,  2071892, -2797779,
  -3930395, -1528703, -3677745, -3041255, -1452451,  3475950,  2176455, -1585221,
  -1257611,  1939314, -4083598, -1000202, -3190144, -3157330, -3632928,   126922,
   3412210,  -983419,  2147896,  2715295, -2967645, -3693493,  -411027, -2477047,
   -671102, -1228525,   -22981, -1308169,  -381987,  1349076,  1852771, -1430430,
  -3343383,   264944,   508951,  3097992,    44288, -1100098,   904516,  3958618,
  -3724342,    -8578,  1653064, -3249728,  2389356,  -210977,   759969, -1316856,
    189548, -3553272,  3159746, -1851402, -2409325,  -177440,  1315589,  1341330,
   1285669, -1584928,  -812732, -1439742, -3019102, -3881060, -3628969,  3839961,
   2091667,  3407706,  2316500,  3817976, -3342478,  2244091, -2446433, -3562462,
    266997,  2434439, -1235728,  3513181, -3520352, -3759364, -1197226, -3193378,
    900702,  1859098,   909542,   819034,   495491, -1613174,   -43260,  -522500,
   -655327, -3122442,  2031748,  3207046, -3556995,  -525098,  -768622, -3595838,
    342297,   286988, -2437823,  4108315,  3437287, -3342277,  1735879,   203044,
   2842341,  2691481, -2590150,  1265009,  4055324,  1247620,  2486353,  1595974,
  -3767016,  1250494,  2635921, -3548272, -2994039,  1869119,  1903435, -1050970,
  -1333058,  1237275, -3318210, -1430225,  -451100,  1312455,  3306115, -1962642,
  -1279661,  1917081, -2546312, -1374803,  1500165,   777191,  2235880,  3406031,
   -542412, -2831860, -1671176, -1846953, -2584293, -3724270,   594136, -3776993,
  -2013608,  2432395,  2454455,  -164721,  1957272,  3369112,   185531, -1207385,
  -3183426,   162844,  1616392,  3014001,   810149,  1652634, -3694233, -1799107,
  -3038916,  3523897,  3866901,   269760,  2213111,  -975884,  1717735,   472078,
   -426683,  1723600, -1803090,  1910376, -1667432, -1104333,  -260646, -3833893,
  -2939036, -2235985,  -420899, -2286327,   183443,  -976891,  1612842, -3545687,
   -554416,  3919660,   -48306, -1362209,  3937738,  1400424,  -846154,  1976782
};

/* Function prototypes */
int32_t montgomery_reduce(int64_t a);
void ntt(int32_t a[N]);

uint32_t loadSamplesIntoSpM(const int32_t *invect, uint32_t len);
void loadZetasIntoSpM(void);
void clearPongBanks(void);
uint32_t readSamplesFromSpM(int32_t *outvect, uint32_t len);
uint32_t compareOutputs(const int32_t *hw, const int32_t *gold, uint32_t len);

void configureStageIndependentRegs(void);
void configureStage(uint32_t stage);

static int32_t vec_in[N];
static int32_t gold_out[N];
static int32_t hw_out[N];

/**
 * @brief Main entry point for 2-CU Radix-2 NTT verification test on Mage.
 *
 * @return int 0 if all 256 output coefficients match Dilithium golden model, non-zero otherwise.
 */
int main(void) {
  // 1. Generate deterministic non-trivial polynomial input coefficients
  for (uint32_t i = 0; i < N; i++) {
    vec_in[i] = (int32_t)(i * 17U) - 123;
    gold_out[i] = vec_in[i];
  }

  // 2. Compute reference Golden Model (CRYSTALS-Dilithium official forward NTT)
  ntt(gold_out);

  // 3. Preload twiddle factors and clear Pong while linear bank addressing is active
  REG_BLOCK_SIZE = 0x00000000U;
  loadZetasIntoSpM();
  clearPongBanks();

  // 4. Configure static, stage-independent Mage control registers (including CPU XOR mode)
  configureStageIndependentRegs();

  // 5. Load input samples into SpM Ping banks linearly (hardware maps XOR-like)
  uint32_t load_errs = loadSamplesIntoSpM(vec_in, N);
  if (load_errs != 0) {
    DRAM_BASE[0] = 0xDEAD0001U;
    DRAM_BASE[1] = load_errs;
    return (int)load_errs;
  }

  // 6. Execute all 8 NTT stages (64 cycles per stage, total 512 execution cycles)
  for (uint32_t stage = 0; stage < 8; stage++) {
    configureStage(stage);

    mage_start();
    mage_wait_done();
  }

  // 7. Read back final processed polynomial from SpM Ping banks linearly
  readSamplesFromSpM(hw_out, N);

  // 8. Validate hardware output against official CRYSTALS-Dilithium golden model
  uint32_t cmperrs = compareOutputs(hw_out, gold_out, N);

  // Report status into DRAM
  DRAM_BASE[0] = (cmperrs == 0) ? 0xCAFE0002U : 0xDEADBEEFU;
  DRAM_BASE[1] = cmperrs;
  DRAM_BASE[2] = (uint32_t)hw_out[0];
  DRAM_BASE[3] = (uint32_t)gold_out[0];
  DRAM_BASE[4] = (uint32_t)hw_out[1];
  DRAM_BASE[5] = (uint32_t)gold_out[1];

  return (int)cmperrs;
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
void ntt(int32_t a[N]) {
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

/**
 * @brief Load input samples into SpM Ping banks linearly.
 *        With Mage CPU XOR mode enabled, the hardware SpM decoder transparently
 *        distributes sequential samples a = 0..255 across Ping banks 0..3
 *        in the conflict-free Radix-2 NTT XOR layout.
 *
 * @param invect Pointer to ordered input array.
 * @param len Number of elements (256).
 * @return uint32_t Number of verification readback mismatches.
 */
uint32_t loadSamplesIntoSpM(const int32_t *invect, uint32_t len) {
  uint32_t errs = 0;
  volatile uint32_t *spm_ping = (volatile uint32_t *)MAGE_DATA_BASE;

  for (uint32_t a = 0; a < len; a++) {
    spm_ping[a] = (uint32_t)invect[a];
  }

  asm volatile("fence w, w" ::: "memory");

  for (uint32_t a = 0; a < len; a++) {
    if (spm_ping[a] != (uint32_t)invect[a]) {
      errs++;
    }
  }

  asm volatile("fence r, r" ::: "memory");
  return errs;
}

/**
 * @brief Preload twiddle factors into Stream 2 (Banks 8 and 9).
 *        Both banks receive all 256 twiddle factors so that CU 0 and CU 1
 *        can access independent twiddles conflict-free in parallel.
 *        Written while linear bank mode is active.
 */
void loadZetasIntoSpM(void) {
  volatile uint32_t *spm_zeta0 = MAGE_BANK(8);
  volatile uint32_t *spm_zeta1 = MAGE_BANK(9);

  for (uint32_t i = 0; i < N; i++) {
    spm_zeta0[i] = (uint32_t)zetas[i];
    spm_zeta1[i] = (uint32_t)zetas[i];
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Clear Pong destination banks with a known poison value to prevent false positives.
 *        Cleared across Banks 4..7 while linear bank mode is active.
 */
void clearPongBanks(void) {
  for (uint32_t b = 4; b < 8; b++) {
    volatile uint32_t *bank = MAGE_BANK(b);
    for (uint32_t i = 0; i < (N / 4U); i++) {
      bank[i] = 0x55AA55AAU;
    }
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Read back processed samples from SpM Ping banks linearly.
 *        With Mage CPU XOR mode enabled, the hardware SpM decoder transparently
 *        gathers samples from the conflict-free XOR bank layout across Banks 0..3
 *        and returns them in natural sequential order a = 0..255.
 *
 * @param outvect Destination buffer.
 * @param len Number of elements (256).
 * @return uint32_t Number of elements read.
 */
uint32_t readSamplesFromSpM(int32_t *outvect, uint32_t len) {
  volatile uint32_t *spm_ping = (volatile uint32_t *)MAGE_DATA_BASE;

  for (uint32_t a = 0; a < len; a++) {
    outvect[a] = (int32_t)spm_ping[a];
  }

  asm volatile("fence r, r" ::: "memory");
  return len;
}

/**
 * @brief Compare hardware output array against golden model array.
 *
 * @param hw Hardware outputs.
 * @param gold Expected golden outputs.
 * @param len Number of elements (256).
 * @return uint32_t Number of mismatches.
 */
uint32_t compareOutputs(const int32_t *hw, const int32_t *gold, uint32_t len) {
  uint32_t errs = 0;
  for (uint32_t i = 0; i < len; i++) {
    if (hw[i] != gold[i]) {
      errs++;
    }
  }
  return errs;
}

/**
 * @brief Configure static, stage-independent Mage control registers for 2 CUs.
 */
void configureStageIndependentRegs(void) {
  /**
   * REG_GEN_CFG: DAE Mode, Iteration Interval (II) = 1
   *   REG_GEN_CFG[18]  = 1 (DAE mode enable)
   *   REG_GEN_CFG[3:0] = 1 (II = 1 cycle per butterfly iteration)
   */
  REG_GEN_CFG = (1U << 18) | 1U;

  /**
   * REG_BLOCK_SIZE: Enable CPU XOR mode for 4 banks (degree = 2)
   *   BLOCK_SIZE[1:0] = 2 (4-way interleaving degree: 2^2 = 4 banks)
   *   BLOCK_SIZE[2]   = 1 (CPU XOR mode enable)
   *   Value: (1 << 2) | 2 = 0x00000006
   */
  REG_BLOCK_SIZE = (1U << MAGE_BLOCK_SIZE_CPU_XOR_MODE_BIT) | 2U;

  /**
   * REG_STREAM_XOR_MODE = 0x00000003
   *   STREAM_XOR_MODE[0] = 1; XOR addressing on Stream 0 (Ping, 4 banks)
   *   STREAM_XOR_MODE[1] = 1; XOR addressing on Stream 1 (Pong, 4 banks)
   *   STREAM_XOR_MODE[2] = 0; Linear addressing on Stream 2 (Zetas, 2 banks)
   */
  REG_STREAM_XOR_MODE = (1U << 1) | (1U << 0);

  /**
   * REG_ILB_HWL = 0x00000000: Start all loop indices from 0
   */
  REG_ILB_HWL = 0x00000000U;

  /**
   * REG_INC_HWL = 0x01010101: Loop index increment = 1
   */
  REG_INC_HWL = 0x01010101U;

  // Set linear stride mode for all 10 AGEs
  for (int i = 0; i < 10; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U;
  }

  // Clear accumulator loop selector registers
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_ACC_HWLP_SEL_1 = 0x00000000U;

  /**
   * Load crossbar and DMEM routing:
   *   Stream 0 (Ping, AGE 0..3): Banks 0..3  -> sel = 0, 1, 2, 3 -> bits [7:0] = 0xE4
   *   Stream 1 (Pong, AGE 4..7): Banks 0..3  -> sel = 0, 1, 2, 3 -> bits [15:8] = 0xE4
   *   Stream 2 (Zeta, AGE 8..9): Banks 0..1  -> sel = 0, 1       -> bits [19:16] = 0x04
   *   Value = 0x0004E4E4
   */
  REG_LOAD_AGE_SEL(0) = 0x0004E4E4U;
  REG_PEA_DMEM_SEL(0) = 0x0004E4E4U;

  /**
   * Output multiplexer selection (REG_OUT_PEA_SEL):
   *   Col 3 (2'b11) selected for all output rows (Rows 0..3 for Ping/Pong):
   *   Bits [19:0] = 0x000FFFFF
   */
  REG_OUT_PEA_SEL = 0x000FFFFFU;

  // Set constants:
  // CU 0: PE(0,1) = QINV, PE(0,2) = Q
  mage_set_pe_constant(QINV_VAL, 0, 1);
  mage_set_pe_constant(Q_VAL, 0, 2);

  // CU 1: PE(2,1) = QINV, PE(2,2) = Q
  mage_set_pe_constant(QINV_VAL, 2, 1);
  mage_set_pe_constant(Q_VAL, 2, 2);

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure hardware loops, strides, AGEs and PEA for a specific NTT stage (2 CUs).
 *
 * @param stage NTT stage index (0 to 7).
 */
void configureStage(uint32_t stage) {
  uint32_t p = 7U - stage;
  uint32_t J = 1U << p;
  bool is_ping_load = ((stage & 1U) == 0U);

  uint32_t flb_reg;
  uint32_t s0, s1;
  uint32_t z0, z1;
  uint32_t base_upper[2];
  uint32_t base_lower[2];
  uint32_t base_zeta[2];

  if (p >= 1U) {
    uint32_t L0 = J / 2U;
    uint32_t L1 = 128U / J;
    flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);
    s0 = 2U;
    s1 = (stage == 0U) ? 0U : (2U * J);

    z0 = 0U;
    z1 = 1U;

    base_upper[0] = 0U;
    base_lower[0] = J;
    base_upper[1] = 1U;
    base_lower[1] = J + 1U;

    base_zeta[0]  = 1U << stage;
    base_zeta[1]  = 1U << stage;
  } else {
    // Stage 7 (p = 0, J = 1 < 2, Exception Branch)
    flb_reg = 63U; // 64 total iterations
    s0 = 4U;
    s1 = 0U;

    z0 = 2U;
    z1 = 0U;

    base_upper[0] = 0U;
    base_lower[0] = 1U;
    base_upper[1] = 2U;
    base_lower[1] = 3U;

    base_zeta[0]  = 128U;
    base_zeta[1]  = 129U;
  }

  // 1. Program Hardware Loop bounds
  REG_ILB_HWL = 0x00000000U;
  REG_FLB_HWL = flb_reg;

  // 2. Program Strides for Ping/Pong AGEs (0..7) and Zeta AGEs (8..9)
  uint32_t ping_pong_stride = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
  uint32_t zeta_stride      = (z0 & 0xFFU) | ((z1 & 0xFFU) << 8);

  for (int i = 0; i < 8; i++) {
    REG_STRIDES(i) = ping_pong_stride;
  }
  REG_STRIDES(8) = zeta_stride;
  REG_STRIDES(9) = zeta_stride;

  // 3. Clear AGE configuration registers
  for (int i = 0; i < 10; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /**
   * 4. Configure AGE instructions:
   *    n_banks = 2 (for 4 banks in Stream 0 and Stream 1)
   *    Upper/Lower Load:  delay = 0, is_load = 1
   *    Upper/Lower Store: delay = 7, is_load = 0
   *    Zeta Load:         delay = 0, is_load = 1, n_banks = 0 (1 bank per CU)
   */
  for (uint32_t u = 0; u < 2; u++) {
    uint32_t load_u  = (1U << 31) | (base_upper[u] << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);
    uint32_t load_l  = (1U << 31) | (base_lower[u] << 15) | (1U << 13) | (2U << 7) | (4U << 4) | (0U << 0);
    uint32_t store_u = (1U << 31) | (base_upper[u] << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (7U << 0);
    uint32_t store_l = (1U << 31) | (base_lower[u] << 15) | (0U << 13) | (2U << 7) | (4U << 4) | (7U << 0);
    uint32_t load_z  = (1U << 31) | (base_zeta[u]  << 15) | (1U << 13) | (u << 9) | (0U << 7) | (4U << 4) | (0U << 0);

    if (is_ping_load) {
      REG_CFG_MAGE_AGE(2 * u)         = load_u;   // Ping upper
      REG_CFG_MAGE_AGE(2 * u + 1)     = load_l;   // Ping lower
      REG_CFG_MAGE_AGE(4 + 2 * u)     = store_u;  // Pong upper
      REG_CFG_MAGE_AGE(4 + 2 * u + 1) = store_l;  // Pong lower
    } else {
      REG_CFG_MAGE_AGE(4 + 2 * u)     = load_u;   // Pong upper
      REG_CFG_MAGE_AGE(4 + 2 * u + 1) = load_l;   // Pong lower
      REG_CFG_MAGE_AGE(2 * u)         = store_u;  // Ping upper
      REG_CFG_MAGE_AGE(2 * u + 1)     = store_l;  // Ping lower
    }
    REG_CFG_MAGE_AGE(8 + u) = load_z;
  }

  /**
   * 5. Program PEA Configuration for both CUs:
   *    CU 0: Rows 0 and 1
   *    CU 1: Rows 2 and 3
   */
  uint32_t mem_in_sample = is_ping_load ? MAGE_PE_MUX_MEM_IN_0 : MAGE_PE_MUX_MEM_IN_1;

  for (uint32_t u = 0; u < 2; u++) {
    uint32_t r0 = 2U * u;
    uint32_t r1 = 2U * u + 1U;

    // PE(r0, 0): Ingest upper sample into delay pipeline
    REG_CFG_PE(r0, 0) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_NOP,
        mem_in_sample,
        0,
        0,
        MAGE_D_PE_INPUT,
        0,
        mem_in_sample,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r1, 0): MUL0 = a(j+k) * zeta mod 2^32, forward a(j) from D_UP Eastward
    REG_CFG_PE(r1, 0) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_MUL,
        mem_in_sample,
        MAGE_PE_MUX_MEM_IN_2, // Zeta
        0,
        MAGE_D_DELAY_OP,
        MAGE_D_UP,
        0,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r1, 1): MULH0 = (a(j+k) *H zeta), forward a(j) from D_LEFT Eastward
    REG_CFG_PE(r1, 1) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_MULH,
        mem_in_sample,
        MAGE_PE_MUX_MEM_IN_2, // Zeta
        0,
        MAGE_D_DELAY_OP,
        MAGE_D_LEFT,
        0,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r0, 1): MUL1 = MUL0 * QINV mod 2^32, ingest MULH0 from DOWN into delay
    REG_CFG_PE(r0, 1) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_MUL,
        MAGE_PE_MUX_SW,
        MAGE_PE_MUX_CONST,
        0,
        MAGE_D_PE_INPUT,
        0,
        MAGE_PE_MUX_DOWN,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r0, 2): MULH1 = MUL1 *H Q, forward MULH0 from D_LEFT Southward
    REG_CFG_PE(r0, 2) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_MULH,
        MAGE_PE_MUX_LEFT,
        MAGE_PE_MUX_CONST,
        0,
        MAGE_D_DELAY_OP,
        MAGE_D_LEFT,
        0,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r1, 2): SUB0 = MULH0 - MULH1 (Montgomery reduce result W), forward a(j) Eastward
    REG_CFG_PE(r1, 2) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_SUB,
        MAGE_PE_MUX_DELAY_OP,
        MAGE_PE_MUX_UP,
        MAGE_D_UP,
        MAGE_D_DELAY_OP,
        MAGE_D_LEFT,
        0,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r0, 3): ADD0: b(j) = a(j) + W (upper butterfly result)
    REG_CFG_PE(r0, 3) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_ADD,
        MAGE_PE_MUX_DELAY_OP,
        MAGE_PE_MUX_SW,
        MAGE_D_SW,
        MAGE_D_PE_OP_NONE,
        0,
        0,
        MAGE_DELAY_DEPTH_1
    );

    // PE(r1, 3): SUB1: b(j+k) = a(j) - W (lower butterfly result)
    REG_CFG_PE(r1, 3) = MAGE_BUILD_PE_CFG_EXT(
        MAGE_PE_OP_SUB,
        MAGE_PE_MUX_DELAY_OP,
        MAGE_PE_MUX_LEFT,
        MAGE_D_LEFT,
        MAGE_D_PE_OP_NONE,
        0,
        0,
        MAGE_DELAY_DEPTH_1
    );
  }

  asm volatile("fence w, w" ::: "memory");
}
