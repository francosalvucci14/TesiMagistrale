domanda : dove posso trovare un'implementazione dell'algoritmo SCAN proposto da Xiaowei Xu, Nurcan Yuruk e Zhidan feng per il clustering di una rete (intesa come grafo) fatta in c++ o pythion?

Sì. Per l’algoritmo **SCAN (Structural Clustering Algorithm for Networks)** del paper di Xiaowei Xu, Nurcan Yuruk e Zhidan Feng esistono alcune implementazioni utilizzabili direttamente o da cui partire.

Le opzioni più pratiche oggi sono:

### Python: PyGOD

La libreria più “ufficiale”/riusabile che include SCAN è:

* [PyGOD SCAN documentation](https://docs.pygod.org/en/v0.4.0/generated/pygod.detector.SCAN.html?utm_source=chatgpt.com) ([docs.pygod.org][1])

Implementa proprio **SCAN per grafi**, con parametri:

* `eps` → soglia di similarità strutturale
* `mu` → numero minimo di vicini strutturali

Esempio minimo:

```python
import torch
from torch_geometric.data import Data
from pygod.detector import SCAN

# edge_index formato PyTorch Geometric
edge_index = torch.tensor([
    [0,1,1,2,3,4],
    [1,0,2,1,4,3]
], dtype=torch.long)

data = Data(edge_index=edge_index)

model = SCAN(eps=0.7, mu=2)

model.fit(data)

print(model.label_)
```

Installazione:

```bash
pip install pygod
```

Dipende da:

* PyTorch
* PyTorch Geometric

Questa è probabilmente la soluzione più semplice se vuoi usare SCAN “vero” senza reimplementarlo.

---

### Implementazione JavaScript molto fedele all’articolo originale

Esiste anche:

* [scanjs su npm](https://npm.io/package/scanjs?utm_source=chatgpt.com) ([npm.io][2])

che dichiara esplicitamente di implementare:

> “Structural Clustering Algorithm designed by Xiaowei Xu, Nurcan Yuruk…”

Il codice è abbastanza leggibile e può essere facilmente portato in Python o C++.

---

### Paper originale

Per riferimento teorico:

* [SCAN: a structural clustering algorithm for networks](https://www.sciweavers.org/publications/scan-structural-clustering-algorithm-networks?utm_source=chatgpt.com) ([sciweavers.org][3])

---

### C++

Non ho trovato una libreria C++ moderna e mantenuta equivalente a PyGOD che implementi SCAN “out of the box”.

Però hai alcune possibilità:

1. usare implementazioni accademiche parallele:

   * [Parallel Index-Based Structural Graph Clustering and Its Approximation](https://arxiv.org/abs/2012.11188?utm_source=chatgpt.com) ([arXiv][4])

2. partire dal codice JS (`scanjs`) e tradurlo in C++

3. implementarlo direttamente: l’algoritmo non è molto lungo.

---

### Se vuoi implementarlo tu

La parte chiave è la **similarità strutturale**:

$$\sigma(v,u)=\frac{|\Gamma(v)\cap\Gamma(u)|}{\sqrt{|\Gamma(v)|\cdot|\Gamma(u)|}}$$

dove:

* $\Gamma(v)$ è il vicinato del nodo (v) incluso il nodo stesso
* due nodi sono “strutturalmente connessi” se $\sigma(v,u) \ge \varepsilon$

Poi il clustering è molto simile a DBSCAN:

* core node ↔ almeno `mu` vicini strutturali
* espansione BFS/DFS
* identificazione di hub e outlier

---

### Consiglio pratico

Se devi:

* **fare ricerca / prototipo rapido** → PyGOD
* **integrare in pipeline C++ ad alte prestazioni** → implementazione custom
* **studiare l’algoritmo** → scanjs + paper originale

Se vuoi, posso anche:

* scriverti una **implementazione completa SCAN in Python con NetworkX**
* oppure una versione **C++ STL-only**
* oppure mostrarti come adattarlo a grafi pesati/diretti.

[1]: https://docs.pygod.org/en/v0.4.0/generated/pygod.detector.SCAN.html?utm_source=chatgpt.com "SCAN - PyGOD 0.4.0 documentation"
[2]: https://npm.io/package/scanjs?utm_source=chatgpt.com "Scanjs NPM | npm.io"
[3]: https://www.sciweavers.org/publications/scan-structural-clustering-algorithm-networks?utm_source=chatgpt.com "SCAN: a structural clustering algorithm for networks | Sciweavers"
[4]: https://arxiv.org/abs/2012.11188?utm_source=chatgpt.com "Parallel Index-Based Structural Graph Clustering and Its Approximation"


---

domanda : no vorrei testare l'algoritmo, come posso implementarlo io da zero?

Per implementare **SCAN da zero** ti conviene partire da una versione minimale e corretta, poi eventualmente ottimizzarla.

L’algoritmo è sorprendentemente compatto: il cuore sta in:

1. similarità strutturale
2. definizione di core node
3. espansione dei cluster stile DBSCAN
4. classificazione finale di hub/outlier

---

# 1. Idea dell’algoritmo

SCAN lavora su un grafo (G=(V,E)).

Due nodi sono “simili” se hanno molti vicini in comune.

La similarità strutturale è:

$\sigma(v,u)=\frac{|\Gamma(v)\cap\Gamma(u)|}{\sqrt{|\Gamma(v)|\cdot|\Gamma(u)|}}$

dove:

* $\Gamma(v)$ = vicini di (v) + (v) stesso
* si usa:

  * `eps` = soglia minima di similarità
  * `mu` = numero minimo di vicini strutturali

Un nodo è **core** se ha almeno `mu` vicini con similarità ≥ `eps`.

---

# 2. Strutture dati

In Python usa:

```python id="0lfx2f"
adj = {
    0: {1,2},
    1: {0,2},
    2: {0,1,3},
    3: {2}
}
```

Con set:

* intersezioni veloci
* codice semplice

---

# 3. Similarità strutturale

Implementazione diretta:

```python id="2m4m8y"
from math import sqrt

def structural_similarity(v, u, adj):

    Nv = adj[v] | {v}
    Nu = adj[u] | {u}

    common = len(Nv & Nu)

    return common / sqrt(len(Nv) * len(Nu))
```

---

# 4. Vicini strutturali

Per ogni nodo calcoli i vicini “sufficientemente simili”.

```python id="6nt2e7"
def epsilon_neighbors(v, adj, eps):

    result = []

    for u in adj[v]:

        s = structural_similarity(v, u, adj)

        if s >= eps:
            result.append(u)

    return result
```

---

# 5. Core node

```python id="bhh8de"
def is_core(v, adj, eps, mu):

    return len(epsilon_neighbors(v, adj, eps)) >= mu
```

---

# 6. Espansione cluster (parte DBSCAN)

Qui nasce davvero SCAN.

```python id="j5n6ul"
from collections import deque

UNCLASSIFIED = -1
NON_MEMBER = -2

def scan(adj, eps=0.7, mu=2):

    cluster_id = 0

    labels = {v: UNCLASSIFIED for v in adj}

    for v in adj:

        if labels[v] != UNCLASSIFIED:
            continue

        if not is_core(v, adj, eps, mu):
            labels[v] = NON_MEMBER
            continue

        # nuovo cluster
        labels[v] = cluster_id

        queue = deque([v])

        while queue:

            x = queue.popleft()

            neighbors = epsilon_neighbors(x, adj, eps)

            if len(neighbors) >= mu:

                for y in neighbors:

                    if labels[y] in [UNCLASSIFIED, NON_MEMBER]:

                        if labels[y] == UNCLASSIFIED:
                            queue.append(y)

                        labels[y] = cluster_id

        cluster_id += 1

    return labels
```

---

# 7. Hub e outlier

Nel paper originale:

* OUTLIER → collegato a un solo cluster
* HUB → collega cluster differenti

Puoi identificarli dopo il clustering.

Esempio:

```python id="0gk8tz"
def classify_hubs_outliers(adj, labels):

    hubs = []
    outliers = []

    for v in adj:

        if labels[v] != -2:
            continue

        neighbor_clusters = set()

        for u in adj[v]:

            if labels[u] >= 0:
                neighbor_clusters.add(labels[u])

        if len(neighbor_clusters) >= 2:
            hubs.append(v)
        else:
            outliers.append(v)

    return hubs, outliers
```

---

# 8. Esempio completo

```python id="zyhyk7"
adj = {
    0:{1,2},
    1:{0,2},
    2:{0,1,3},
    3:{2,4,5},
    4:{3,5},
    5:{3,4},
    6:{7},
    7:{6}
}

labels = scan(adj, eps=0.7, mu=2)

print(labels)

hubs, outliers = classify_hubs_outliers(adj, labels)

print("hubs:", hubs)
print("outliers:", outliers)
```

---

# 9. Complessità

La versione naive è circa:

[
O\left(\sum_{(u,v)\in E}\min(d(u),d(v))\right)
]

ma in pratica:

* su grafi piccoli/medi va bene
* su grafi grandi serve caching

---

# 10. Ottimizzazioni importanti

## A. Precomputare le similarità

Eviti di ricalcolarle.

```python id="4hujg8"
sim_cache = {}

for v in adj:
    for u in adj[v]:
        if (u,v) not in sim_cache:
            sim_cache[(v,u)] = structural_similarity(v,u,adj)
```

---

## B. Intersezione sul set più piccolo

Molto importante.

```python id="4stwtk"
small, large = (Nv, Nu) if len(Nv) < len(Nu) else (Nu, Nv)

common = sum(1 for x in small if x in large)
```

---

## C. CSR arrays (stile C++)

Per grandi grafi:

* adjacency list compressa
* niente set Python

---

# 11. Differenza fondamentale con DBSCAN

DBSCAN usa distanza geometrica.

SCAN usa:

* struttura locale del grafo
* similarità dei vicinati

Per questo trova:

* comunità dense
* hub
* outlier

molto bene nei social network.

---

# 12. Se vuoi una versione “seria”

Posso anche scriverti:

* implementazione:

  * NetworkX
  * NumPy
  * C++
  * OpenMP parallela
* parser edge-list
* benchmark
* confronto SCAN vs Louvain vs Leiden
* versione ottimizzata per grafi grandi
* implementazione identica al paper originale.

---

domanda : usa networkx e numpy poi ottimizza

Di seguito ti costruisco una versione progressiva:

1. **SCAN con NetworkX** → leggibile e facile da verificare
2. **Versione ottimizzata NumPy**
3. **Strategie per grafi enormi** (milioni di archi)

---

# 1. Versione base con NetworkX

Installa:

```bash id="4h7nfw"
pip install networkx numpy
```

---

## Implementazione completa

```python id="rydepc"
import networkx as nx
import numpy as np
from math import sqrt
from collections import deque

UNCLASSIFIED = -1
NON_MEMBER = -2


class SCAN:

    def __init__(self, eps=0.7, mu=2):

        self.eps = eps
        self.mu = mu

        self.labels_ = {}
        self.hubs_ = []
        self.outliers_ = []

    # -------------------------------------------------
    # Similarità strutturale
    # -------------------------------------------------

    def structural_similarity(self, G, v, u):

        Nv = set(G.neighbors(v))
        Nu = set(G.neighbors(u))

        Nv.add(v)
        Nu.add(u)

        common = len(Nv & Nu)

        return common / sqrt(len(Nv) * len(Nu))

    # -------------------------------------------------
    # epsilon-neighbors
    # -------------------------------------------------

    def epsilon_neighbors(self, G, v):

        result = []

        for u in G.neighbors(v):

            s = self.structural_similarity(G, v, u)

            if s >= self.eps:
                result.append(u)

        return result

    # -------------------------------------------------
    # core node
    # -------------------------------------------------

    def is_core(self, G, v):

        return len(self.epsilon_neighbors(G, v)) >= self.mu

    # -------------------------------------------------
    # fit
    # -------------------------------------------------

    def fit(self, G):

        labels = {v: UNCLASSIFIED for v in G.nodes()}

        cluster_id = 0

        for v in G.nodes():

            if labels[v] != UNCLASSIFIED:
                continue

            if not self.is_core(G, v):

                labels[v] = NON_MEMBER
                continue

            # nuovo cluster
            labels[v] = cluster_id

            queue = deque([v])

            while queue:

                x = queue.popleft()

                neighbors = self.epsilon_neighbors(G, x)

                if len(neighbors) >= self.mu:

                    for y in neighbors:

                        if labels[y] in [UNCLASSIFIED, NON_MEMBER]:

                            if labels[y] == UNCLASSIFIED:
                                queue.append(y)

                            labels[y] = cluster_id

            cluster_id += 1

        self.labels_ = labels

        self._classify_hubs_outliers(G)

        return self

    # -------------------------------------------------
    # hub / outlier
    # -------------------------------------------------

    def _classify_hubs_outliers(self, G):

        hubs = []
        outliers = []

        for v in G.nodes():

            if self.labels_[v] != NON_MEMBER:
                continue

            neighbor_clusters = set()

            for u in G.neighbors(v):

                if self.labels_[u] >= 0:
                    neighbor_clusters.add(self.labels_[u])

            if len(neighbor_clusters) >= 2:
                hubs.append(v)
            else:
                outliers.append(v)

        self.hubs_ = hubs
        self.outliers_ = outliers
```

---

# 2. Esempio di utilizzo

```python id="4m8ofh"
G = nx.karate_club_graph()

scan = SCAN(eps=0.7, mu=2)

scan.fit(G)

print(scan.labels_)
print("hubs:", scan.hubs_)
print("outliers:", scan.outliers_)
```

---

# 3. Problema della versione naive

Questa implementazione:

* ricostruisce continuamente i set
* ricalcola le similarità
* usa Python puro

Su grafi grandi diventa lentissima.

---

# 4. Versione ottimizzata NumPy

Ora facciamo una versione seria.

Idea:

* convertire il grafo in adjacency list compatte
* cache delle similarità
* intersezioni efficienti

---

# 5. Preprocessing efficiente

```python id="0fh9bp"
def build_adj_sets(G):

    adj = {}

    for v in G.nodes():

        s = set(G.neighbors(v))
        s.add(v)

        adj[v] = s

    return adj
```

---

# 6. Similarità ottimizzata

Qui arriva la prima vera ottimizzazione.

```python id="ajx4kl"
def structural_similarity_fast(v, u, adj):

    Nv = adj[v]
    Nu = adj[u]

    # iteriamo sul più piccolo
    if len(Nv) > len(Nu):
        Nv, Nu = Nu, Nv

    common = sum((x in Nu) for x in Nv)

    return common / np.sqrt(len(adj[v]) * len(adj[u]))
```

Questo riduce enormemente il costo.

---

# 7. Cache delle similarità

CRUCIALE.

```python id="0fey9d"
sim_cache = {}

def similarity_cached(v, u, adj):

    key = (v, u) if v < u else (u, v)

    if key not in sim_cache:

        sim_cache[key] = structural_similarity_fast(v, u, adj)

    return sim_cache[key]
```

---

# 8. Versione scalabile

```python id="tnh2zm"
class FastSCAN:

    def __init__(self, eps=0.7, mu=2):

        self.eps = eps
        self.mu = mu

    def fit(self, G):

        adj = build_adj_sets(G)

        labels = {v: -1 for v in G.nodes()}

        sim_cache = {}

        def sim(v, u):

            key = (v, u) if v < u else (u, v)

            if key not in sim_cache:

                sim_cache[key] = structural_similarity_fast(
                    v, u, adj
                )

            return sim_cache[key]

        cluster_id = 0

        for v in G.nodes():

            if labels[v] != -1:
                continue

            eps_neighbors = [
                u for u in G.neighbors(v)
                if sim(v, u) >= self.eps
            ]

            if len(eps_neighbors) < self.mu:

                labels[v] = -2
                continue

            labels[v] = cluster_id

            queue = deque(eps_neighbors)

            while queue:

                x = queue.popleft()

                if labels[x] == -1:
                    labels[x] = cluster_id

                x_neighbors = [
                    u for u in G.neighbors(x)
                    if sim(x, u) >= self.eps
                ]

                if len(x_neighbors) >= self.mu:

                    for y in x_neighbors:

                        if labels[y] in [-1, -2]:

                            if labels[y] == -1:
                                queue.append(y)

                            labels[y] = cluster_id

            cluster_id += 1

        self.labels_ = labels

        return self
```

---

# 9. Complessità reale

Il collo di bottiglia è:

$$\Gamma(v)\cap\Gamma(u)$$

La complessità dipende dalla densità locale.

Su social network sparsi:

* molto meglio del caso pessimo
* ancora costoso oltre ~10M archi

---

# 10. Come scalarlo DAVVERO

Qui entrano le tecniche serie.

---

# A. Adjacency list ordinate + two pointers

INVECE dei set Python.

Questo è il salto enorme.

```text
[1,5,8,11]
[1,3,8,20]
```

Intersezione:

* O(deg(v)+deg(u))
* cache friendly
* vettorizzabile

---

# B. CSR matrix (Compressed Sparse Row)

Formato standard:

* scipy sparse
* graph-tool
* igraph
* cuGraph

Memoria drasticamente ridotta.

---

# C. Parallelizzazione

Le similarità possono essere computate in parallelo.

Con:

* multiprocessing
* numba
* OpenMP
* CUDA

SCAN parallelizza molto bene.

---

# D. Pruning matematico

Osservazione:

Se:

$$\frac{\min(d(u),d(v))}{\sqrt{d(u)d(v)}} < \varepsilon$$

allora NON serve calcolare l’intersezione.

Questo pruning accelera enormemente.

---

# E. Approximate SCAN

Per grafi enormi:

* MinHash
* LSH
* sampling

eviti confronti inutili.

---

# 11. Per grafi veramente enormi

Consiglio pratico:

| Dimensione   | Strategia          |
| ------------ | ------------------ |
| < 100k archi | NetworkX           |
| 100k – 5M    | NumPy + cache      |
| 5M – 100M    | CSR + Numba/OpenMP |
| >100M        | GPU/cuGraph        |

---

# 12. Stack moderno consigliato

Se vuoi fare benchmarking serio:

* NetworkX → debugging
* SciPy sparse → memoria
* Numba → JIT
* graph-tool → ultra veloce
* cuGraph → GPU

---

# 13. Posso anche scriverti

Posso generarti:

* versione:

  * Numba
  * SciPy sparse CSR
  * OpenMP C++
  * CUDA
* benchmark automatici
* visualizzazione comunità
* parser SNAP datasets
* implementazione identica al paper
* confronto con Louvain/Leiden/Infomap
* versione streaming/dinamica.

---

domanda: come uso il minhashing

Il **MinHash** è probabilmente il modo più intelligente per rendere SCAN scalabile su grafi enormi.

L’idea fondamentale è:

SCAN richiede continuamente:

$$|\Gamma(u)\cap\Gamma(v)|$$

cioè intersezioni di vicinati.

Questo è costosissimo.

Con MinHash tu:

* NON calcoli più le intersezioni esatte
* costruisci una “firma” compatta del vicinato
* stimi rapidamente la similarità

---

# 1. Il punto chiave

La similarità strutturale di SCAN è:

$$\sigma(u,v)=\frac{|\Gamma(u)\cap\Gamma(v)|}{\sqrt{|\Gamma(u)|\cdot|\Gamma(v)|}}$$

MinHash invece approssima la **Jaccard similarity**:

$$J(u,v)=\frac{|\Gamma(u)\cap\Gamma(v)|}{|\Gamma(u)\cup\Gamma(v)|}$$

Le due cose sono fortemente correlate.

---

# 2. Come usare MinHash in SCAN

Hai due possibilità.

---

# APPROCCIO A — filtro candidato

Quello migliore.

## Idea

1. MinHash trova rapidamente coppie POTENZIALMENTE simili
2. solo su quelle calcoli la similarità SCAN vera

Questo evita il 99.9% dei confronti.

---

# Pipeline

## Step 1 — neighborhood sets

```python id="m9h02j"
adj[v] = set(neighbors(v)) | {v}
```

---

## Step 2 — MinHash signatures

Per ogni nodo costruisci una firma:

```text id="rjpj3o"
node -> [12, 81, 5, 90, ...]
```

---

## Step 3 — LSH buckets

Metti firme simili nello stesso bucket.

Ottieni:

* candidate pairs

---

## Step 4 — similarity esatta SCAN

SOLO per candidate pairs:

```python id="12qkq2"
if exact_structural_similarity(u,v) >= eps:
```

---

# 3. Libreria migliore

Usa:

```bash id="jv3f91"
pip install datasketch
```

---

# 4. Implementazione reale

---

## Costruzione MinHash

```python id="txl9el"
from datasketch import MinHash

def build_minhash(neighbors, num_perm=128):

    m = MinHash(num_perm=num_perm)

    for x in neighbors:

        m.update(str(x).encode())

    return m
```

---

## Creazione firme

```python id="z1qdbw"
signatures = {}

for v in G.nodes():

    neigh = set(G.neighbors(v))
    neigh.add(v)

    signatures[v] = build_minhash(neigh)
```

---

# 5. Locality Sensitive Hashing (LSH)

Qui avviene la magia.

```python id="c0pn3n"
from datasketch import MinHashLSH

lsh = MinHashLSH(
    threshold=0.5,
    num_perm=128
)

for v, sig in signatures.items():

    lsh.insert(str(v), sig)
```

---

# 6. Recupero candidate pairs

```python id="4pm5v0"
candidate_pairs = set()

for v in G.nodes():

    candidates = lsh.query(signatures[v])

    for u in candidates:

        u = int(u)

        if u != v:
            candidate_pairs.add(
                tuple(sorted((u,v)))
            )
```

Ora NON confronti più tutti gli archi.

---

# 7. Similarità SCAN esatta solo sui candidati

```python id="95f5ks"
def structural_similarity(u, v, adj):

    Nu = adj[u]
    Nv = adj[v]

    inter = len(Nu & Nv)

    return inter / np.sqrt(len(Nu)*len(Nv))
```

---

# 8. Complessità

---

## SCAN classico

Quasi:

$$O\left(\sum_{(u,v)\in E}\min(d(u),d(v))\right)$$

---

## SCAN + MinHash

Diventa:

$$O(n \cdot k)$$

per costruire le firme + pochi confronti reali.

Dove:

* (k)=num_perm

Enorme miglioramento.

---

# 9. Dove funziona benissimo

MinHash è ottimo quando:

* grafi sparsi
* community ben separate
* social network
* web graphs

---

# 10. Problema importante

SCAN usa:

$$\frac{|A\cap B|}{\sqrt{|A||B|}}$$

NON Jaccard.

Quindi:

* MinHash non approssima direttamente SCAN
* ma è eccellente come filtro preliminare

---

# 11. Trucco migliore: upper bound pruning

Puoi fare:

Se:

$$J(A,B)<t$$

allora sicuramente:

$$\sigma(A,B)<\varepsilon$$

scegliendo bene (t).

Quindi:

* MinHash elimina confronti impossibili.

---

# 12. Strategia industriale vera

Su grafi enormi si fa:

```text id="qms2zt"
LSH
 ↓
candidate edges
 ↓
exact structural similarity
 ↓
SCAN expansion
```

Questa è la pipeline usata realmente.

---

# 13. Ulteriore accelerazione: edge-only scan

Nel paper originale:

* confronti solo nodi adiacenti

Quindi:

* MinHash sugli edge endpoints
* enorme riduzione

---

# 14. Versione super scalabile

Per milioni di nodi:

| Tecnica    | Effetto          |
| ---------- | ---------------- |
| CSR sparse | memoria          |
| MinHash    | pruning          |
| LSH        | candidate search |
| Numba      | CPU              |
| OpenMP     | multicore        |
| GPU        | massive speedup  |

---

# 15. Architettura che ti consiglio

## Fase 1 — prototipo

* NetworkX
* datasketch

## Fase 2 — medio-grande

* scipy sparse CSR
* numpy arrays
* numba

## Fase 3 — enorme

* graph-tool
* RAPIDS cuGraph
* custom C++

---

# 16. ATTENZIONE

MinHash aiuta tantissimo SOLO se:

* il grafo è enorme
* la densità è bassa

Su grafi piccoli:

* overhead inutile.

---

# 17. Se vuoi il passo successivo

Posso scriverti:

* SCAN + MinHash completo
* versione con LSH banding ottimale
* versione CSR + Numba
* benchmark su SNAP datasets
* implementazione streaming
* implementazione GPU
* pruning teorico corretto per SCAN
* versione approssimata quasi lineare.

---

domanda : e se volessi estendere questi concetti al clustering temporale?

Questa è una direzione molto interessante, perché SCAN si presta bene ai **grafi dinamici/temporali**.
Il problema vero è: nel tempo non cambiano solo i nodi, ma soprattutto la **struttura locale dei vicinati**, cioè proprio ciò che SCAN usa.

Quindi il passaggio naturale è:

```text id="q2z4al"
SCAN statico
→
Dynamic SCAN
→
Temporal community detection
```

Ci sono almeno 4 modi seri per estendere SCAN ai grafi temporali.

---

# 1. Snapshot-based SCAN (il più semplice)

Il metodo più immediato.

Hai:

```text id="gnf8i8"
G(t1), G(t2), G(t3), ...
```

Esegui SCAN indipendentemente su ogni snapshot temporale.

---

## Pipeline

```text id="fbbz7x"
snapshot t
   ↓
SCAN
   ↓
clusters(t)
```

Poi colleghi le community nel tempo.

---

# Tracking delle community

Per collegare cluster tra tempi successivi:

usa overlap:

$$\text{overlap}(C_i^t,C_j^{t+1})=\frac{|C_i^t\cap C_j^{t+1}|}{|C_i^t\cup C_j^{t+1}|}$$

Se overlap alto:

* stessa community evoluta.

---

# Vantaggi

Molto semplice.

---

# Svantaggi

* instabile
* rumore temporale
* cluster “saltano”
* costoso se molti snapshot

---

# 2. Incremental SCAN (molto migliore)

Qui aggiorni i cluster SENZA ricalcolare tutto.

Osservazione cruciale:

Quando aggiungi/rimuovi un edge:

* cambia solo il vicinato locale
* quindi cambiano solo similarità locali.

---

# Idea

Quando arriva un nuovo edge:

```text id="dtxx5p"
(u,v)
```

ricalcoli SOLO:

* similarity(u,*)
* similarity(v,*)
* core-status locale

---

# Complessità

Molto più bassa.

Tipicamente:

```text id="r1qqko"
O(local neighborhood)
```

invece di:

* O(E)

---

# Strutture dati necessarie

Devi mantenere:

```python id="fjlwmr"
adj
similarity cache
core flags
cluster labels
```

---

# Aggiornamento locale

Quando cambia un edge:

1. aggiorni adjacency
2. aggiorni similarità locali
3. controlli:

   * nuovi core
   * core persi
4. propaghi modifiche BFS locali

---

# Questo è il vero “dynamic SCAN”

Molto usato nei:

* social network
* fraud detection
* communication graphs

---

# 3. Time-decayed SCAN

Molto elegante.

Invece di usare edge binari:

```text id="6fjlwm"
0/1
```

usi pesi temporali.

---

# Edge decay

Un edge recente pesa di più:

genui{"math_block_widget_always_prefetch_v2":{"content":"w(t)=e^{-\lambda \Delta t}"}}

dove:

* $\Delta t$= età edge
* $\lambda$= decay

---

# Similarità temporale

Diventa:

$$\sigma_t(u,v)=\frac{\sum_{x\in\Gamma(u)\cap\Gamma(v)} w_x}{\sqrt{\sum_{x\in\Gamma(u)} w_x\sum_{x\in\Gamma(v)} w_x}}$$

---

# Effetto

Le community:

* si adattano nel tempo
* dimenticano vecchie connessioni
* seguono trend recenti

Molto potente.

---

# 4. Temporal motif SCAN (ricerca avanzata)

Qui entri nella frontiera della ricerca.

Non consideri solo edge:

* ma pattern temporali.

Esempio:

```text id="6v3ajh"
A → B entro 5 sec
B → C entro 5 sec
```

Questa diventa una “struttura temporale”.

---

# Similarità sui motif

Confronti:

* sequenze temporali locali
* non solo vicinati statici.

Questo è molto usato in:

* cybersecurity
* financial fraud
* communication networks

---

# 5. MinHash temporale

Qui si collega alla tua domanda precedente.

Puoi fare:

```text id="n8svw0"
neighbor set
+
timestamp
```

---

# Temporal MinHash

Per esempio:

```python id="8dww14"
(node, time_bucket)
```

oppure:

```python id="mgg1yv"
neighbor_id | recency_weight
```

---

# Weighted MinHash

Esistono versioni:

* weighted MinHash
* consistent weighted sampling

che funzionano molto bene.

---

# 6. Architettura moderna consigliata

Per un vero clustering temporale scalabile:

```text id="1s5ml3"
stream edges
    ↓
incremental adjacency
    ↓
LSH / MinHash
    ↓
local structural similarity updates
    ↓
dynamic cluster maintenance
```

---

# 7. La vera difficoltà teorica

In SCAN statico:

* cluster = componenti dense.

Nel tempo:

* le community:

  * nascono
  * muoiono
  * si fondono
  * si dividono.

Quindi devi modellare:

* persistence
* smoothness temporale.

---

# 8. Stabilizzazione temporale

Molto importante.

Aggiungi un termine di inerzia:

$$L = \alpha\cdot \text{structural similarity} + (1-\alpha)\cdot \text{historical consistency}$$

---

# 9. Versione pratica che ti consiglio

## Se vuoi fare ricerca seria:

### STEP 1

Implementa:

* SCAN statico ottimizzato

### STEP 2

Aggiungi:

* incremental updates

### STEP 3

Aggiungi:

* temporal decay

### STEP 4

Aggiungi:

* MinHash pruning

---

# 10. Versione “industriale”

Se il grafo è enorme:

| Tecnica           | Necessaria |
| ----------------- | ---------- |
| CSR sparse        | sì         |
| dynamic adjacency | sì         |
| similarity cache  | sì         |
| MinHash/LSH       | sì         |
| parallel BFS      | sì         |
| streaming updates | sì         |

---

# 11. Direzione molto promettente

Una cosa molto potente è:

## SCAN + GNN

Usare:

* structural similarity
* embedding neurali

insieme.

---

# 12. Formula moderna

Puoi definire:

$$S(u,v)=\alpha\cdot \sigma(u,v)+(1-\alpha)\cdot \cos(h_u,h_v)$$

dove:

* $(h_u)$= embedding GNN
* $(\sigma)$= structural similarity SCAN.

Questo funziona molto bene.

---

# 13. Se vuoi davvero svilupparlo

Posso aiutarti a costruire:

* Dynamic SCAN completo
* Temporal SCAN con decay
* SCAN + MinHash + LSH
* streaming implementation
* CSR + Numba
* benchmark SNAP
* framework per evoluzione comunità
* visualizzazione temporale
* paper architecture stile research-grade.
