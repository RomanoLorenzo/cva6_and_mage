#!/usr/bin/env bash

# -----------------------------------------------------------------------------
# 1. Sourcing Guard (Bash-only)
# -----------------------------------------------------------------------------
if [[ "${BASH_SOURCE[0]}" == "${0}" ]]; then
    echo "[ERROR] Please source this script instead of executing it:"
    echo "        source ${BASH_SOURCE[0]}"
    exit 1 2>/dev/null || return 1
fi

# Canonical Root Directory
export ROOT_PROJECT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

# -----------------------------------------------------------------------------
# 2. RISC-V Toolchain Configuration
# -----------------------------------------------------------------------------
export RISCV="${RISCV:-$ROOT_PROJECT/tools/riscv}"
export INSTALL_DIR="$RISCV"

if [ ! -d "$RISCV" ]; then
    echo "[WARNING] RISCV directory does not exist: $RISCV"
fi

export LIBRARY_PATH="$RISCV/lib${LIBRARY_PATH:+:$LIBRARY_PATH}"
export LD_LIBRARY_PATH="$RISCV/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export C_INCLUDE_PATH="$RISCV/include${C_INCLUDE_PATH:+:$C_INCLUDE_PATH}"
export CPLUS_INCLUDE_PATH="$RISCV/include${CPLUS_INCLUDE_PATH:+:$CPLUS_INCLUDE_PATH}"

# Auto-detect RISC-V toolchain executable prefix (e.g., riscv64-unknown-elf-)
if [ -z "$CV_SW_PREFIX" ] && [ -d "$RISCV/bin" ]; then
    DETECTED_BIN="$(ls -1 "$RISCV/bin"/riscv* 2>/dev/null | head -n 1)"
    if [ -n "$DETECTED_BIN" ]; then
        export CV_SW_PREFIX="$(echo "$DETECTED_BIN" | rev | cut -d '/' -f 1 | cut -d '-' -f 2- | rev)-"
    fi
fi

export RISCV_CC="${RISCV_CC:-$RISCV/bin/${CV_SW_PREFIX}gcc}"
export RISCV_OBJCOPY="${RISCV_OBJCOPY:-$RISCV/bin/${CV_SW_PREFIX}objcopy}"

# -----------------------------------------------------------------------------
# 3. Verification & Design Tree Paths
# -----------------------------------------------------------------------------
export RTL_PATH="$ROOT_PROJECT"
export TB_PATH="$ROOT_PROJECT/verif/tb/core"
export TESTS_PATH="$ROOT_PROJECT/verif/tests"
export RISCV_DV_ROOT="$ROOT_PROJECT/verif/sim/dv"
export CVA6_DV_ROOT="$ROOT_PROJECT/verif/env/corev-dv"
# export DV_SIMULATORS="veri-testharness,spike"
export DV_SIMULATORS="veri-testharness"

# -----------------------------------------------------------------------------
# 4. Simulators (Verilator & Spike)
# -----------------------------------------------------------------------------
if [ -z "$VERILATOR_INSTALL_DIR" ]; then
    export VERILATOR_INSTALL_DIR="$ROOT_PROJECT/tools/verilator-v5.008"
fi

if [ -z "$SPIKE_SRC_DIR" ] || [ "$SPIKE_INSTALL_DIR" = "__local__" ]; then
    export SPIKE_SRC_DIR="$ROOT_PROJECT/verif/core-v-verif/vendor/riscv/riscv-isa-sim"
fi

if [ -z "$SPIKE_INSTALL_DIR" ] || [ "$SPIKE_INSTALL_DIR" = "__local__" ]; then
    export SPIKE_INSTALL_DIR="$ROOT_PROJECT/tools/spike"
fi

export SPIKE_PATH="$SPIKE_INSTALL_DIR/bin"

# -----------------------------------------------------------------------------
# 4.1 EDA & Build Tools (Verible, Bender, GTKWave)
# -----------------------------------------------------------------------------
export VERIBLE_INSTALL_DIR="${VERIBLE_INSTALL_DIR:-$ROOT_PROJECT/tools/verible}"
export BENDER_INSTALL_DIR="${BENDER_INSTALL_DIR:-$ROOT_PROJECT/tools/bender}"
export GTKWAVE_INSTALL_DIR="${GTKWAVE_INSTALL_DIR:-$ROOT_PROJECT/tools/gtkwave}"

# -----------------------------------------------------------------------------
# 5. Python Virtual Environment
# -----------------------------------------------------------------------------
VENV_DIR="$ROOT_PROJECT/tools/venv"

if [ -f "$VENV_DIR/bin/activate" ]; then
    # shellcheck disable=SC1091
    source "$VENV_DIR/bin/activate"
else
    echo "[WARNING] Virtual environment NOT FOUND at $VENV_DIR"
fi

# -----------------------------------------------------------------------------
# 6. System PATH Consolidation
# -----------------------------------------------------------------------------
export PATH="$VERILATOR_INSTALL_DIR/bin:$SPIKE_PATH:$RISCV/bin:$VERIBLE_INSTALL_DIR/bin:$BENDER_INSTALL_DIR/bin:$GTKWAVE_INSTALL_DIR/bin:$PATH"

export CVA6_REPO_DIR="/home/lorenzo_administrator/VLSILAB_Intern/projects/cva6_and_mage"

# -----------------------------------------------------------------------------
# 7. Environment Status Summary
# -----------------------------------------------------------------------------
echo "================================================="
echo " CVA6 Unified Environment Configured Successfully"
echo "  [INFO] GCC           : $(which "$RISCV_CC" 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Python        : $(which python 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Verilator     : $(which verilator 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Spike         : $(which spike 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Verible       : $(which verible-verilog-format 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Bender        : $(which bender 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] GTKWave       : $(which gtkwave 2>/dev/null || echo 'NOT FOUND')"
echo "  [INFO] Prefix        : ${CV_SW_PREFIX:-not detected}"
echo "  [INFO] ROOT_PROJECT  : $ROOT_PROJECT"
echo "  [INFO] RISCV         : $RISCV"
echo "  [INFO] CVA6_REPO_DIR : ${CVA6_REPO_DIR}"
echo "  [INFO] Simulators    : $DV_SIMULATORS"
echo "================================================="