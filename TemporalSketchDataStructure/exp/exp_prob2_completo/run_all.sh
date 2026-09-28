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
TAU=0.70
K=64
B=8
NUM_QUERIES=100

# Dataset e mu calibrati
DATASETS=(
  "normalized_datasets/aves-sparrow-social    aves-sparrow    1"
  "normalized_datasets/chess_year             chess_year      4"
  "normalized_datasets/CollegeMsg             CollegeMsg      30"
  "normalized_datasets/fb-forum_1             fb-forum_1      200"
)

echo "============================================================="
echo " ESPERIMENTO PROBLEMA 2 — ESECUZIONE COMPLETA"
echo "============================================================="
echo ""

# STEP 1: Compilazione
echo "[STEP 1/4] Compilazione..."
echo "  -> normalize_timestamps.cpp"
g++ -O2 -std=c++17 -o normalize_ts normalize_timestamps.cpp
echo "  -> esperimento_opt.cpp"
g++ -O2 -std=c++17 -o exp_opt esperimento_opt.cpp
echo "  -> Completata."
echo ""

# STEP 2: Normalizzazione
echo "[STEP 2/4] Normalizzazione timestamp..."
mkdir -p normalized_datasets
DS_FILES=$(find sorted_datasets/ -type f | sort)
if [ -z "$DS_FILES" ]; then
  echo "  [ERRORE] Nessun file in sorted_datasets/"
  exit 1
fi
./normalize_ts $DS_FILES
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
