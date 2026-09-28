import sys
import os
import glob
import pandas as pd
import matplotlib.pyplot as plt

def process_results(files):
    # Se nessun file viene passato da riga di comando, cerca in 'results/'
    if not files:
        files = glob.glob(os.path.join("results", "*_summary.csv"))
        if not files:
            # Fallback alla cartella corrente
            files = glob.glob("*_summary.csv")

    if not files:
        print("Errore: nessun file '*_summary.csv' trovato nella cartella 'results/' o corrente.")
        sys.exit(1)

    print(f"File summary identificati ({len(files)}): {files}")

    dfs = []
    for f in files:
        dfs.append(pd.read_csv(f))
    full_df = pd.concat(dfs, ignore_index=True)

    algo_map = {
        "BruteForce": "Baseline Brute Force",
        "MiaSoluzione": "Mia Soluzione (LSH Unificato)",
        "Alternativa": "Alternativa (T-mu LSH Indipendenti)"
    }
    full_df["algorithm"] = full_df["algorithm"].map(algo_map)

    # 1. TABELLA TEMPI MEDI QUERY
    pivot_time = full_df.pivot(index="dataset", columns="algorithm", values="avg_query_time_us")

    # 2. TABELLA MEMORIA AUSILIARIA (MB)
    pivot_mem = full_df.pivot(index="dataset", columns="algorithm", values="memory_mb")

    cols_order = [
        "Baseline Brute Force",
        "Mia Soluzione (LSH Unificato)",
        "Alternativa (T-mu LSH Indipendenti)"
    ]
    pivot_time = pivot_time.reindex(columns=cols_order)
    pivot_mem = pivot_mem.reindex(columns=cols_order)

    # Cartella di output richiesta
    out_dir = "tables"
    os.makedirs(out_dir, exist_ok=True)

    print("\n" + "=" * 80)
    print(" TABELLA 1: TEMPO MEDIO DI RISOLUZIONE DELLE QUERY (us)")
    print("=" * 80)
    print(pivot_time.round(2).to_string())
    print("=" * 80)

    print("\n" + "=" * 80)
    print(" TABELLA 2: SPAZIO AUSILIARIO OCCUPATO DALLE STRUTTURE (MB)")
    print("=" * 80)
    print(pivot_mem.round(4).to_string())
    print("=" * 80)

    # Salvataggio delle tabelle nella cartella tables/
    file_tabella_tempi = os.path.join(out_dir, "tabella_tempi_query.csv")
    file_tabella_memoria = os.path.join(out_dir, "tabella_spazio_mb.csv")
    pivot_time.to_csv(file_tabella_tempi)
    pivot_mem.to_csv(file_tabella_memoria)
    print(f"\nTabelle salvate in '{file_tabella_tempi}' e '{file_tabella_memoria}'.")

    # =========================================================================
    # PLOT COMPARATIVO MULTI-DATASET
    # =========================================================================
    fig, axes = plt.subplots(1, 2, figsize=(14, 5))

    # Grafico Tempi Query (Scala logaritmica)
    pivot_time.plot(kind="bar", ax=axes[0], colormap="viridis", edgecolor="black", alpha=0.85)
    axes[0].set_title("Confronto Tempi Medi di Query", fontsize=12, fontweight="bold")
    axes[0].set_ylabel("Tempo Medio di Query (us) [Log Scale]", fontsize=11)
    axes[0].set_yscale("log")
    axes[0].set_xlabel("Dataset", fontsize=11)
    axes[0].grid(True, linestyle=":", alpha=0.6, which="both")
    axes[0].legend(fontsize=9)
    axes[0].tick_params(axis="x", rotation=0)

    # Grafico Memoria Ausiliaria
    pivot_mem.plot(kind="bar", ax=axes[1], colormap="plasma", edgecolor="black", alpha=0.85)
    axes[1].set_title("Confronto Spazio Ausiliario Occupato", fontsize=12, fontweight="bold")
    axes[1].set_ylabel("Memoria Ausiliaria (MB)", fontsize=11)
    axes[1].set_xlabel("Dataset", fontsize=11)
    axes[1].grid(True, linestyle=":", alpha=0.6)
    axes[1].legend(fontsize=9)
    axes[1].tick_params(axis="x", rotation=0)

    plt.tight_layout()
    plot_output = os.path.join(out_dir, "confronto_benchmark_dataset.png")
    plt.savefig(plot_output, dpi=300)
    print(f"Grafico riassuntivo salvato in '{plot_output}'.")
    plt.close()

if __name__ == "__main__":
    files = sys.argv[1:]
    process_results(files)