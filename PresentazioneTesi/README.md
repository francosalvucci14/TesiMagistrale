# Presentazione tesi — Beamer (revisione)

## Contenuto

- `presentazione.tex`: **sorgente LaTeX Beamer**, formato 16:9.
- `presentazione.pdf`: versione compilata (30 slide, incluse copertina, roadmap, separatori di sezione e ringraziamenti).
- `figures/`: ritagli vettoriali dei grafici sperimentali estratti dalla tesi `main(2).pdf`.

## Compilazione

Da terminale, nella cartella del progetto:

```bash
pdflatex -interaction=nonstopmode presentazione.tex
pdflatex -interaction=nonstopmode presentazione.tex
```

Richiede una distribuzione TeX con `beamer`, `tikz`, `pgfplots` e i pacchetti elencati nel preambolo; tutte le immagini usate sono già in `figures/`.

## Principali modifiche applicate

1. Neighborhood Search statico: formulazione con `S ⊆ V` e `σ(S,N(u)) ≥ τ`, con esempio insiemistico e vicinato del candidato.
2. Scelta esplicita di Jaccard al posto di una generica similarità; MinHash, proprietà di collisione, bande LSH e verifica finale.
3. Variante 1: **prima** building time, spazio e query time teorici; **poi** due ingredienti della soluzione e relative slide: LSH unificato con pruning `W(v)` e oracolo `O` per gli sketch.
4. Variante 2: slide dedicata a building, spazio e query del filtro geometrico sul primo componente MinHash, separando il filtro dalla verifica successiva.
5. Resto dei benchmark, grafici, conclusioni e struttura grafica Beamer preservati.

## Figure

- `oracle_er.pdf`, `oracle_synthetic.pdf`: esperimenti recupero sketch, figura 7 della tesi.
- `pruning_sfhh.pdf`, `pruning_enron.pdf`: analisi pruning, figura 8.
- `candidates_college.pdf`, `candidates_enron.pdf`: selettività filtro variante 2, figura 11.

I diagrammi rimanenti sono vettoriali e realizzati direttamente in TikZ/PGFPlots, modificabili dentro `presentazione.tex`.

## Nota sui bound

- I bound LSH della variante 1 descrivono **l'indice**; l'oracolo per il recupero degli sketch è contabilizzato separatamente.
- I bound sul primo MinHash della variante 2 descrivono **l'indice geometrico e il filtro** e non includono il costo della verifica successiva tramite signature completa.
