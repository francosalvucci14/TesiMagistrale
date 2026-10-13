#!/usr/bin/env python3
"""
plot_oracle.py - grafici per l'esperimento sull'oracolo del primo blocco.

Legge i CSV prodotti da exp_oracle (queries.csv, rects_per_node.csv, datasets.csv)
e salva i grafici (PNG, opzionalmente PDF) in --out.

Uso:  python3 plot_oracle.py --results results --out plots [--pdf]
"""
import argparse
import os

import matplotlib
matplotlib.use("Agg")
import matplotlib.pyplot as plt
import numpy as np
import pandas as pd


def save(fig, out, name, pdf):
    fig.tight_layout()
    fig.savefig(os.path.join(out, name + ".png"), dpi=160)
    if pdf:
        fig.savefig(os.path.join(out, name + ".pdf"))
    plt.close(fig)
    print("salvato", name)


def colors(datasets):
    cmap = plt.get_cmap("tab10")
    return {d: cmap(i % 10) for i, d in enumerate(datasets)}


def plot_pct_vs_mu(q, out, pdf):
    """Mediana e IQR della % di candidati al variare di mu/T, una linea per dataset."""
    ds = sorted(q.dataset.unique())
    col = colors(ds)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for d in ds:
        g = q[q.dataset == d].copy()
        g["mu_key"] = g.mu_over_T.round(6)
        agg = g.groupby("mu_key").cand_nodes_pct.agg(
            med="median", q25=lambda s: s.quantile(.25), q75=lambda s: s.quantile(.75)).reset_index()
        ax.plot(agg.mu_key, agg.med, marker="o", label=d, color=col[d])
        ax.fill_between(agg.mu_key, agg.q25, agg.q75, alpha=.2, color=col[d])
    ax.set_xscale("symlog", linthresh=1e-3)
    ax.set_yscale("symlog", linthresh=1e-2)
    ax.set_xlabel(r"$\mu / T$")
    ax.set_ylabel("candidati (% dei nodi)")
    ax.set_title("Candidati dell'oracolo vs ampiezza della finestra (mediana, IQR)")
    ax.grid(alpha=.3, which="both")
    ax.legend()
    save(fig, out, "candidates_pct_vs_mu", pdf)


def plot_boxplots(q, out, pdf, col_name, ylabel, fname, log=False):
    ds = sorted(q.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.2 * len(ds), 4), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = q[q.dataset == d]
        mus = sorted(g.mu.unique())
        data = [g[g.mu == m][col_name].values for m in mus]
        labs = [str(int(m)) for m in mus]
        try:
            ax.boxplot(data, tick_labels=labs, flierprops=dict(markersize=2))
        except TypeError:  # matplotlib < 3.9
            ax.boxplot(data, labels=labs, flierprops=dict(markersize=2))
        ax.set_title(d)
        ax.set_xlabel(r"$\mu$")
        ax.set_ylabel(ylabel)
        if log:
            ax.set_yscale("symlog", linthresh=1)
        ax.grid(alpha=.3)
        ax.tick_params(axis="x", rotation=45)
    save(fig, out, fname, pdf)


def plot_rects_cdf(r, out, pdf):
    ds = sorted(r.dataset.unique())
    col = colors(ds)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for d in ds:
        v = np.sort(r[r.dataset == d].num_rects.values)
        y = np.arange(1, len(v) + 1) / len(v)
        ax.step(np.maximum(v, 0.5), y, where="post", label=d, color=col[d])
    ax.set_xscale("log")
    ax.set_xlabel(r"rettangoli per nodo $|\mathcal{R}(v)|$")
    ax.set_ylabel("frazione di nodi (CDF)")
    ax.set_title("Distribuzione del numero di rettangoli per nodo")
    ax.grid(alpha=.3, which="both")
    ax.legend()
    save(fig, out, "rects_per_node_cdf", pdf)


def plot_rects_vs_active(r, out, pdf):
    ds = sorted(r.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.2 * len(ds), 4), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = r[r.dataset == d]
        ax.scatter(g.num_active_times, g.num_rects, s=6, alpha=.4)
        mx = max(g.num_active_times.max(), 1)
        ax.plot([1, mx], [1, mx], "r--", lw=1, label=r"$|\mathcal{R}(v)|=|\Lambda(v)|$")
        ax.set_xscale("log"); ax.set_yscale("log")
        ax.set_xlabel(r"$|\Lambda(v)|$ (istanti attivi)")
        ax.set_ylabel(r"$|\mathcal{R}(v)|$")
        ax.set_title(d)
        ax.grid(alpha=.3, which="both")
        ax.legend()
    save(fig, out, "rects_vs_active_times", pdf)


def plot_rects_of_queried(r, q, out, pdf):
    """Numero di rettangoli dei nodi v interrogati vs numero di candidati."""
    ds = sorted(q.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.2 * len(ds), 4), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = q[q.dataset == d]
        sc = ax.scatter(g.num_rects_v, g.cand_nodes_pct, c=np.log10(g.mu + 1), s=8, alpha=.6,
                        cmap="viridis")
        ax.set_xscale("log")
        ax.set_yscale("symlog", linthresh=1e-2)
        ax.set_xlabel(r"$|\mathcal{R}(v)|$ del nodo interrogato")
        ax.set_ylabel("candidati (% dei nodi)")
        ax.set_title(d)
        ax.grid(alpha=.3, which="both")
        fig.colorbar(sc, ax=ax, label=r"$\log_{10}(\mu+1)$")
    save(fig, out, "candidates_vs_rects_of_v", pdf)


def plot_cand_hist(q, out, pdf):
    ds = sorted(q.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.2 * len(ds), 4), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = q[q.dataset == d]
        ax.hist(g.cand_nodes_pct, bins=40)
        ax.set_yscale("log")
        ax.set_xlabel("candidati (% dei nodi)")
        ax.set_ylabel("numero di query")
        ax.set_title(d)
        ax.grid(alpha=.3)
    save(fig, out, "candidates_pct_hist", pdf)


def plot_krel(q, out, pdf):
    ds = sorted(q.dataset.unique())
    col = colors(ds)
    fig, ax = plt.subplots(figsize=(7, 4.5))
    for d in ds:
        g = q[q.dataset == d].copy()
        g["mu_key"] = g.mu_over_T.round(6)
        agg = g.groupby("mu_key").k_rel_rects.mean().reset_index()
        ax.plot(agg.mu_key, agg.k_rel_rects, marker="o", label=d, color=col[d])
    ax.set_xscale("symlog", linthresh=1e-3)
    ax.set_yscale("log")
    ax.set_xlabel(r"$\mu / T$")
    ax.set_ylabel(r"media di $k_v(\mu)$ (rettangoli rilevanti)")
    ax.set_title("Rettangoli rilevanti (iterazioni del ciclo esterno)")
    ax.grid(alpha=.3, which="both")
    ax.legend()
    save(fig, out, "relevant_rects_vs_mu", pdf)


def plot_heatmaps(q, out, pdf):
    ds = sorted(q.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.5 * len(ds), 5), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = q[q.dataset == d]
        piv = g.pivot_table(index="v", columns="mu", values="cand_nodes_pct", aggfunc="mean")
        rect = g.groupby("v").num_rects_v.first()
        piv = piv.loc[rect.sort_values().index]
        im = ax.imshow(np.log10(piv.values + 1e-2), aspect="auto", cmap="magma", origin="lower")
        ax.set_xticks(range(len(piv.columns)))
        ax.set_xticklabels([str(int(c)) for c in piv.columns], rotation=45)
        ax.set_xlabel(r"$\mu$")
        ax.set_ylabel(r"nodi $v$ (ordinati per $|\mathcal{R}(v)|$)")
        ax.set_title(d)
        fig.colorbar(im, ax=ax, label=r"$\log_{10}$(% candidati + 0.01)")
    save(fig, out, "heatmap_v_mu", pdf)


def plot_summary_bar(q, out, pdf):
    ds = sorted(q.dataset.unique())
    mus = sorted(q.mu_over_T.round(6).unique())
    fig, ax = plt.subplots(figsize=(7, 4.5))
    w = 0.8 / max(len(mus), 1)
    qq = q.copy()
    qq["mu_key"] = qq.mu_over_T.round(6)
    for i, m in enumerate(mus):
        vals = [qq[(qq.dataset == d) & (qq.mu_key == m)].cand_nodes_pct.mean() for d in ds]
        ax.bar(np.arange(len(ds)) + i * w, vals, w, label=rf"$\mu/T$={m:g}")
    ax.set_xticks(np.arange(len(ds)) + 0.4 - w / 2)
    ax.set_xticklabels(ds, rotation=20)
    ax.set_yscale("symlog", linthresh=1e-2)
    ax.set_ylabel("candidati medi (% dei nodi)")
    ax.set_title("Candidati medi per dataset e ampiezza")
    ax.grid(alpha=.3, axis="y")
    ax.legend(fontsize=7)
    save(fig, out, "summary_bar", pdf)


def plot_cand_times(c, out, pdf):
    """Distribuzione dei tempi t (inizio finestra, normalizzato su T) dei candidati (u,t)."""
    ds = sorted(c.dataset.unique())
    fig, axes = plt.subplots(1, len(ds), figsize=(4.2 * len(ds), 4), squeeze=False)
    for ax, d in zip(axes[0], ds):
        g = c[c.dataset == d]
        if "t" in g.columns:
            ts, w = g.t.values, np.ones(len(g))
        else:  # intervalli: ogni (u,t_lo..t_hi) pesa (t_hi-t_lo+1) al centro dell'intervallo
            ts, w = (g.t_lo.values + g.t_hi.values) / 2.0, (g.t_hi - g.t_lo + 1).values
        ax.hist(ts, bins=50, weights=w)
        ax.set_xlabel("istante t (inizio finestra)")
        ax.set_ylabel("numero di coppie (u,t)")
        ax.set_title(d)
        ax.grid(alpha=.3)
    save(fig, out, "candidate_times_hist", pdf)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--results", default="results")
    ap.add_argument("--out", default="plots")
    ap.add_argument("--pdf", action="store_true", help="salva anche in PDF")
    a = ap.parse_args()
    os.makedirs(a.out, exist_ok=True)

    q = pd.read_csv(os.path.join(a.results, "queries.csv"))
    r = pd.read_csv(os.path.join(a.results, "rects_per_node.csv"))
    d = pd.read_csv(os.path.join(a.results, "datasets.csv"))
    print(d.to_string(index=False))

    plot_pct_vs_mu(q, a.out, a.pdf)
    plot_boxplots(q, a.out, a.pdf, "cand_nodes_pct", "candidati (% dei nodi)", "candidates_pct_box")
    plot_boxplots(q, a.out, a.pdf, "cand_nodes", "numero di nodi candidati", "candidates_abs_box", log=True)
    plot_cand_hist(q, a.out, a.pdf)
    plot_krel(q, a.out, a.pdf)
    plot_rects_cdf(r, a.out, a.pdf)
    plot_rects_vs_active(r, a.out, a.pdf)
    plot_rects_of_queried(r, q, a.out, a.pdf)
    plot_heatmaps(q, a.out, a.pdf)
    plot_summary_bar(q, a.out, a.pdf)
    cpath = os.path.join(a.results, "candidates.csv")
    if os.path.exists(cpath):
        c = pd.read_csv(cpath)
        if len(c):
            plot_cand_times(c, a.out, a.pdf)


if __name__ == "__main__":
    main()