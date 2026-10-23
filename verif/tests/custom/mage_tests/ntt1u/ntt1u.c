// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: ntt1u.c
// Description: Full 8-stage Radix-2 NTT verification test on Mage CGRA
//              using 1 Computing Unit (2x4 PEA) with conflict-free XOR addressing
//              and Montgomery modular reduction.
//              Outputs are validated against the official CRYSTALS-Dilithium CPU golden model.

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"

#define DRAM_BASE             ((volatile uint32_t *)(0x80000000UL))
#define N                     256U
#define Q_VAL                 8380417U  // Modulus q
#define QINV_VAL              58728449U // q^(-1) mod 2^32

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

static inline uint32_t get_sn_1u(uint32_t a);
static inline uint32_t get_bi_1u(uint32_t a);
static inline uint32_t get_ba_1u(uint32_t a);

uint32_t loadSamplesIntoSpM(const int32_t *invect, uint32_t len);
void loadZetasIntoSpM(void);
void clearPongBanks(void);
uint32_t readSamplesFromSpM(int32_t *outvect, uint32_t len);
uint32_t compareOutputs(const int32_t *hw, const int32_t *gold, uint32_t len);

void configureStageIndependentRegs(void);
void configureStage(uint32_t stage);

int main(void) {
  int32_t vec_in[N];
  int32_t gold_out[N];
  int32_t hw_out[N];

  // 1. Generate deterministic non-trivial polynomial input coefficients
  for (uint32_t i = 0; i < N; i++) {
    vec_in[i] = (int32_t)(i * 17U) - 123;
    gold_out[i] = vec_in[i];
  }

  // 2. Compute reference Golden Model (CRYSTALS-Dilithium official forward NTT)
  ntt(gold_out);

  // 3. Load input samples into SpM Ping banks according to XOR addressing
  uint32_t load_errs = loadSamplesIntoSpM(vec_in, N);
  if (load_errs != 0) {
    DRAM_BASE[0] = 0xDEAD0001U;
    DRAM_BASE[1] = load_errs;
    return (int)load_errs;
  }

  // 4. Preload all 1024 twiddle factors into Bank 4 (Zeta stream)
  loadZetasIntoSpM();

  // 5. Clear Pong destination banks with poison pattern
  clearPongBanks();

  // 6. Configure static, stage-independent Mage control registers
  configureStageIndependentRegs();

  // 7. Execute all 8 NTT stages sequentially
  for (uint32_t stage = 0; stage < 8; stage++) {
    configureStage(stage);
    mage_start();
    mage_wait_done();
  }

  // 8. Read back final transformed coefficients from Ping banks
  readSamplesFromSpM(hw_out, N);

  // 9. Compare hardware output against official Dilithium golden model
  uint32_t cmperrs = compareOutputs(hw_out, gold_out, N);

  // 10. Write results to DRAM and return error count
  DRAM_BASE[0] = (cmperrs == 0) ? 0xCAFE0001U : 0xDEADBEEFU;
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
 * @brief Load input samples into SpM Ping banks according to XOR pattern.
 *
 * @param invect Pointer to ordered input array.
 * @param len Number of elements (256).
 * @return uint32_t Number of verification readback mismatches.
 */
uint32_t loadSamplesIntoSpM(const int32_t *invect, uint32_t len) {
  uint32_t errs = 0;

  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    bank[ba] = (uint32_t)invect[a];
  }

  asm volatile("fence w, w" ::: "memory");

  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    if (bank[ba] != (uint32_t)invect[a]) {
      errs++;
    }
  }

  asm volatile("fence r, r" ::: "memory");
  return errs;
}

/**
 * @brief Preload twiddle factors into Bank 4 (Zeta stream).
 *        Direct 1:1 mapping: Bank 4[k] = zetas[k] for k = 0..255 (256 words).
 */
void loadZetasIntoSpM(void) {
  volatile uint32_t *bank4 = MAGE_BANK(4);
  for (uint32_t i = 0; i < N; i++) {
    bank4[i] = (uint32_t)zetas[i];
  }
  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Clear Pong destination banks with a known poison value to prevent false positives.
 */
void clearPongBanks(void) {
  volatile uint32_t *bank2 = MAGE_BANK(2);
  volatile uint32_t *bank3 = MAGE_BANK(3);

  for (uint32_t i = 0; i < 128U; i++) {
    bank2[i] = 0x55AA55AAU;
    bank3[i] = 0x55AA55AAU;
  }

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Read back processed samples from SpM Ping banks according to XOR pattern.
 *
 * @param outvect Destination buffer.
 * @param len Number of elements (256).
 * @return uint32_t Number of elements read.
 */
uint32_t readSamplesFromSpM(int32_t *outvect, uint32_t len) {
  for (uint32_t a = 0; a < len; a++) {
    uint32_t bi = get_bi_1u(a);
    uint32_t ba = get_ba_1u(a);
    volatile uint32_t *bank = MAGE_BANK(bi);
    outvect[a] = (int32_t)bank[ba];
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
 * @brief Configure static, stage-independent Mage control registers.
 */
void configureStageIndependentRegs(void) {
  /**
   * REG_GEN_CFG: DAE Mode, Iteration Interval (II) = 1
   *   REG_GEN_CFG[18]  = 1 (DAE mode enable)
   *   REG_GEN_CFG[3:0] = 1 (II = 1 cycle per butterfly iteration)
   */
  REG_GEN_CFG = (1U << 18) | 1U;

  /**
   * REG_BLOCK_SIZE = 0x00000000
   *   BLOCK_SIZE[1:0] = 0; Linear 1 word per block
   */
  REG_BLOCK_SIZE = 0x00000000U;

  /**
   * REG_STREAM_XOR_MODE = 0x00000003
   *   STREAM_XOR_MODE[0] = 1; XOR addressing on Stream 0 (Ping)
   *   STREAM_XOR_MODE[1] = 1; XOR addressing on Stream 1 (Pong)
   *   STREAM_XOR_MODE[2] = 0; Linear addressing on Stream 2 (Zetas)
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

  // Set linear stride mode for all 5 AGEs
  for (int i = 0; i < 5; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U;
  }

  // Clear accumulator loop selector registers
  REG_ACC_HWLP_SEL_0 = 0x00000000U;

  /**
   * Load crossbar and DMEM routing:
   *   Stream 0 Bank 0 -> AGE 0 (sel = 0)
   *   Stream 0 Bank 1 -> AGE 1 (sel = 1)
   *   Stream 1 Bank 2 -> AGE 2 (sel = 0)
   *   Stream 1 Bank 3 -> AGE 3 (sel = 1)
   *   Stream 2 Bank 4 -> AGE 4 (sel = 0)
   *   REG_LOAD_AGE_SEL(0) = (1U << 6) | (0U << 4) | (1U << 2) | (0U << 0) = 0x00000044
   */
  REG_LOAD_AGE_SEL(0) = 0x00000044U;
  REG_PEA_DMEM_SEL(0) = 0x00000044U;

  /**
   * Output multiplexer selection (REG_OUT_PEA_SEL):
   *   Bits [1:0]: AGE 0 (Row 0) -> Col 3 (PE 0,3: b(j))   -> 2'b11
   *   Bits [3:2]: AGE 1 (Row 1) -> Col 3 (PE 1,3: b(j+k)) -> 2'b11
   *   Bits [5:4]: AGE 2 (Row 0) -> Col 3 (PE 0,3: b(j))   -> 2'b11
   *   Bits [7:6]: AGE 3 (Row 1) -> Col 3 (PE 1,3: b(j+k)) -> 2'b11
   *   REG_OUT_PEA_SEL = 0x000000FF
   */
  REG_OUT_PEA_SEL = (3U << 6) | (3U << 4) | (3U << 2) | (3U << 0);

  // Set constants:
  // PE(0,1): QINV = 58728449
  mage_set_pe_constant(QINV_VAL, 0, 1);
  // PE(0,2): Q = 8380417
  mage_set_pe_constant(Q_VAL, 0, 2);

  asm volatile("fence w, w" ::: "memory");
}

/**
 * @brief Configure hardware loops, strides, AGEs and PEA for a specific NTT stage.
 *
 * @param stage NTT stage index (0 to 7).
 */
void configureStage(uint32_t stage) {
  uint32_t p = 7U - stage;
  uint32_t J = 1U << p;
  bool is_ping_load = ((stage & 1U) == 0U);

  uint32_t L0 = J;
  uint32_t L1 = 128U / J;
  uint32_t flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);

  uint32_t s0 = 1U;
  uint32_t s1 = (stage == 0U) ? 0U : (2U * J);
  uint32_t z0 = 0U;
  uint32_t z1 = 1U;
  uint32_t base_upper = 0U;
  uint32_t base_lower = J;
  uint32_t base_zeta  = 1U << stage;

  // 1. Program Hardware Loop bounds
  REG_ILB_HWL = 0x00000000U;
  REG_FLB_HWL = flb_reg;

  // 2. Program Strides for Ping/Pong AGEs (0..3) and Zeta AGE (4)
  uint32_t ping_pong_stride = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
  uint32_t zeta_stride      = (z0 & 0xFFU) | ((z1 & 0xFFU) << 8);

  REG_STRIDES(0) = ping_pong_stride;
  REG_STRIDES(1) = ping_pong_stride;
  REG_STRIDES(2) = ping_pong_stride;
  REG_STRIDES(3) = ping_pong_stride;
  REG_STRIDES(4) = zeta_stride;

  // 3. Clear AGE configuration registers
  for (int i = 0; i < 5; i++) {
    REG_CFG_MAGE_AGE(i) = 0x00000000U;
  }

  /**
   * 4. Configure AGE instructions:
   *    Upper/Lower Load:  delay = 0, is_load = 1, n_banks = 1 (2 banks interleaved)
   *    Upper/Lower Store: delay = 7, is_load = 0, n_banks = 1 (2 banks interleaved)
   *    Zeta Load:         delay = 0, is_load = 1, n_banks = 0 (1 single bank)
   */
  uint32_t load_upper  = (1U << 31) | (base_upper << 15) | (1U << 13) | (1U << 7) | (4U << 4) | (0U << 0);
  uint32_t load_lower  = (1U << 31) | (base_lower << 15) | (1U << 13) | (1U << 7) | (4U << 4) | (0U << 0);
  uint32_t store_upper = (1U << 31) | (base_upper << 15) | (0U << 13) | (1U << 7) | (4U << 4) | (7U << 0);
  uint32_t store_lower = (1U << 31) | (base_lower << 15) | (0U << 13) | (1U << 7) | (4U << 4) | (7U << 0);
  uint32_t load_zeta   = (1U << 31) | (base_zeta  << 15) | (1U << 13) | (0U << 7) | (4U << 4) | (0U << 0);

  if (is_ping_load) {
    // Ping (Stream 0) -> Pong (Stream 1)
    REG_CFG_MAGE_AGE(0) = load_upper;
    REG_CFG_MAGE_AGE(1) = load_lower;
    REG_CFG_MAGE_AGE(2) = store_upper;
    REG_CFG_MAGE_AGE(3) = store_lower;
  } else {
    // Pong (Stream 1) -> Ping (Stream 0)
    REG_CFG_MAGE_AGE(2) = load_upper;
    REG_CFG_MAGE_AGE(3) = load_lower;
    REG_CFG_MAGE_AGE(0) = store_upper;
    REG_CFG_MAGE_AGE(1) = store_lower;
  }
  REG_CFG_MAGE_AGE(4) = load_zeta;

  /**
   * 5. Program PEA Configuration:
   *    In Ping->Pong: Row 0 reads Ping Upper (MEM_IN_0), Row 1 reads Ping Lower (MEM_IN_0).
   *    In Pong->Ping: Row 0 reads Pong Upper (MEM_IN_1), Row 1 reads Pong Lower (MEM_IN_1).
   *    Both rows always read Zeta from MEM_IN_2.
   */
  uint32_t mem_in_sample = is_ping_load ? MAGE_PE_MUX_MEM_IN_0 : MAGE_PE_MUX_MEM_IN_1;

  // Clear all PE configurations
  for (int r = 0; r < 2; r++) {
    for (int c = 0; c < 4; c++) {
      REG_CFG_PE(r, c) = 0x00000000U;
    }
  }

  // PE(0,0): Ingest upper sample into delay pipeline
  REG_CFG_PE(0, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_NOP,
      mem_in_sample,
      0,
      0,
      MAGE_D_PE_INPUT,
      0,
      mem_in_sample,
      MAGE_DELAY_DEPTH_1
  );

  // PE(1,0): MUL0 = ajk * zeta mod 2^32, forward a(j) from D_UP Eastward
  REG_CFG_PE(1, 0) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MUL,
      mem_in_sample,
      MAGE_PE_MUX_MEM_IN_2, // Zeta
      0,
      MAGE_D_DELAY_OP,
      MAGE_D_UP,
      0,
      MAGE_DELAY_DEPTH_1
  );

  // PE(1,1): MULH0 = (ajk *H zeta), forward a(j) from D_LEFT Eastward
  REG_CFG_PE(1, 1) = MAGE_BUILD_PE_CFG_EXT(
      MAGE_PE_OP_MULH,
      mem_in_sample,
      MAGE_PE_MUX_MEM_IN_2, // Zeta
      0,
      MAGE_D_DELAY_OP,
      MAGE_D_LEFT,
      0,
      MAGE_DELAY_DEPTH_1
  );

  // PE(0,1): MUL1 = MUL0 * QINV mod 2^32, ingest MULH0 from DOWN into delay
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

  // PE(0,2): MULH1 = MUL1 *H Q, forward MULH0 from D_LEFT Southward
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

  // PE(1,2): SUB0 = MULH0 - MULH1 (Montgomery reduce result W), forward a(j) Eastward
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

  // PE(0,3): ADD0: b(j) = a(j) + W (upper butterfly result)
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

  // PE(1,3): SUB1: b(j+k) = a(j) - W (lower butterfly result)
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
