#!/usr/bin/env bash
# =============================================================================
# Complete experiment for the variable-mu oracle, same workflow as previous:
#   1. Compile discretize_ts.cpp and experiment.cpp
#   2. Discretize all files in sorted_datasets/ using ONLY discretize_ts.cpp
#   3. Build the 4D oracle ONCE per dataset; run QUERIES nodes at every mu
#   4. Plot first-MinHash filter results for each dataset
# =============================================================================
set -euo pipefail
SCRIPT_DIR="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
cd "$SCRIPT_DIR"

# Global parameters; also overridable on the command line as ENV variables.
NUM_QUERIES="${NUM_QUERIES:-0}"    # 0 = all nodes, sample without replacement
MAX_INDEX_MB="${MAX_INDEX_MB:-6144}"
SEED="${SEED:-42}"                 # query sampling, NOT the MinHash hash seed (fixed 42)
MUS="${MUS:-0,1,5,10}"             # integers in discretized units
SINGLE_NODE_ID="${SINGLE_NODE_ID:-}"
PLOT_PAIRS="${PLOT_PAIRS:-0}"      # 0 = distinct candidate nodes (default)

# 'path dataset_name mu_csv' (mu values are discrete integers).
# Input produced by discretize_ts.cpp always ends in .txt even for .mtx sources.
DATASETS=(
  "discretized_datasets/chess_year.txt chess_year 0,1,2,4,7,8"
  "discretized_datasets/CollegeMsg.txt CollegeMsg 0,10,20,30,50,100,500,800"
  "discretized_datasets/ia-yahoo-messages.txt ia-yahoo-messages 0,5,10,20"
  "discretized_datasets/SFHH-conf-sensor.txt SFHH-conf-sensor 0,20,50,100,200,500,700,1000"
  "discretized_datasets/ia-enron-email-dynamic.txt ia-enron-email-dynamic 0,10,20,40,100,200,500,700"
  "discretized_datasets/ia-facebook-wall-wosn-dir.txt ia-facebook-wall-wosn-dir 0,50,100,200,500,1000,2500,5000"
)

SORTED_DIR=sorted_datasets
DISCRETIZED_DIR=discretized_datasets
OUTPUT_DIR=results
PLOT_DIR="$OUTPUT_DIR/plots"
CSV="$OUTPUT_DIR/all_datasets.csv"
BIN="$OUTPUT_DIR/oracle_exp"

mkdir -p "$OUTPUT_DIR" "$PLOT_DIR" "$DISCRETIZED_DIR"
echo '============================================================='
echo ' ORACOLO 4D BASATO SULLA PRIMA COMPONENTE MINHASH'
echo '============================================================='

echo '[STEP 1/4] Compilazione...'
g++ -O2 -std=c++17 -o "$OUTPUT_DIR/discretize_ts" discretize_ts.cpp
g++ -O3 -march=native -DNDEBUG -std=c++20 -Wall -Wextra -o "$BIN" experiment.cpp

echo '[STEP 2/4] Discretizzazione con il discretize_ts.cpp ORIGINALE...'
if [[ ! -d "$SORTED_DIR" ]]; then
  echo "  [ERRORE] Directory $SORTED_DIR/ non trovata" >&2
  exit 1
fi
mapfile -d '' -t ds_sources < <(find "$SORTED_DIR" -type f -print0 | sort -z)
if ((${#ds_sources[@]} == 0)); then
  echo "  [ERRORE] Nessun dataset in $SORTED_DIR/" >&2
  exit 1
fi
"$OUTPUT_DIR/discretize_ts" "${ds_sources[@]}"

echo '[STEP 3/4] Esecuzione esperimenti sui dataset discretizzati...'
rm -f -- "$CSV"
success=0
skipped=0
run_one() {
  local ds_path="$1" ds_name="$2" mus="$3"
  if [[ ! -f "$ds_path" ]]; then
    echo "  [SKIP] Dataset non trovato: $ds_path" >&2
    ((skipped+=1))
    return
  fi
  echo "  -------------------------------------------------------"
  echo "  Dataset: $ds_name | mu=$mus | query=$NUM_QUERIES"
  local options=(--dataset "$ds_path" --out "$CSV" --mus "$mus"
                 --queries "$NUM_QUERIES" --max-index-mb "$MAX_INDEX_MB"
                 --seed "$SEED")
  "$BIN" "${options[@]}"
  ((success+=1))
}
if (($#)); then
  for ds_path in "$@"; do
    run_one "$ds_path" "$(basename -- "$ds_path")" "$MUS"
  done
else
  for entry in "${DATASETS[@]}"; do
    read -r ds_path ds_name mu_csv <<< "$entry"
    run_one "$ds_path" "$ds_name" "${mu_csv:-$MUS}"
  done
fi
if ((success==0)); then
  echo '[ERRORE] Nessun dataset elaborato' >&2
  exit 1
fi

echo '[STEP 4/4] Grafici dei candidati del filtro per ciascun dataset...'
plot_options=(--csv "$CSV" --out-dir "$PLOT_DIR" --seed "$SEED")
if [[ -n "$SINGLE_NODE_ID" ]]; then plot_options+=(--single-node-id "$SINGLE_NODE_ID"); fi
if [[ "$PLOT_PAIRS" == 1 ]]; then plot_options+=(--pairs); fi
python3 plot.py "${plot_options[@]}"

echo '============================================================='
echo " Completed: $success | Skipped: $skipped"
echo " CSV:   $CSV"
echo " Plots: $PLOT_DIR/"
echo '============================================================='
