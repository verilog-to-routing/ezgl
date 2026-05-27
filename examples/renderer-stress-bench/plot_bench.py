"""
Generate benchmark plots from renderer-stress-bench result files.

Each figure covers one mode (headless / ui).
Each subplot covers one primitive type.
Each line on a subplot is one implementation (gtk, qt-it1, qt-it2, qt-deferred).
X axis = number of primitives (log scale).
Y axis = time in ms (log scale).
"""

import re
import os
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# ---------------------------------------------------------------------------
# Config
# ---------------------------------------------------------------------------

RESULTS_DIR = os.path.dirname(os.path.abspath(__file__))

IMPLEMENTATIONS = [
    ("gtk",         "gtk-renderer-stress-bench-results.txt",       "#4e9a06", "o"),
    ("qt-it1",      "qt-it1-renderer-stress-bench-results.txt",    "#3465a4", "s"),
    ("qt-it2",      "qt-it2-renderer-stress-bench-results.txt",    "#f57900", "^"),
    ("qt-deferred", "qt-deffered-renderer-stress-bench-results.txt","#cc0000", "D"),
]

MODES = ["headless", "ui"]

PRIMITIVE_TYPES = [
    "solid lines",
    "transparen lines",
    "solid rects",
    "transparen rects",
    "variadic lines",
    "variadic rects",
]

N_VALUES = [1_000, 10_000, 100_000, 1_000_000]

# ---------------------------------------------------------------------------
# Parse result files
# ---------------------------------------------------------------------------

def parse_results(filepath):
    """Return dict: (mode, n, primitive_type) -> ms"""
    data = {}
    if not os.path.exists(filepath):
        return data
    with open(filepath) as f:
        for line in f:
            line = line.strip()
            if not line:
                continue
            # e.g. "headless:1000 solid lines      1.49 ms"
            m = re.match(r'^(headless|ui):(\d+)\s+(.+?)\s+([\d.]+)\s+ms', line)
            if not m:
                continue
            mode   = m.group(1)
            n      = int(m.group(2))
            ptype  = m.group(3).strip()
            ms     = float(m.group(4))
            data[(mode, n, ptype)] = ms
    return data

all_data = {}
for name, fname, color, marker in IMPLEMENTATIONS:
    path = os.path.join(RESULTS_DIR, fname)
    all_data[name] = parse_results(path)

# ---------------------------------------------------------------------------
# Plot
# ---------------------------------------------------------------------------

for mode in MODES:
    n_cols = 3
    n_rows = 2
    fig, axes = plt.subplots(n_rows, n_cols,
                             figsize=(15, 8),
                             sharex=True)
    fig.suptitle(f"Renderer stress bench — {mode} mode", fontsize=14, fontweight="bold")

    for idx, ptype in enumerate(PRIMITIVE_TYPES):
        ax = axes[idx // n_cols][idx % n_cols]
        ax.set_title(ptype, fontsize=11)

        for name, fname, color, marker in IMPLEMENTATIONS:
            ys = []
            xs = []
            for n in N_VALUES:
                val = all_data[name].get((mode, n, ptype))
                if val is not None:
                    xs.append(n)
                    ys.append(val)
            if xs:
                ax.plot(xs, ys,
                        label=name,
                        color=color,
                        marker=marker,
                        linewidth=1.8,
                        markersize=6)

        ax.set_xscale("log")
        ax.set_yscale("log")
        ax.set_xlabel("primitives", fontsize=9)
        ax.set_ylabel("time (ms)", fontsize=9)
        ax.xaxis.set_major_formatter(ticker.FuncFormatter(
            lambda v, _: f"{int(v):,}"))
        ax.tick_params(axis="x", labelsize=8)
        ax.tick_params(axis="y", labelsize=8)
        ax.grid(True, which="both", linestyle="--", linewidth=0.4, alpha=0.6)
        ax.legend(fontsize=8)

    fig.tight_layout()
    out = os.path.join(RESULTS_DIR, f"bench_{mode}.png")
    fig.savefig(out, dpi=150)
    print(f"Saved {out}")

plt.close("all")
print("Done.")
