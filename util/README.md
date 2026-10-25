Content:

* vendor.py
  - vendorization script
  - copied from https://github.com/openhwgroup/cv32e40p/blob/master/util/vendor.py, commit 69e839e

* mage_soc_manager.py
  - Backend manager for `mage-attach` and `mage-detach`.
  - Automatically manages patching and restoring CVA6 SoC files (`ariane_soc_pkg.sv`, `ariane_peripherals.sv`, `ariane_testharness.sv`, `Flist.cva6`).
  - Tracks the currently attached Mage configuration and provides status reporting.

* check_mage_cfg.py
  - Pre-simulation validator invoked by `make magesim`.
  - Inspects target C test programs for expected configuration (`// MAGE_CFG_HJSON = ...`).
  - Validates tests against the attached SoC configuration and generated RTL/headers, providing informative warnings on mismatch or detachment.
