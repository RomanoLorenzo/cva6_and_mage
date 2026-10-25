#!/usr/bin/env python3
# Copyright 2026 Politecnico di Torino.
# Solderpad Hardware License, Version 2.1, see LICENSE.md for details.
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
#
# File: check_mage_cfg.py
# Description: Validates whether a target C test program matches the currently attached Mage CGRA configuration.

import sys
import os
import re
import argparse
from pathlib import Path

# Add project root to sys.path
root_dir = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(root_dir))

from util.mage_soc_manager import MageSoCManager


def extract_expected_config(c_file_path):
    """Extracts expected MAGE_CFG_HJSON path from the C test file header."""
    if not c_file_path.exists():
        return None

    try:
        with open(c_file_path, "r", encoding="utf-8", errors="ignore") as f:
            for _ in range(120):  # Check first 120 lines
                line = f.readline()
                if not line:
                    break
                m = re.search(r"//\s*MAGE_CFG_HJSON\s*=\s*(.+)", line)
                if m:
                    return m.group(1).strip()
    except Exception:
        pass
    return None


def format_rel(path, base):
    """Formats a path relative to base if possible, else returns original path string."""
    if path is None:
        return ""
    p = Path(path)
    try:
        return str(p.relative_to(base))
    except Exception:
        return str(p)


def check_configuration(test_path_str, strict=False):
    """Checks the test against the attached and generated Mage configuration."""
    test_path = Path(test_path_str)
    if not test_path.is_absolute():
        candidates = [
            (Path.cwd() / test_path).resolve(),
            (root_dir / "verif" / "sim" / test_path).resolve(),
            (root_dir / test_path).resolve(),
            (root_dir / "verif" / "tests" / "custom" / "mage_tests" / test_path).resolve(),
        ]
        for c in candidates:
            if c.exists():
                test_path = c
                break
        else:
            test_path = candidates[0]

    display_test = format_rel(test_path, root_dir)

    manager = MageSoCManager(root_dir=root_dir)
    is_attached = manager.is_attached()
    attached_cfg = manager.get_attached_config_path()
    gen_cfg = manager.get_generated_config_path()

    expected_cfg_str = extract_expected_config(test_path)

    has_warning = False

    # Case D: Mage is not attached to SoC
    if not is_attached:
        has_warning = True
        print(f"[WARNING] You are trying to run the file {display_test} on Mage, but Mage is not attached to the SoC.")
        if expected_cfg_str:
            print(f"          {display_test} is designed to run on configuration {expected_cfg_str}")
        else:
            print(f"          Unable to retrieve the configuration that file {display_test} expects.")

    # Case C: Unable to retrieve expected configuration from C file
    elif expected_cfg_str is None:
        has_warning = True
        attached_display = format_rel(attached_cfg, root_dir) if attached_cfg else "Unknown"
        print(f"[WARNING] Unable to retrieve the configuration that file {display_test} expects.")
        print(f"          {attached_display} currently attached to the SoC")

    # Case B: Unable to retrieve Mage configuration from attached SoC
    elif attached_cfg is None:
        has_warning = True
        print(f"[WARNING] Unable to retrieve Mage configuration.")
        print(f"          {display_test} is designed to run on configuration {expected_cfg_str}")

    # Case A: Mismatched configuration
    else:
        expected_name = Path(expected_cfg_str).name
        attached_name = attached_cfg.name
        attached_display = format_rel(attached_cfg, root_dir)

        if expected_name != attached_name:
            has_warning = True
            print(f"[WARNING] You are trying to run the file {display_test} on a wrong configuration of Mage")
            print(f"          {display_test} is designed to run on configuration {expected_cfg_str}")
            print(f"          {attached_display} currently attached to the SoC")
        else:
            # Check for potential desync between SoC attachment and sw/mage.h (generated RTL)
            if gen_cfg and gen_cfg.name != attached_name:
                has_warning = True
                gen_display = format_rel(gen_cfg, root_dir)
                print(f"[WARNING] Mage RTL / header mismatch detected:")
                print(f"          SoC is attached with: {attached_display}")
                print(f"          Mage RTL was generated for: {gen_display}")
                print(f"          Please run: make -C ips/Mage-and-Rogue mage-gen MAGE_CFG_HJSON={attached_display}")
            else:
                print(f"[INFO] Mage configuration match: {attached_name}")
                print(f"       Test file: {display_test}")
                print(f"       Attached : {attached_display}")

    if has_warning and strict:
        sys.exit(1)

    sys.exit(0)


def main():
    parser = argparse.ArgumentParser(
        description="Verify that a target C test program matches the attached Mage CGRA configuration."
    )
    parser.add_argument(
        "--test",
        "-t",
        type=str,
        required=True,
        help="Path to the C test source file.",
    )
    parser.add_argument(
        "--strict",
        action="store_true",
        default=os.environ.get("MAGE_CHECK_STRICT", "0") == "1",
        help="Treat warnings as fatal errors (exit code 1).",
    )

    args = parser.parse_args()
    check_configuration(args.test, strict=args.strict)


if __name__ == "__main__":
    main()

