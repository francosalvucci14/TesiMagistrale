# Esperimento 6.2 — Temporal Neighborhood Search

Questo esperimento valuta la ricerca di vicinati simili in **grafi temporali**, confrontando quattro approcci:

- **BruteForce**: baseline esaustiva su nodi e finestre temporali.
- **MiaSoluzione**: indice LSH unificato con pruning delle finestre rilevanti \(W(v)\).
- **MiaSoluzione_NoWv**: variante senza pruning temporale.
- **Alternativa**: indici LSH indipendenti per finestra temporale.

Gli sketch **MinHash** dei vicinati temporali vengono ricavati dalla **TemporalRangeForest**. Per ogni nodo sorgente interrogato, la query verifica se esistono un altro nodo e una finestra di ampiezza \(\mu\) con similarità stimata almeno pari a \(\tau\).

L'esperimento confronta **tempo medio di query**, **tempo di costruzione** e **memoria ausiliaria**; analizza inoltre la dimensione di \(W(v)\) e il **recall delle risposte positive** della soluzione LSH rispetto alla brute force. La baseline è esaustiva rispetto alla verifica sulle signature MinHash, non rispetto al Jaccard esatto.

## Requisiti

- Linux, Bash e `g++` con supporto C++20.
- Python 3 con `numpy`, `pandas` e `matplotlib`:

```bash
python3 -m pip install numpy pandas matplotlib
```

## Riproduzione dell'esperimento

1. Collocare nella stessa directory `esperimento_opt.cpp`, `discretize_ts.cpp`, `plot_analysis.py`, `process_result.py` e `run_all.sh`.
2. Inserire i dataset originali nella cartella `sorted_datasets/`, con un arco temporale per riga nel formato `u v t` (timestamp intero).
3. Verificare nell'array `DATASETS` di `run_all.sh` i nomi dei dataset e il parametro `mu` per ciascuno.
4. Eseguire dalla directory del progetto:

```bash
bash run_all.sh
```

Lo script compila i programmi C++, discretizza i timestamp, esegue gli algoritmi su ogni dataset configurato e genera i risultati e i grafici.

La configurazione attuale di `run_all.sh` usa `TAU=0.35`, `K=64`, `B=8` e `NUM_QUERIES=8000`. Per riprodurre gli stessi esperimenti, mantenere invariati **dataset, valori di `mu`, parametri e codice**. Il campionamento delle query e le funzioni MinHash utilizzano seed fisso `42`; i tempi possono variare in base all'hardware.

## Output

- `discretized_datasets/`: dataset con timestamp discretizzati.
- `results/`: CSV con benchmark, analisi di \(W(v)\) e confronto delle risposte positive.
- `plot_analysis/`: grafici di pruning e recall rispetto alla brute force.
- `tables/`: tabelle riassuntive e grafici benchmark (LSH vs alternativa; brute force vs LSH).

Per **rigenerare soltanto grafici e tabelle** a partire dai CSV già presenti in `results/`, eseguire:

```bash
python3 plot_analysis.py
python3 process_result.py
```

> **Nota:** `run_all.sh` effettua nuovamente la discretizzazione dei file presenti in `sorted_datasets/`. Per conservare i risultati già ottenuti, utilizzare gli script Python senza rilanciare l'intera pipeline.
