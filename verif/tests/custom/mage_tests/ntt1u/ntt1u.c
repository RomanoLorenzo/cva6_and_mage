// Copyright 2026 Politecnico di Torino.
// Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
// SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
//
// File: ntt1u.c
// Description: Full 8-stage Radix-2 NTT verification test on Mage CGRA
//              using 1 parallel Computing Unit(s) (2x4 PEA, U = 1, 5 SpM banks)
//              with conflict-free XOR addressing and Montgomery modular reduction.
//              Instrumented with cycle and retired instruction counting for all 19 phases.
//              Execution completes in 1024 cycles (128 cycles/stage x 8 stages).
//              Outputs are validated against the official CRYSTALS-Dilithium CPU golden model.

// MAGE_CFG_HJSON = ips/Mage-and-Rogue/config/dae/cfg_ntt_1u_3stream.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"
#include "mage_regs.h"

#include "encoding.h"

#define NUM_PERF_PHASES 19

typedef enum {
  PHASE_INIT_SPM_WRITE      = 0,
  PHASE_CFG_STAGE_INDEP     = 1,
  PHASE_CFG_STAGE_0         = 2,
  PHASE_EXEC_STAGE_0        = 3,
  PHASE_CFG_STAGE_1         = 4,
  PHASE_EXEC_STAGE_1        = 5,
  PHASE_CFG_STAGE_2         = 6,
  PHASE_EXEC_STAGE_2        = 7,
  PHASE_CFG_STAGE_3         = 8,
  PHASE_EXEC_STAGE_3        = 9,
  PHASE_CFG_STAGE_4         = 10,
  PHASE_EXEC_STAGE_4        = 11,
  PHASE_CFG_STAGE_5         = 12,
  PHASE_EXEC_STAGE_5        = 13,
  PHASE_CFG_STAGE_6         = 14,
  PHASE_EXEC_STAGE_6        = 15,
  PHASE_CFG_STAGE_7         = 16,
  PHASE_EXEC_STAGE_7        = 17,
  PHASE_FINAL_SPM_READ      = 18
} perf_phase_t;

typedef struct {
  uint32_t cycles;
  uint32_t instr;
} phase_metric_t;

static phase_metric_t perf_data[NUM_PERF_PHASES];
static uint32_t start_instret;

#define PERF_START() do { \
  write_csr(mcycle, 0); \
  start_instret = (uint32_t)read_csr(minstret); \
} while (0)

#define PERF_STOP(p) do { \
  perf_data[p].cycles = (uint32_t)read_csr(mcycle); \
  perf_data[p].instr  = (uint32_t)read_csr(minstret) - start_instret; \
} while (0)

#define PERF_MAGIC 0x50455246U // 'PERF'

#define DRAM_BASE             ((volatile uint32_t *)(0x80000000UL))
#define N                     256U
#define Q_VAL                 8380417U  // Modulus q
#define QINV_VAL              58728449U // q^(-1) mod 2^32
#define N_UNITS               1U        // 1 Computing Unit(s)

#define MAGE_START() do { \
  asm volatile("fence rw, rw" ::: "memory"); \
  REG_STATUS = (1U << MAGE_STATUS_START_BIT); \
} while (0)

#define MAGE_WAIT_DONE() do { \
  while (!((REG_STATUS >> MAGE_STATUS_DONE_BIT) & 1U)); \
  asm volatile("fence rw, rw" ::: "memory"); \
} while (0)

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
void loadZetasIntoSpM(void);
void clearPongBanks(void);
uint32_t compareOutputs(const int32_t *hw, const int32_t *gold, uint32_t len);
void configureStageIndependentRegs(void);
void configureStage(uint32_t stage);

static int32_t vec_in[N];
static int32_t gold_out[N];
static int32_t hw_out[N];

int main(void) {
  clear_csr(mcountinhibit, 1); // Unfreeze performance counters

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

  // 4. Set CPU XOR mode for Ping SpM sample access
  REG_STREAM_XOR_MODE = (1U << 1) | (1U << 0);
  REG_BLOCK_SIZE = (1U << MAGE_BLOCK_SIZE_CPU_XOR_MODE_BIT) | 1U;

  // Phase 1: Initial SpM sample write by the CPU
  volatile uint32_t *spm_ping = (volatile uint32_t *)MAGE_DATA_BASE;
  PERF_START();
  for (uint32_t a = 0; a < N; a++) {
    spm_ping[a] = (uint32_t)vec_in[a];
  }
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_INIT_SPM_WRITE);

  // Phase 2: Stage-independent configuration register write
  PERF_START();
  configureStageIndependentRegs();
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_INDEP);

  /* ================= STAGE 0 ================= */
  // Phase 3: Stage 0 stage-dependent configuration register write
  PERF_START();
  configureStage(0);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_0);

  // Phase 4: Stage 0 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_0);

  /* ================= STAGE 1 ================= */
  // Phase 5: Stage 1 stage-dependent configuration register write
  PERF_START();
  configureStage(1);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_1);

  // Phase 6: Stage 1 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_1);

  /* ================= STAGE 2 ================= */
  // Phase 7: Stage 2 stage-dependent configuration register write
  PERF_START();
  configureStage(2);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_2);

  // Phase 8: Stage 2 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_2);

  /* ================= STAGE 3 ================= */
  // Phase 9: Stage 3 stage-dependent configuration register write
  PERF_START();
  configureStage(3);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_3);

  // Phase 10: Stage 3 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_3);

  /* ================= STAGE 4 ================= */
  // Phase 11: Stage 4 stage-dependent configuration register write
  PERF_START();
  configureStage(4);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_4);

  // Phase 12: Stage 4 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_4);

  /* ================= STAGE 5 ================= */
  // Phase 13: Stage 5 stage-dependent configuration register write
  PERF_START();
  configureStage(5);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_5);

  // Phase 14: Stage 5 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_5);

  /* ================= STAGE 6 ================= */
  // Phase 15: Stage 6 stage-dependent configuration register write
  PERF_START();
  configureStage(6);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_6);

  // Phase 16: Stage 6 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_6);

  /* ================= STAGE 7 ================= */
  // Phase 17: Stage 7 stage-dependent configuration register write
  PERF_START();
  configureStage(7);
  asm volatile("fence w, w" ::: "memory");
  PERF_STOP(PHASE_CFG_STAGE_7);

  // Phase 18: Stage 7 execution
  PERF_START();
  mage_start();
  mage_wait_done();
  PERF_STOP(PHASE_EXEC_STAGE_7);

  // Phase 19: Final SpM sample readback by the CPU
  PERF_START();
  for (uint32_t a = 0; a < N; a++) {
    hw_out[a] = (int32_t)spm_ping[a];
  }
  asm volatile("fence r, r" ::: "memory");
  PERF_STOP(PHASE_FINAL_SPM_READ);

  // 8. Validate final output against end-to-end Dilithium golden model
  uint32_t total_errors = compareOutputs(hw_out, gold_out, N);

  // 9. Report status and performance metrics into DRAM
  DRAM_BASE[0] = (total_errors == 0) ? (0xCAFE0000U | N_UNITS) : 0xDEADBEEFU;
  DRAM_BASE[1] = total_errors;
  DRAM_BASE[2] = (uint32_t)hw_out[0];
  DRAM_BASE[3] = (uint32_t)gold_out[0];
  DRAM_BASE[4] = (uint32_t)hw_out[1];
  DRAM_BASE[5] = (uint32_t)gold_out[1];

  DRAM_BASE[10] = PERF_MAGIC;
  for (int i = 0; i < NUM_PERF_PHASES; i++) {
    DRAM_BASE[12 + 2 * i]     = perf_data[i].cycles;
    DRAM_BASE[12 + 2 * i + 1] = perf_data[i].instr;
  }

  return (int)total_errors;
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

void loadZetasIntoSpM(void) {
  volatile uint32_t *spm_zeta = (volatile uint32_t *)(MAGE_DATA_BASE + 4 * BANK_SIZE_BYTES);
  for (uint32_t i = 0; i < N; i++) {
    spm_zeta[i] = (uint32_t)zetas[i];
  }
  asm volatile("fence w, w" ::: "memory");
}

void clearPongBanks(void) {
  for (uint32_t b = 2; b < 4; b++) {
    volatile uint32_t *bank = (volatile uint32_t *)(MAGE_DATA_BASE + b * BANK_SIZE_BYTES);
    for (uint32_t i = 0; i < (N / 2U); i++) {
      bank[i] = 0x55AA55AAU;
    }
  }
  asm volatile("fence w, w" ::: "memory");
}

uint32_t compareOutputs(const int32_t *hw, const int32_t *gold, uint32_t len) {
  uint32_t errs = 0;
  for (uint32_t i = 0; i < len; i++) {
    if (hw[i] != gold[i]) {
      errs++;
    }
  }
  return errs;
}

void configureStageIndependentRegs(void) {
  REG_GEN_CFG = (1U << 18) | 1U;
  REG_BLOCK_SIZE = (1U << MAGE_BLOCK_SIZE_CPU_XOR_MODE_BIT) | 1U;
  REG_STREAM_XOR_MODE = (1U << 1) | (1U << 0);
  REG_ILB_HWL = 0x00000000U;
  REG_INC_HWL = 0x01010101U;

  for (int i = 0; i < 5; i++) {
    REG_STRIDE_MODE(i) = 0x00000000U;
  }
  REG_ACC_HWLP_SEL_0 = 0x00000000U;
  REG_LOAD_AGE_SEL(0) = 0x00000044U;
  REG_PEA_DMEM_SEL(0) = 0x00000044U;
  REG_OUT_PEA_SEL = (3U << 6) | (3U << 4) | (3U << 2) | (3U << 0);
  mage_set_pe_constant(QINV_VAL, 0, 1);
  mage_set_pe_constant(Q_VAL,    0, 2);

  for (uint32_t cu = 0; cu < N_UNITS; cu++) {
    uint32_t r0 = 2U * cu;
    uint32_t r1 = 2U * cu + 1U;
    REG_CFG_PE(r0, 1) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_MUL,  MAGE_PE_MUX_SW,       MAGE_PE_MUX_CONST, 0, MAGE_D_PE_INPUT, 0, MAGE_PE_MUX_DOWN, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r0, 2) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_MULH, MAGE_PE_MUX_LEFT,     MAGE_PE_MUX_CONST, 0, MAGE_D_DELAY_OP, MAGE_D_LEFT, 0, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r1, 2) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_SUB,  MAGE_PE_MUX_DELAY_OP, MAGE_PE_MUX_UP,   MAGE_D_UP, MAGE_D_DELAY_OP, MAGE_D_LEFT, 0, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r0, 3) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_ADD,  MAGE_PE_MUX_DELAY_OP, MAGE_PE_MUX_SW,   MAGE_D_SW, MAGE_D_PE_OP_NONE, 0, 0, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r1, 3) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_SUB,  MAGE_PE_MUX_DELAY_OP, MAGE_PE_MUX_LEFT, MAGE_D_LEFT, MAGE_D_PE_OP_NONE, 0, 0, MAGE_DELAY_DEPTH_1);
  }
  asm volatile("fence w, w" ::: "memory");
}


void configureStage(uint32_t stage) {
  uint32_t p = 7U - stage;
  uint32_t J = 1U << p;
  bool is_ping_load = ((stage & 1U) == 0U);

  uint32_t flb_reg;
  uint32_t s0, s1;
  uint32_t z0, z1;
  uint32_t base_upper[N_UNITS];
  uint32_t base_lower[N_UNITS];
  uint32_t base_zeta[N_UNITS];

  uint32_t L0 = J;
  uint32_t L1 = 128U / J;
  flb_reg = ((L1 - 1U) << 8) | (L0 - 1U);
  s0 = 1U;
  s1 = (stage == 0U) ? 0U : (2U * J);
  z0 = 0U;
  z1 = 1U;
  base_upper[0] = 0U;
  base_lower[0] = J;
  base_zeta[0]  = 1U << stage;

  REG_ILB_HWL = 0x00000000U;
  REG_FLB_HWL = flb_reg;

  uint32_t ping_pong_stride = (s0 & 0xFFU) | ((s1 & 0xFFU) << 8);
  uint32_t zeta_stride      = (z0 & 0xFFU) | ((z1 & 0xFFU) << 8);

  for (int i = 0; i < 4; i++) {
    REG_STRIDES(i) = ping_pong_stride;
  }
  for (int i = 4; i < 5; i++) {
    REG_STRIDES(i) = zeta_stride;
  }

  uint32_t n_banks_val = 1U;
  for (uint32_t cu = 0; cu < N_UNITS; cu++) {
    uint32_t load_u  = (1U << 31) | (base_upper[cu] << 15) | (1U << 13) | (n_banks_val << 7) | (4U << 4) | (0U << 0);
    uint32_t load_l  = (1U << 31) | (base_lower[cu] << 15) | (1U << 13) | (n_banks_val << 7) | (4U << 4) | (0U << 0);
    uint32_t store_u = (1U << 31) | (base_upper[cu] << 15) | (0U << 13) | (n_banks_val << 7) | (4U << 4) | (7U << 0);
    uint32_t store_l = (1U << 31) | (base_lower[cu] << 15) | (0U << 13) | (n_banks_val << 7) | (4U << 4) | (7U << 0);
    uint32_t load_z  = (1U << 31) | (base_zeta[cu]  << 15) | (1U << 13) | ((cu & 3U) << 9) | (0U << 7) | (4U << 4) | (0U << 0);

    if (is_ping_load) {
      REG_CFG_MAGE_AGE(2 * cu)             = load_u;
      REG_CFG_MAGE_AGE(2 * cu + 1)         = load_l;
      REG_CFG_MAGE_AGE(2 + 2 * cu)     = store_u;
      REG_CFG_MAGE_AGE(2 + 2 * cu + 1) = store_l;
    } else {
      REG_CFG_MAGE_AGE(2 + 2 * cu)     = load_u;
      REG_CFG_MAGE_AGE(2 + 2 * cu + 1) = load_l;
      REG_CFG_MAGE_AGE(2 * cu)             = store_u;
      REG_CFG_MAGE_AGE(2 * cu + 1)         = store_l;
    }
    REG_CFG_MAGE_AGE(4 + cu) = load_z;
  }

  uint32_t mem_in_sample = is_ping_load ? MAGE_PE_MUX_MEM_IN_0 : MAGE_PE_MUX_MEM_IN_1;
  for (uint32_t cu = 0; cu < N_UNITS; cu++) {
    uint32_t r0 = 2U * cu;
    uint32_t r1 = 2U * cu + 1U;
    REG_CFG_PE(r0, 0) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_NOP,  mem_in_sample, 0, 0, MAGE_D_PE_INPUT, 0, mem_in_sample, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r1, 0) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_MUL,  mem_in_sample, MAGE_PE_MUX_MEM_IN_2, 0, MAGE_D_DELAY_OP, MAGE_D_UP,   0, MAGE_DELAY_DEPTH_1);
    REG_CFG_PE(r1, 1) = MAGE_BUILD_PE_CFG_EXT(MAGE_PE_OP_MULH, mem_in_sample, MAGE_PE_MUX_MEM_IN_2, 0, MAGE_D_DELAY_OP, MAGE_D_LEFT, 0, MAGE_DELAY_DEPTH_1);
  }

  asm volatile("fence w, w" ::: "memory");
}
