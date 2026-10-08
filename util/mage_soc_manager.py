#!/usr/bin/env python3
# Copyright 2026 Politecnico di Torino.
# Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#
# File: mage_soc_manager.py
# Description: Automated attachment and detachment of Mage CGRA (DAE mode) to CVA6 SoC AXI bus.

import os
import sys
import re
import shutil
import argparse
from pathlib import Path

try:
    import hjson
except ImportError:
    hjson = None
    import json


# Sentinel comment delimiters
TAG_START_FMT = "// >>>>> MAGE_ATTACH_START: {tag} >>>>>"
TAG_END_FMT = "// <<<<< MAGE_ATTACH_END: {tag} <<<<<"
TAG_REGEX = re.compile(
    r"\n?[ \t]*// >>>>> MAGE_ATTACH_START: [A-Za-z0-9_]+ >>>>>.*?"
    r"// <<<<< MAGE_ATTACH_END: [A-Za-z0-9_]+ <<<<<[ \t]*",
    re.DOTALL,
)
ORIG_COMMENT_REGEX = re.compile(r"^[ \t]*// ORIG:[ \t]?(.*)$", re.MULTILINE)


class MageSoCManager:
    """Manages attaching and detaching Mage to the CVA6 SoC."""

    def __init__(self, root_dir=None, config_path=None):
        if root_dir is None:
            # Script is inside util/, so root is one directory up
            self.root_dir = Path(__file__).resolve().parent.parent
        else:
            self.root_dir = Path(root_dir).resolve()

        if config_path is None:
            self.config_path = (
                self.root_dir
                / "ips"
                / "Mage-and-Rogue"
                / "config"
                / "dae"
                / "cfg_2x2_2x2_full.hjson"
            )
        else:
            self.config_path = Path(config_path).resolve()

        self.backup_dir = self.root_dir / ".mage_soc_backup"

        # Target files in CVA6 SoC
        self.soc_pkg_path = self.root_dir / "corev_apu" / "tb" / "ariane_soc_pkg.sv"
        self.periph_path = self.root_dir / "corev_apu" / "tb" / "ariane_peripherals.sv"
        self.testharness_path = (
            self.root_dir / "corev_apu" / "tb" / "ariane_testharness.sv"
        )
        self.flist_core_path = self.root_dir / "core" / "Flist.cva6"
        self.target_files = [
            self.soc_pkg_path,
            self.periph_path,
            self.testharness_path,
            self.flist_core_path,
        ]

    def _load_hjson(self, path):
        with open(path, "r", encoding="utf-8") as f:
            content = f.read()
        if hjson is not None:
            return hjson.loads(content)
        # Fallback: simple strip of single-line and multi-line comments
        no_single = re.sub(r"//.*$", "", content, flags=re.MULTILINE)
        no_multi = re.sub(r"/\*.*?\*/", "", no_single, flags=re.DOTALL)
        return json.loads(no_multi)

    def get_config_params(self):
        """Reads configuration and extracts relevant architectural parameters."""
        if not self.config_path.exists():
            raise FileNotFoundError(
                f"Mage configuration file not found: {self.config_path}"
            )

        cfg = self._load_hjson(self.config_path)

        dae_cgra = cfg.get("dae_cgra", 1)
        if dae_cgra != 1:
            raise ValueError(
                f"Configuration at {self.config_path} is not in DAE mode (dae_cgra={dae_cgra}). "
                "Only DAE mode is supported for AXI attachment."
            )

        dae_params = cfg.get("dae_params", {})
        n_age_tot = dae_params.get("n_age_tot", 4)
        num_words = dae_params.get("num_words", 1024)

        # SpM size in bytes (each word is 32-bit = 4 bytes)
        spm_bytes = n_age_tot * num_words * 4

        # AXI window reserved for SpM: 1MB standard window (0x100000)
        # or next power-of-2 if spm_bytes > 1MB
        data_window_bytes = max(0x100000, 1 << (spm_bytes - 1).bit_length())

        return {
            "n_banks": n_age_tot,
            "num_words": num_words,
            "spm_bytes": spm_bytes,
            "data_window_bytes": data_window_bytes,
            "ctrl_base": 0x50000000,
            "ctrl_len": 0x1000,
            "data_base": 0xF0000000,
            "data_len": data_window_bytes,
            "plic_irq_source": 7,
        }

    def is_attached(self):
        """Checks if Mage is currently attached to the SoC."""
        for p in self.target_files:
            if not p.exists():
                continue
            with open(p, "r", encoding="utf-8") as f:
                if "// >>>>> MAGE_ATTACH_START" in f.read():
                    return True
        return False

    def create_backup(self):
        """Creates backup of pristine SoC files."""
        self.backup_dir.mkdir(parents=True, exist_ok=True)
        for target in self.target_files:
            if not target.exists():
                raise FileNotFoundError(f"Target SoC file not found: {target}")
            backup_file = self.backup_dir / target.name
            if not backup_file.exists():
                shutil.copy2(target, backup_file)

    def restore_backup(self):
        """Restores files from backup and removes backup dir."""
        if self.backup_dir.exists():
            for target in self.target_files:
                backup_file = self.backup_dir / target.name
                if backup_file.exists():
                    shutil.copy2(backup_file, target)
            shutil.rmtree(self.backup_dir, ignore_errors=True)

    def _strip_sentinels(self, content):
        """Removes injected sentinel blocks and restores original lines."""
        # Restore lines commented with '// ORIG: '
        def restore_orig(match):
            block = match.group(0)
            orig_lines = ORIG_COMMENT_REGEX.findall(block)
            if orig_lines:
                return "\n" + "\n".join(orig_lines) + "\n"
            return ""

        cleaned = TAG_REGEX.sub(restore_orig, content)
        # Remove leftover multiple empty lines
        cleaned = re.sub(r"\n{3,}", "\n\n", cleaned)
        return cleaned

    def attach(self):
        """Attaches Mage to CVA6 SoC files."""
        params = self.get_config_params()

        # If already attached, detach cleanly first to ensure idempotency
        if self.is_attached():
            print("[INFO] Mage is already attached. Detaching prior configuration first...")
            self.detach()

        print(f"[INFO] Backing up pristine SoC files to {self.backup_dir}...")
        self.create_backup()

        print("[INFO] Applying Mage attachment modifications:")
        print(f"       Configuration: {self.config_path.name}")
        print(f"       SpM: {params['n_banks']} banks x {params['num_words']} words = {params['spm_bytes'] / 1024:.1f} KB")
        print(f"       MageCtrlBase: 0x{params['ctrl_base']:08X} (Length: 0x{params['ctrl_len']:X})")
        print(f"       MageDataBase: 0x{params['data_base']:08X} (Length: 0x{params['data_len']:X})")
        print(f"       PLIC IRQ Source: {params['plic_irq_source']}")

        # 1. Modify ariane_soc_pkg.sv
        self._patch_soc_pkg(params)

        # 2. Modify ariane_peripherals.sv
        self._patch_peripherals(params)

        # 3. Modify ariane_testharness.sv
        self._patch_testharness(params)

        # 4. Modify core/Flist.cva6
        self._patch_flist(params)

        print("[SUCCESS] Mage successfully attached to CVA6 SoC!")

    def detach(self):
        """Detaches Mage from CVA6 SoC files and restores pristine state."""
        if not self.is_attached() and not self.backup_dir.exists():
            print("[INFO] Mage is not currently attached. SoC is already clean.")
            return

        print("[INFO] Detaching Mage and restoring SoC files to pristine state...")

        # Restore from backup directory if available
        if self.backup_dir.exists():
            self.restore_backup()
        else:
            # Fallback: strip tags directly
            for target in self.target_files:
                if target.exists():
                    with open(target, "r", encoding="utf-8") as f:
                        content = f.read()
                    cleaned = self._strip_sentinels(content)
                    with open(target, "w", encoding="utf-8") as f:
                        f.write(cleaned)

        print("[SUCCESS] Mage detached. SoC files restored to original pristine state.")

    def _patch_soc_pkg(self, params):
        with open(self.soc_pkg_path, "r", encoding="utf-8") as f:
            content = f.read()

        # A. Enum axi_slaves_t
        enum_target = re.search(r"(\bDebug\s*=\s*9\s*)(\n\s*\}\s*axi_slaves_t;)", content)
        if not enum_target:
            raise RuntimeError("Could not find 'Debug = 9' in ariane_soc_pkg.sv")
        enum_inj = (
            f"{enum_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: SLAVES_ENUM >>>>>\n"
            f"    ,\n"
            f"    MageCtrl = 10,\n"
            f"    MageData = 11\n"
            f"// <<<<< MAGE_ATTACH_END: SLAVES_ENUM <<<<<"
            f"{enum_target.group(2)}"
        )
        content = content[:enum_target.start()] + enum_inj + content[enum_target.end():]

        # B. NB_PERIPHERALS
        nb_periph_target = re.search(r"(localparam\s+NB_PERIPHERALS\s*=\s*Debug\s*\+\s*1\s*;)", content)
        if not nb_periph_target:
            raise RuntimeError("Could not find 'localparam NB_PERIPHERALS = Debug + 1;' in ariane_soc_pkg.sv")
        nb_inj = (
            f"// >>>>> MAGE_ATTACH_START: NB_PERIPHERALS >>>>>\n"
            f"  localparam NB_PERIPHERALS = MageData + 1;\n"
            f"// ORIG: {nb_periph_target.group(1)}\n"
            f"// <<<<< MAGE_ATTACH_END: NB_PERIPHERALS <<<<<"
        )
        content = content[:nb_periph_target.start()] + nb_inj + content[nb_periph_target.end():]

        # C. Lengths
        gpio_len_target = re.search(r"(localparam\s+logic\[63:0\]\s+GPIOLength\s*=\s*64'h1000\s*;)", content)
        if not gpio_len_target:
            raise RuntimeError("Could not find 'GPIOLength' in ariane_soc_pkg.sv")
        len_inj = (
            f"{gpio_len_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: LENGTHS >>>>>\n"
            f"  localparam logic[63:0] MageCtrlLength = 64'h{params['ctrl_len']:X};\n"
            f"  localparam logic[63:0] MageDataLength = 64'h{params['data_len']:X};\n"
            f"// <<<<< MAGE_ATTACH_END: LENGTHS <<<<<"
        )
        content = content[:gpio_len_target.start()] + len_inj + content[gpio_len_target.end():]

        # D. Base Addresses in soc_bus_start_t
        gpio_base_target = re.search(r"(GPIOBase\s*=\s*64'h4000_0000\s*,)", content)
        if not gpio_base_target:
            raise RuntimeError("Could not find 'GPIOBase' in ariane_soc_pkg.sv")
        base_inj = (
            f"{gpio_base_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: BASE_ADDRS >>>>>\n"
            f"    MageCtrlBase = 64'h{params['ctrl_base']:08X},\n"
            f"    MageDataBase = 64'h{params['data_base']:08X},\n"
            f"// <<<<< MAGE_ATTACH_END: BASE_ADDRS <<<<<"
        )
        content = content[:gpio_base_target.start()] + base_inj + content[gpio_base_target.end():]

        with open(self.soc_pkg_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  [+] Patched {self.soc_pkg_path.relative_to(self.root_dir)}")

    def _patch_peripherals(self, params):
        with open(self.periph_path, "r", encoding="utf-8") as f:
            content = f.read()

        # A. Module port list: add mage_irq_i
        irq_o_target = re.search(r"(output\s+logic\s*\[1:0\]\s*irq_o\s*,)", content)
        if not irq_o_target:
            raise RuntimeError("Could not find 'output logic [1:0] irq_o,' in ariane_peripherals.sv")
        port_inj = (
            f"{irq_o_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: PERIPH_PORT >>>>>\n"
            f"    input  logic       mage_irq_i      ,\n"
            f"// <<<<< MAGE_ATTACH_END: PERIPH_PORT <<<<<"
        )
        content = content[:irq_o_target.start()] + port_inj + content[irq_o_target.end():]

        # B. PLIC irq_sources mapping
        plic_irq_target = re.search(r"(assign\s+irq_sources\[ariane_soc::NumSources-1:7\]\s*=\s*'0\s*;)", content)
        if not plic_irq_target:
            raise RuntimeError("Could not find 'assign irq_sources[ariane_soc::NumSources-1:7] = '0;' in ariane_peripherals.sv")
        plic_inj = (
            f"// >>>>> MAGE_ATTACH_START: PLIC_IRQ >>>>>\n"
            f"    assign irq_sources[{params['plic_irq_source']}] = mage_irq_i;\n"
            f"    assign irq_sources[ariane_soc::NumSources-1:{params['plic_irq_source'] + 1}] = '0;\n"
            f"// ORIG: {plic_irq_target.group(1)}\n"
            f"// <<<<< MAGE_ATTACH_END: PLIC_IRQ <<<<<"
        )
        content = content[:plic_irq_target.start()] + plic_inj + content[plic_irq_target.end():]

        with open(self.periph_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  [+] Patched {self.periph_path.relative_to(self.root_dir)}")

    def _patch_testharness(self, params):
        with open(self.testharness_path, "r", encoding="utf-8") as f:
            content = f.read()

        # A. addr_map rules
        dram_rule_target = re.search(
            r"('\{\s*idx:\s*ariane_soc::DRAM,\s*start_addr:\s*ariane_soc::DRAMBase,\s*end_addr:\s*ariane_soc::DRAMBase\s*\+\s*ariane_soc::DRAMLength\s*\}\s*)(\n\s*\};)",
            content,
        )
        if not dram_rule_target:
            raise RuntimeError("Could not find DRAM rule in addr_map in ariane_testharness.sv")
        rule_inj = (
            f"{dram_rule_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: ADDR_MAP >>>>>\n"
            f"    ,\n"
            f"    '{{ idx: ariane_soc::MageCtrl, start_addr: ariane_soc::MageCtrlBase, end_addr: ariane_soc::MageCtrlBase + ariane_soc::MageCtrlLength }},\n"
            f"    '{{ idx: ariane_soc::MageData, start_addr: ariane_soc::MageDataBase, end_addr: ariane_soc::MageDataBase + ariane_soc::MageDataLength }}\n"
            f"// <<<<< MAGE_ATTACH_END: ADDR_MAP <<<<<"
            f"{dram_rule_target.group(2)}"
        )
        content = content[:dram_rule_target.start()] + rule_inj + content[dram_rule_target.end():]

        # B. IRQ connection in i_ariane_peripherals
        periph_irq_target = re.search(r"(\.irq_o\s*\(\s*irqs\s*\)\s*,)", content)
        if not periph_irq_target:
            raise RuntimeError("Could not find '.irq_o ( irqs ),' in ariane_testharness.sv")
        periph_inj = (
            f"{periph_irq_target.group(1)}\n"
            f"// >>>>> MAGE_ATTACH_START: PERIPH_IRQ_CONN >>>>>\n"
            f"    .mage_irq_i( mage_intr                    ),\n"
            f"// <<<<< MAGE_ATTACH_END: PERIPH_IRQ_CONN <<<<<"
        )
        content = content[:periph_irq_target.start()] + periph_inj + content[periph_irq_target.end():]

        # C. mage_axi_wrapper instance
        uart_bus_target = re.search(
            r"(uart_bus\s+#\s*\(\s*\.BAUD_RATE\s*\(\s*115200\s*\)\s*,\s*\.PARITY_EN\s*\(\s*0\s*\)\s*\)\s*i_uart_bus\s*\(\s*\.rx\s*\(\s*tx\s*\)\s*,\s*\.tx\s*\(\s*rx\s*\)\s*,\s*\.rx_en\s*\(\s*1'b1\s*\)\s*\)\s*;)",
            content,
        )
        if not uart_bus_target:
            raise RuntimeError("Could not find 'i_uart_bus' in ariane_testharness.sv")

        wrapper_inj = (
            f"{uart_bus_target.group(1)}\n\n"
            f"// >>>>> MAGE_ATTACH_START: MAGE_WRAPPER_INST >>>>>\n"
            f"  // ---------------\n"
            f"  // Mage CGRA (DAE Mode)\n"
            f"  // ---------------\n"
            f"  ariane_axi_soc::req_slv_t  mage_ctrl_req,  mage_data_req;\n"
            f"  ariane_axi_soc::resp_slv_t mage_ctrl_resp, mage_data_resp;\n"
            f"  logic                      mage_intr;\n\n"
            f"  `AXI_ASSIGN_TO_REQ(mage_ctrl_req, master[ariane_soc::MageCtrl])\n"
            f"  `AXI_ASSIGN_FROM_RESP(master[ariane_soc::MageCtrl], mage_ctrl_resp)\n\n"
            f"  `AXI_ASSIGN_TO_REQ(mage_data_req, master[ariane_soc::MageData])\n"
            f"  `AXI_ASSIGN_FROM_RESP(master[ariane_soc::MageData], mage_data_resp)\n\n"
            f"  mage_axi_wrapper #(\n"
            f"    .AxiAddrWidth ( AXI_ADDRESS_WIDTH            ),\n"
            f"    .AxiDataWidth ( AXI_DATA_WIDTH               ),\n"
            f"    .AxiIdWidth   ( ariane_axi_soc::IdWidthSlave ),\n"
            f"    .AxiUserWidth ( AXI_USER_WIDTH               ),\n"
            f"    .MaxTrans     ( 4                            ),\n"
            f"    .axi_req_t    ( ariane_axi_soc::req_slv_t    ),\n"
            f"    .axi_rsp_t    ( ariane_axi_soc::resp_slv_t   )\n"
            f"  ) i_mage_axi_wrapper (\n"
            f"    .clk_i           ( clk_i          ),\n"
            f"    .rst_ni          ( ndmreset_n     ),\n"
            f"    .axi_req_i       ( mage_data_req  ),\n"
            f"    .axi_rsp_o       ( mage_data_resp ),\n"
            f"    .mage_intr_o     ( mage_intr      ),\n"
            f"    .axi_ctrl_req_i  ( mage_ctrl_req  ),\n"
            f"    .axi_ctrl_rsp_o  ( mage_ctrl_resp )\n"
            f"  );\n"
            f"// <<<<< MAGE_ATTACH_END: MAGE_WRAPPER_INST <<<<<"
        )
        content = content[:uart_bus_target.start()] + wrapper_inj + content[uart_bus_target.end():]

        with open(self.testharness_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  [+] Patched {self.testharness_path.relative_to(self.root_dir)}")

    def _patch_flist(self, params):
        with open(self.flist_core_path, "r", encoding="utf-8") as f:
            content = f.read()

        flist_inj = (
            "\n// >>>>> MAGE_ATTACH_START: MAGE_FLIST >>>>>\n"
            "-F ${CVA6_REPO_DIR}/ips/Mage-and-Rogue/Flist.mage\n"
            "// <<<<< MAGE_ATTACH_END: MAGE_FLIST <<<<<\n"
        )
        content = content.rstrip() + "\n" + flist_inj

        with open(self.flist_core_path, "w", encoding="utf-8") as f:
            f.write(content)
        print(f"  [+] Patched {self.flist_core_path.relative_to(self.root_dir)}")

    def status(self):
        """Displays current attachment status."""
        attached = self.is_attached()
        print(f"Mage Attachment Status: {'ATTACHED' if attached else 'DETACHED'}")
        if attached:
            try:
                params = self.get_config_params()
                print(f"  Config: {self.config_path.name}")
                print(f"  SpM Banks: {params['n_banks']}, Bank Size: {params['num_words']} words ({params['spm_bytes'] / 1024:.1f} KB total)")
                print(f"  MageCtrl: [0x{params['ctrl_base']:08X} - 0x{params['ctrl_base'] + params['ctrl_len']:08X}]")
                print(f"  MageData: [0x{params['data_base']:08X} - 0x{params['data_base'] + params['data_len']:08X}]")
                print(f"  PLIC IRQ: Line {params['plic_irq_source']}")
            except Exception as e:
                print(f"  (Could not read config details: {e})")
        print(f"  Backup directory present: {'YES' if self.backup_dir.exists() else 'NO'}")

