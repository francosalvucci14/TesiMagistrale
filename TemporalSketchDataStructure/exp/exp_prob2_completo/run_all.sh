#!/bin/bash
# =============================================================================
# run_all.sh — Esecuzione completa dell'esperimento Problema 2
#
# Esegue tutto da zero:
#   1. Compila il normalizzatore e l'esperimento
#   2. Normalizza i dataset
#   3. Lancia l'esperimento su ciascun dataset (uno alla volta)
#   4. Genera i plot e le tabelle
# =============================================================================

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SCRIPT_DIR"

# Parametri globali
TAU=0.4
K=128
B=8
NUM_QUERIES=300

# Dataset e mu calibrati
DATASETS=(
  "discretized_datasets/aves-sparrow-social.txt    aves-sparrow    1"
  "discretized_datasets/fb-forum_1.txt             fb-forum_1      900"
  "discretized_datasets/chess_year.txt             chess_year      2"
  "discretized_datasets/CollegeMsg.txt             CollegeMsg      900" #30
  "discretized_datasets/email-Eu-core-temporal_sort.txt email-Eu-core-temporal_sort 900"
  "discretized_datasets/ia-facebook-wall-wosn-dir.txt ia-facebook-wall-wosn-dir 1500"
)

echo "============================================================="
echo " ESPERIMENTO PROBLEMA 2 — ESECUZIONE COMPLETA"
echo "============================================================="
echo ""

# STEP 1: Compilazione
echo "[STEP 1/4] Compilazione..."
echo "  -> discretize_ts.cpp"
g++ -O2 -std=c++17 -o discretize_ts discretize_ts.cpp
echo "  -> esperimento_opt.cpp"
#g++ -O2 -std=c++17 -o exp_opt esperimento_opt.cpp
g++ -O3 -march=native -std=c++20 -o exp_opt esperimento_opt.cpp
echo "  -> Completata."
echo ""

# STEP 2: Normalizzazione
echo "[STEP 2/4] Normalizzazione timestamp..."
mkdir -p discretized_datasets
DS_FILES=$(find sorted_datasets/ -type f | sort)
if [ -z "$DS_FILES" ]; then
  echo "  [ERRORE] Nessun file in sorted_datasets/"
  exit 1
fi
./discretize_ts $DS_FILES
echo ""

# STEP 3: Esperimenti (uno alla volta)
echo "[STEP 3/4] Esecuzione esperimenti..."
mkdir -p results

for entry in "${DATASETS[@]}"; do
  read -r ds_path ds_name mu <<<"$entry"
  if [ ! -f "$ds_path" ]; then
    echo "  [SKIP] File non trovato: $ds_path"
    continue
  fi
  echo ""
  echo "  -------------------------------------------------------"
  echo "  Dataset: $ds_name | mu=$mu | tau=$TAU | k=$K | b=$B"
  echo "  -------------------------------------------------------"
  ./exp_opt "$ds_path" "$ds_name" "$mu" "$TAU" "$K" "$B" "$NUM_QUERIES"
  echo "  -> Completato: $ds_name"
done
echo ""

# STEP 4: Plot e tabelle
echo "[STEP 4/4] Generazione plot e tabelle..."

if [ -f plot_analysis.py ]; then
  echo "  -> Plot di analisi W(v)..."
  python3 plot_analysis.py
fi

if [ -f process_result.py ]; then
  echo "  -> Tabelle riassuntive..."
  python3 process_result.py
fi

echo ""
echo "============================================================="
echo " ESECUZIONE COMPLETATA"
echo "============================================================="
echo " Risultati: results/"
echo " Plot:      plot_analysis/"
echo " Tabelle:   tables/"
echo ""
