#!/usr/bin/env bash
# ==============================================================================
# Script per ordinare dataset temporali (formato: u v t) rispetto alla 3ª colonna
# ==============================================================================

set -e

if [ "$#" -lt 1 ]; then
    echo "Uso: $0 <dataset_1.txt> [dataset_2.txt] ... [dataset_X.txt]"
    exit 1
fi

OUT_DIR="sorted_datasets"
mkdir -p "$OUT_DIR"

for file in "$@"; do
    if [ ! -f "$file" ]; then
        echo "[ATTENZIONE] File '$file' non trovato, salto..."
        continue
    fi

    filename=$(basename -- "$file")
    out_file="$OUT_DIR/$filename"

    echo "Ordinamento di '$file' per timestamp t (3ª colonna) -> '$out_file'..."

    # Filtra righe di commento (# o %) e ordina numericamente sulla colonna 3
    # LC_ALL=C garantisce la massima velocità di parsing dei caratteri
    LC_ALL=C awk '!/^[#%]/ && NF>=3 {print $1, $2, $3}' "$file" | \
    LC_ALL=C sort -k3,3n -S 50% --parallel=4 -o "$out_file"

    echo "-> File ordinato salvato in: $out_file"
done

echo "Tutti i dataset sono stati ordinati e salvati in '$OUT_DIR/'."