#!/usr/bin/env python3
"""One 2x3 panel figure per dataset: first-MinHash filter vs full sketch validation.

Rows: preliminary filter / verified full k-component MinHash similarity.
Columns: one uniformly sampled query node vs mu / mean over sampled query nodes
         vs mu / rank distribution for ONE common mu*.

Common mu* is selected as the mu with the largest mean number of FILTER nodes,
so that filter and verification distributions refer to exactly the same windows.
"""
import argparse
import csv
import random
import re
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np


def safe_name(path):
    stem = Path(path).stem
    return re.sub(r'[^A-Za-z0-9_.-]', '_', stem) or 'dataset'


def read_data(path):
    groups = defaultdict(list)
    with open(path, newline='', encoding='utf-8') as stream:
        reader = csv.DictReader(stream)
        mandatory = {'dataset', 'node_id', 'mu', 'filter_nodes', 'verified_nodes', 'filter_pairs', 'verified_pairs'}
        if not mandatory.issubset(reader.fieldnames or []):
            raise ValueError('Incompatible results CSV: missing ' + ', '.join(sorted(mandatory - set(reader.fieldnames or []))))
        for row in reader:
            groups[row['dataset']].append(row)
    if not groups:
        raise ValueError('No query results found')
    return groups


def plot_dataset(dataset, rows, dest, seed, forced_node=None, ranked_mu=None,
                 pairs=False, ranked_log_y=False):
    names = sorted({row['node_id'] for row in rows})
    mus = sorted({int(row['mu']) for row in rows})
    if not names or not mus:
        raise ValueError('Empty dataset queries: ' + dataset)
    if forced_node is not None and forced_node not in names:
        raise ValueError(f'Node {forced_node!r} does not appear among queries for {dataset}')
    node = forced_node if forced_node is not None else random.Random(seed).choice(names)
    lookup = {(row['node_id'], int(row['mu'])): row for row in rows}
    if len(lookup) != len(rows):
        raise ValueError('Repeated (node_id,mu) rows detected for ' + dataset)
    expected = len(names)*len(mus)
    if len(lookup) != expected:
        raise ValueError('Incomplete query grid for ' + dataset + ': some nodes lack mu results')

    ticks = mus if len(mus) <= 8 else [mus[i] for i in np.unique(np.linspace(0, len(mus)-1, 8, dtype=int))]
    first_metric = 'filter_pairs' if pairs else 'filter_nodes'
    final_metric = 'verified_pairs' if pairs else 'verified_nodes'
    metrics = [first_metric, final_metric]
    values = {}
    for metric in metrics:
        values[metric] = np.array([[float(lookup[(name, mu)][metric]) for mu in mus]
                                   for name in names], dtype=float)
    filter_means = values[first_metric].mean(axis=0)
    best_idx = int(np.argmax(filter_means))
    mu_star = mus[best_idx] if ranked_mu is None else ranked_mu
    if mu_star not in mus:
        raise ValueError(f'mu for ranked distribution {mu_star} not among queried mu values: {mus}')
    rank_idx = mus.index(mu_star)
    node_idx = names.index(node)
    n_vertices = rows[0].get('vertices', '?')
    k = rows[0].get('k', '?')
    tau = rows[0].get('tau', '?')
    count_desc = 'Coppie (u,t)' if pairs else 'Nodi distinti'

    fig, axes = plt.subplots(2, 3, figsize=(19.5, 10.0), layout='constrained')
    row_titles = [f'FILTRO PRELIMINARE — stessa componente MinHash (h[0])',
                  f'VERIFICA COMPLETA — similarità MinHash a {k} componenti ≥ {tau}']
    for i, metric in enumerate(metrics):
        mat = values[metric]
        means = mat.mean(axis=0)
        samples = mat[:, rank_idx]
        ranked = np.sort(samples)[::-1]
        xrank = np.arange(1, len(ranked) + 1)
        mean_star = float(np.mean(samples))
        # 1. Fixed randomly sampled query node, all mu.
        ax = axes[i, 0]
        ax.plot(mus, mat[node_idx], marker='o', linewidth=1.8, markersize=5)
        ax.set_title(f'Nodo query casuale v = {node}')
        ax.set_xlabel('μ (unità temporali discretizzate)')
        ax.set_ylabel(f'{count_desc} candidati')
        ax.set_xticks(ticks)
        ax.tick_params(axis='x', rotation=35)
        ax.grid(alpha=0.23)

        # 2. Mean per mu across EXACTLY the same query nodes.
        ax = axes[i, 1]
        ax.plot(mus, means, marker='o', linewidth=1.8, markersize=5)
        ax.set_title(f'Media su {len(names)} nodi query')
        ax.set_xlabel('μ (unità temporali discretizzate)')
        ax.set_ylabel(f'Media {count_desc.lower()} candidati')
        ax.set_xticks(ticks)
        ax.tick_params(axis='x', rotation=35)
        ax.grid(alpha=0.23)

        # 3. Ranked query nodes for ONE common mu* (chosen on filter).
        ax = axes[i, 2]
        ax.scatter(xrank, ranked, s=12, alpha=0.8)
        ax.axhline(mean_star, color='red', linewidth=2,
                   label=f'Media = {mean_star:,.2f}')
        ax.set_title(f'Nodi in ordine decrescente — μ* = {mu_star}')
        ax.set_xlabel('Rango dei nodi query')
        ax.set_ylabel(f'{count_desc} candidati')
        if ranked_log_y:
            ax.set_yscale('symlog', linthresh=1)
        ax.grid(alpha=0.23)
        ax.legend(loc='upper right')

        # Row label, kept outside the charts to make phases unmistakable.
        axes[i, 0].annotate(row_titles[i], xy=(0, 1), xycoords='axes fraction',
                            xytext=(0, 19), textcoords='offset points',
                            weight='bold', fontsize=11, va='bottom')

    if not pairs:
        # Same y scale across the matching columns allows a direct comparison.
        for j in (0, 1, 2):
            ymax = max(axes[0,j].get_ylim()[1], axes[1,j].get_ylim()[1])
            axes[0,j].set_ylim(bottom=0, top=max(1, ymax))
            axes[1,j].set_ylim(bottom=0, top=max(1, ymax))
    fig.suptitle(f'{Path(dataset).name} | n={n_vertices} | k={k} | τ={tau} | {len(names)} nodi query\n'
                 'Confronto: candidati del filtro vs candidati confermati dalla signature completa',
                 fontsize=14)
    dest.mkdir(parents=True, exist_ok=True)
    filename = safe_name(dataset) + '_comparison.png'
    out = dest / filename
    fig.savefig(out, dpi=155)
    plt.close(fig)
    return out, node, mu_star, float(filter_means.max()), float(values[final_metric][:,rank_idx].mean())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default='results/all_datasets.csv')
    parser.add_argument('--out-dir', default='results/plots')
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--single-node-id', type=str, default=None)
    parser.add_argument('--ranked-mu', type=int, default=None)
    parser.add_argument('--pairs', action='store_true', help='Plot formal candidate pairs instead of distinct nodes')
    parser.add_argument('--ranked-log-y', action='store_true')
    args=parser.parse_args()
    dest=Path(args.out_dir)
    grouped=read_data(args.csv)
    summary=[]
    for dataset, rows in grouped.items():
        out, v, mu_star, highest_filter_mean, final_mean = plot_dataset(
            dataset, rows, dest, args.seed, args.single_node_id,
            args.ranked_mu, args.pairs, args.ranked_log_y)
        print('Created:', out)
        summary.append(f'{dataset}\t{out.name}\tnode={v}\tmu_star={mu_star}\tfilter_highest_mean={highest_filter_mean:.3f}\tverified_mean_at_mu_star={final_mean:.3f}')
    dest.mkdir(parents=True, exist_ok=True)
    (dest/'plot_summary.txt').write_text('\n'.join(summary)+'\n', encoding='utf-8')


if __name__=='__main__':
    main()
