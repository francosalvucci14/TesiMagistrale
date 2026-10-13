# Esperimento oracolo — Problema 2

Questa versione implementa l'oracolo del **primo blocco MinHash** come descritto nel file LaTeX della soluzione euristica:

- costruzione di `h_v(t)` su tutta la timeline discreta `[1..T]`, con `+inf` sugli istanti senza vicini;
- costruzione dei rettangoli `R_{v,p} = [L_p+1,p] x [p,R_p-1]` per i soli `p` con etichetta finita;
- liste `L(v)` ordinate per span decrescente;
- dizionario `H[x]` e range tree 4D sui punti `(l,p,r,s)` dei rettangoli con etichetta `x`;
- query esatta del filtro preliminare `C(v,mu)`.

## Output CSV
Per ogni query `(v, mu)` vengono salvati:

- `candidate_nodes`: numero di **nodi candidati distinti** `u != v`.
- `candidate_pairs`: numero di **coppie candidate** `(u,t)`.

I plot usano **di default `candidate_nodes`**, così la size dei candidati resta al più `n-1`.

## Plot
Per ogni dataset viene generato **un solo file PNG** con 3 pannelli:

1. **Nodo singolo random vs mu**: `|Candidates(v,mu)|` al variare di `mu`.
2. **Media vs mu**: media di `|Candidates(v,mu)|` sui nodi query campionati.
3. **Ranked nodes**: per il valore `mu*` che massimizza la media, i nodi query ordinati in senso decrescente per size dei candidati, con linea rossa sulla media.

## Esecuzione
```bash
chmod +x run_all.sh
./run_all.sh
```

Variabili utili:

```bash
NUM_QUERIES=100
MAX_INDEX_MB=1024
SEED=42
METRIC=candidate_nodes
SINGLE_NODE_ID=123   # opzionale
RANKED_LOG_Y=1       # opzionale
```
