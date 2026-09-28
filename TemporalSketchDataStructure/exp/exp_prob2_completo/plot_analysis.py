import sys
import os
import glob
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt

def plot_wv_and_gain(input_path):
    # Se il path non esiste, cerca nella cartella 'results/'
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

    # Cartella di output richiesta
    out_dir = "plot_analysis"
    os.makedirs(out_dir, exist_ok=True)

    fig, axes = plt.subplots(1, 2, figsize=(15, 6))

    # =========================================================================
    # PLOT 1: Analisi della size di W(v) e verifica dei bound
    # =========================================================================
    ax1 = axes[0]
    T_minus_mu = df["no_pruning_windows"].iloc[0]

    # Baseline senza pruning
    ax1.axhline(y=T_minus_mu, color="#d62728", linestyle="--", linewidth=1.5,
                label=f"Senza Pruning ($T - \\mu = {T_minus_mu}$)")

    # Bound teorici ordinati per lambda
    df_sorted = df.sort_values(by="lambda_size")
    ax1.plot(df_sorted["lambda_size"], df_sorted["upper_bound"],
             color="#ff7f0e", linestyle="-", linewidth=2.0,
             label=r"Upper Bound: $\min(T-\mu, (\mu+1)|\Lambda(v)|)$")

    ax1.plot(df_sorted["lambda_size"], df_sorted["lower_bound"],
             color="#2ca02c", linestyle="-.", linewidth=2.0,
             label=r"Lower Bound (Caso denso): $|\Lambda(v)| + \mu$")

    # Nodi sperimentali resi piccoli per evitare occlusioni
    ax1.scatter(df["lambda_size"], df["w_size"],
                color="#1f77b4", s=14, alpha=0.55, edgecolors="none", zorder=4,
                label=r"Nodi Sperimentali $|W(v)|$")

    ax1.set_xlabel(r"Cardinalità eventi attivi $|\Lambda(v)|$", fontsize=12)
    ax1.set_ylabel(r"Numero finestre rilevanti $|W(v)|$", fontsize=12)
    ax1.set_title(f"Analisi di $W(v)$ e Verifica Bound Teorici\n({dataset_name})", fontsize=13, fontweight="bold")
    ax1.grid(True, linestyle=":", alpha=0.6)
    ax1.legend(loc="lower right", fontsize=10, framealpha=0.9)

    # =========================================================================
    # PLOT 2: Fattore di Guadagno (Ordinato dal migliore al peggiore)
    # =========================================================================
    ax2 = axes[1]
    df_sorted_gain = df.sort_values(by="gain_factor", ascending=False).reset_index(drop=True)
    ranks = np.arange(1, len(df_sorted_gain) + 1)
    gains = df_sorted_gain["gain_factor"].values

    avg_gain = gains.mean()
    avg_pruned_pct = (1.0 - (df["w_size"].mean() / T_minus_mu)) * 100.0

    # Punti singoli uniti da linea tratteggiata (NO barre)
    ax2.plot(ranks, gains, color="#1f77b4", linestyle="--", linewidth=1.2, alpha=0.85, zorder=2)
    ax2.scatter(ranks, gains, color="#1f77b4", s=12, alpha=0.75, zorder=3,
                label=r"Fattore $\frac{T-\mu}{|W(v)|}$ per nodo")

    # Linea orizzontale riduzione media
    ax2.axhline(y=avg_gain, color="#d62728", linestyle="-", linewidth=2.0, zorder=4,
                label=f"Riduzione media: {avg_gain:.2f}x ({avg_pruned_pct:.1f}% potate)")

    ax2.set_xlabel("Rango Nodi (ordinati per guadagno decrescente)", fontsize=12)
    ax2.set_ylabel(r"Fattore di Guadagno $\frac{T-\mu}{|W(v)|}$", fontsize=12)
    ax2.set_title(f"Efficacia del Pruning Temporale\n({dataset_name})", fontsize=13, fontweight="bold")
    ax2.grid(True, linestyle=":", alpha=0.6)
    ax2.legend(loc="upper right", fontsize=10, framealpha=0.9)

    plt.tight_layout()
    output_png = os.path.join(out_dir, f"{dataset_name}_pruning_analysis.png")
    plt.savefig(output_png, dpi=300)
    print(f"Plot salvato con successo in: {output_png}")
    plt.close()

if __name__ == "__main__":
    files = sys.argv[1:]
    if not files:
        files = glob.glob(os.path.join("results", "*_wv_analysis.csv"))
        if not files:
            files = glob.glob("*_wv_analysis.csv")

    if not files:
        print("Errore: nessun file '*_wv_analysis.csv' trovato nella cartella 'results/' o corrente.")
        sys.exit(1)

    print(f"File di analisi identificati ({len(files)}): {files}")
    for file in files:
        plot_wv_and_gain(file)