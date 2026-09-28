from math import sqrt
import networkx as nx
import datetime

import optparse

from temporal_metrics import summarize_temporal_metrics

parser = optparse.OptionParser()
parser.add_option("-e", "--eps",action="store", dest="eps",default=0.5, help="Epsilon threshold for structural similarity (default: 0.5)",type="float")
parser.add_option("-m", "--mu", action="store", dest="mu",default=2, help="Minimum number of epsilon neighbors to be a core vertex (default: 2)",type="int")
parser.add_option("-t", "--tau", action="store", dest="tau",default=2, help="Size of the intervals [t,t+tau] (default: 2)",type="int")
parser.add_option("-a","--algorithm", action="store", dest="algorithm",default="MAFIA", help="Algorithm to use for stable core extraction: MAFIA or Nte (default: MAFIA)",type="string")
options, args = parser.parse_args()

class TSCANF:
    def __init__(self, eps, mu, tau, graph, T):
        self.eps = eps
        self.mu = mu
        self.tau = tau
        self.graph = graph
        self.T = T
        self.detemporal_graph = self.create_detemporal_graph_from_temporal_graph()
        
        # Inizializza le cache globali richieste dal paper per evitare ricalcoli
        self.S_eps_cache = {}
        self.sigma_cache = {}

    def create_detemporal_graph_from_temporal_graph(self):
        # Crea un dizionario di set per il de-temporal graph
        detemporal_graph = {}
        for u in self.graph.nodes():
            detemporal_graph[u] = set()
            
        if isinstance(self.graph, nx.MultiGraph) or isinstance(self.graph, nx.MultiDiGraph):
            edges = self.graph.edges(keys=True, data=True)
        else:
            edges = ((u, v, None, data) for u, v, data in self.graph.edges(data=True))
            
        for u, v, key, data in edges:
            if u != v: # Ignora self-loops se presenti
                detemporal_graph[u].add(v)
                detemporal_graph[v].add(u)
                
        return detemporal_graph

    def temporal_neighbors_at_time(self, u, t):
        # Restituisce i vicini di u ESATTAMENTE allo snapshot t
        neighbors_at_time = set()
        if isinstance(self.graph, nx.MultiGraph) or isinstance(self.graph, nx.MultiDiGraph):
            edges = self.graph.edges(u, keys=True, data=True)
        else:
            edges = ((u, v, None, data) for v, data in self.graph[u].items())

        for _, v, key, data in edges:
            if data.get('time') == t:
                neighbors_at_time.add(v)
        return neighbors_at_time

    def temporal_structural_similarity(self, u, v, t):
        Nu_t = self.temporal_neighbors_at_time(u, t)
        
        # Dal paper: se non c'è l'arco (u,v) al tempo t, la similarità è 0
        if v not in Nu_t:
            return 0.0
            
        Nv_t = self.temporal_neighbors_at_time(v, t)

        # Closed neighborhood (aggiunge se stessi al set per calcolare la formula SCAN corretta)
        Nu_t_closed = Nu_t.union({u})
        Nv_t_closed = Nv_t.union({v})

        intersection = Nu_t_closed.intersection(Nv_t_closed)
        return len(intersection) / sqrt(len(Nu_t_closed) * len(Nv_t_closed))
    
    def eps_stable_similarity(self, u, v, T, eps):
        # Conta IN QUANTI SNAPSHOT la similarità è >= eps (Definizione 2 del paper)
        count_snapshots = 0
        for t in range(1, T + 1):
            sim = self.temporal_structural_similarity(u, v, t)
            if sim >= eps:
                count_snapshots += 1
        return count_snapshots

    def get_cached_S_eps(self, u, v):
        edge = tuple(sorted([u, v]))
        if edge not in self.S_eps_cache:
            self.S_eps_cache[edge] = self.eps_stable_similarity(u, v, self.T, self.eps)
        return self.S_eps_cache[edge]
    
    def get_Nte_neighbors(self,u):
        Nte = set()
        for v in self.detemporal_graph[u]:
            count_snap = self.eps_stable_similarity(u,v,self.T,self.eps)
            if count_snap >= self.tau:
                Nte.add(v)
        return Nte
    
    def get_cached_sigma(self, u, v, t):
        key = tuple(sorted([u, v]) + [t])
        if key not in self.sigma_cache:
            self.sigma_cache[key] = self.temporal_structural_similarity(u, v, t)
        return self.sigma_cache[key]

    def find_weak_core(self):
        """ Implementazione esatta dell'Algorithm 2: WeakCore """
        weak_cores = set()
        
        # cd: lower bound (calcolati validi), cd_bar: upper bound (massimo teorico) 
        cd = {u: 0 for u in self.detemporal_graph}
        cd_bar = {u: len(self.detemporal_graph[u]) for u in self.detemporal_graph} 
        
        for u in self.detemporal_graph:
            if cd[u] < self.mu and cd_bar[u] >= self.mu: 
                for v in self.detemporal_graph[u]:
                    # Controlliamo se l'arco è già stato esplorato da un altro nodo
                    edge = tuple(sorted([u, v]))
                    if edge not in self.S_eps_cache:
                        # Calcola e salva in cache 
                        s_eps = self.get_cached_S_eps(u, v)
                        
                        if s_eps >= self.tau:
                            cd[u] += 1
                            cd[v] += 1 # Aggiorna anche il vicino 
                        else:
                            cd_bar[u] -= 1
                            cd_bar[v] -= 1 # Aggiorna anche il vicino 
                    
                    # Early termination 
                    if cd[u] >= self.mu or cd_bar[u] < self.mu:
                        break
                        
            if cd[u] >= self.mu: 
                weak_cores.add(u)
                
        return weak_cores

    def find_strong_core(self, WC):
        """ Implementazione esatta dell'Algorithm 3: StrongCore """
        strong_cores = set()
        
        cs = {}
        cs_bar = {}
        
        # Inizializzazione bounds degli snapshot per i nodi in WeakCore 
        for u in WC:
            cs[u] = 0
            cs_bar[u] = 0
            for i in range(1, self.T + 1):
                N_i_u = self.temporal_neighbors_at_time(u, i)
                if len(N_i_u) >= self.mu: 
                    cs_bar[u] += 1
        
        for u in WC:
            if cs[u] < self.tau and cs_bar[u] >= self.tau: 
                for i in range(1, self.T + 1):
                    N_i_u = self.temporal_neighbors_at_time(u, i)
                    
                    sd_i_u = 0
                    sd_bar_i_u = len(N_i_u)
                    
                    if sd_bar_i_u >= self.mu: 
                        for v in N_i_u:
                            # Usa la cache per le similarità dei singoli snapshot 
                            sigma = self.get_cached_sigma(u, v, i)
                            
                            if sigma >= self.eps: 
                                sd_i_u += 1
                            else:
                                sd_bar_i_u -= 1
                                
                            # Early termination per il singolo snapshot 
                            if sd_i_u >= self.mu or sd_bar_i_u < self.mu:
                                break
                        
                        if sd_i_u >= self.mu:
                            cs[u] += 1
                        if sd_bar_i_u < self.mu:
                            cs_bar[u] -= 1
                            
                    # Early termination globale per il nodo u 
                    if cs[u] >= self.tau or cs_bar[u] < self.tau:
                        break
                        
            if cs[u] >= self.tau: 
                strong_cores.add(u)
                
        return strong_cores
    

    def find_stable_using_nte(self,strong_cores):
        
        start = datetime.datetime.now()
        stable_cores = set()

        for node in strong_cores:
            Nte_u = self.get_Nte_neighbors(node)
            if len(Nte_u) >= self.mu:
                stable_cores.add(node)
        
        end = datetime.datetime.now()
        interval = (end-start).total_seconds()
        print(f"Running time of StableCore (Nte): {interval}")
        return stable_cores


    
    def find_stable_core_mafia(self, strong_cores):
        """ Estrae i (mu, tau, eps)-stable cores usando l'approccio MAFIA (Bitmap) """
        start = datetime.datetime.now()
        stable_cores = set()
        
        for u in strong_cores:
            # Step 1: Creazione dei vettori verticali (Bitmap)
            bitmaps = {}
            valid_neighbors = []
            Nte = self.get_Nte_neighbors(u)    
            print(f"Nte set for node {u} -> {Nte}")
            #for v in self.detemporal_graph[u]:
            for v in Nte:
                bitmap = 0
                for t in range(1, self.T + 1):
                    # Se la similarità supera eps, "accendiamo" il t-esimo bit
                    if self.get_cached_sigma(u, v, t) >= self.eps:
                        bitmap |= (1 << (t - 1))
                        
                # Pruning preventivo: l'item deve avere un supporto >= tau da solo
                # Usiamo bin(x).count('1') per contare i bit accesi (supporto)
                if bin(bitmap).count('1') >= self.tau:
                    bitmaps[v] = bitmap
                    valid_neighbors.append(v)
            
            # Se i vicini validi rimasti sono meno di mu, è impossibile formare il pattern
            if len(valid_neighbors) < self.mu:
                continue

            # Step 2: Ricerca in profondità (DFS) tipica di MAFIA
            def dfs_mafia(current_size, current_bitmap, candidates):
                # Caso Base: Abbiamo trovato una "stella" di dimensione >= mu
                if current_size >= self.mu:
                    return True
                
                if bin(current_bitmap).count('1') < self.tau:
                    return False

                # Pruning Strutturale: Non ci sono abbastanza candidati rimasti per arrivare a mu
                if current_size + len(candidates) < self.mu:
                    return False
                
                for i, v in enumerate(candidates):
                    # Il cuore di MAFIA: Intersezione fulminea tramite AND logico
                    new_bitmap = current_bitmap & bitmaps[v]
                    
                    # Pruning di Frequenza: Se il supporto scende sotto tau, il ramo muore qui
                    if bin(new_bitmap).count('1') >= self.tau:
                        # Ricorsione sul prossimo livello
                        if dfs_mafia(current_size + 1, new_bitmap, candidates[i+1:]):
                            return True
                return False
                
            # Iniziamo la DFS con un "Super-Bitmap" con tutti i T bit accesi (111...1)
            initial_bitmap = (1 << self.T) - 1
            
            # Se la DFS restituisce True, il nodo u ha un pattern massimale valido
            if dfs_mafia(0, initial_bitmap, valid_neighbors):
                stable_cores.add(u)
        
        end = datetime.datetime.now()
        interval = (end-start).total_seconds()
        print(f"Running time of StableCore (MAFIA): {interval}")
        return stable_cores

    def find_connected_components(self, core_graph):
        """
        Trova le componenti connesse in un grafo non orientato (usato per raggruppare i core).
        core_graph è un dizionario: nodo -> set(vicini_core)
        """
        visited = set()
        components = []

        for node in core_graph:
            if node not in visited:
                # Esegue una BFS per trovare l'intera componente connessa
                component = set()
                queue = [node]
                visited.add(node)

                while queue:
                    current = queue.pop(0)
                    component.add(current)
                    for neighbor in core_graph.get(current, set()):
                        if neighbor not in visited:
                            visited.add(neighbor)
                            queue.append(neighbor)
                
                components.append(component)
        
        return components

    def build_stable_clusters(self, stable_cores):
        """
        Implementazione dell'Algorithm 5 (TSCAN-A) del paper.
        Costruisce i cluster finali unendo gli stable core e aggiungendo i nodi raggiungibili.
        """
        # Step 1: Crea un grafo G_c solo per i core
        core_graph = {u: set() for u in stable_cores}
        visited_pairs = set()

        for u in stable_cores:
            # Guarda solo i vicini che sono anch'essi Stable Core
            for v in self.detemporal_graph[u].intersection(stable_cores):
                # Evita di controllare l'arco due volte (u,v) e (v,u)
                edge = tuple(sorted([u, v]))
                if edge not in visited_pairs:
                    visited_pairs.add(edge)
                    
                    # Controlla se sono (tau, eps)-connessi
                    s_eps = self.get_cached_S_eps(u, v)
                    if s_eps >= self.tau:
                        core_graph[u].add(v)
                        core_graph[v].add(u)

        # Step 2: Trova le componenti connesse nel grafo dei core
        core_components = self.find_connected_components(core_graph)

        # Step 3: Cluster dei nodi non-core
        final_clusters = []
        for core_component in core_components:
            cluster = set(core_component) # Inizia con i core
            
            # Aggiungi tutti i nodi raggiungibili (tau, eps)-connessi
            # Definition 5: Un nodo appartiene al cluster se è raggiungibile da un core del cluster
            reachable_nodes = set()
            for core_node in core_component:
                for neighbor in self.detemporal_graph[core_node]:
                    if neighbor not in cluster:
                        s_eps = self.get_cached_S_eps(core_node, neighbor)
                        if s_eps >= self.tau:
                            reachable_nodes.add(neighbor)
            
            cluster.update(reachable_nodes)
            final_clusters.append(cluster)

        return final_clusters

    def run(self):
        
        print("---"*10)
        print("Inizio estrazione Weak Cores...")
        print("---"*10)
        weak_cores = self.find_weak_core()
        print(f"> Trovati {len(weak_cores)} Weak Cores.")
        print("---"*10)
        print("Inizio estrazione Strong Cores...")
        strong_cores = self.find_strong_core(weak_cores)
        print("---"*10)
        print(f"> Trovati {len(strong_cores)} Strong Cores.")
        print("---"*10)
        print("Inizio estrazione Stable Cores (MAFIA)...")
        stable_cores = self.find_stable_core_mafia(strong_cores)
        print("---"*10)
        print(f"> Trovati {len(stable_cores)} Stable Cores.")
        print("---"*10)
        print("Inizio estrazione Stable Cores (Nte)...")
        stable_cores2 = self.find_stable_using_nte(strong_cores)
        print("---"*10)
        print(f"> Trovati {len(stable_cores2)} Stable Cores.")
        print("---"*10)
        print("Costruzione degli Stable Clusters (TSCAN-A)...")
        print("---"*10)
        clusters = self.build_stable_clusters(stable_cores)
        print(f"> Trovati {len(clusters)} Stable Clusters.")
        print("---"*10)
        print("Costruzione degli Stable Clusters (TSCAN-A - Nte)...")
        print("---"*10)
        clusters2 = self.build_stable_clusters(stable_cores2)
        print(f"> Trovati {len(clusters2)} Stable Clusters.")
        
        return weak_cores, strong_cores, stable_cores, stable_cores2, clusters, clusters2
    
def print_temporal_graph(graph):
    if isinstance(graph, nx.MultiGraph) or isinstance(graph, nx.MultiDiGraph):
        edges = graph.edges(keys=True, data=True)
    else:
        edges = ((u, v, None, data) for u, v, data in graph.edges(data=True))

    for u, v, key, data in edges:
        t = data.get('time')
        print(f"Edge: {u} - {v}, Time: {t}")

def print_detemporal_graph(detemporal_graph):
    for node, neighbors in detemporal_graph.items():
        print(f"Node: {node}, Neighbors: {neighbors}")

def create_graph_from_ds(dataset):
    """
    Prende il dataset nel formato i j   timestamp e lo trasforma nel grafo MultiGraph di NetworkX
    """
    G = nx.MultiGraph()
    for line in dataset:
        parts = line.strip().split()
        if len(parts) != 3:
            continue  # Salta righe malformate
        u, v, t = parts
        G.add_edge(u, v, time=int(t))
    return G

def build_example_graph():
    G = nx.MultiGraph()
    G.add_edge('A', 'B', time=2)
    G.add_edge('A', 'B', time=3)
    G.add_edge('A', 'B', time=4)

    G.add_edge('A', 'C', time=1)
    G.add_edge('A', 'C', time=2)
    G.add_edge('A', 'C', time=3)
    G.add_edge('A', 'C', time=4)
    
    G.add_edge('A', 'D', time=4)
    G.add_edge('A', 'D', time=5)
    G.add_edge('A', 'D', time=6)
    
    G.add_edge('B', 'C', time=1)
    G.add_edge('B', 'C', time=2)
    G.add_edge('B', 'C', time=3)
    G.add_edge('B', 'C', time=7)
    
    G.add_edge('B', 'D', time=2)
    G.add_edge('B', 'D', time=3)
    G.add_edge('B', 'D', time=4)
    
    G.add_edge('C', 'D', time=1)
    G.add_edge('C', 'D', time=2)
    G.add_edge('C', 'D', time=3)
    G.add_edge('C', 'D', time=7)

    G.add_edge('C', 'E', time=1)
    G.add_edge('C', 'E', time=2)
    G.add_edge('C', 'E', time=7)

    G.add_edge('D', 'E', time=7)

    G.add_edge('E', 'F', time=2)
    G.add_edge('E', 'F', time=3)
    G.add_edge('E', 'F', time=4)
    G.add_edge('E', 'F', time=5)

    G.add_edge('E', 'H', time=2)
    G.add_edge('E', 'H', time=4)
    G.add_edge('E', 'H', time=6)
    G.add_edge('E', 'H', time=7)

    G.add_edge('F', 'H', time=2)
    G.add_edge('F', 'H', time=3)
    G.add_edge('F', 'H', time=5)
    G.add_edge('F', 'H', time=6)
    G.add_edge('F', 'H', time=7)

    G.add_edge('F', 'G', time=2)
    G.add_edge('F', 'G', time=3)
    G.add_edge('F', 'G', time=5)

    G.add_edge('G', 'H', time=2)
    G.add_edge('G', 'H', time=5)
    G.add_edge('G', 'H', time=6)
    G.add_edge('G', 'H', time=7)

    return G

def read_dataset_from_file(file_path):
    dataset = []
    with open(file_path, 'r') as f:
        for line in f:
            dataset.append(line.strip())
    name = file_path.split('/')[-1].split('.')[0]  # Estrae il nome del dataset dal percorso del file
    return dataset, name

def test_TSCANF():
    ds = 1
    #G = build_example_graph()
    # Dataset di esempio
    
    dataset,ds_name = read_dataset_from_file("CollegeMsg.txt")
    G = create_graph_from_ds(dataset)
    print("Temporal Graph:")
    print_temporal_graph(G)
    print("\nDetemporal Graph:")

    eps = options.eps
    mu = options.mu
    tau = options.tau
    tscan = TSCANF(eps, mu, tau, G, 7)

    detemporal_graph = tscan.create_detemporal_graph_from_temporal_graph()
    print_detemporal_graph(detemporal_graph)

    weak_core, strong_core, stable_core, stable_cores2, clusters, clusters2 = tscan.run()
    print(f"\nFinal Result: eps={eps}, mu={mu}, tau={tau}")
    print("Weak Core:", weak_core)
    print("Strong Core:", strong_core)
    print("Stable Core:", stable_core)
    print("Stable Core (Nte):", stable_cores2)
    print("Clusters:")
    for idx, cluster in enumerate(clusters):
        print(f"  Cluster {idx + 1}: {cluster}")
    print("---"*10)
    print("Clusters (Nte):")
    for idx, cluster in enumerate(clusters2):
        print(f"  Cluster {idx + 1}: {cluster}")

    print("---"*10)
    algo = options.algorithm
    print(f"Metriche di valutazione (per versione {algo}):")
    if algo == "MAFIA":
        metrics = summarize_temporal_metrics(G, clusters)
    elif algo == "Nte":
        metrics = summarize_temporal_metrics(G, clusters2)
    for name, value in metrics.items():
        print(f"  {name}: {value}")

    if ds:
        file_metrics = f"metrics_{algo}_eps{eps}_mu{mu}_tau{tau}_ds{ds_name}.txt"
    else:
        file_metrics = f"metrics_{algo}_eps{eps}_mu{mu}_tau{tau}.txt"

    with open("metrics/"+file_metrics, "w") as f:
        f.write(f"eps: {eps}\n")
        f.write(f"mu: {mu}\n")
        f.write(f"tau: {tau}\n")
        f.write(f"Weak Core: {weak_core}\n")
        f.write(f"Strong Core: {strong_core}\n")
        if algo == "MAFIA":
            f.write(f"Stable Core: {stable_core}\n")
            f.write("Clusters:\n")
            for idx, cluster in enumerate(clusters):
                f.write(f"  Cluster {idx + 1}: {cluster}\n")
            f.write("---"*10 + "\n")
        elif algo == "Nte":
            f.write(f"Stable Core (Nte): {stable_cores2}\n")
            f.write("Clusters (Nte):\n")
            for idx, cluster in enumerate(clusters2):
                f.write(f"  Cluster {idx + 1}: {cluster}\n")
        f.write("---"*10 + "\n")
        f.write(f"Metriche di valutazione (per versione {algo}):\n")
        for name, value in metrics.items():
            f.write(f"  {name}: {value}\n")

    # return {
    #     "eps": eps,
    #     "mu": mu,
    #     "tau": tau,
    #     "weak_core": weak_core,
    #     "strong_core": strong_core,
    #     "stable_core": stable_core,
    #     "stable_core_nte": stable_cores2,
    #     "clusters": clusters,
    #     "clusters_nte": clusters2,
    #     "metrics": metrics,
    # }

if __name__ == "__main__":
    start_main = datetime.datetime.now()
    test_TSCANF()
    end_main = datetime.datetime.now()
    interval = (end_main-start_main).total_seconds()
    print(f"Running time of StableCore (MAFIA): {interval}")

