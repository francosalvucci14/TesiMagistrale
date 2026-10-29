import sys
import os
import glob
import pandas as pd
import numpy as np
import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker


# Dimensioni dei font del grafico di analisi W(v), modificabili a piacere.
FONT_SIZE_AXIS_LABELS = 28
FONT_SIZE_TICKS = 22
FONT_SIZE_LEGEND = 22
FONT_SIZE_TITLE = 22


def plot_wv_and_gain(input_path):
    actual_path = input_path
    if not os.path.exists(actual_path):
        candidate = os.path.join("results_K_grande", input_path)
        if os.path.exists(candidate):
            actual_path = candidate
        else:
            print(f"Errore: impossibile trovare il file né in '{input_path}' né in '{candidate}'.")
            sys.exit(1)

    df = pd.read_csv(actual_path)
    base_name = os.path.basename(actual_path)
    dataset_name = base_name.replace("_wv_analysis.csv", "")

    out_dir = "plot_analysis_K_grande"
    os.makedirs(out_dir, exist_ok=True)

    T_minus_mu = int(df["no_pruning_windows"].iloc[0])
    n_nodes = len(df)

    fig, ax1 = plt.subplots(1, 1, figsize=(21, 10))

    # =========================================================================
    # PLOT 1: Analisi di W(v) — bound teorici + punti sperimentali
    # =========================================================================

    # I dati CSV storici possono contenere il vecchio lower_bound = |Lambda(v)| + mu.
    # Per il grafico si usa invece |Lambda(v)|, ricalcolandolo dalla colonna lambda_size.
    # Questo bound e' valido lontano dagli estremi della timeline: vicino ai bordi
    # piu' eventi possono corrispondere alla stessa finestra iniziale ammissibile,
    # producendo |W(v)| < |Lambda(v)|. Tali punti sono omessi SOLO dallo scatter
    # di questo pannello, non dai CSV ne' dal plot del fattore di guadagno.
    df_active = df[df["lambda_size"] > 0].sort_values("lambda_size")
    df_bound = df_active[df_active["lambda_size"] <= T_minus_mu]
    df_scatter = df[df["w_size"] >= df["lambda_size"]]
    excluded = len(df) - len(df_scatter)
    if excluded:
        print(f"[{dataset_name}] Effetti di bordo: {excluded} punti con "
              "|W(v)| < |Lambda(v)| esclusi dal solo scatter dei bound.")

    # Upper bound invariato.
    ax1.plot(df_active["lambda_size"], df_active["upper_bound"],
             color="#ff7f0e", linestyle="-", linewidth=2.0, zorder=3,
             label=r"Upper Bound: $\min(T-\mu,\;(\mu+1)|\Lambda(v)|)$")

    # Lower bound aggiornato: |Lambda(v)|, senza usare la vecchia colonna lower_bound.
    # Non estendere la retta oltre il numero di finestre disponibili (|W(v)| <= T-mu).
    ax1.plot(df_bound["lambda_size"], df_bound["lambda_size"],
             color="#2ca02c", linestyle="-.", linewidth=1.8, zorder=3,
             label=r"Lower Bound (senza effetti di bordo): $|\Lambda(v)|$")

    # Punti sperimentali: esclude soltanto i casi che violano il bound non troncato.
    ax1.scatter(df_scatter["lambda_size"], df_scatter["w_size"],
                color="#1f77b4", s=8, alpha=0.45, edgecolors="none", zorder=4,
                label=r"Valori di $|W(v)|$")

    # Formattazione assi: evita notazione scientifica generica
    ax1.xaxis.set_major_formatter(ticker.FuncFormatter(
        lambda x, _: f"{int(x):,}" if x == int(x) else f"{x:,.1f}"))
    ax1.yaxis.set_major_formatter(ticker.FuncFormatter(
        lambda y, _: f"{int(y):,}" if y == int(y) else f"{y:,.1f}"))

    ax1.set_xlabel(r"Cardinalità eventi attivi $|\Lambda(v)|$",
                   fontsize=FONT_SIZE_AXIS_LABELS)
    ax1.set_ylabel(r"Numero finestre rilevanti $|W(v)|$",
                   fontsize=FONT_SIZE_AXIS_LABELS)
    ax1.tick_params(axis="both", labelsize=FONT_SIZE_TICKS)
    ax1.set_title(
        f"Analisi di $W(v)$ — Bound Teorici vs Dati Sperimentali\n"
        f"({dataset_name}, {n_nodes:,} nodi, $T-\\mu={T_minus_mu:,}$)",
        fontsize=FONT_SIZE_TITLE, fontweight="bold")
    ax1.grid(True, linestyle=":", alpha=0.6)
    ax1.legend(loc="upper left", fontsize=FONT_SIZE_LEGEND, framealpha=0.9)

    # # =========================================================================
    # # PLOT 2: Fattore di guadagno ordinato e riduzione media
    # # =========================================================================
    # ax2 = axes[1]

    # df_gain = df.sort_values("gain_factor", ascending=False).reset_index(drop=True)
    # ranks = np.arange(1, len(df_gain) + 1)
    # gains = df_gain["gain_factor"].to_numpy(dtype=float)

    # avg_gain = gains.mean()
    # mean_wv = df["w_size"].mean()
    # avg_pruned_pct = max(0.0, (1.0 - mean_wv / T_minus_mu) * 100.0) if T_minus_mu > 0 else 0.0

    # ax2.plot(ranks, gains, color="#1f77b4", linestyle="--", linewidth=1.0,
    #          alpha=0.7, zorder=2)
    # ax2.scatter(ranks, gains, color="#1f77b4", s=6, alpha=0.65, zorder=3,
    #             label=r"Fattore $\frac{T-\mu}{|W(v)|}$ per nodo")
    # ax2.axhline(y=avg_gain, color="#d62728", linestyle="-", linewidth=2.0, zorder=4,
    #             label=f"Riduzione media: {avg_gain:.2f}× ({avg_pruned_pct:.1f}% finestre potate)")

    # ax2.set_xscale("log")
    # ax2.set_yscale("log")
    # ax2.set_xlim(0.8, n_nodes * 1.25)
    # ax2.xaxis.set_major_locator(ticker.LogLocator(base=10.0, numticks=20))
    # ax2.xaxis.set_major_formatter(ticker.FuncFormatter(
    #     lambda x, _: f"{int(x):,}" if x >= 1 else ""))
    # ax2.xaxis.set_minor_formatter(ticker.NullFormatter())
    # ax2.yaxis.set_major_formatter(ticker.FuncFormatter(
    #     lambda y, _: f"{y:,.0f}" if y >= 1 else f"{y:g}"))
    # ax2.yaxis.set_minor_formatter(ticker.NullFormatter())

    # ax2.set_xlabel("Rango nodi (guadagno decrescente) [scala log]", fontsize=12)
    # ax2.set_ylabel(r"Fattore di Guadagno $\frac{T-\mu}{|W(v)|}$", fontsize=12)
    # ax2.set_title(
    #     f"Efficacia del Pruning Temporale\n"
    #     f"({dataset_name}, {n_nodes:,} nodi)",
    #     fontsize=12, fontweight="bold")
    # ax2.grid(True, linestyle=":", alpha=0.6, which="major")
    # ax2.legend(loc="upper right", fontsize=9, framealpha=0.9)

    # # =========================================================================
    # # PLOT 3: CCDF del fattore di guadagno
    # # =========================================================================
    # ax3 = axes[2]

    # gains = df["gain_factor"].to_numpy(dtype=float)
    # if not np.all(np.isfinite(gains) & (gains > 0)):
    #     raise ValueError("I valori di 'gain_factor' devono essere finiti e positivi per il plot log-log.")

    # gain_values, gain_counts = np.unique(gains, return_counts=True)
    # ccdf = np.cumsum(gain_counts[::-1])[::-1] / len(gains)

    # ax3.plot(gain_values, ccdf, color="#1f77b4", linestyle="-", linewidth=1.2,
    #          alpha=0.8, zorder=2)
    # ax3.scatter(gain_values, ccdf, color="#1f77b4", s=12, alpha=0.75, zorder=3,
    #             label=r"CCDF empirica: $\Pr(G \geq g)$")

    # ax3.set_xscale("log")
    # ax3.set_yscale("log")
    # ax3.xaxis.set_major_locator(ticker.LogLocator(base=10.0))
    # ax3.yaxis.set_major_locator(ticker.LogLocator(base=10.0))
    # ax3.xaxis.set_minor_formatter(ticker.NullFormatter())
    # ax3.yaxis.set_minor_formatter(ticker.NullFormatter())

    # ax3.set_xlabel(r"Fattore di Guadagno $G=\frac{T-\mu}{|W(v)|}$", fontsize=12)
    # ax3.set_ylabel(r"CCDF $\Pr(G \geq g)$", fontsize=12)
    # ax3.set_title(
    #     f"CCDF del Fattore di Guadagno (scala log-log)\n"
    #     f"({dataset_name}, {n_nodes:,} nodi)",
    #     fontsize=12, fontweight="bold")
    # ax3.grid(True, linestyle=":", alpha=0.6, which="both")
    # ax3.legend(loc="best", fontsize=9, framealpha=0.9)

    plt.tight_layout()
    output_png = os.path.join(out_dir, f"{dataset_name}_pruning_analysis_only_bound.png")
    plt.savefig(output_png, dpi=300, bbox_inches="tight")
    print(f"Plot salvato in: {output_png}")
    plt.close()


def plot_check_validity(input_path):
    """Confronta le query booleane della LSH con la baseline esaustiva.

    Ogni riga e' un nodo sorgente diverso, per cui il conteggio non duplica nodi.
    Il test e' rispetto alla soglia sulle stesse signature MinHash (non Jaccard esatto).
    """
    df = pd.read_csv(input_path)
    required = {"query_index", "source_node_id", "bruteforce_found", "mia_soluzione_found"}
    missing = required.difference(df.columns)
    if missing:
        raise ValueError(f"{input_path}: colonne mancanti: {sorted(missing)}")
    if df.empty:
        raise ValueError(f"{input_path}: nessuna query da visualizzare")
    if df["source_node_id"].duplicated().any():
        raise ValueError(f"{input_path}: nodi sorgente ripetuti; il conteggio richiede nodi distinti")
    if not df["query_index"].is_monotonic_increasing:
        df = df.sort_values("query_index")

    baseline = df["bruteforce_found"].to_numpy(dtype=int)
    ours = df["mia_soluzione_found"].to_numpy(dtype=int)
    if not np.isin(baseline, [0, 1]).all() or not np.isin(ours, [0, 1]).all():
        raise ValueError(f"{input_path}: i risultati devono essere booleani (0 o 1)")

    bf_total = int(baseline.sum())
    our_total = int(ours.sum())
    true_positives = int(np.count_nonzero((baseline == 1) & (ours == 1)))
    false_negatives = int(np.count_nonzero((baseline == 1) & (ours == 0)))
    false_positives = int(np.count_nonzero((baseline == 0) & (ours == 1)))
    recall = true_positives / bf_total if bf_total else None

    dataset_name = os.path.basename(input_path).removesuffix("_validity.csv")
    out_dir = "plot_analysis_K_grande/"
    os.makedirs(out_dir, exist_ok=True)

    # L'andamento e' cumulativo per numero di nodi sorgente interrogati.
    x = np.arange(len(df) + 1)
    bf_cumulative = np.r_[0, np.cumsum(baseline)]
    our_cumulative = np.r_[0, np.cumsum(ours)]

    fig, (ax, ax_bar) = plt.subplots(1, 2, figsize=(16, 6),
                                      gridspec_kw={"width_ratios": [1.65, 1]})
    ax.plot(x, bf_cumulative, color="#292929", linewidth=2.2,
            label=f"Brute Force (esaustiva): {bf_total:,}")
    ax.plot(x, our_cumulative, color="#1f77b4", linewidth=2.2,
            label=f"Mia soluzione (LSH): {our_total:,}")
    ax.fill_between(x, our_cumulative, bf_cumulative,
                    where=bf_cumulative > our_cumulative,
                    alpha=0.18, color="#d62728", interpolate=True,
                    label="Differenza di risposte positive")
    ax.set_xlim(0, len(df))
    ax.set_ylim(bottom=0)
    ax.xaxis.set_major_formatter(ticker.StrMethodFormatter("{x:,.0f}"))
    ax.yaxis.set_major_formatter(ticker.StrMethodFormatter("{x:,.0f}"))
    ax.set_xlabel("Numero di nodi sorgente interrogati (distinti)")
    ax.set_ylabel("Query con risposta True (cumulativo)")
    ax.set_title("Andamento cumulativo delle risposte positive")
    ax.grid(True, linestyle=":", alpha=0.55)
    ax.legend(loc="upper left", fontsize=10)

    labels = ["GroundTruth", "NostraSoluzione"]
    values = [bf_total, our_total]
    bars = ax_bar.bar(labels, values, color=["#525252", "#1f77b4"],
                      width=0.55, edgecolor="white")
    for bar, value in zip(bars, values):
        ax_bar.annotate(f"{value:,}",
                        (bar.get_x() + bar.get_width() / 2, bar.get_height()),
                        xytext=(0, 5), textcoords="offset points", ha="center",
                        va="bottom", fontsize=12, fontweight="bold")
    ax_bar.set_ylabel("Nodi sorgente con query True")
    ax_bar.set_ylim(0, max(max(values), 1) * 1.25)
    ax_bar.grid(True, axis="y", linestyle=":", alpha=0.45)
    ax_bar.set_axisbelow(True)
    ax_bar.set_title("Risposte positive sul campione")
    recall_text = f"{recall * 100:.2f}%" if recall is not None else "N/D (BF=0)"
    message = (f"Recall vs GroundTruth: {recall_text}\n"
               f"Validi non trovati: {false_negatives:,}\n"
               f"Falsi positivi: {false_positives:,}")
    ax_bar.text(0.5, 0.96, message, transform=ax_bar.transAxes,
                ha="center", va="top", fontsize=10,
                bbox={"boxstyle": "round,pad=0.5", "facecolor": "white", "edgecolor": "#bbbbbb"})

    fig.suptitle(f"Verifica validità vs GroundTruth — {dataset_name}  "
                 f"({len(df):,} query distinte)", fontsize=14, fontweight="bold")
    fig.text(0.5, 0.015,
             "Criterio di validità: soglia applicata alla similarità MinHash stimata, non al Jaccard esatto",
             ha="center", va="center", fontsize=9, color="#555555")
    fig.tight_layout(rect=[0, 0.045, 1, 0.95])
    output_png = os.path.join(out_dir, f"{dataset_name}_check_validity.png")
    fig.savefig(output_png, dpi=300, bbox_inches="tight")
    plt.close(fig)
    print(f"Plot validita' salvato in: {output_png} | Recall: {recall_text} | "
          f"FN: {false_negatives} | FP: {false_positives}")


if __name__ == "__main__":
    arguments = sys.argv[1:]
    if arguments:
        wv_files = [f for f in arguments if f.endswith("_wv_analysis.csv")]
        validity_files = [f for f in arguments if f.endswith("_validity.csv")]
        # Compatibilita' con il vecchio comando python3 plot_analysis.py file_wv_analysis.csv
        for f in wv_files:
            related = f.removesuffix("_wv_analysis.csv") + "_validity.csv"
            if os.path.exists(related) and related not in validity_files:
                validity_files.append(related)
    else:
        wv_files = sorted(glob.glob(os.path.join("results_K_grande", "*_wv_analysis.csv")))
        if not wv_files:
            wv_files = sorted(glob.glob("*_wv_analysis.csv"))
        validity_files = sorted(glob.glob(os.path.join("results_K_grande", "*_validity.csv")))
        if not validity_files:
            validity_files = sorted(glob.glob("*_validity.csv"))

    if not wv_files and not validity_files:
        print("Errore: nessun file '*_wv_analysis.csv' o '*_validity.csv' trovato.")
        sys.exit(1)

    for f in wv_files:
        plot_wv_and_gain(f)  # vecchi plot invariati
    for f in validity_files:
        plot_check_validity(f)  # nuovo plot addizionale per dataset
