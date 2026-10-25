// MAGE_CFG_HJSON = ips/Mage-and-Rogue/config/dae/cfg_2x2_2x2_full.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#define DRAM_BASE ((volatile uint32_t *)(0x80000000UL)) // DRAM base address

/*=============================================================================
 * BASE ADDRESS AND CONTROL REGISTER DEFINITIONS (MageCtrl @ 0x5000_0000)
 *===========================================================================*/
#define MAGE_CTRL_BASE      0x50000000UL // Control registers base address
#define MAGE_DATA_BASE      0xF0000000UL // SpM base address

#define BANK_SIZE_BYTES     0x1000U      // 1024 32-bit words = 4096 bytes
#define MAGE_BANK(n)        ((volatile uint32_t *)(MAGE_DATA_BASE + (n) * BANK_SIZE_BYTES))

/*=============================================================================
 * CONTROL REGISTER DEFINITIONS
 *===========================================================================*/
#define REG_STATUS          (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x00))
#define REG_GEN_CFG         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x04))
#define REG_ILB_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x08))
#define REG_BLOCK_SIZE      (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x0C))
#define REG_FLB_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x10))
#define REG_INC_HWL         (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x14))

#define REG_STRIDES_0       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x18))
#define REG_STRIDES_1       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x1C))
#define REG_STRIDES_2       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x20))
#define REG_STRIDES_3       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x24))

#define REG_STRIDE_MODE_0   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x28))
#define REG_STRIDE_MODE_1   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x2C))
#define REG_STRIDE_MODE_2   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x30))
#define REG_STRIDE_MODE_3   (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x34))

#define REG_ACC_HWLP_SEL_0  (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x38))

#define REG_CFG_PE_00       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x3C))
#define REG_CFG_PE_01       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x40))
#define REG_CFG_PE_10       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x44))
#define REG_CFG_PE_11       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x48))

#define REG_OUT_PEA_SEL     (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x4C))
#define REG_LOAD_AGE_SEL    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x50))
#define REG_PEA_DMEM_SEL    (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x54))

#define REG_CFG_AGE_0       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x58))
#define REG_CFG_AGE_1       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x5C))
#define REG_CFG_AGE_2       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x60))
#define REG_CFG_AGE_3       (*(volatile uint32_t *)(MAGE_CTRL_BASE + 0x64))

#define VECTOR_LEN          8 // Number of samples to transfer

int main(void) {
  volatile uint32_t *dramptr = DRAM_BASE;
  uint32_t inerrors = 0;
  uint32_t outerrors = 0;

  /*==========================================================================
   * STEP 1: Create two sample vectors in host memory
   *========================================================================*/
  uint32_t vec_in_0[VECTOR_LEN] = {0x00000000, 0x11111111, 0x22222222, 0x33333333,
                                   0x44444444, 0x55555555, 0x66666666, 0x77777777};
  uint32_t vec_in_1[VECTOR_LEN] = {0x88888888, 0x99999999, 0xaaaaaaaa, 0xbbbbbbbb,
                                   0xcccccccc, 0xdddddddd, 0xeeeeeeee, 0xffffffff};

  /*==========================================================================
   * STEP 2: Copy vectors to Mage SpM input banks
   *========================================================================*/
  // Action: Direct MMIO write of samples into SpM banks.
  // Rationale: Mage directly accesses its multi-bank SpM;
  // Bank 0 will hold vec_in_0 and Bank 1 will hold vec_in_1.
  volatile uint32_t *spm_bank0 = MAGE_BANK(0);
  volatile uint32_t *spm_bank1 = MAGE_BANK(1);
  for (int i = 0; i < VECTOR_LEN; i++) {
    spm_bank0[i] = vec_in_0[i];
    spm_bank1[i] = vec_in_1[i];
  }

  asm volatile("fence w, w" ::: "memory");

  // Perform verification check
  volatile uint32_t spm_bank0_readback[VECTOR_LEN];
  volatile uint32_t spm_bank1_readback[VECTOR_LEN];

  for (int i = 0; i < VECTOR_LEN; i++) {
    spm_bank0_readback[i] = spm_bank0[i];
    spm_bank1_readback[i] = spm_bank1[i];

    if (spm_bank0_readback[i] != vec_in_0[i] || 
        spm_bank1_readback[i] != vec_in_1[i]) {
      inerrors++;
    }
  }
  asm volatile("fence r, r" ::: "memory");

  // Write to DRAM for debug
  for (int i = 0; i < VECTOR_LEN; i++) {
    DRAM_BASE[i] = spm_bank0_readback[i];
    DRAM_BASE[i + VECTOR_LEN] = spm_bank1_readback[i];
  }
  asm volatile("fence w, w" ::: "memory");

  /*==========================================================================
   * STEP 3: Configure Mage Control Registers
   *========================================================================*/

  // --- 3.1 General Configuration ---
  // Action: Set II = 0 and fully static mode.
  // Rationale: No temporal instruction multiplexing; the pipeline
  // executes a continuous cycle-by-cycle data stream.
  REG_GEN_CFG = 0x00000000U;

  // --- 3.2 Hardware Loops (HWLP) ---
  // Action: Initialize the lower bound of all loops to 0.
  // Rationale: The load/store loop must iterate starting from index 0.
  REG_ILB_HWL = 0x00000000U;

  // Action: Set the final bound of Loop 0 (innermost) to (VECTOR_LEN - 1) = 7.
  // Rationale: Mage hardware loops evaluate loop bounds inclusively (0..7),
  // executing exactly 8 iterations (one sample transferred per cycle).
  REG_FLB_HWL = (uint32_t)(VECTOR_LEN - 1);

  // Action: Set the Loop 0 increment step to 1.
  // Rationale: The index must increment by one element every clock cycle.
  REG_INC_HWL = 0x00000001U;

  // --- 3.3 AGE Strides and Addressing Modes ---
  // Action: Set Loop 0 stride to 1 word (4 bytes) for all 4 AGEs.
  // Rationale: At each iteration, each AGE engine must point to the next word
  // of its respective bank (sequential access).
  REG_STRIDES_0 = 0x00000001U; // AGE 0 (Stream 0, Load Bank 0)
  REG_STRIDES_1 = 0x00000001U; // AGE 1 (Stream 0, Load Bank 1)
  REG_STRIDES_2 = 0x00000001U; // AGE 2 (Stream 1, Store Bank 2)
  REG_STRIDES_3 = 0x00000001U; // AGE 3 (Stream 1, Store Bank 3)

  // Action: Select linear mode for all AGEs.
  // Rationale: Flat sequential addressing (Addr = Base + 1 * i), without tiling.
  REG_STRIDE_MODE_0 = 0x00000000U;
  REG_STRIDE_MODE_1 = 0x00000000U;
  REG_STRIDE_MODE_2 = 0x00000000U;
  REG_STRIDE_MODE_3 = 0x00000000U;

  // --- 3.4 AGE Configuration Instructions ---
  // AGE instruction layout:
  // [31]     isActive = 1
  // [30:23]  ageIvConstraint = 0
  // [22:15]  ivConst = 0 (base offset)
  // [14]     isAccStore = 0
  // [13]     loadNotStore (1 = LOAD, 0 = STORE)
  // [12:11]  blockSize = 0
  // [10:9]   startBank (group-relative bank index: 0 or 1)
  // [8:7]    numBanks = 0 (1 bank)
  // [6:4]    selIvConstraint = 4 (4 = UNCONSTRAINED, issues an address every iteration)
  // [3:0]    hwlp = 0 (driven by Loop 0)

  // AGE 0: LOAD from Bank 0 (Group 0, Relative Bank 0)
  // Action: Enable AGE 0 in unconstrained LOAD mode on Loop 0.
  // Rationale: Must generate read addresses for vec_in_0 from Bank 0.
  REG_CFG_AGE_0 = (1U << 31) | (1U << 13) | (0U << 9) | (4U << 4) | (0U << 0);

  // AGE 1: LOAD from Bank 1 (Group 0, Relative Bank 1 -> bit 9 = 1)
  // Action: Enable AGE 1 in LOAD mode on Bank 1.
  // Rationale: Must generate read addresses for vec_in_1 from Bank 1.
  REG_CFG_AGE_1 = (1U << 31) | (1U << 13) | (1U << 9) | (4U << 4) | (0U << 0);

  // AGE 2: STORE to Bank 2 (Group 1, Relative Bank 0 -> loadNotStore = 0)
  // Action: Enable AGE 2 in STORE mode for the first destination bank.
  // Rationale: Must generate write addresses into Bank 2 for the output stream.
  REG_CFG_AGE_2 = (1U << 31) | (0U << 13) | (0U << 9) | (4U << 4) | (3U << 0);

  // AGE 3: STORE to Bank 3 (Group 1, Relative Bank 1 -> startBank = 1, bit 9 = 1)
  // Action: Enable AGE 3 in STORE mode for the second destination bank.
  // Rationale: Must generate write addresses into Bank 3 for the second stream.
  REG_CFG_AGE_3 = (1U << 31) | (0U << 13) | (1U << 9) | (4U << 4) | (3U << 0);

  // --- 3.5 Load Stream Crossbar (Banks -> PEA Connection) ---
  // Action: Assign AGE 0 to port 0 (bits [1:0] = 0) and AGE 1 to port 1 (bits [3:2] = 1).
  // Rationale: PEA port MEM_IN_0 will receive data read from Bank 0 via AGE 0,
  // and MEM_IN_1 will receive data read from Bank 1 via AGE 1.
  REG_LOAD_AGE_SEL = (1U << 2) | (0U << 0);

  // --- 3.6 Processing Element Array (PEA) Configuration ---
  // PE instruction layout: [12:8] opcode, [7:4] input B, [3:0] input A
  // Opcode FW_A = 11 (0x0B). Input MEM_IN_0 = 1, MEM_IN_1 = 2, CONSTANT = 0.

  // PE 0,0: Forwarding of MEM_IN_0
  // Action: Set PE 0,0 to FW_A with input A = MEM_IN_0.
  // Rationale: Forwards vec_in_0 data to its output register without modifying its value.
  REG_CFG_PE_00 = (0x0BU << 8) | (0U << 4) | (1U << 0);

  // PE 1,0: Forwarding of MEM_IN_1
  // Action: Set PE 1,0 to FW_A with input A = MEM_IN_1.
  // Rationale: Forwards vec_in_1 data to its output register without modifying its value.
  REG_CFG_PE_10 = (0x0BU << 8) | (0U << 4) | (2U << 0);

  // PE 0,1 and PE 1,1 unused
  REG_CFG_PE_01 = 0x00000000U; // NOP
  REG_CFG_PE_11 = 0x00000000U; // NOP

  // --- 3.7 PEA Row Output Selection ---
  // Layout: bits [1:0] = Row 0 Left Out,  bits [3:2] = Row 1 Left Out,
  //         bits [5:4] = Row 0 Right Out, bits [7:6] = Row 1 Right Out.
  // Action: Select column 0 for Row 0 Right Out and column 0 for Row 1.
  // Rationale: Right outputs feed the Stream 1 crossbar (banks 2 and 3).
  // Row 0 right output must sample PE 0,0 (column 0) and Row 1 right output must sample PE 1,0 (column 0).
  REG_OUT_PEA_SEL = (0U << 6) | (0U << 4) | (0U << 2) | (0U << 0);

  // --- 3.8 Store Stream Crossbar (PEA -> Output Banks Connection) ---
  // Action: Assign Row 0 output (PE 0,0) to Group 1 Bank 0 (Bank 2, bits [5:4]=0),
  // and Row 1 output (PE 1,0) to Group 1 Bank 1 (Bank 3, bits [7:6]=1 -> 1 << 6 = 0x40).
  // Rationale: Physically routes data forwarded by PE 0,0 into Bank 2 and from PE 1,0 into Bank 3.
  REG_PEA_DMEM_SEL = (1U << 6) | (0U << 4);

  asm volatile("fence w, w" ::: "memory");

  // --- 3.9 Execution and Completion Wait ---
  // Action: Write 1 to the START bit of the status register.
  // Rationale: Transitions the Mage FSM from IDLE to EXEC, activating the AGU and PEA.
  REG_STATUS = (1U << 0);

  // Action: Poll the DONE bit (bit 1) of REG_STATUS.
  // Rationale: Synchronizes the CVA6 core with the completion of hardware execution before reading results.
  while (!((REG_STATUS >> 1) & 0x1U)) {
    // Wait for completion
  }

  asm volatile("fence r, r" ::: "memory");

  /*==========================================================================
   * STEP 4: Read from output banks and verify correctness
   *========================================================================*/
  volatile uint32_t *spm_bank2 = MAGE_BANK(2);
  volatile uint32_t *spm_bank3 = MAGE_BANK(3);

  volatile uint32_t spm_bank2_readback[VECTOR_LEN];
  volatile uint32_t spm_bank3_readback[VECTOR_LEN];

  for (int i = 0; i < VECTOR_LEN; i++) {
    spm_bank2_readback[i] = spm_bank2[i];
    spm_bank3_readback[i] = spm_bank3[i];

    if (spm_bank2_readback[i] != vec_in_0[i] || 
        spm_bank3_readback[i] != vec_in_1[i]) {
      outerrors++;
    }
  }
  asm volatile("fence r, r" ::: "memory");

  // Write to DRAM for debug
  for (int i = 0; i < VECTOR_LEN; i++) {
    DRAM_BASE[i + 2 * VECTOR_LEN] = spm_bank2_readback[i];
    DRAM_BASE[i + 3 * VECTOR_LEN] = spm_bank3_readback[i];
  }

  // Write the number of errors
  DRAM_BASE[4 * VECTOR_LEN] = inerrors;
  DRAM_BASE[4 * VECTOR_LEN + 1] = outerrors;

  asm volatile("fence w, w" ::: "memory");

  return 0;
}