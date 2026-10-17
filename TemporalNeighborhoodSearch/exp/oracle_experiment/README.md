# Esperimento oracolo — Problema 2

Questa versione implementa l'oracolo del **primo blocco MinHash** come descritto nel file LaTeX della soluzione euristica:

- costruzione di `h_v(t)` su tutta la timeline discreta `[1..T]`, con `+inf` sugli istanti senza vicini;
- costruzione dei rettangoli `R_{v,p} = [L_p+1,p] x [p,R_p-1]` per i soli `p` con etichetta finita;
- liste `L(v)` ordinate per span decrescente;
- dizionario `H[x]` e range tree 4D sui punti `(l,p,r,s)` dei rettangoli con etichetta `x`;
- query esatta del filtro preliminare `C(v,mu)`.

## Output CSV
Per ogni query `(v, mu)` vengono salvati:

- `filter_nodes`: numero di **nodi candidati distinti** `u != v`.
- `filter_pairs`: numero di **coppie candidate** `(u,t)`.

L'esperimento misura solo il filtro dell'oracolo basato sulla prima componente MinHash;
non esegue una verifica successiva con la signature completa. I plot usano **di
default `filter_nodes`**, così la size dei candidati resta al più `n-1`.

## Plot
Per ogni dataset viene generato **un solo file PNG** con due pannelli:

1. **Nodo singolo random vs μ**: `|Candidates(v,μ)|` al variare di `μ`.
2. **Media vs μ**: media di `|Candidates(v,μ)|` sui nodi query campionati.

I valori di `μ` hanno spaziatura uniforme sull'asse X e sono mostrati come
etichette, così valori numericamente distanti non comprimono quelli più piccoli.

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
SINGLE_NODE_ID=123   # opzionale
```
