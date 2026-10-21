#!/usr/bin/env python3
"""Grafici leggibili dei candidati del filtro MinHash, uno per dataset.

Compatibile con i CSV e gli argomenti dello script originale.
Viene mostrata soltanto la media dei candidati sui nodi sorgente,
in funzione dei valori di mu testati.
"""

import argparse
import csv
import math
import random
import re
from collections import defaultdict
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np

# Presentazione per tesi: grafici grandi e leggibili.
plt.rcParams.update({
    "font.size": 20,
    "axes.titlesize": 25,
    "axes.labelsize": 22,
    "xtick.labelsize": 18,
    "ytick.labelsize": 18,
    "pdf.fonttype": 42,
})


def safe_name(path):
    stem = Path(path).stem
    return re.sub(r'[^A-Za-z0-9_.-]', '_', stem) or 'dataset'


def read_data(path):
    groups = defaultdict(list)
    with open(path, newline='', encoding='utf-8') as stream:
        reader = csv.DictReader(stream)
        mandatory = {'dataset', 'node_id', 'mu', 'filter_nodes', 'filter_pairs'}
        missing = mandatory - set(reader.fieldnames or [])
        if missing:
            raise ValueError('Incompatible results CSV: missing ' + ', '.join(sorted(missing)))
        for row in reader:
            groups[row['dataset']].append(row)
    if not groups:
        raise ValueError('No query results found')
    return groups


def format_number(value, digits=0):
    """Formattazione italiana (migliaia con punto, decimali con virgola)."""
    formatted = f'{value:,.{digits}f}'
    return formatted.replace(',', '#').replace('.', ',').replace('#', '.')


def auto_digits(data, integer=False):
    if integer:
        return 0
    span = float(np.ptp(data))
    if span == 0:
        absolute = abs(float(data[0]))
        if absolute == 0:
            return 0
        if absolute >= 1:
            return 2
        return min(7, max(3, math.ceil(-math.log10(absolute)) + 2))
    if span >= 20:
        return 1
    if span >= 1:
        return 2
    if span >= .1:
        return 3
    if span >= .01:
        return 4
    return 5


def percentage_delta(data):
    initial, final = float(data[0]), float(data[-1])
    if initial == 0:
        return 'non definita (valore iniziale = 0)' if final else '0%'
    return f'{(final - initial) / initial * 100:+.2f}%'.replace('.', ',')


def set_y_axis(ax, data, mode='auto', integer=False):
    """Tre rappresentazioni possibili: lineare da 0, zoom lineare, log.

    Auto:  - piccole oscillazioni relative -> zoom;
           - rapporto massimo/minimo molto alto e nessuno zero -> log;
           - negli altri casi -> lineare da zero.
    """
    low, high = float(np.min(data)), float(np.max(data))
    if mode == 'auto':
        if low > 0 and high / low >= 100:
            mode = 'log'
        elif low > 0 and (high - low) / high <= 0.25:
            mode = 'zoom'
        else:
            mode = 'zero'

    if mode == 'log':
        if low <= 0:
            raise ValueError('Scala logaritmica non disponibile: almeno un valore e <= 0.')
        ratio = max(high / low, 1.2)
        ax.set_yscale('log')
        ax.set_ylim(low / ratio**0.15, high * ratio**0.20)
        ax.yaxis.set_major_locator(ticker.LogLocator(base=10, numticks=8))
        ax.yaxis.set_minor_formatter(ticker.NullFormatter())
        scale_label = 'asse Y logaritmico'
    elif mode == 'zoom':
        pad = max((high - low) * .35, high * .005, .6 if integer else .08)
        ax.set_ylim(max(0, low - pad), high + pad * 1.4)
        ax.yaxis.set_major_locator(ticker.MaxNLocator(nbins=6, integer=integer))
        scale_label = 'asse Y ingrandito: non parte da 0'
    else:
        ax.set_ylim(0, max(1.0, high * 1.28))
        ax.yaxis.set_major_locator(ticker.MaxNLocator(nbins=6, integer=integer))
        scale_label = 'asse Y da 0'

    digits = auto_digits(data, integer=integer)
    ax.yaxis.set_major_formatter(
        ticker.FuncFormatter(lambda value, _: format_number(value, digits if not integer else 0)))
    return scale_label, digits


def add_panel(ax, mus, data, title, y_label, y_mode, *, integer=False):
    x = np.arange(len(mus))  # punti equidistanti: mu selezionati, NON asse numerico
    line, = ax.plot(x, data, marker='o', linewidth=3.2, markersize=9,
                    zorder=3)
    ax.axhline(data[0], linestyle=':', linewidth=2.0, alpha=.55,
               color=line.get_color(), zorder=2)

    scale_note, digits = set_y_axis(ax, data, y_mode, integer=integer)
    lo, hi = float(np.min(data)), float(np.max(data))
    delta = float(data[-1] - data[0])
    info = (f'Min {format_number(lo, digits)}  ·  Max {format_number(hi, digits)}'
            f'  ·  Δ primo→ultimo {format_number(delta, digits)} ({percentage_delta(data)})')
    ax.set_title(title + '\n' + info, fontsize=22, pad=18, linespacing=1.45)
    ax.set_ylabel(y_label, fontsize=21)
    ax.set_xlabel('μ — valori testati (intervalli grafici equidistanti)', fontsize=19)

    max_ticks = 14
    tick_ids = np.arange(len(mus)) if len(mus) <= max_ticks else np.unique(
        np.linspace(0, len(mus) - 1, max_ticks, dtype=int))
    ax.set_xticks(tick_ids)
    ax.set_xticklabels([str(mus[i]) for i in tick_ids])
    ax.set_xlim(-0.45, len(mus) - .55 if len(mus) > 1 else .45)
    ax.tick_params(axis='x', rotation=35, labelsize=18)
    ax.tick_params(axis='y', labelsize=18)
    ax.grid(True, axis='y', linestyle='--', linewidth=.8, alpha=.32)
    ax.grid(True, axis='x', linestyle=':', linewidth=.7, alpha=.16)
    ax.set_axisbelow(True)
    # ax.text(0.0, -0.19, scale_note + ' · linea tratteggiata = valore al primo μ',
    #         transform=ax.transAxes, ha='left', va='top', fontsize=17,
    #         color='#555555', clip_on=False)

    # Etichette puntuali: sempre tutte su griglie piccole; campionamento su griglie grandi.
    if len(mus) <= 12:
        labeled = list(range(len(mus)))
    else:
        labeled = sorted(set(np.linspace(0, len(mus) - 1, 10, dtype=int))
                         | {int(np.argmin(data)), int(np.argmax(data))})
    for index in labeled:
        value = float(data[index])
        # Alterna la posizione delle etichette vicine per ridurre le sovrapposizioni.
        bottom, _ = ax.get_ylim()
        sign = 1 if (index % 2 == 0 or value <= bottom + (ax.get_ylim()[1] - bottom) * .035) else -1
        ax.annotate(format_number(value, digits), (x[index], value),
                    xytext=(0, 15 * sign), textcoords='offset points',
                    ha='center', va='bottom' if sign > 0 else 'top',
                    fontsize=16, color=line.get_color(), zorder=5,
                    clip_on=False)


def plot_dataset(dataset, rows, dest, seed, forced_node=None, pairs=False,
                 y_scale='auto'):
    names = sorted({row['node_id'] for row in rows})
    mus = sorted({int(row['mu']) for row in rows})
    if not names or not mus:
        raise ValueError('Empty dataset queries: ' + dataset)
    lookup = {(row['node_id'], int(row['mu'])): row for row in rows}
    if len(lookup) != len(rows):
        raise ValueError('Repeated (node_id,mu) rows detected for ' + dataset)
    if len(lookup) != len(names)*len(mus):
        raise ValueError('Incomplete query grid for ' + dataset + ': some nodes lack mu results')

    metric = 'filter_pairs' if pairs else 'filter_nodes'
    values = np.array([[float(lookup[(name, mu)][metric]) for mu in mus]
                       for name in names], dtype=float)
    if not np.isfinite(values).all() or (values < 0).any():
        raise ValueError(f'Invalid candidate counts in {dataset}')
    means = values.mean(axis=0)
    n_vertices = rows[0].get('vertices', '?')
    count_desc = 'Coppie (u,t)' if pairs else 'Nodi distinti'

    fig, ax = plt.subplots(figsize=(16, 10))
    add_panel(ax, mus, means, f'Media su {len(names):,} nodi sorgente'.replace(',', '.'),
              f'Media {count_desc.lower()} candidati', y_scale)

    title = (f'{Path(dataset).name}   ·   n = {n_vertices}   ·   Q = {format_number(len(names))}\n'
             'Candidati del filtro preliminare (prima componente MinHash)')
    fig.suptitle(title, fontsize=27, fontweight='semibold', y=.99)

    # Rendere leggibile anche la portata del filtro rispetto alla dimensione del grafo.
    # note = ('Ogni punto indica il numero di candidati per il valore μ specificato; '
    #         'l’asse X usa spaziatura uniforme fra i valori campionati.')
    # if not pairs:
    #     try:
    #         total_nodes = int(n_vertices)
    #         if total_nodes > 0:
    #             shares = 100 * means / total_nodes
    #             pct_digits = 2 if shares.max() >= .1 else (4 if shares.max() >= .001 else 6)
    #             note += (f'   ·   Media candidati / n: '
    #                      f'{shares.min():.{pct_digits}f}–{shares.max():.{pct_digits}f}%'.replace('.', ','))
    #     except (TypeError, ValueError):
    #         pass
    # import textwrap
    # note = '\n'.join(textwrap.wrap(note, width=110))
    # fig.text(.5, .035, note, ha='center', va='center', fontsize=14, color='#555555')
    fig.subplots_adjust(left=.12, right=.96, top=.77, bottom=.28)

    dest.mkdir(parents=True, exist_ok=True)
    out = dest / (safe_name(dataset) + '_comparison.png')
    # Salva in entrambi i formati. Il PDF mantiene il testo vettoriale.
    fig.savefig(out.with_suffix('.pdf'), dpi=180, facecolor='white', bbox_inches='tight')
    fig.savefig(out, dpi=150, facecolor='white', bbox_inches='tight')
    plt.close(fig)
    return out, float(means.max())


def main():
    parser = argparse.ArgumentParser(description='Candidate plots, with adaptive Y axes for all datasets.')
    parser.add_argument('--csv', default='results/all_datasets.csv')
    parser.add_argument('--out-dir', default='results/plots')
    parser.add_argument('--seed', type=int, default=42, help="Compatibilità: ignorato (nessun nodo singolo viene selezionato)")
    parser.add_argument('--single-node-id', type=str, default=None, help='Compatibilità: ignorato (si mostra soltanto la media)')
    parser.add_argument('--pairs', action='store_true', help='Plot candidate pairs instead of distinct nodes')
    parser.add_argument('--y-scale', choices=('auto', 'zero', 'zoom', 'log'), default='auto',
                        help='Y-axis: auto (default), start at zero, zoom to variations or logarithmic')
    args = parser.parse_args()

    dest = Path(args.out_dir)
    grouped = read_data(args.csv)
    summary = []
    for dataset, rows in grouped.items():
        out, highest_filter_mean = plot_dataset(
            dataset, rows, dest, args.seed, args.single_node_id,
            args.pairs, args.y_scale)
        print('Created:', out)
        summary.append(f'{dataset}\t{out.name}\tfilter_highest_mean={highest_filter_mean:.3f}')
    dest.mkdir(parents=True, exist_ok=True)
    (dest / 'plot_summary.txt').write_text('\n'.join(summary) + '\n', encoding='utf-8')


if __name__ == '__main__':
    main()
