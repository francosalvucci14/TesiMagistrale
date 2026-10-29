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
        candidate = os.path.join("results_K_grande_gt", input_path)
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


def plot_check_validity_legacy(input_path):
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
    print(f"Plot validita' legacy salvato in: {output_png} | Recall: {recall_text} | "
          f"FN: {false_negatives} | FP: {false_positives}")


def plot_check_validity(input_path):
    """Recall delle DUE strategie LSH rispetto alle coppie (u,t) della BF.

    Metriche separate:
    - recall delle coppie: micro-recall su TUTTE le soluzioni ammissibili;
    - recall delle query: quante query esistenziali True sono recuperate.

    La ground truth si calcola con la Jaccard esatta sui vicinati reali.
    Le LSH restituiscono solo risultati verificati contro lo stesso criterio.
    """
    df = pd.read_csv(input_path)
    required = {
        "query_index", "source_node_id", "groundtruth_pairs",
        "mia_soluzione_pairs", "alternativa_pairs",
    }
    missing = required.difference(df.columns)
    if missing:
        raise ValueError(
            f"{input_path}: colonne mancanti: {sorted(missing)}. "
            "Rigenera il CSV con esperimento_opt_K_grande_groundtruth.cpp."
        )
    if df.empty:
        raise ValueError(f"{input_path}: nessuna query da visualizzare")
    if df["source_node_id"].duplicated().any():
        raise ValueError(f"{input_path}: nodi sorgente ripetuti")
    df = df.sort_values("query_index").reset_index(drop=True)

    gt = df["groundtruth_pairs"].to_numpy(dtype=np.int64)
    mine = df["mia_soluzione_pairs"].to_numpy(dtype=np.int64)
    alt = df["alternativa_pairs"].to_numpy(dtype=np.int64)
    if np.any(gt < 0) or np.any(mine < 0) or np.any(alt < 0):
        raise ValueError(f"{input_path}: conteggi negativi non validi")
    if np.any(mine > gt) or np.any(alt > gt):
        raise ValueError(f"{input_path}: conteggi LSH maggiori della ground truth")

    gt_total, mine_total, alt_total = (int(x.sum()) for x in (gt, mine, alt))
    gt_positive = gt > 0
    n_positive = int(gt_positive.sum())
    mine_positive = mine > 0
    alt_positive = alt > 0

    # Undefined when there are no valid ground-truth pairs (or positive queries).
    recall_mine_pairs = (mine_total / gt_total) if gt_total else np.nan
    recall_alt_pairs = (alt_total / gt_total) if gt_total else np.nan
    recall_mine_queries = (
        int(np.count_nonzero(gt_positive & mine_positive)) / n_positive
        if n_positive else np.nan
    )
    recall_alt_queries = (
        int(np.count_nonzero(gt_positive & alt_positive)) / n_positive
        if n_positive else np.nan
    )

    dataset = os.path.basename(input_path).removesuffix("_validity.csv")
    out_dir = "plot_analysis_K_grande"
    os.makedirs(out_dir, exist_ok=True)

    col_gt, col_mine, col_alt = "#343a40", "#137cbd", "#e58b2a"
    fig = plt.figure(figsize=(18, 11))
    grid = fig.add_gridspec(2, 2, height_ratios=[1.13, 1], hspace=0.36, wspace=0.22)
    ax_curve = fig.add_subplot(grid[0, :])
    ax_each = fig.add_subplot(grid[1, 0])
    ax_recall = fig.add_subplot(grid[1, 1])

    # 1. Conteggi cumulativi: la ground truth rimane la curva superiore.
    q = np.arange(len(df) + 1)
    ax_curve.plot(q, np.r_[0, np.cumsum(gt)], linewidth=2.7, color=col_gt,
                  label=f"Ground truth (Brute Force): {gt_total:,}")
    ax_curve.plot(q, np.r_[0, np.cumsum(mine)], linewidth=3.0, color=col_mine,
                  label=f"Mia soluzione: {mine_total:,}", zorder=4)
    ax_curve.plot(q, np.r_[0, np.cumsum(alt)], linewidth=2.3, color=col_alt,
                  label=f"Alternativa (T−μ LSH): {alt_total:,}",
                  linestyle="--", zorder=5)
    ax_curve.set_xlim(0, len(df))
    ax_curve.set_ylim(bottom=0)
    ax_curve.set_xlabel("Numero di query eseguite (nodi sorgente distinti)", fontsize=13)
    ax_curve.set_ylabel("Coppie ammissibili (u, t), cumulativo", fontsize=13)
    ax_curve.set_title("Soluzioni ammissibili trovate: confronto cumulativo",
                       fontsize=16, fontweight="bold")
    ax_curve.yaxis.set_major_formatter(ticker.StrMethodFormatter("{x:,.0f}"))
    ax_curve.grid(True, alpha=0.35, linestyle=":")
    ax_curve.legend(loc="upper left", framealpha=0.96, fontsize=11)

    # 2. Recall per singola query, solo dove la ground truth non e' vuota.
    valid_x = df.loc[gt_positive, "query_index"].to_numpy(dtype=int)
    mine_per_query = 100.0 * mine[gt_positive] / gt[gt_positive] if n_positive else []
    alt_per_query = 100.0 * alt[gt_positive] / gt[gt_positive] if n_positive else []
    if n_positive:
        ax_each.scatter(valid_x, mine_per_query, s=37, alpha=0.8,
                        color=col_mine, label="Mia soluzione", zorder=3)
        ax_each.scatter(valid_x, alt_per_query, s=33, alpha=0.72, marker="x",
                        color=col_alt, label="Alternativa", zorder=4)
    else:
        ax_each.text(0.5, 0.5, "Recall non definita:\nnessuna coppia valida nella BF",
                     transform=ax_each.transAxes, ha="center", va="center", fontsize=12)
    ax_each.set_xlim(0.5, len(df) + 0.5)
    ax_each.set_ylim(-5, 105)
    ax_each.set_xlabel("Indice della query", fontsize=12)
    ax_each.set_ylabel("Recall coppie per query (%)", fontsize=12)
    ax_each.set_title("Recall sulle singole query positive", fontsize=14, fontweight="bold")
    ax_each.grid(True, alpha=0.35, linestyle=":")
    ax_each.legend(loc="lower left", fontsize=10) if n_positive else None

    # 3. Le due definizioni di recall, affiancate per le due soluzioni.
    categories = ["Coppie (u,t)", "Query con risposta True"]
    xx = np.arange(2)
    width = 0.34
    mine_values = [recall_mine_pairs, recall_mine_queries]
    alt_values = [recall_alt_pairs, recall_alt_queries]
    bars_mine = ax_recall.bar(xx - width / 2,
                             [100 * z if np.isfinite(z) else 0 for z in mine_values],
                             width, label="Mia soluzione", color=col_mine)
    bars_alt = ax_recall.bar(xx + width / 2,
                            [100 * z if np.isfinite(z) else 0 for z in alt_values],
                            width, label="Alternativa", color=col_alt)
    for bars, vals in ((bars_mine, mine_values), (bars_alt, alt_values)):
        for bar, value in zip(bars, vals):
            label = f"{value * 100:.2f}%" if np.isfinite(value) else "N/D"
            ax_recall.annotate(label,
                               (bar.get_x() + bar.get_width() / 2, bar.get_height()),
                               xytext=(0, 5), textcoords="offset points",
                               va="bottom", ha="center", fontsize=11, fontweight="bold")
    ax_recall.axhline(100, color=col_gt, ls="--", lw=1.1, alpha=0.55)
    ax_recall.set_ylim(0, 113)
    ax_recall.set_xticks(xx, categories)
    ax_recall.set_ylabel("Recall rispetto alla Brute Force (%)", fontsize=12)
    ax_recall.set_title("Recall aggregata delle due strategie", fontsize=14, fontweight="bold")
    ax_recall.grid(True, axis="y", alpha=0.35, linestyle=":")
    ax_recall.set_axisbelow(True)
    ax_recall.legend(loc="upper center", fontsize=10)

    fig.suptitle(f"Ground truth esaustiva vs LSH — {dataset} ({len(df):,} query)",
                 fontsize=19, fontweight="bold")
    fig.text(0.5, 0.025,
             "Ground truth = tutte le coppie (u,t) che superano la soglia sulla Jaccard esatta. "
             f"Query con almeno una soluzione: {n_positive:,}/{len(df):,}. "
             f"Falsi negativi: Mia={gt_total - mine_total:,}; Alternativa={gt_total - alt_total:,}.",
             ha="center", va="center", fontsize=10.5, color="#444444")
    fig.subplots_adjust(top=0.91, bottom=0.095, left=0.07, right=0.98)
    output_png = os.path.join(out_dir, f"{dataset}_check_validity.png")
    fig.savefig(output_png, dpi=240, bbox_inches="tight")
    plt.close(fig)

    def fmt(x):
        return f"{100*x:.2f}%" if np.isfinite(x) else "N/D"
    print(f"Plot recall salvato in: {output_png}")
    print(f"[{dataset}] Recall coppie: Mia={fmt(recall_mine_pairs)}; "
          f"Alternativa={fmt(recall_alt_pairs)} | "
          f"Recall query: Mia={fmt(recall_mine_queries)}; "
          f"Alternativa={fmt(recall_alt_queries)}")


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
        wv_files = sorted(glob.glob(os.path.join("results_K_grande_gt", "*_wv_analysis.csv")))
        if not wv_files:
            wv_files = sorted(glob.glob("*_wv_analysis.csv"))
        validity_files = sorted(glob.glob(os.path.join("results_K_grande_gt", "*_validity.csv")))
        if not validity_files:
            validity_files = sorted(glob.glob("*_validity.csv"))

    if not wv_files and not validity_files:
        print("Errore: nessun file '*_wv_analysis.csv' o '*_validity.csv' trovato.")
        sys.exit(1)

    for f in wv_files:
        plot_wv_and_gain(f)  # vecchi plot invariati
    for f in validity_files:
        plot_check_validity(f)  # nuovo plot addizionale per dataset
