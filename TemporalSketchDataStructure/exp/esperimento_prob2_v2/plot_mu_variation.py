#!/usr/bin/env python3
# =============================================================================
# plot_mu_variation.py
# -----------------------------------------------------------------------------
# Genera i grafici richiesti a partire da "results_mu_variation.csv" (prodotto
# da experiment_mu_variation.cpp). Il CSV contiene, per ogni singola query:
#   mu, method, query_id, v, query_time_us, space_entries
#
# Grafici prodotti (cartella di output di default: plots_mu/):
#   1) plot_1_tempi_medi_vs_mu.png   -> andamento dei tempi MEDI di query al
#                                       variare di mu, per i tre metodi
#   2) plot_2_spazio_occupato.png    -> spazio occupato dalle tre soluzioni
#                                       (al variare di mu)
#   3) plot_3_confronto_finale.png   -> confronto complessivo delle tre
#                                       soluzioni (tempo vs spazio, tutti i mu)
#   4) plot_4_PLACEHOLDER_speedup.png -> PLACEHOLDER: il quarto grafico
#                                       richiesto e' arrivato troncato nel
#                                       messaggio originale ("grafico che
#                                       mostri" seguito da nulla). Qui viene
#                                       mostrato, a titolo di segnaposto, lo
#                                       speedup delle due soluzioni LSH
#                                       rispetto al naive al variare di mu.
#                                       Va sostituito/adattato non appena la
#                                       richiesta viene chiarita.
#
# Uso:
#   python3 plot_mu_variation.py [percorso_csv] [cartella_output]
#   (default: results_mu_variation.csv  ->  plots_mu/)
# =============================================================================
import sys
import os
import pandas as pd
import matplotlib.pyplot as plt

METHOD_STYLE = {
    "naive":         dict(color="#d62728", marker="o", label="naive (brute-force)"),
    "mia_soluzione": dict(color="#2ca02c", marker="s", label="mia soluzione (LSH unificato + W(v))"),
    "alternativa":   dict(color="#1f77b4", marker="^", label="alternativa (T-mu LSH indipendenti)"),
}
METHOD_ORDER = ["naive", "mia_soluzione", "alternativa"]


def load(csv_path: str) -> pd.DataFrame:
    if not os.path.exists(csv_path):
        print(f"Errore: file non trovato: {csv_path}")
        print("Esegui prima ./experiment_mu_variation (compilato da "
              "experiment_mu_variation.cpp), che genera results_mu_variation.csv "
              "nella cartella corrente.")
        sys.exit(1)
    return pd.read_csv(csv_path)


def aggregate(df: pd.DataFrame) -> pd.DataFrame:
    """Una riga per (mu, method): tempo medio/std di query e spazio occupato
    (costante per (mu, method), quindi 'first' è corretto)."""
    g = df.groupby(["mu", "method"]).agg(
        query_time_us_mean=("query_time_us", "mean"),
        query_time_us_std=("query_time_us", "std"),
        space_entries=("space_entries", "first"),
    ).reset_index()
    return g


# =============================================================================
# 1) Tempi MEDI di query al variare di mu
# =============================================================================
def plot_1_tempi_medi_vs_mu(agg: pd.DataFrame, outdir: str) -> None:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    for method in METHOD_ORDER:
        sub = agg[agg["method"] == method].sort_values("mu")
        style = METHOD_STYLE[method]
        ax.errorbar(sub["mu"], sub["query_time_us_mean"], yerr=sub["query_time_us_std"],
                    capsize=3, **style)
    ax.set_yscale("log")
    ax.set_xlabel("mu (ampiezza della finestra)")
    ax.set_ylabel("tempo medio di query (us, scala log)")
    ax.set_title("Tempo medio di query al variare di mu")
    ax.grid(True, alpha=0.3, which="both")
    ax.legend()
    fig.tight_layout()
    path = os.path.join(outdir, "plot_1_tempi_medi_vs_mu.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"[ok] {path}")


# =============================================================================
# 2) Spazio occupato dalle soluzioni
# =============================================================================
def plot_2_spazio_occupato(agg: pd.DataFrame, outdir: str) -> None:
    fig, ax = plt.subplots(figsize=(8, 5.5))
    for method in METHOD_ORDER:
        sub = agg[agg["method"] == method].sort_values("mu")
        style = METHOD_STYLE[method]
        ax.plot(sub["mu"], sub["space_entries"], **style)
    ax.set_xlabel("mu (ampiezza della finestra)")
    ax.set_ylabel("spazio occupato\n(# hash table + # riferimenti a nodo indicizzati)")
    ax.set_title("Spazio occupato dalle soluzioni al variare di mu")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    path = os.path.join(outdir, "plot_2_spazio_occupato.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"[ok] {path}")


# =============================================================================
# 3) Confronto finale fra le tre soluzioni (tempo vs spazio, tutti i mu)
# =============================================================================
def plot_3_confronto_finale(agg: pd.DataFrame, outdir: str) -> None:
    fig, ax = plt.subplots(figsize=(8, 6))
    for method in METHOD_ORDER:
        sub = agg[agg["method"] == method].sort_values("mu")
        style = METHOD_STYLE[method]
        sizes = 40 + 6 * (sub["mu"] - sub["mu"].min())  # punti più grandi = mu più alto
        ax.scatter(sub["space_entries"], sub["query_time_us_mean"],
                   s=sizes, color=style["color"], marker=style["marker"],
                   label=style["label"], alpha=0.85, edgecolors="black", linewidths=0.5)
        # collega i punti in ordine di mu per mostrare la traiettoria
        ax.plot(sub["space_entries"], sub["query_time_us_mean"],
                color=style["color"], alpha=0.4, linewidth=1)
    ax.set_yscale("log")
    ax.set_xlabel("spazio occupato")
    ax.set_ylabel("tempo medio di query (us, scala log)")
    ax.set_title("Confronto finale: tempo vs spazio occupato\n"
                 "(ogni punto = un valore di mu; punti più grandi = mu più grande)")
    ax.grid(True, alpha=0.3, which="both")
    ax.legend()
    fig.tight_layout()
    path = os.path.join(outdir, "plot_3_confronto_finale.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"[ok] {path}")


# =============================================================================
# 4) PLACEHOLDER — la richiesta originale è arrivata troncata ("grafico che
#    mostri" senza seguito). In attesa di chiarimento, si mostra lo speedup
#    delle due soluzioni LSH rispetto al naive, al variare di mu.
# =============================================================================
def plot_4_placeholder_speedup(agg: pd.DataFrame, outdir: str) -> None:
    piv = agg.pivot(index="mu", columns="method", values="query_time_us_mean")
    if "naive" not in piv.columns:
        print("[skip] plot 4 (placeholder): colonna 'naive' assente")
        return
    fig, ax = plt.subplots(figsize=(8, 5.5))
    for method in ["mia_soluzione", "alternativa"]:
        if method not in piv.columns:
            continue
        speedup = piv["naive"] / piv[method]
        ax.plot(piv.index, speedup, **METHOD_STYLE[method])
    ax.axhline(1.0, color="gray", linestyle="--", linewidth=1)
    ax.set_xlabel("mu (ampiezza della finestra)")
    ax.set_ylabel("speedup rispetto al naive (tempo_naive / tempo_metodo)")
    ax.set_title("[PLACEHOLDER — richiesta originale troncata, da confermare]\n"
                 "Speedup delle soluzioni LSH rispetto al naive, al variare di mu")
    ax.grid(True, alpha=0.3)
    ax.legend()
    fig.tight_layout()
    path = os.path.join(outdir, "plot_4_PLACEHOLDER_speedup.png")
    fig.savefig(path, dpi=150)
    plt.close(fig)
    print(f"[ok] {path}  (PLACEHOLDER: sostituire quando la richiesta del 4o grafico sarà chiarita)")


def main():
    csv_path = sys.argv[1] if len(sys.argv) > 1 else "results_mu_variation.csv"
    outdir = sys.argv[2] if len(sys.argv) > 2 else "plots_mu"
    os.makedirs(outdir, exist_ok=True)

    df = load(csv_path)
    agg = aggregate(df)

    plot_1_tempi_medi_vs_mu(agg, outdir)
    plot_2_spazio_occupato(agg, outdir)
    plot_3_confronto_finale(agg, outdir)
    plot_4_placeholder_speedup(agg, outdir)

    print(f"\nFatto. Grafici salvati in: {os.path.abspath(outdir)}")


if __name__ == "__main__":
    main()
