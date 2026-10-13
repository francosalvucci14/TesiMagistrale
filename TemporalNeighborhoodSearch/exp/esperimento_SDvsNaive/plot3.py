import argparse
import sys
import matplotlib.pyplot as plt
import matplotlib.ticker as ticker
import numpy as np
import pandas as pd


def sci_notation_formatter(x, pos):
  """Formatta i tick dell'asse Y in notazione scientifica esplicita:

  es. 2000 -> 2 x 10^3, 50000 -> 5 x 10^4, senza mai stampare potenze pure 10^X.
  """
  if x <= 0:
    return ''
  exponent = int(np.floor(np.log10(x)))
  coeff = int(round(x / (10**exponent)))
  return f'${coeff} \\times 10^{{{exponent}}}$'


def time_unit_formatter(x, pos):
  """Formatta i tick dell'asse Y con l'unita' di misura temporale automatica:

  - ns (< 1000)
  - us (1000 <= x < 1.000.000)
  - ms (>= 1.000.000)
  es. 2000 -> 2 us, 50000 -> 50 us, 500000 -> 500 us, 1000000 -> 1 ms.
  """
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
    csv_file='/results/results_interval.csv',
    output_file='/plots/plot_interval.png',
    mode='unit',
    include_10x=False,
):
  """mode:

  - "unit": mostra l'unita' di misura direttamente su ciascun tick (es. 2 us, 5
  us, 20 us, 50 us, 200 us, 500 us)
  - "sci": mostra la notazione scientifica con mantissa (es. 2 x 10^3, 5 x 10^3,
  2 x 10^4, 5 x 10^4)
  include_10x:
  - False (default): esclude i tick 10^X puri (10^3, 10^4, ...) lasciando solo
  [2, 5]
  - True: include anche i tick intermedi con coefficiente 1
  """
  try:
    df = pd.read_csv(csv_file)
  except FileNotFoundError:
    print(
        f"File '{csv_file}' non trovato. Esegui prima l'esperimento C++ per"
        ' generare i dati.'
    )
    return

  # Calcolo dello speedup medio
  if 'speedup' in df.columns:
    avg_speedup = df['speedup'].mean()
  else:
    avg_speedup = (df['naive_time_ns'] / df['trf_time_ns']).mean()

  num_intervals = len(df)
  if num_intervals == 0:
      print("Il file CSV non contiene dati.")
      return
  
  # 1. Dimensioni dinamiche della figura in base al numero di intervalli
  fig_width = max(10, min(20, num_intervals * 0.8))
  fig, ax = plt.subplots(figsize=(fig_width, 6))

  # Riconosce 'mu' o 'interval'
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

  # Configurazione scala logaritmica dell'asse Y
  ax.set_yscale('log')

  # Calcolo dinamico dei ticks sull'asse Y in base ai dati effettivi nel CSV
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

  # Formattazione dell'asse Y in base alla modalita' scelta
  if mode.lower() in ['unit', 'unita', 'tempo']:
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(time_unit_formatter))
    ax.set_ylabel('Tempo Medio di Query', fontsize=11)
  else:
    ax.yaxis.set_major_formatter(ticker.FuncFormatter(sci_notation_formatter))
    ax.set_ylabel('Tempo Medio di Query (nanosecondi)', fontsize=11)

  # Titolo con padding adeguato per non toccare la legenda
  ax.set_title(
      "Tempo Medio di Query vs Ampiezza dell'Intervallo Temporale (μ)",
      fontsize=13,
      fontweight='bold',
      pad=38,
  )
  ax.set_xlabel(
      'Ampiezza Intervallo Temporale (μ)'
      if x_col == 'mu'
      else 'Intervallo Temporale',
      fontsize=11,
  )

  ax.grid(True, which='major', ls='--', alpha=0.55)
  ax.grid(True, which='minor', ls=':', alpha=0.25)

  # Elemento proxy per inserire lo Speedup Medio nella legenda esterna
  speedup_proxy = plt.Line2D(
      [0], [0], color='none', label=f'Speedup Medio: {avg_speedup:.2f}x'
  )

  handles, labels = ax.get_legend_handles_labels()
  handles.append(speedup_proxy)
  labels.append(f'Speedup Medio: {avg_speedup:.2f}x')

  # Legenda posizionata SOPRA il grafico, staccata dalle linee
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
  parser = argparse.ArgumentParser(description='Plot benchmark TRF vs Naive')
  parser.add_argument(
      'csv_file',
      nargs='?',
      default='results/results_interval.csv',
      help='Percorso del file CSV (default: results_interval.csv)',
  )
  parser.add_argument(
      '-o',
      '--output',
      default='plots/plot_interval.png',
      help='Nome del file immagine di output (default: plot_interval.png)',
  )
  parser.add_argument(
      '-m',
      '--mode',
      choices=['unit', 'sci'],
      default='unit',
      help=(
          "Modalita' asse Y: 'unit' (es. 2 us, 50 us, 1 ms) oppure 'sci' (es."
          ' 2x10^3, 5x10^4). Default: unit'
      ),
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
  plot_results(
      csv_file=args.csv_file,
      output_file=args.output,
      mode=args.mode,
      include_10x=args.include_10x,
  )
