#!/usr/bin/env python3
"""One two-panel figure per dataset showing first-MinHash oracle candidates."""
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
        mandatory = {'dataset', 'node_id', 'mu', 'filter_nodes', 'filter_pairs'}
        if not mandatory.issubset(reader.fieldnames or []):
            raise ValueError('Incompatible results CSV: missing ' + ', '.join(sorted(mandatory - set(reader.fieldnames or []))))
        for row in reader:
            groups[row['dataset']].append(row)
    if not groups:
        raise ValueError('No query results found')
    return groups


def plot_dataset(dataset, rows, dest, seed, forced_node=None, pairs=False):
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

    x_values = np.arange(len(mus))
    first_metric = 'filter_pairs' if pairs else 'filter_nodes'
    values = np.array([[float(lookup[(name, mu)][first_metric]) for mu in mus]
                       for name in names], dtype=float)
    means = values.mean(axis=0)
    node_idx = names.index(node)
    n_vertices = rows[0].get('vertices', '?')
    count_desc = 'Coppie (u,t)' if pairs else 'Nodi distinti'

    fig, axes = plt.subplots(1, 2, figsize=(15, 5.5), layout='constrained')
    axes[0].plot(x_values, values[node_idx], marker='o', linewidth=1.8, markersize=5)
    axes[0].set_title(f'Nodo query casuale v = {node}')
    axes[0].set_xlabel('μ (unità temporali discretizzate)')
    axes[0].set_ylabel(f'{count_desc} candidati')

    axes[1].plot(x_values, means, marker='o', linewidth=1.8, markersize=5)
    axes[1].set_title(f'Media su {len(names)} nodi query')
    axes[1].set_xlabel('μ (unità temporali discretizzate)')
    axes[1].set_ylabel(f'Media {count_desc.lower()} candidati')

    for ax in axes:
        ax.set_xticks(x_values)
        ax.set_xticklabels([str(mu) for mu in mus])
        ax.tick_params(axis='x', rotation=35)
        ax.set_xlim(-0.5, len(mus) - 0.5)
        ax.set_ylim(bottom=0)
        ax.grid(alpha=0.23)

    fig.suptitle(f'{Path(dataset).name} | n={n_vertices} | {len(names)} nodi query\n'
                 'Candidati del filtro preliminare basato sulla prima componente MinHash',
                 fontsize=14)
    dest.mkdir(parents=True, exist_ok=True)
    filename = safe_name(dataset) + '_comparison.png'
    out = dest / filename
    fig.savefig(out, dpi=155)
    plt.close(fig)
    return out, node, float(means.max())


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--csv', default='results/all_datasets.csv')
    parser.add_argument('--out-dir', default='results/plots')
    parser.add_argument('--seed', type=int, default=42)
    parser.add_argument('--single-node-id', type=str, default=None)
    parser.add_argument('--pairs', action='store_true', help='Plot formal candidate pairs instead of distinct nodes')
    args=parser.parse_args()
    dest=Path(args.out_dir)
    grouped=read_data(args.csv)
    summary=[]
    for dataset, rows in grouped.items():
        out, v, highest_filter_mean = plot_dataset(
            dataset, rows, dest, args.seed, args.single_node_id, args.pairs)
        print('Created:', out)
        summary.append(f'{dataset}\t{out.name}\tnode={v}\tfilter_highest_mean={highest_filter_mean:.3f}')
    dest.mkdir(parents=True, exist_ok=True)
    (dest/'plot_summary.txt').write_text('\n'.join(summary)+'\n', encoding='utf-8')


if __name__=='__main__':
    main()
