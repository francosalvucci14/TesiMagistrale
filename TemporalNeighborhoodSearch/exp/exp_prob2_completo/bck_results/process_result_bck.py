import sys
import os
import glob
import pandas as pd
import numpy as np
import matplotlib
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


# ============================================================================
# CONVERSIONE AUTOMATICA μs → unità leggibile
# ============================================================================
def format_time(us_val):
    """Converte microsecondi in stringa leggibile con unità adatta."""
    if us_val >= 1e6:
        return f"{us_val / 1e6:.3f} s"
    elif us_val >= 1e3:
        return f"{us_val / 1e3:.2f} ms"
    else:
        return f"{us_val:.2f} μs"


def best_time_unit(us_series):
    """Restituisce (divisore, label unità) per normalizzare una serie di valori in μs."""
    mx = us_series.max()
    if mx >= 1e6:
        return 1e6, "s"
    elif mx >= 1e3:
        return 1e3, "ms"
    else:
        return 1.0, "μs"


def process_results(files):
    if not files:
        files = glob.glob(os.path.join("results", "*_summary.csv"))
        if not files:
            files = glob.glob("*_summary.csv")

    if not files:
        print("Errore: nessun file '*_summary.csv' trovato in 'results/' o nella cartella corrente.")
        sys.exit(1)

    print(f"File summary trovati ({len(files)}): {files}")

    dfs = [pd.read_csv(f) for f in files]
    full_df = pd.concat(dfs, ignore_index=True)

    # Mappa nomi interni → nomi leggibili
    algo_map = {
        "BruteForce":      "Brute Force",
        "BaselinePruning": "Baseline + Pruning W(v)",
        "MiaSoluzione":    "Mia Soluzione (LSH Unificato)",
        "Alternativa":     "Alternativa (T-μ LSH Indipendenti)",
    }
    # Mantieni solo algoritmi presenti nei dati
    full_df["algorithm"] = full_df["algorithm"].map(algo_map).fillna(full_df["algorithm"])

    # Ordine colonne
    cols_order = [c for c in [
        "Brute Force",
        "Baseline + Pruning W(v)",
        "Mia Soluzione (LSH Unificato)",
        "Alternativa (T-μ LSH Indipendenti)",
    ] if c in full_df["algorithm"].unique()]

    # Pivot tabelle
    pivot_time_us  = full_df.pivot(index="dataset", columns="algorithm", values="avg_query_time_us").reindex(columns=cols_order)
    pivot_mem      = full_df.pivot(index="dataset", columns="algorithm", values="memory_mb").reindex(columns=cols_order)

    n_datasets = len(pivot_time_us)

    # -------------------------------------------------------------------------
    # TABELLA TEMPI: converti automaticamente in unità leggibile
    # -------------------------------------------------------------------------
    div, unit = best_time_unit(pivot_time_us.values.flatten()[~np.isnan(pivot_time_us.values.flatten())])
    pivot_time_display = pivot_time_us / div

    out_dir = "tables"
    os.makedirs(out_dir, exist_ok=True)

    print("\n" + "=" * 80)
    print(f" TABELLA 1: TEMPO MEDIO DI QUERY ({unit})")
    print("=" * 80)
    print(pivot_time_display.round(4).to_string())
    print("=" * 80)

    print("\n" + "=" * 80)
    print(" TABELLA 2: SPAZIO AUSILIARIO (MB)")
    print("=" * 80)
    print(pivot_mem.round(4).to_string())
    print("=" * 80)

    # Salva CSV (sempre in μs per precisione)
    pivot_time_us.to_csv(os.path.join(out_dir, "tabella_tempi_query_us.csv"))
    pivot_time_display.to_csv(os.path.join(out_dir, f"tabella_tempi_query_{unit.replace('μ','u')}.csv"))
    pivot_mem.to_csv(os.path.join(out_dir, "tabella_spazio_mb.csv"))
    print(f"\nTabelle salvate in '{out_dir}/'.")

    # =========================================================================
    # PLOT COMPARATIVO — asse X adattivo al numero di dataset
    # =========================================================================
    fig_w = max(10, 4.5 * n_datasets)
    fig, axes = plt.subplots(1, 2, figsize=(fig_w, 5))

    # Larghezza barre adattiva
    bar_width = max(0.12, 0.6 / max(len(cols_order), 1))
    x = np.arange(n_datasets)
    colors = plt.cm.tab10.colors

    # --- PLOT TEMPI (con asse Y in unità leggibile + tick significativi) ---
    ax0 = axes[0]
    for i, col in enumerate(cols_order):
        vals = pivot_time_display[col].values if col in pivot_time_display else np.full(n_datasets, np.nan)
        offset = (i - (len(cols_order) - 1) / 2) * bar_width
        bars = ax0.bar(x + offset, vals, width=bar_width * 0.92,
                       label=col, color=colors[i % len(colors)],
                       edgecolor="black", alpha=0.85)

    ax0.set_yscale("log")

    # Tick Y significativi: no "10^N" generico — usa valori reali
    ax0.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda y, _: (f"{y:.3g} {unit}" if y >= 0.1 else f"{y:.1e} {unit}")))
    ax0.yaxis.set_minor_formatter(ticker.NullFormatter())

    ax0.set_xticks(x)
    ax0.set_xticklabels(pivot_time_display.index, rotation=20 if n_datasets > 3 else 0,
                        ha="right" if n_datasets > 3 else "center", fontsize=10)
    ax0.set_xlabel("Dataset", fontsize=11)
    ax0.set_ylabel(f"Tempo medio di query ({unit}) [scala log]", fontsize=11)
    ax0.set_title("Confronto Tempi Medi di Query", fontsize=12, fontweight="bold")
    ax0.grid(True, linestyle=":", alpha=0.6, which="both", axis="y")
    ax0.legend(fontsize=8, loc="upper right")

    # --- PLOT MEMORIA ---
    ax1 = axes[1]
    for i, col in enumerate(cols_order):
        vals = pivot_mem[col].values if col in pivot_mem else np.full(n_datasets, np.nan)
        offset = (i - (len(cols_order) - 1) / 2) * bar_width
        ax1.bar(x + offset, vals, width=bar_width * 0.92,
                label=col, color=colors[i % len(colors)],
                edgecolor="black", alpha=0.85)

    ax1.set_xticks(x)
    ax1.set_xticklabels(pivot_mem.index, rotation=20 if n_datasets > 3 else 0,
                        ha="right" if n_datasets > 3 else "center", fontsize=10)
    ax1.set_xlabel("Dataset", fontsize=11)
    ax1.set_ylabel("Spazio ausiliario (MB)", fontsize=11)
    ax1.set_title("Confronto Spazio Ausiliario", fontsize=12, fontweight="bold")
    ax1.grid(True, linestyle=":", alpha=0.6, axis="y")
    ax1.legend(fontsize=8, loc="upper right")

    plt.tight_layout()
    plot_output = os.path.join(out_dir, "confronto_benchmark_dataset.png")
    plt.savefig(plot_output, dpi=300, bbox_inches="tight")
    print(f"Grafico salvato in '{plot_output}'.")
    plt.close()


if __name__ == "__main__":
    files = sys.argv[1:]
    process_results(files)