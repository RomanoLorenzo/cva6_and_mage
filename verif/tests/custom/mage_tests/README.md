# Custom Mage Verification Tests

This directory contains standalone bare-metal C test applications designed to run on the CVA6 processor and exercise the integrated Mage CGRA accelerator.

---

## Directory Overview

| Test Directory | Target Configuration | Description |
| :--- | :--- | :--- |
| **`ntt4u/`** | `cfg_ntt_4u_3stream.hjson` | Full 8-stage forward Radix-2 NTT on CRYSTALS-Dilithium polynomials using 4 parallel CUs (8x4 PEA, 20 SpM banks). Verified in 256 cycles. |
| **`ntt2u/`** | `cfg_ntt_2u_3stream.hjson` | Full 8-stage forward Radix-2 NTT using 2 parallel CUs (4x4 PEA, 10 SpM banks). Verified in 512 cycles. |
| **`ntt1u/`** | `cfg_ntt_1u_3stream.hjson` | Full 8-stage forward Radix-2 NTT using 1 CU (2x4 PEA, 5 SpM banks). Verified in 1024 cycles. |
| **`b1u/`** | `cfg_ntt_1cu_3stream.hjson` | Functional verification of a single Radix-2 butterfly with Montgomery reduction across 8 iterations. |
| **`bank_transfer/`** | `cfg_2x2_2x2_full.hjson` | Basic data movement and bank copy test across Mage Scratchpad Memory. |
| **`xor_cpu_test/`** | `cfg_ntt_1u_3stream.hjson` | Verification of transparent hardware XOR mapping during linear CPU read and write operations. |
| **`xor1u/`, `xor2u/`, `xor4u/`** | `cfg_ntt_*u_3stream.hjson` | Conflict-free bank address generation and verification for 1, 2, and 4 parallel computing units. |

---

## How to Write a New Mage Test

### 1. Tag Your Target Hardware Configuration
To enable automated pre-simulation validation, specify the hardware configuration your test expects near the top of your C source file (after the license header and before `#include` directives):

```C
// MAGE_CFG_HJSON = ips/Mage-and-Rogue/config/dae/<your_config>.hjson

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

#include "mage.h"
#include "mage_cva6.h"
#include "mage_regs.h"
```

### 2. Status and Error Reporting Convention
Tests communicate execution results to the simulation harness through the base of DRAM:
- `DRAM_BASE[0] = 0xCAFE0000 | <unit_id>` on success (or `0xDEADBEEF` on error).
- `DRAM_BASE[1] = total_error_count` (0 on success).
- `return (int)total_error_count;` from `main()`. A return code of `0` signals `*** SUCCESS *** (tohost = 0)` to the simulation harness.

---

## How to Run Simulations

From the `verif/sim` directory:

```Bash
make magesim TAR=<test_dir>/<test_file>.c
```

For example, to run the 4-CU NTT test:
```Bash
make magesim TAR=ntt4u/ntt4u.c
```

Before compiling, the build system automatically inspects your test's `MAGE_CFG_HJSON` tag:
- If the test matches the configuration attached to the SoC, it prints a confirmation and proceeds with the simulation.
- If there is a mismatch (e.g. running a 1-CU test while the 4-CU configuration is attached), it displays a prominent warning detailing the discrepancy.

