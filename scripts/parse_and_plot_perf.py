#!/usr/bin/env python3
import os
import sys
import re
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

# Set MPLCONFIGDIR to /tmp to avoid permission errors
os.environ["MPLCONFIGDIR"] = "/tmp/matplotlib_perf"

PHASE_NAMES = [
    "Initial SpM Write",
    "Cfg Stage Indep",
    "Cfg Stage 0",
    "Exec Stage 0",
    "Cfg Stage 1",
    "Exec Stage 1",
    "Cfg Stage 2",
    "Exec Stage 2",
    "Cfg Stage 3",
    "Exec Stage 3",
    "Cfg Stage 4",
    "Exec Stage 4",
    "Cfg Stage 5",
    "Exec Stage 5",
    "Cfg Stage 6",
    "Exec Stage 6",
    "Cfg Stage 7",
    "Exec Stage 7",
    "Final SpM Read",
]

TEST_ORDER = [
    "gold_ntt",
    "ntt1u",
    "ntt1u_raw",
    "ntt2u",
    "ntt2u_raw",
    "ntt4u",
    "ntt4u_raw",
]

TEST_LABELS = {
    "gold_ntt": "CVA6 (SW Gold)",
    "ntt1u": "Mage 1 CU (Func)",
    "ntt1u_raw": "Mage 1 CU (Raw)",
    "ntt2u": "Mage 2 CU (Func)",
    "ntt2u_raw": "Mage 2 CU (Raw)",
    "ntt4u": "Mage 4 CU (Func)",
    "ntt4u_raw": "Mage 4 CU (Raw)",
}

def parse_log_file(filepath):
    if not os.path.exists(filepath):
        print(f"Warning: File not found: {filepath}")
        return None

    is_gold = "gold_ntt" in filepath
    
    cycles_list = [0] * 19
    instr_list  = [0] * 19
    status_code = None
    total_errors = None

    with open(filepath, 'r', errors='ignore') as f:
        lines = f.readlines()

    if is_gold:
        for line in lines:
            m0 = re.search(r"mem 0x80000000 0x([0-9a-fA-F]+)", line)
            if m0:
                gold_cycles = int(m0.group(1), 16)
                cycles_list[3] = gold_cycles # Attribute all to execution
            m1 = re.search(r"mem 0x80000004 0x([0-9a-fA-F]+)", line)
            if m1:
                gold_instr = int(m1.group(1), 16)
                instr_list[3] = gold_instr
        return {
            "status_code": "PASS",
            "errors": 0,
            "cycles": cycles_list,
            "instr": instr_list,
            "total_cycles": sum(cycles_list),
            "total_instr": sum(instr_list),
        }

    dram_writes = {}
    for line in lines:
        m = re.search(r"mem 0x([0-9a-fA-F]{8}) 0x([0-9a-fA-F]+)", line)
        if m:
            addr = int(m.group(1), 16)
            val  = int(m.group(2), 16)
            dram_writes[addr] = val

    if 0x80000000 in dram_writes:
        status_code = dram_writes[0x80000000]
    if 0x80000004 in dram_writes:
        total_errors = dram_writes[0x80000004]

    magic = dram_writes.get(0x80000028, 0)
    has_dram_perf = (magic == 0x50455246)

    if has_dram_perf:
        for p in range(19):
            c_addr = 0x80000030 + 8 * p
            i_addr = 0x80000034 + 8 * p
            cycles_list[p] = dram_writes.get(c_addr, 0)
            instr_list[p]  = dram_writes.get(i_addr, 0)
    else:
        for line in lines:
            mp = re.search(r"PHASE\s+(\d+):\s+cycles=(\d+),\s+instr=(\d+)", line)
            if mp:
                p = int(mp.group(1))
                if 0 <= p < 19:
                    cycles_list[p] = int(mp.group(2))
                    instr_list[p]  = int(mp.group(3))

    return {
        "status_code": hex(status_code) if status_code is not None else "N/A",
        "errors": total_errors,
        "cycles": cycles_list,
        "instr": instr_list,
        "total_cycles": sum(cycles_list),
        "total_instr": sum(instr_list),
    }

def plot_stacked_chart(results, out_img):
    plt.rcParams['font.sans-serif'] = 'DejaVu Sans'
    plt.rcParams['axes.edgecolor'] = '#CCCCCC'
    plt.rcParams['axes.linewidth'] = 0.8
    
    fig, ax = plt.subplots(figsize=(19, 10), dpi=300)
    
    # 5 aggregated segments
    categories = [
        "Initial SpM Write",
        "Stage-Independent Cfg",
        "Stage-Dependent Cfg (0-7)",
        "Hardware / SW Execution",
        "Final SpM Read"
    ]
    
    # Color palette
    colors = [
        "#3498DB", # Soft Blue (SpM Write)
        "#E67E22", # Amber / Orange (Stage Indep)
        "#E74C3C", # Coral Red (Stage Dep Cfg)
        "#2ECC71", # Emerald Green (HW / SW Exec)
        "#9B59B6", # Amethyst Purple (SpM Read)
    ]
    
    gold_cycles = results["gold_ntt"]["total_cycles"]
    gold_instr  = results["gold_ntt"]["total_instr"]

    n_tests = len(TEST_ORDER)
    group_width = 2.4
    bar_width = 0.85
    spacing = 0.12
    
    x_centers = np.arange(n_tests) * group_width
    
    for idx, test in enumerate(TEST_ORDER):
        res = results[test]
        xc = x_centers[idx]
        x_cyc = xc - (bar_width / 2.0 + spacing / 2.0)
        x_ins = xc + (bar_width / 2.0 + spacing / 2.0)
        
        # Deconstruct into 5 components
        if test == "gold_ntt":
            cyc_segments = [0, 0, 0, res["total_cycles"], 0]
            ins_segments = [0, 0, 0, res["total_instr"], 0]
        else:
            c = res["cycles"]
            i = res["instr"]
            # 0: SpM Write (p=0)
            # 1: Cfg Indep (p=1)
            # 2: Cfg Stages 0-7 (p=2, 4, 6, 8, 10, 12, 14, 16)
            # 3: Exec Stages 0-7 (p=3, 5, 7, 9, 11, 13, 15, 17)
            # 4: SpM Read (p=18)
            cfg_dep_cyc = sum(c[p] for p in [2, 4, 6, 8, 10, 12, 14, 16])
            exec_cyc    = sum(c[p] for p in [3, 5, 7, 9, 11, 13, 15, 17])
            cfg_dep_ins = sum(i[p] for p in [2, 4, 6, 8, 10, 12, 14, 16])
            exec_ins    = sum(i[p] for p in [3, 5, 7, 9, 11, 13, 15, 17])
            
            cyc_segments = [c[0], c[1], cfg_dep_cyc, exec_cyc, c[18]]
            ins_segments = [i[0], i[1], cfg_dep_ins, exec_ins, i[18]]
        
        # Plot Cycles Bar
        bottom_cyc = 0
        for seg_idx, val in enumerate(cyc_segments):
            if val > 0:
                bar = ax.bar(x_cyc, val, bar_width, bottom=bottom_cyc, color=colors[seg_idx],
                             edgecolor='white', linewidth=0.7,
                             label=categories[seg_idx] if idx == 1 else "")
                # Annotate segment if tall enough
                if val >= 2500:
                    y_pos = bottom_cyc + val / 2.0
                    ax.text(x_cyc, y_pos, f"{val:,}", ha='center', va='center',
                            color='white', fontsize=7.5, fontweight='bold',
                            bbox=dict(boxstyle='round,pad=0.15', facecolor='black', alpha=0.35, lw=0))
                bottom_cyc += val
                
        # Total on top of Cycles Bar
        ax.text(x_cyc, bottom_cyc + 2000, f"{res['total_cycles']:,}\n(Cycles)",
                ha='center', va='bottom', fontsize=8, fontweight='bold', color='#1A252F')
        
        # Plot Instructions Bar
        bottom_ins = 0
        for seg_idx, val in enumerate(ins_segments):
            if val > 0:
                bar = ax.bar(x_ins, val, bar_width, bottom=bottom_ins, color=colors[seg_idx],
                             edgecolor='white', linewidth=0.7, alpha=0.85)
                # Annotate segment if tall enough
                if val >= 2000:
                    y_pos = bottom_ins + val / 2.0
                    ax.text(x_ins, y_pos, f"{val:,}", ha='center', va='center',
                            color='white', fontsize=7.5, fontweight='bold',
                            bbox=dict(boxstyle='round,pad=0.15', facecolor='black', alpha=0.35, lw=0))
                bottom_ins += val
                
        # Total on top of Instructions Bar
        ax.text(x_ins, bottom_ins + 2000, f"{res['total_instr']:,}\n(Instr)",
                ha='center', va='bottom', fontsize=8, fontweight='bold', color='#2C3E50')
        
        # Speedup Badge above the pair
        sp_cyc = gold_cycles / res['total_cycles'] if res['total_cycles'] > 0 else 0
        sp_ins = gold_instr  / res['total_instr']  if res['total_instr']  > 0 else 0
        max_h = max(bottom_cyc, bottom_ins)
        
        if test == "gold_ntt":
            badge_text = "Baseline\n1.00x"
            badge_col = '#7F8C8D'
        else:
            badge_text = f"Speedup: {sp_cyc:.2f}x (cyc)\nReduct: {sp_ins:.2f}x (ins)"
            badge_col = '#16A085' if "raw" in test else '#2980B9'
            
        ax.text(xc, max_h + 14000, badge_text, ha='center', va='bottom',
                fontsize=8.5, fontweight='bold', color=badge_col,
                bbox=dict(boxstyle='round,pad=0.35', facecolor='white', edgecolor=badge_col, lw=1.5, alpha=0.95))

    ax.set_xticks(x_centers)
    ax.set_xticklabels([TEST_LABELS[t] for t in TEST_ORDER], fontsize=10.5, fontweight='bold')
    ax.set_ylabel("Count (Decimal Units)", fontsize=12, fontweight='bold', labelpad=10)
    ax.set_title("Forward NTT Performance Profiling: CVA6 Baseline vs. Mage CGRA (1, 2, 4 Computing Units)\nClock Cycles (mcycle) & Retired Instructions (minstret) Across 7 Test Configurations",
                 fontsize=14, fontweight='bold', pad=25)
    
    ax.set_ylim(0, 225000)
    ax.grid(axis='y', linestyle='--', alpha=0.4)
    
    # Custom Legend
    handles, labels = ax.get_legend_handles_labels()
    # Deduplicate legend
    by_label = dict(zip(labels, handles))
    ax.legend(by_label.values(), by_label.keys(), loc='upper right', fontsize=9.5, framealpha=0.95)
    
    plt.tight_layout()
    os.makedirs(os.path.dirname(out_img), exist_ok=True)
    plt.savefig(out_img, dpi=300)
    plt.close()
    print(f"\n[SUCCESS] Generated stacked column chart: {out_img}")

def main():
    sim_dir = sys.argv[1] if len(sys.argv) > 1 else "verif/sim/out_2026-10-27/veri-testharness_sim"
    out_img = sys.argv[2] if len(sys.argv) > 2 else "./results"

    results = {}
    for test in TEST_ORDER:
        log_file = os.path.join(sim_dir, f"{test}.cv32a60x.log")
        data = parse_log_file(log_file)
        if data:
            results[test] = data
            print(f"Loaded {test}: Total Cycles = {data['total_cycles']}, Total Instr = {data['total_instr']}, Errors = {data.get('errors')}")
        else:
            print(f"Missing {test}")

    if not results:
        print("No results parsed.")
        return

    # Print Detailed Markdown Breakdown Table
    print("\n" + "="*80)
    print("DETAILED PHASE-BY-PHASE BREAKDOWN (DECIMAL COUNTS)")
    print("="*80)
    header = f"{'Phase':<24}" + "".join([f"{t:>15}" for t in TEST_ORDER if t in results])
    print(header)
    print("-" * len(header))
    
    print("\n--- CLOCK CYCLES ---")
    for p in range(19):
        row = f"{PHASE_NAMES[p]:<24}"
        for t in TEST_ORDER:
            if t in results:
                row += f"{results[t]['cycles'][p]:>15,d}"
        print(row)
    
    total_row = f"{'TOTAL CYCLES':<24}"
    for t in TEST_ORDER:
        if t in results:
            total_row += f"{results[t]['total_cycles']:>15,d}"
    print("-" * len(header))
    print(total_row)

    if "gold_ntt" in results and results["gold_ntt"]["total_cycles"] > 0:
        gold_cyc = results["gold_ntt"]["total_cycles"]
        sp_row = f"{'SPEEDUP (vs Gold)':<24}"
        for t in TEST_ORDER:
            if t in results:
                sp = gold_cyc / results[t]["total_cycles"] if results[t]["total_cycles"] > 0 else 0
                sp_row += f"{sp:>14.2f}x"
        print(sp_row)

    print("\n--- RETIRED INSTRUCTIONS ---")
    for p in range(19):
        row = f"{PHASE_NAMES[p]:<24}"
        for t in TEST_ORDER:
            if t in results:
                row += f"{results[t]['instr'][p]:>15,d}"
        print(row)
    
    total_row_i = f"{'TOTAL INSTRUCTIONS':<24}"
    for t in TEST_ORDER:
        if t in results:
            total_row_i += f"{results[t]['total_instr']:>15,d}"
    print("-" * len(header))
    print(total_row_i)

    if "gold_ntt" in results and results["gold_ntt"]["total_instr"] > 0:
        gold_ins = results["gold_ntt"]["total_instr"]
        sp_row_i = f"{'INSTRUCTION REDUCTION':<24}"
        for t in TEST_ORDER:
            if t in results:
                sp = gold_ins / results[t]["total_instr"] if results[t]["total_instr"] > 0 else 0
                sp_row_i += f"{sp:>14.2f}x"
        print(sp_row_i)

    plot_stacked_chart(results, out_img)

if __name__ == "__main__":
    main()
