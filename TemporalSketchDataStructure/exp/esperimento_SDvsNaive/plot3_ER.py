import argparse
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import pandas as pd


def sci_notation_formatter(x, pos):
  """Formatta i tick dell'asse Y in notazione scientifica esplicita."""
  if x <= 0:
    return ''
  exponent = int(np.floor(np.log10(x)))
  coeff = int(round(x / (10**exponent)))
  return f'${coeff} \\times 10^{{{exponent}}}$'


def time_unit_formatter(x, pos):
  """Formatta i tick dell'asse Y con l'unita' di misura temporale automatica."""
  if x <= 0:
    return ''
  if x < 1_000:
    return f'{int(x)} ns'
  elif x < 1_000_000:
    val = x / 1_000
    return f'{int(val)} $\\mu$s' if val == int(val) else f'{val:.1f} $\\mu$s'
  else:
    val = x / 1_000_000
    return f'{int(val)} ms' if val == int(val) else f'{val:.1f} ms'


def plot_results(
    csv_file,
    output_file,
    graph_label='Erdos-Renyi',
    mode='unit',
    include_10x=False,
):
  try:
    df = pd.read_csv(csv_file)
  except FileNotFoundError:
    print(
        f"Errore: File '{csv_file}' non trovato. Esegui prima l'esperimento C++"
        ' per generare i dati.'
    )
    return

  # Calcolo dello speedup medio
  if 'speedup' in df.columns:
    avg_speedup = df['speedup'].mean()
  else:
    avg_speedup = (df['naive_time_ns'] / df['trf_time_ns']).mean()

  num_intervals = len(df)
  if num_intervals == 0:
    print('Il file CSV non contiene dati.')
    return

  # Dimensioni dinamiche della figura
  fig_width = max(10, min(20, num_intervals * 0.8))
  fig, ax = plt.subplots(figsize=(fig_width, 6))

  x_col = (
      'mu'
      if 'mu' in df.columns
      else ('interval' if 'interval' in df.columns else df.columns[0])
  )

  # Curve TRF vs Naive
  ax.plot(
      df[x_col],
      df['trf_time_ns'],
      marker='o',
      markersize=7,
      linestyle='-',
      color='#1f77b4',
      label='Temporal Range Forest (Query su Pre-indicizzato)',
      linewidth=2.5,
  )
  ax.plot(
      df[x_col],
      df['naive_time_ns'],
      marker='s',
      markersize=7,
      linestyle='--',
      color='#d62728',
      label='Naive (Ricostruzione Sketch da Zero)',
      linewidth=2.5,
  )

  # Scala logaritmica asse Y
  ax.set_yscale('log')

  y_min = min(df['trf_time_ns'].min(), df['naive_time_ns'].min())
  y_max = max(df['trf_time_ns'].max(), df['naive_time_ns'].max())

  dec_start = int(np.floor(np.log10(y_min)))
  dec_end = int(np.ceil(np.log10(y_max)))

  multipliers = [1, 2, 5] if include_10x else [2, 5]

  y_ticks = []
  for dec in range(dec_start, dec_end + 1):
    for mult in multipliers:
      val = mult * (10**dec)
      if val >= y_min * 0.7 and val <= y_max * 1.5:
        y_ticks.append(val)
  y_ticks = sorted(list(set(y_ticks)))
  ax.set_yticks(y_ticks)

  # Formattazione ticks asse Y
  if mode.lower() in ['unit', 'unita', 'tempo']:
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(time_unit_formatter))
    ax.set_ylabel('Tempo Medio di Query', fontsize=11)
  else:
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(sci_notation_formatter))
    ax.set_ylabel('Tempo Medio di Query (nanosecondi)', fontsize=11)

  title_text = (
      "Tempo Medio di Query vs Ampiezza dell'Intervallo Temporale (μ)\n"
      f'[{graph_label}]'
  )
  ax.set_title(title_text, fontsize=12, fontweight='bold', pad=45)
  ax.set_xlabel(
      'Ampiezza Intervallo Temporale (μ)'
      if x_col == 'mu'
      else 'Intervallo Temporale',
      fontsize=11,
  )

  ax.grid(True, which='major', ls='--', alpha=0.55)
  ax.grid(True, which='minor', ls=':', alpha=0.25)

  # Proxy artist per mostrare lo Speedup Medio nella legenda
  speedup_proxy = plt.Line2D(
      [0], [0], color='none', label=f'Speedup Medio: {avg_speedup:.2f}x'
  )

  handles, labels = ax.get_legend_handles_labels()
  handles.append(speedup_proxy)
  labels.append(f'Speedup Medio: {avg_speedup:.2f}x')

  ax.legend(
      handles=handles,
      labels=labels,
      loc='lower center',
      bbox_to_anchor=(0.5, 1.01),
      ncol=3,
      fontsize=9.5,
      frameon=True,
      framealpha=0.95,
      edgecolor='#cccccc',
  )

  plt.tight_layout()
  plt.savefig(output_file, dpi=300)
  print(f"Grafico salvato con successo in '{output_file}' (modalita': {mode})")


def parse_cli():
  parser = argparse.ArgumentParser(
      description='Plot benchmark TRF vs Naive per intervalli temporali'
  )
  parser.add_argument(
      'csv_file',
      nargs='?',
      default=None,
      help=(
          'Percorso personalizzato del file CSV (opzionale, ha priorita su'
          ' --graph)'
      ),
  )
  parser.add_argument(
      '-g',
      '--graph',
      choices=['er', 'standard', 'synthetic'],
      default='er',
      help=(
          "Specifica il tipo di grafo: 'er' (Erdos-Renyi) o"
          " 'standard'/'synthetic'. Default: er"
      ),
  )
  parser.add_argument(
      '-o',
      '--output',
      default=None,
      help='Nome del file immagine di output (PNG). Se omesso, viene dedotto.',
  )
  parser.add_argument(
      '-m',
      '--mode',
      choices=['unit', 'sci'],
      default='unit',
      help="Modalita' asse Y: 'unit' oppure 'sci'. Default: unit",
  )
  parser.add_argument(
      '--include-10x',
      action='store_true',
      default=False,
      help=(
          'Se specificato, include anche i tick con mantissa 1 (es. 10^4).'
          ' Default: False'
      ),
  )
  return parser.parse_args()


if __name__ == '__main__':
  args = parse_cli()

  # Normalizzazione tipo di grafo
  is_er = args.graph.lower() == 'er'
  graph_label = (
      'Grafo Erdős–Rényi' if is_er else 'Grafo Sintetico Standard'
  )

  # Risoluzione automatica del file CSV se non passato come argomento posizionale
  if args.csv_file:
    csv_file = args.csv_file
  else:
    csv_file = (
        'results/results_interval_er.csv'
        if is_er
        else 'results/results_interval_standard.csv'
    )

  # Risoluzione automatica del file di output PNG
  if args.output:
    output_file = args.output
  else:
    output_file = (
        'plots/plot_interval_er.png' if is_er else 'plots/plot_interval_standard.png'
    )

  plot_results(
      csv_file=csv_file,
      output_file=output_file,
      graph_label=graph_label,
      mode=args.mode,
      include_10x=args.include_10x,
  )
