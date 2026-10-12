# import sys
# import os
# import glob
# import pandas as pd
# import numpy as np
# import matplotlib
# matplotlib.use("Agg")
# import matplotlib.pyplot as plt
# import matplotlib.ticker as ticker
# from matplotlib.patches import Patch


# # ============================================================================
# # FORMATTAZIONE (valori precisi sopra le barre e tick degli assi)
# # ============================================================================
# def format_time(us_val):
#     """Microsecondi -> stringa con unità adatta e 2 decimali (es. '70.45 ms')."""
#     if us_val >= 1e6:
#         return f"{us_val / 1e6:.2f} s"
#     elif us_val >= 1e3:
#         return f"{us_val / 1e3:.2f} ms"
#     else:
#         return f"{us_val:.2f} μs"


# def format_mem(mb_val):
#     """MB -> stringa con unità adatta e 2 decimali (es. '1.08 GB')."""
#     if mb_val <= 0:
#         return "0 MB"
#     if mb_val >= 1024:
#         return f"{mb_val / 1024:.2f} GB"
#     if mb_val >= 1:
#         return f"{mb_val:.2f} MB"
#     return f"{mb_val * 1024:.2f} KB"


# def time_tick(us, _=None):
#     if us >= 1e6:
#         return f"{us / 1e6:g} s"
#     if us >= 1e3:
#         return f"{us / 1e3:g} ms"
#     return f"{us:g} μs"


# def mem_tick(mb, _=None):
#     # Tick a decadi di MB (0.1 MB, 1 MB, ..., 1000 MB, 10000 MB): scala leggibile in modo uniforme
#     if mb >= 1:
#         return f"{mb:g} MB"
#     return f"{mb * 1000:g} KB"


# # ============================================================================
# # DATI
# # ============================================================================
# ALGO_MAP = {
#     "BruteForce":        "Baseline (Brute Force)",
#     "MiaSoluzione":      "Mia Sol. (LSH + W(v))",
#     "MiaSoluzione_NoWv": "Mia Sol. (LSH, no W(v))",
#     "Alternativa":       "Alternativa (T-μ LSH Indip.)",
# }
# ALGO_ORDER = list(ALGO_MAP.values())


# def pivot(df, col, cols_order):
#     return (df.pivot_table(index="dataset", columns="algorithm", values=col, aggfunc="first")
#               .reindex(columns=cols_order))


# def grouped_bars(ax, values, cols_order, colors, label_fn, zero_label=None):
#     """Barre raggruppate per dataset, con il valore preciso scritto sopra ogni barra.
#     Le barre con valore <= 0 non sono rappresentabili in scala log: se zero_label è dato,
#     viene scritta quell'etichetta in fondo all'asse."""
#     n_datasets = len(values)
#     n_alg = len(cols_order)
#     x = np.arange(n_datasets)
#     bar_width = 0.8 / max(n_alg, 1)

#     for i, col in enumerate(cols_order):
#         vals = values[col].to_numpy(dtype=float)
#         offset = (i - (n_alg - 1) / 2) * bar_width
#         plot_vals = np.where(vals > 0, vals, np.nan)
#         ax.bar(x + offset, plot_vals, width=bar_width * 0.92, color=colors[col],
#                edgecolor="black", linewidth=0.6, alpha=0.9, zorder=3)
#         for xi, v in zip(x + offset, vals):
#             if np.isfinite(v) and v > 0:
#                 ax.annotate(label_fn(v), (xi, v), xytext=(0, 3), textcoords="offset points",
#                             ha="center", va="bottom", fontsize=8, rotation=90, zorder=5)
#             elif np.isfinite(v) and zero_label is not None:
#                 ax.annotate(zero_label, (xi, 0.0), xycoords=("data", "axes fraction"),
#                             xytext=(0, 3), textcoords="offset points",
#                             ha="center", va="bottom", fontsize=8, rotation=90, zorder=5)

#     ax.set_xticks(x)
#     ax.set_xticklabels(values.index, rotation=20 if n_datasets > 2 else 0,
#                        ha="right" if n_datasets > 2 else "center", fontsize=10)
#     ax.set_xlim(-0.6, n_datasets - 0.4)
#     ax.set_xlabel("Dataset", fontsize=11)


# def set_log_limits(ax, values):
#     pos = values.to_numpy(dtype=float)
#     pos = pos[np.isfinite(pos) & (pos > 0)]
#     ax.set_yscale("log")
#     ax.yaxis.set_major_locator(ticker.LogLocator(base=10.0, numticks=50))   # un tick per ogni decade
#     ax.set_ylim(bottom=pos.min() / 4, top=pos.max() * 40)   # spazio per le etichette sopra le barre
#     ax.yaxis.set_minor_formatter(ticker.NullFormatter())


# def process_results(files):
#     if not files:
#         files = glob.glob(os.path.join("results", "*_summary.csv"))
#         if not files:
#             files = glob.glob("*_summary.csv")
#     if not files:
#         print("Errore: nessun file '*_summary.csv' trovato in 'results/' o nella cartella corrente.")
#         sys.exit(1)
#     print(f"File summary trovati ({len(files)}): {files}")

#     full_df = pd.concat([pd.read_csv(f) for f in files], ignore_index=True)
#     # Ignora eventuali righe di vecchie esecuzioni (es. BaselinePruning) non più prodotte
#     full_df = full_df[full_df["algorithm"].isin(ALGO_MAP.keys())].copy()
#     full_df["algorithm"] = full_df["algorithm"].map(ALGO_MAP)
#     if "build_time_ms" not in full_df.columns:
#         full_df["build_time_ms"] = np.nan

#     cols_order = [c for c in ALGO_ORDER if c in full_df["algorithm"].unique()]
#     colors = {c: plt.cm.tab10.colors[i] for i, c in enumerate(cols_order)}

#     time_us = pivot(full_df, "avg_query_time_us", cols_order)
#     mem_mb = pivot(full_df, "memory_mb", cols_order)
#     build_ms = pivot(full_df, "build_time_ms", cols_order)
#     n_datasets = len(time_us)

#     # -------------------------------------------------------------------------
#     # TABELLE
#     # -------------------------------------------------------------------------
#     out_dir = "tables"
#     os.makedirs(out_dir, exist_ok=True)

#     def show(title, table):
#         print("\n" + "=" * 90)
#         print(f" {title}")
#         print("=" * 90)
#         print(table.round(4).to_string())
#         print("=" * 90)

#     show("TABELLA 1: TEMPO MEDIO DI QUERY (μs)", time_us)
#     show("TABELLA 2: SPAZIO AUSILIARIO (MB)", mem_mb)
#     show("TABELLA 3: TEMPO DI COSTRUZIONE (ms)", build_ms)

#     time_us.to_csv(os.path.join(out_dir, "tabella_tempi_query_us.csv"))
#     mem_mb.to_csv(os.path.join(out_dir, "tabella_spazio_mb.csv"))
#     build_ms.to_csv(os.path.join(out_dir, "tabella_tempi_costruzione_ms.csv"))
#     print(f"\nTabelle salvate in '{out_dir}/'.")

#     # -------------------------------------------------------------------------
#     # PLOT: tempo medio di query | spazio ausiliario | tempo di costruzione
#     # -------------------------------------------------------------------------
#     panel_w = max(6.5, 1.8 * n_datasets)
#     fig, axes = plt.subplots(1, 3, figsize=(3 * panel_w, 6.5))
#     ax_t, ax_m, ax_b = axes

#     # --- Tempo medio di query (scala log)
#     grouped_bars(ax_t, time_us, cols_order, colors, label_fn=format_time)
#     set_log_limits(ax_t, time_us)
#     ax_t.yaxis.set_major_formatter(ticker.FuncFormatter(time_tick))
#     ax_t.set_ylabel("Tempo medio di query [scala log]", fontsize=11)
#     ax_t.set_title("Confronto Tempi Medi di Query", fontsize=12, fontweight="bold")
#     ax_t.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

#     # --- Spazio ausiliario (scala log; la baseline ha 0 MB e viene indicata con etichetta)
#     grouped_bars(ax_m, mem_mb, cols_order, colors, label_fn=format_mem, zero_label="0 MB")
#     set_log_limits(ax_m, mem_mb)
#     ax_m.yaxis.set_major_formatter(ticker.FuncFormatter(mem_tick))
#     ax_m.set_ylabel("Spazio ausiliario [scala log]", fontsize=11)
#     ax_m.set_title("Confronto Spazio Ausiliario", fontsize=12, fontweight="bold")
#     ax_m.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

#     # --- Tempo di costruzione (ms -> μs per riusare i formatter; scala log)
#     build_us = build_ms * 1000.0
#     if build_us.isna().all().all():
#         ax_b.text(0.5, 0.5, "build_time_ms non presente nei summary\n(ri-esegui l'esperimento aggiornato)",
#                   ha="center", va="center", transform=ax_b.transAxes)
#         ax_b.set_axis_off()
#     else:
#         grouped_bars(ax_b, build_us, cols_order, colors, label_fn=format_time)
#         set_log_limits(ax_b, build_us)
#         ax_b.yaxis.set_major_formatter(ticker.FuncFormatter(time_tick))
#         ax_b.set_ylabel("Tempo di costruzione [scala log]", fontsize=11)
#         ax_b.set_title("Confronto Tempi di Costruzione", fontsize=12, fontweight="bold")
#         ax_b.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

#     handles = [Patch(facecolor=colors[c], edgecolor="black", label=c) for c in cols_order]
#     fig.legend(handles=handles, loc="lower center", ncol=len(cols_order), fontsize=10,
#                frameon=True, bbox_to_anchor=(0.5, 0.0))
#     plt.tight_layout(rect=[0, 0.06, 1, 1])

#     plot_output = os.path.join(out_dir, "confronto_benchmark_dataset.png")
#     plt.savefig(plot_output, dpi=300, bbox_inches="tight")
#     print(f"Grafico salvato in '{plot_output}'.")
#     plt.close()


# if __name__ == "__main__":
#     process_results(sys.argv[1:])

import sys
import os
import glob
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
from matplotlib.patches import Patch


# ============================================================================
# FORMATTAZIONE (valori precisi sopra le barre e tick degli assi)
# ============================================================================
def format_time(us_val):
    """Microsecondi -> stringa con unità adatta e 2 decimali (es. '70.45 ms')."""
    if us_val >= 1e6:
        return f"{us_val / 1e6:.2f} s"
    elif us_val >= 1e3:
        return f"{us_val / 1e3:.2f} ms"
    else:
        return f"{us_val:.2f} μs"


def format_mem(mb_val):
    """MB -> stringa con unità adatta e 2 decimali (es. '1.08 GB')."""
    if mb_val <= 0:
        return "0 MB"
    if mb_val >= 1024:
        return f"{mb_val / 1024:.2f} GB"
    if mb_val >= 1:
        return f"{mb_val:.2f} MB"
    return f"{mb_val * 1024:.2f} KB"


def time_tick(us, _=None):
    if us >= 1e6:
        return f"{us / 1e6:g} s"
    if us >= 1e3:
        return f"{us / 1e3:g} ms"
    return f"{us:g} μs"


def mem_tick(mb, _=None):
    # Tick a decadi di MB (0.1 MB, 1 MB, ..., 1000 MB, 10000 MB): scala leggibile in modo uniforme
    if mb >= 1:
        return f"{mb:g} MB"
    return f"{mb * 1000:g} KB"


# ============================================================================
# DATI
# ============================================================================
ALGO_MAP = {
    "BruteForce":   "Baseline (Brute Force)",
    "MiaSoluzione": "Mia Soluzione (LSH Unificato)",
    # "MiaSoluzione_NoWv": "Mia Soluzione senza W(v)",
    #"Alternativa":  "Alternativa (T-μ LSH Indipendenti)",
    "Alternativa": "Alternativa (T-μ LSH Indipendenti)",
}
ALGO_ORDER = list(ALGO_MAP.values())


def pivot(df, col, cols_order):
    return (df.pivot_table(index="dataset", columns="algorithm", values=col, aggfunc="first")
              .reindex(columns=cols_order))


def grouped_bars(ax, values, cols_order, colors, label_fn, zero_label=None):
    """Barre raggruppate per dataset, con il valore preciso scritto sopra ogni barra.
    Le barre con valore <= 0 non sono rappresentabili in scala log: se zero_label è dato,
    viene scritta quell'etichetta in fondo all'asse."""
    n_datasets = len(values)
    n_alg = len(cols_order)
    x = np.arange(n_datasets)
    bar_width = 0.8 / max(n_alg, 1)

    for i, col in enumerate(cols_order):
        vals = values[col].to_numpy(dtype=float)
        offset = (i - (n_alg - 1) / 2) * bar_width
        plot_vals = np.where(vals > 0, vals, np.nan)
        ax.bar(x + offset, plot_vals, width=bar_width * 0.92, color=colors[col],
               edgecolor="black", linewidth=0.6, alpha=0.9, zorder=3)
        for xi, v in zip(x + offset, vals):
            if np.isfinite(v) and v > 0:
                ax.annotate(label_fn(v), (xi, v), xytext=(0, 3), textcoords="offset points",
                            ha="center", va="bottom", fontsize=8, rotation=90, zorder=5)
            elif np.isfinite(v) and zero_label is not None:
                ax.annotate(zero_label, (xi, 0.0), xycoords=("data", "axes fraction"),
                            xytext=(0, 3), textcoords="offset points",
                            ha="center", va="bottom", fontsize=8, rotation=90, zorder=5)

    ax.set_xticks(x)
    ax.set_xticklabels(values.index, rotation=20 if n_datasets > 2 else 0,
                       ha="right" if n_datasets > 2 else "center", fontsize=10)
    ax.set_xlim(-0.6, n_datasets - 0.4)
    ax.set_xlabel("Dataset", fontsize=11)


def set_log_limits(ax, values):
    pos = values.to_numpy(dtype=float)
    pos = pos[np.isfinite(pos) & (pos > 0)]
    ax.set_yscale("log")
    ax.yaxis.set_major_locator(ticker.LogLocator(base=10.0, numticks=50))   # un tick per ogni decade
    ax.set_ylim(bottom=pos.min() / 4, top=pos.max() * 40)   # spazio per le etichette sopra le barre
    ax.yaxis.set_minor_formatter(ticker.NullFormatter())


def process_results(files):
    if not files:
        files = glob.glob(os.path.join("results", "*_summary.csv"))
        if not files:
            files = glob.glob("*_summary.csv")
    if not files:
        print("Errore: nessun file '*_summary.csv' trovato in 'results/' o nella cartella corrente.")
        sys.exit(1)
    print(f"File summary trovati ({len(files)}): {files}")

    full_df = pd.concat([pd.read_csv(f) for f in files], ignore_index=True)
    # Ignora eventuali righe di vecchie esecuzioni (es. BaselinePruning) non più prodotte
    full_df = full_df[full_df["algorithm"].isin(ALGO_MAP.keys())].copy()
    full_df["algorithm"] = full_df["algorithm"].map(ALGO_MAP)
    if "build_time_ms" not in full_df.columns:
        full_df["build_time_ms"] = np.nan

    cols_order = [c for c in ALGO_ORDER if c in full_df["algorithm"].unique()]
    colors = {c: plt.cm.tab10.colors[i] for i, c in enumerate(cols_order)}

    time_us = pivot(full_df, "avg_query_time_us", cols_order)
    mem_mb = pivot(full_df, "memory_mb", cols_order)
    build_ms = pivot(full_df, "build_time_ms", cols_order)
    n_datasets = len(time_us)

    # -------------------------------------------------------------------------
    # TABELLE
    # -------------------------------------------------------------------------
    out_dir = "tables"
    os.makedirs(out_dir, exist_ok=True)

    def show(title, table):
        print("\n" + "=" * 90)
        print(f" {title}")
        print("=" * 90)
        print(table.round(4).to_string())
        print("=" * 90)

    show("TABELLA 1: TEMPO MEDIO DI QUERY (μs)", time_us)
    show("TABELLA 2: SPAZIO AUSILIARIO (MB)", mem_mb)
    show("TABELLA 3: TEMPO DI COSTRUZIONE (ms)", build_ms)

    time_us.to_csv(os.path.join(out_dir, "tabella_tempi_query_us.csv"))
    mem_mb.to_csv(os.path.join(out_dir, "tabella_spazio_mb.csv"))
    build_ms.to_csv(os.path.join(out_dir, "tabella_tempi_costruzione_ms.csv"))
    print(f"\nTabelle salvate in '{out_dir}/'.")

    # -------------------------------------------------------------------------
    # PLOT: tempo medio di query | spazio ausiliario | tempo di costruzione
    # -------------------------------------------------------------------------
    panel_w = max(6.5, 1.8 * n_datasets)
    fig, axes = plt.subplots(1, 3, figsize=(3 * panel_w, 6.5))
    ax_t, ax_m, ax_b = axes

    # --- Tempo medio di query (scala log)
    grouped_bars(ax_t, time_us, cols_order, colors, label_fn=format_time)
    set_log_limits(ax_t, time_us)
    ax_t.yaxis.set_major_formatter(ticker.FuncFormatter(time_tick))
    ax_t.set_ylabel("Tempo medio di query [scala log]", fontsize=11)
    ax_t.set_title("Confronto Tempi Medi di Query", fontsize=12, fontweight="bold")
    ax_t.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

    # --- Spazio ausiliario (scala log; la baseline ha 0 MB e viene indicata con etichetta)
    grouped_bars(ax_m, mem_mb, cols_order, colors, label_fn=format_mem, zero_label="0 MB")
    set_log_limits(ax_m, mem_mb)
    ax_m.yaxis.set_major_formatter(ticker.FuncFormatter(mem_tick))
    ax_m.set_ylabel("Spazio ausiliario [scala log]", fontsize=11)
    ax_m.set_title("Confronto Spazio Ausiliario", fontsize=12, fontweight="bold")
    ax_m.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

    # --- Tempo di costruzione (ms -> μs per riusare i formatter; scala log)
    build_us = build_ms * 1000.0
    if build_us.isna().all().all():
        ax_b.text(0.5, 0.5, "build_time_ms non presente nei summary\n(ri-esegui l'esperimento aggiornato)",
                  ha="center", va="center", transform=ax_b.transAxes)
        ax_b.set_axis_off()
    else:
        grouped_bars(ax_b, build_us, cols_order, colors, label_fn=format_time)
        set_log_limits(ax_b, build_us)
        ax_b.yaxis.set_major_formatter(ticker.FuncFormatter(time_tick))
        ax_b.set_ylabel("Tempo di costruzione [scala log]", fontsize=11)
        ax_b.set_title("Confronto Tempi di Costruzione", fontsize=12, fontweight="bold")
        ax_b.grid(True, linestyle=":", alpha=0.6, which="both", axis="y", zorder=0)

    handles = [Patch(facecolor=colors[c], edgecolor="black", label=c) for c in cols_order]
    fig.legend(handles=handles, loc="lower center", ncol=len(cols_order), fontsize=10,
               frameon=True, bbox_to_anchor=(0.5, 0.0))
    plt.tight_layout(rect=[0, 0.06, 1, 1])

    plot_output = os.path.join(out_dir, "confronto_benchmark_dataset.png")
    plt.savefig(plot_output, dpi=300, bbox_inches="tight")
    print(f"Grafico salvato in '{plot_output}'.")
    plt.close()


if __name__ == "__main__":
    process_results(sys.argv[1:])