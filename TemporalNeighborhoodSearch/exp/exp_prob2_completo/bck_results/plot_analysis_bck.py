import sys
import os
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


def plot_wv_and_gain(input_path):
    actual_path = input_path
    if not os.path.exists(actual_path):
        candidate = os.path.join("results", input_path)
        if os.path.exists(candidate):
            actual_path = candidate
        else:
            print(f"Errore: impossibile trovare il file né in '{input_path}' né in '{candidate}'.")
            sys.exit(1)

    df = pd.read_csv(actual_path)
    base_name = os.path.basename(actual_path)
    dataset_name = base_name.replace("_wv_analysis.csv", "")

    out_dir = "plot_analysis"
    os.makedirs(out_dir, exist_ok=True)

    T_minus_mu = int(df["no_pruning_windows"].iloc[0])
    n_nodes = len(df)

    fig, axes = plt.subplots(1, 2, figsize=(15, 6))

    # =========================================================================
    # PLOT 1: Analisi di W(v) — bound teorici + punti sperimentali
    # =========================================================================
    ax1 = axes[0]

    # Rimuovi i nodi senza attività (lambda=0, w=0) dal plot bound
    df_active = df[df["lambda_size"] > 0].sort_values("lambda_size")

    # Baseline senza pruning
    ax1.axhline(y=T_minus_mu, color="#d62728", linestyle="--", linewidth=1.5,
                label=f"Senza Pruning ($T-\\mu = {T_minus_mu:,}$)")

    # Upper bound come linea continua sui nodi ordinati per lambda
    ax1.plot(df_active["lambda_size"], df_active["upper_bound"],
             color="#ff7f0e", linestyle="-", linewidth=2.0, zorder=3,
             label=r"Upper Bound: $\min(T-\mu,\;(\mu+1)|\Lambda(v)|)$")

    # Lower bound
    ax1.plot(df_active["lambda_size"], df_active["lower_bound"],
             color="#2ca02c", linestyle="-.", linewidth=1.8, zorder=3,
             label=r"Lower Bound (caso denso): $|\Lambda(v)|+\mu$")

    # Punti sperimentali — piccoli, semi-trasparenti
    ax1.scatter(df["lambda_size"], df["w_size"],
                color="#1f77b4", s=8, alpha=0.45, edgecolors="none", zorder=4,
                label=r"$|W(v)|$ sperimentale")

    # Formattazione assi: evita notazione scientifica generica
    ax1.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f"{int(x):,}" if x == int(x) else f"{x:,.1f}"))
    ax1.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda y, _: f"{int(y):,}" if y == int(y) else f"{y:,.1f}"))

    ax1.set_xlabel(r"Cardinalità eventi attivi $|\Lambda(v)|$", fontsize=12)
    ax1.set_ylabel(r"Numero finestre rilevanti $|W(v)|$", fontsize=12)
    ax1.set_title(
        f"Analisi di $W(v)$ — Bound Teorici vs Dati Sperimentali\n"
        f"({dataset_name}, {n_nodes:,} nodi, $T-\\mu={T_minus_mu:,}$)",
        fontsize=12, fontweight="bold")
    ax1.grid(True, linestyle=":", alpha=0.6)
    ax1.legend(loc="upper left", fontsize=9, framealpha=0.9)

    # =========================================================================
    # PLOT 2: Fattore di Guadagno — ordinato dal migliore al peggiore
    # =========================================================================
    ax2 = axes[1]

    df_gain = df.sort_values("gain_factor", ascending=False).reset_index(drop=True)
    ranks = np.arange(1, len(df_gain) + 1)
    gains = df_gain["gain_factor"].values

    avg_gain = gains.mean()
    # Percentuale di finestre potate in media
    mean_wv = df["w_size"].mean()
    avg_pruned_pct = max(0.0, (1.0 - mean_wv / T_minus_mu) * 100.0) if T_minus_mu > 0 else 0.0

    # Curva tratteggiata con punti piccoli
    ax2.plot(ranks, gains, color="#1f77b4", linestyle="--", linewidth=1.0,
             alpha=0.7, zorder=2)
    ax2.scatter(ranks, gains, color="#1f77b4", s=6, alpha=0.65, zorder=3,
                label=r"Fattore $\frac{T-\mu}{|W(v)|}$ per nodo")

    # Linea media
    ax2.axhline(y=avg_gain, color="#d62728", linestyle="-", linewidth=2.0, zorder=4,
                label=f"Riduzione media: {avg_gain:.2f}× ({avg_pruned_pct:.1f}% finestre potate)")

    # Asse x adattivo: mostra massimo 10 tick significativi
    step = max(1, n_nodes // 10)
    ax2.xaxis.set_major_locator(ticker.MultipleLocator(step))
    ax2.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f"{int(x):,}"))

    # Asse y: usa formato numerico leggibile
    ax2.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda y, _: f"{y:,.1f}" if y < 1000 else f"{y:,.0f}"))

    ax2.set_xlabel("Rango nodi (guadagno decrescente)", fontsize=12)
    ax2.set_ylabel(r"Fattore di Guadagno $\frac{T-\mu}{|W(v)|}$", fontsize=12)
    ax2.set_title(
        f"Efficacia del Pruning Temporale\n"
        f"({dataset_name}, {n_nodes:,} nodi)",
        fontsize=12, fontweight="bold")
    ax2.grid(True, linestyle=":", alpha=0.6)
    ax2.legend(loc="upper right", fontsize=9, framealpha=0.9)

    plt.tight_layout()
    output_png = os.path.join(out_dir, f"{dataset_name}_pruning_analysis.png")
    plt.savefig(output_png, dpi=300, bbox_inches="tight")
    print(f"Plot salvato in: {output_png}")
    plt.close()


if __name__ == "__main__":
    files = sys.argv[1:]
    if not files:
        files = glob.glob(os.path.join("results", "*_wv_analysis.csv"))
        if not files:
            files = glob.glob("*_wv_analysis.csv")

    if not files:
        print("Errore: nessun file '*_wv_analysis.csv' trovato in 'results/' o nella cartella corrente.")
        sys.exit(1)

    print(f"File analisi trovati ({len(files)}): {files}")
    for f in files:
        plot_wv_and_gain(f)