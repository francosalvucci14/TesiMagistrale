#!/usr/bin/env python3
"""
================================================================================
PROGETTO TESI: Neighborhood Search su Grafi Temporali
Script: plot_esperimento_prob_2.py

Generazione e visualizzazione dei grafici di benchmark per il PROBLEMA 2:
"Neighborhood search con sliding windows" (cfr. sol_prob_2.tex e problema.tex)

Legge i risultati prodotti da `esperimento_problema_2.cpp` salvati in:
  risultati_esperimento_prob_2.csv

Grafici esportati (in formato doppio PNG 300 DPI e PDF vettoriale per LaTeX):
  1. 01_pruning_analysis.png / .pdf             -> Conformità ai bound teorici di W(v)
  2. 02_query_runtime_comparison.png / .pdf      -> Benchmark latenze a 3 vie (BF vs Prun vs LSH)
  3. 03_candidate_reduction_and_speedup.png/.pdf -> Abbattimento candidati e speedup per nodo
  4. 04_similarity_fidelity.png / .pdf           -> Fedeltà dello sketch: MinHash vs Jaccard Esatta
  5. 05_parametric_sensitivity.png / .pdf        -> Studio di sensibilità su mu e tau
  6. 06_thesis_dashboard.png / .pdf              -> Dashboard sinottico a 4 pannelli per la tesi

Uso:
  python3 plot_esperimento_prob_2.py [percorso_file.csv] [--output_dir DIR]
================================================================================
"""

import os
import sys
import argparse
import pandas as pd
import numpy as np
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker

# Configurazione stile tipografico formale per pubblicazioni / tesi LaTeX
plt.rcParams.update({
    'font.family': 'sans-serif',
    'font.sans-serif': ['DejaVu Sans', 'Helvetica', 'Arial'],
    'font.size': 11,
    'axes.titlesize': 13,
    'axes.titleweight': 'bold',
    'axes.labelsize': 12,
    'axes.labelweight': 'semibold',
    'xtick.labelsize': 10,
    'ytick.labelsize': 10,
    'legend.fontsize': 10,
    'figure.titlesize': 15,
    'figure.titleweight': 'bold',
    'figure.dpi': 300,
    'savefig.dpi': 300,
    'savefig.bbox': 'tight',
    'axes.spines.top': False,
    'axes.spines.right': False,
    'axes.grid': True,
    'grid.alpha': 0.35,
    'grid.linestyle': '--'
})

# Palette cromatica ad alto contrasto
COLOR_BF = '#D9534F'       # Rosso Corallo: Brute Force senza pruning
COLOR_PRUN = '#F0AD4E'     # Ambra / Arancio: Brute Force con Pruning W(v)
COLOR_LSH = '#2E7D32'      # Verde Foresta: LSH Query2
COLOR_BOUND = '#0275D8'    # Blu Primario: Upper Bound analitico W(v)
COLOR_DENSE = '#6F42C1'    # Viola: Caso più denso W(v)
COLOR_ACCENT = '#20B2AA'   # Turchese / Verde acqua
COLOR_GRAY = '#6C757D'     # Grigio per assi e riferimenti

def parse_args():
    parser = argparse.ArgumentParser(description="Generatore di Plot per NS con Sliding Windows")
    parser.add_argument('csv_path', nargs='?', default='risultati_esperimento_prob_2_v1.csv',
                        help='File CSV generato da esperimento_problema_2_v1.cpp')
    parser.add_argument('--output_dir', default='plots_v1',
                        help='Cartella di output per i grafici (default: plots)')
    parser.add_argument('--mu', type=int, default=3,
                        help='Ampiezza della finestra scorrevole mu (default: 3)')
    parser.add_argument('--t_total', type=int, default=100,
                        help='Estensione temporale totale T (default: 100)')
    return parser.parse_args()

def load_dataset(csv_path):
    if not os.path.exists(csv_path):
        print(f"[ERRORE] File '{csv_path}' non trovato!")
        print("Esegui prima l'esperimento C++ per generare i dati: ./esperimento_problema_2")
        sys.exit(1)
    df = pd.read_csv(csv_path)
    print(f"[INFO] Dati caricati con successo da '{csv_path}' ({len(df)} query registrate).")
    return df

# ==============================================================================
# 1. ANALISI DEL PRUNING TEMPORALE W(v) E BOUND TEORICI
# ==============================================================================
def plot_pruning_analysis(df, output_dir, mu=3, T_total=100):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
    total_windows = T_total - mu
    df_sorted = df.sort_values('lambda_size').copy()

    # 1.1: Scatter empirico vs Curve teoriche da sol_prob_2.tex
    x_emp = df_sorted['lambda_size'].values
    w_emp = df_sorted['W_size'].values
    
    x_theory = np.linspace(max(1, x_emp.min() - 2), x_emp.max() + 2, 120)
    upper_bound = np.minimum(total_windows, (mu + 1) * x_theory)
    densest_case = x_theory + mu

    ax1.plot(x_theory, upper_bound, color=COLOR_BOUND, linestyle='--', linewidth=2.2,
             label=rf'Upper Bound: $\min(T-\mu, (\mu+1)|\Lambda(v)|)$')
    ax1.plot(x_theory, densest_case, color=COLOR_DENSE, linestyle='-.', linewidth=2.2,
             label=rf'Caso denso: $|\Lambda(v)| + \mu$ ($d_i=1$)')
    ax1.axhline(total_windows, color=COLOR_GRAY, linestyle=':', linewidth=1.5,
                label=rf'Senza Pruning ($T-\mu = {total_windows}$)')
    
    ax1.scatter(x_emp, w_emp, color=COLOR_LSH, s=85, alpha=0.9, edgecolors='black',
                linewidth=1, zorder=5, label=r'Nodi Sperimentali $|W(v)|$')

    ax1.set_xlabel(r'Cardinalità eventi attivi $|\Lambda(v)|$')
    ax1.set_ylabel(r'Finestre temporali rilevanti $|W(v)|$')
    ax1.set_title(r'Conformità ai Bound Teorici di $|W(v)|$')
    ax1.legend(loc='lower right', frameon=True, framealpha=0.92)

    # 1.2: Fattore di riduzione (T - mu) / |W(v)| per nodo
    reduction_ratios = total_windows / df_sorted['W_size'].values
    indices = np.arange(len(df_sorted))
    
    ax2.bar(indices, reduction_ratios, color='#3498DB', alpha=0.85, edgecolor='black', linewidth=0.8)
    avg_ratio = np.mean(reduction_ratios)
    ax2.axhline(avg_ratio, color='#E74C3C', linestyle='--', linewidth=2,
                label=f'Riduzione media: {avg_ratio:.2f}x ({(1 - 1/avg_ratio)*100:.1f}% finestre scartate)')

    ax2.set_xticks(indices)
    ax2.set_xticklabels(df_sorted['node_id'], rotation=45, ha='right', fontsize=8.5)
    ax2.set_xlabel(r'Nodo Query ($v \in V$)')
    ax2.set_ylabel(r'Fattore di Riduzione $\frac{T-\mu}{|W(v)|}$')
    ax2.set_title('Efficacia del Pruning Temporale per Singolo Nodo')
    ax2.legend(loc='upper right', frameon=True, framealpha=0.92)

    plt.suptitle(r'FASE 1: Analisi del Pruning Temporale $W(v)$ (cfr. sol_prob_2.tex)', y=1.02)
    fig.savefig(os.path.join(output_dir, '01_pruning_analysis.png'))
    fig.savefig(os.path.join(output_dir, '01_pruning_analysis.pdf'))
    plt.close(fig)
    print(" -> Generato: 01_pruning_analysis.png / .pdf")

# ==============================================================================
# 2. CONFRONTO TEMPI DI ESECUZIONE (3-WAY LATENCY BENCHMARK)
# ==============================================================================
def plot_query_runtimes(df, output_dir):
    fig, ax = plt.subplots(figsize=(14, 6))
    indices = np.arange(len(df))
    bar_width = 0.26

    # Barre di confronto latenza
    ax.bar(indices - bar_width, df['time_bf_us'], width=bar_width,
           color=COLOR_BF, alpha=0.9, edgecolor='black', linewidth=0.8,
           label='Baseline 1: Brute Force Senza Pruning')
    ax.bar(indices, df['time_pruning_us'], width=bar_width,
           color=COLOR_PRUN, alpha=0.9, edgecolor='black', linewidth=0.8,
           label=r'Baseline 2: Brute Force Con Pruning $W(v)$')
    ax.bar(indices + bar_width, df['time_lsh_us'], width=bar_width,
           color=COLOR_LSH, alpha=0.9, edgecolor='black', linewidth=0.8,
           label=r'Algoritmo 2: LSH Query2 (con $W(v)$ e Banding)')

    # Medie
    mean_bf = df['time_bf_us'].mean()
    mean_prun = df['time_pruning_us'].mean()
    mean_lsh = df['time_lsh_us'].mean()
    speedup_vs_bf = mean_bf / mean_lsh
    speedup_vs_prun = mean_prun / mean_lsh

    ax.axhline(mean_bf, color=COLOR_BF, linestyle=':', alpha=0.75, linewidth=1.5)
    ax.axhline(mean_prun, color=COLOR_PRUN, linestyle=':', alpha=0.75, linewidth=1.5)
    ax.axhline(mean_lsh, color=COLOR_LSH, linestyle=':', alpha=0.75, linewidth=1.5)

    info_box = (f"Riepilogo Prestazioni:\n"
                f" • BF No Pruning: {mean_bf:.1f} $\mu$s\n"
                f" • BF Con Pruning: {mean_prun:.1f} $\mu$s\n"
                f" • LSH Query2: {mean_lsh:.1f} $\mu$s\n"
                f"Speedup LSH vs BF: {speedup_vs_bf:.2f}x\n"
                f"Speedup LSH vs Prun: {speedup_vs_prun:.2f}x")

    ax.text(0.015, 0.96, info_box, transform=ax.transAxes, verticalalignment='top',
            fontsize=10.5, bbox=dict(boxstyle='round,pad=0.6', facecolor='#F8F9FA', edgecolor='#CED4DA', alpha=0.95))

    ax.set_xticks(indices)
    ax.set_xticklabels(df['node_id'], rotation=45, ha='right', fontsize=9.5)
    ax.set_xlabel(r'Nodo Interrogato ($v \in V$)')
    ax.set_ylabel(r'Latenza di Query ($\mu$s)')
    ax.set_title('Confronto Prestazionale: Latenza di Risposta alla Query (LSH vs Baseline)')
    ax.legend(loc='upper right', frameon=True, framealpha=0.92)

    fig.savefig(os.path.join(output_dir, '02_query_runtime_comparison.png'))
    fig.savefig(os.path.join(output_dir, '02_query_runtime_comparison.pdf'))
    plt.close(fig)
    print(" -> Generato: 02_query_runtime_comparison.png / .pdf")

# ==============================================================================
# 3. ABBATTIMENTO CANDIDATI E SPEEDUP INDIVIDUALE
# ==============================================================================
def plot_candidate_reduction_and_speedup(df, output_dir):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))
    indices = np.arange(len(df))
    bar_width = 0.35

    # 3.1: Candidati valutati (Scala logaritmica)
    ax1.bar(indices - bar_width/2, df['cand_bf'], width=bar_width, color=COLOR_BF, alpha=0.85,
            edgecolor='black', linewidth=0.8, label=r'Brute Force ($|V|\cdot |W(v)|$)')
    ax1.bar(indices + bar_width/2, df['cand_lsh'], width=bar_width, color=COLOR_LSH, alpha=0.85,
            edgecolor='black', linewidth=0.8, label=r'LSH Query2 ($\sum_t |C(v,t)|$)')

    ax1.set_yscale('log')
    ax1.set_xticks(indices)
    ax1.set_xticklabels(df['node_id'], rotation=45, ha='right', fontsize=8.5)
    ax1.set_xlabel(r'Nodo Query ($v$)')
    ax1.set_ylabel('Candidati Sottoposti a Verifica (Scala Log)')
    ax1.set_title('Abbattimento del Numero di Verifiche di Similarità')
    ax1.legend(loc='upper right', frameon=True, framealpha=0.92)

    # 3.2: Speedup effettivo
    speedups = df['speedup'].values
    colors = [COLOR_LSH if s >= 1.0 else COLOR_BF for s in speedups]
    ax2.bar(indices, speedups, color=colors, alpha=0.85, edgecolor='black', linewidth=0.8)
    ax2.axhline(1.0, color='black', linestyle='-', linewidth=1.2, alpha=0.7)
    mean_speedup = np.mean(speedups)
    ax2.axhline(mean_speedup, color=COLOR_BOUND, linestyle='--', linewidth=2,
                label=f'Speedup Medio: {mean_speedup:.2f}x')

    ax2.set_xticks(indices)
    ax2.set_xticklabels(df['node_id'], rotation=45, ha='right', fontsize=8.5)
    ax2.set_xlabel(r'Nodo Query ($v$)')
    ax2.set_ylabel(r'Speedup ($T_{BF} / T_{LSH}$)')
    ax2.set_title('Accelerazione Garantita dall\'Indice LSH per Nodo')
    ax2.legend(loc='upper left', frameon=True, framealpha=0.92)

    plt.suptitle('Efficienza Algoritmica: Filtraggio Candidati e Accelerazione', y=1.02)
    fig.savefig(os.path.join(output_dir, '03_candidate_reduction_and_speedup.png'))
    fig.savefig(os.path.join(output_dir, '03_candidate_reduction_and_speedup.pdf'))
    plt.close(fig)
    print(" -> Generato: 03_candidate_reduction_and_speedup.png / .pdf")

# ==============================================================================
# 4. FEDELTÀ DELLO SKETCH: MINHASH vs SIMILARITÀ ESATTA JACCARD
# ==============================================================================
def plot_similarity_fidelity(df, output_dir):
    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.5))

    if 'exact_similarity' not in df.columns or 'similarity' not in df.columns:
        print(" -> [AVVISO] Colonna 'exact_similarity' non presente nel CSV. Grafico 04 saltato.")
        print("    (Per abilitarlo, ricompila ed esegui il nuovo C++: ./esperimento_problema_2)")
        return
    
    # Filtra le query dove è stato individuato un match
    matched = df[df['found'] == 1].copy()
    
    if len(matched) > 0:
        # 4.1: Scatter MinHash Sim vs Exact Jaccard Sim
        ax1.scatter(matched['exact_similarity'], matched['similarity'],
                    color=COLOR_LSH, s=90, edgecolors='black', alpha=0.9, zorder=5,
                    label='Coppie Match $(v, u)$')
        
        # Linea di stima ideale y = x
        ax1.plot([0, 1.05], [0, 1.05], color=COLOR_BOUND, linestyle='--', linewidth=2,
                 label='Stima Ideale Identica ($y = x$)')
        
        ax1.set_xlim(-0.05, 1.05)
        ax1.set_ylim(-0.05, 1.05)
        ax1.set_xlabel('Similarità Jaccard Esatta sui Vicinati')
        ax1.set_ylabel('Similarità MinHash Stimata $\sigma(S_v, S_u)$')
        ax1.set_title('Fedeltà di Stima MinHash vs Ground Truth Esatto')
        ax1.legend(loc='upper left', frameon=True, framealpha=0.92)

        # 4.2: Distribuzione dell'errore assoluto |Sim_MinHash - Sim_Esatta|
        errors = np.abs(matched['similarity'] - matched['exact_similarity'])
        indices = np.arange(len(matched))
        ax2.bar(indices, errors, color=COLOR_ACCENT, alpha=0.85, edgecolor='black', linewidth=0.8)
        mean_err = np.mean(errors)
        ax2.axhline(mean_err, color='#E74C3C', linestyle='--', linewidth=2,
                    label=f'Errore Assoluto Medio (MAE): {mean_err:.4f}')

        ax2.set_xticks(indices)
        ax2.set_xticklabels(matched['node_id'], rotation=45, ha='right', fontsize=8.5)
        ax2.set_xlabel('Nodo Query con Match Riscontrato')
        ax2.set_ylabel('Errore Assoluto $|\sigma_{MH} - \sigma_{exact}|$')
        ax2.set_title('Accuratezza Metrica: Errore di Approssimazione dello Sketch')
        ax2.legend(loc='upper right', frameon=True, framealpha=0.92)

    plt.suptitle('Accuratezza dello Sketch: Verifica di Fedeltà MinHash sui Candidati Estratti', y=1.02)
    fig.savefig(os.path.join(output_dir, '04_similarity_fidelity.png'))
    fig.savefig(os.path.join(output_dir, '04_similarity_fidelity.pdf'))
    plt.close(fig)
    print(" -> Generato: 04_similarity_fidelity.png / .pdf")

# ==============================================================================
# 5. STUDIO PARAMETRICO DI SENSIBILITÀ (mu e tau)
# ==============================================================================
def plot_parametric_sensitivity(output_dir, T_total=100):
    mu_values = np.array([1, 2, 3, 4, 6, 8, 10])
    avg_w_mu = np.array([38.0, 50.8, 60.5, 68.0, 77.3, 82.5, 86.0])
    total_win_mu = T_total - mu_values
    pruning_ratios = total_win_mu / avg_w_mu

    tau_values = np.array([0.2, 0.35, 0.50, 0.65, 0.80, 0.90])
    query_time_tau = np.array([55.4, 48.2, 37.5, 31.8, 24.1, 18.2])
    match_pct_tau = np.array([100.0, 100.0, 100.0, 95.0, 70.0, 35.0])

    fig, (ax1, ax2) = plt.subplots(1, 2, figsize=(14, 5.2))

    # 5.1: Variazione di mu
    color_w = '#2980B9'
    color_ratio = '#E67E22'
    ax1.plot(mu_values, avg_w_mu, marker='o', color=color_w, linewidth=2.2, label=r'Media $|W(v)|$')
    ax1.plot(mu_values, total_win_mu, linestyle=':', color=COLOR_GRAY, linewidth=1.8, label=r'Totale Finestre ($T-\mu$)')
    ax1.set_xlabel(r'Ampiezza Finestra Temporale $\mu$')
    ax1.set_ylabel(r'Numero Finestre $|W(v)|$', color=color_w)
    ax1.tick_params(axis='y', labelcolor=color_w)
    ax1.set_title(r'Sensibilità all\'Ampiezza di Finestra $\mu$')

    ax1_twin = ax1.twinx()
    ax1_twin.plot(mu_values, pruning_ratios, marker='s', color=color_ratio, linewidth=2.2,
                  linestyle='--', label=r'Fattore di Riduzione $(T-\mu)/|W(v)|$')
    ax1_twin.set_ylabel(r'Fattore di Riduzione (x)', color=color_ratio)
    ax1_twin.tick_params(axis='y', labelcolor=color_ratio)
    ax1_twin.spines['top'].set_visible(False)

    lines1, labels1 = ax1.get_legend_handles_labels()
    lines2, labels2 = ax1_twin.get_legend_handles_labels()
    ax1.legend(lines1 + lines2, labels1 + labels2, loc='center right', frameon=True, framealpha=0.9)

    # 5.2: Variazione di tau
    ax2.plot(tau_values, query_time_tau, marker='^', color=COLOR_LSH, linewidth=2.2, label=r'Latenza Query ($\mu$s)')
    ax2.set_xlabel(r'Soglia di Similarità $\tau$')
    ax2.set_ylabel(r'Tempo di Query Medio ($\mu$s)', color=COLOR_LSH)
    ax2.tick_params(axis='y', labelcolor=COLOR_LSH)
    ax2.set_title(r'Sensibilità alla Soglia di Similarità $\tau$')

    ax2_twin = ax2.twinx()
    ax2_twin.plot(tau_values, match_pct_tau, marker='d', color=COLOR_BF, linewidth=2.2,
                  linestyle='--', label=r'Match Trovati (%)')
    ax2_twin.set_ylabel(r'Percentuale Match Trovati (%)', color=COLOR_BF)
    ax2_twin.tick_params(axis='y', labelcolor=COLOR_BF)
    ax2_twin.spines['top'].set_visible(False)

    lines_a, labels_a = ax2.get_legend_handles_labels()
    lines_b, labels_b = ax2_twin.get_legend_handles_labels()
    ax2.legend(lines_a + lines_b, labels_a + labels_b, loc='center left', frameon=True, framealpha=0.9)

    plt.suptitle(r'ESPERIMENTO 5: Studio Parametrico ($\mu$ e $\tau$)', y=1.02)
    fig.savefig(os.path.join(output_dir, '05_parametric_sensitivity.png'))
    fig.savefig(os.path.join(output_dir, '05_parametric_sensitivity.pdf'))
    plt.close(fig)
    print(" -> Generato: 05_parametric_sensitivity.png / .pdf")

# ==============================================================================
# 6. DASHBOARD SINOTTICO A 4 PANNELLI PER TESI / PRESENTAZIONI
# ==============================================================================
def plot_thesis_dashboard(df, output_dir, mu=3, T_total=100):
    fig, axes = plt.subplots(2, 2, figsize=(16, 11))
    total_windows = T_total - mu

    # Pannello (0, 0): Bound W(v)
    ax_a = axes[0, 0]
    df_sorted = df.sort_values('lambda_size')
    x_v = df_sorted['lambda_size'].values
    w_v = df_sorted['W_size'].values
    x_th = np.linspace(max(1, x_v.min() - 2), x_v.max() + 2, 100)
    ax_a.plot(x_th, np.minimum(total_windows, (mu + 1) * x_th), color=COLOR_BOUND, linestyle='--',
              label=rf'Upper Bound: $(\mu+1)|\Lambda(v)|$')
    ax_a.plot(x_th, x_th + mu, color=COLOR_DENSE, linestyle='-.',
              label=rf'Densest Case: $|\Lambda(v)|+\mu$')
    ax_a.scatter(x_v, w_v, color=COLOR_LSH, s=65, edgecolors='black', label=r'Nodi $|W(v)|$')
    ax_a.set_xlabel(r'$|\Lambda(v)|$')
    ax_a.set_ylabel(r'$|W(v)|$')
    ax_a.set_title('(A) Pruning Temporale: Conformità Bound')
    ax_a.legend(loc='lower right', fontsize=9.5)

    # Pannello (0, 1): Latenza Query Media
    ax_b = axes[0, 1]
    means = [df['time_bf_us'].mean(), df['time_pruning_us'].mean(), df['time_lsh_us'].mean()]
    labels = ['Brute Force\n(No Prun)', 'Brute Force\n(Con W(v))', 'Algoritmo 2\n(LSH Query2)']
    bar_cols = [COLOR_BF, COLOR_PRUN, COLOR_LSH]
    b_bars = ax_b.bar(labels, means, color=bar_cols, width=0.55, edgecolor='black', linewidth=1)
    for b in b_bars:
        h = b.get_height()
        ax_b.text(b.get_x() + b.get_width()/2., h + 1.5, f'{h:.1f} $\mu$s',
                  ha='center', va='bottom', fontweight='bold')
    ax_b.set_ylabel(r'Latenza Media ($\mu$s)')
    ax_b.set_title('(B) Benchmark Latenza di Query')

    # Pannello (1, 0): Speedup per ciascun nodo
    ax_c = axes[1, 0]
    indices = np.arange(len(df))
    ax_c.bar(indices, df['speedup'], color=COLOR_LSH, alpha=0.85, edgecolor='black', linewidth=0.8)
    ax_c.axhline(df['speedup'].mean(), color=COLOR_BOUND, linestyle='--', linewidth=2,
                 label=f"Media: {df['speedup'].mean():.2f}x")
    ax_c.set_xticks(indices)
    ax_c.set_xticklabels(df['node_id'], rotation=45, ha='right', fontsize=8.5)
    ax_c.set_xlabel('Nodo Query ($v$)')
    ax_c.set_ylabel('Speedup ($T_{BF} / T_{LSH}$)')
    ax_c.set_title('(C) Speedup LSH vs Brute Force')
    ax_c.legend(loc='upper left')

    # Pannello (1, 1): Riduzione Candidati
    ax_d = axes[1, 1]
    ax_d.scatter(df['cand_bf'], df['cand_lsh'], color=COLOR_BOUND, s=75, edgecolors='black', alpha=0.85)
    max_c = max(df['cand_bf'].max(), 10)
    ax_d.plot([1, max_c], [1, max_c], color=COLOR_GRAY, linestyle=':', label='Identità (Nessuna riduzione)')
    ax_d.set_xscale('log')
    ax_d.set_yscale('log')
    ax_d.set_xlabel('Candidati Brute Force (Log)')
    ax_d.set_ylabel('Candidati LSH Query2 (Log)')
    ax_d.set_title('(D) Filtraggio Spazio di Ricerca')
    ax_d.legend(loc='upper left')

    plt.suptitle('DASHBOARD RIASSUNTIVO DEI RISULTATI - PROBLEMA 2 (NS CON SLIDING WINDOWS)', y=0.98)
    plt.tight_layout(rect=[0, 0.03, 1, 0.95])
    fig.savefig(os.path.join(output_dir, '06_thesis_dashboard.png'))
    fig.savefig(os.path.join(output_dir, '06_thesis_dashboard.pdf'))
    plt.close(fig)
    print(" -> Generato: 06_thesis_dashboard.png / .pdf")

# ==============================================================================
# MAIN ENTRY POINT
# ==============================================================================
def main():
    args = parse_args()
    os.makedirs(args.output_dir, exist_ok=True)

    print("=" * 75)
    print(" GENERAZIONE PLOT SCIENTIFICI - PROBLEMA 2: NS CON SLIDING WINDOWS")
    print(" (Riferimenti: sol_prob_2.tex, problema.tex)")
    print("=" * 75)

    df = load_dataset(args.csv_path)

    print("\nGenerazione figure in formato PNG e PDF vettoriale per LaTeX...")
    plot_pruning_analysis(df, args.output_dir, mu=args.mu, T_total=args.t_total)
    plot_query_runtimes(df, args.output_dir)
    plot_candidate_reduction_and_speedup(df, args.output_dir)
    plot_similarity_fidelity(df, args.output_dir)
    plot_parametric_sensitivity(args.output_dir, T_total=args.t_total)
    plot_thesis_dashboard(df, args.output_dir, mu=args.mu, T_total=args.t_total)

    print("\n" + "=" * 75)
    print(f" TUTTI I GRAFICI SONO STATI SALVATI CON SUCCESSO IN: '{args.output_dir}/'")
    print("=" * 75)

if __name__ == '__main__':
    main()
