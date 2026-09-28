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
        
        self.S_eps_cache = {}
        self.sigma_cache = {}

    def create_detemporal_graph_from_temporal_graph(self):
        # USIAMO UNA LISTA E NON UN SET PER PRESERVARE L'ORDINE ORIGINALE DI INSERIMENTO
        # È fondamentale per aggiornare i limiti 'l' e 'u' nello stesso ordine di tpSCAN.py
        detemporal_graph = {}
        for u in self.graph.nodes():
            detemporal_graph[u] = []
            
        if isinstance(self.graph, nx.MultiGraph) or isinstance(self.graph, nx.MultiDiGraph):
            edges = self.graph.edges(keys=True, data=True)
        else:
            edges = ((u, v, None, data) for u, v, data in self.graph.edges(data=True))
            
        for u, v, key, data in edges:
            if u != v:
                if v not in detemporal_graph[u]:
                    detemporal_graph[u].append(v)
                if u not in detemporal_graph[v]:
                    detemporal_graph[v].append(u)
                    
        return detemporal_graph

    def temporal_neighbors_at_time(self, u, t):
        neighbors_at_time = set()
        if isinstance(self.graph, nx.MultiGraph) or isinstance(self.graph, nx.MultiDiGraph):
            edges = self.graph.edges(u, keys=True, data=True)
        else:
            edges = ((u, v, None, data) for v, data in self.graph[u].items())

        for _, v, key, data in edges:
            if data.get('time') == t:
                neighbors_at_time.add(v)
        return neighbors_at_time

    def get_temporal_edges_list(self, u, v):
        """ Restituisce la lista di tutti i timestamp in cui esiste un arco tra u e v """
        if isinstance(self.graph, nx.MultiGraph) or isinstance(self.graph, nx.MultiDiGraph):
            edges = self.graph.get_edge_data(u, v)
            if not edges: return []
            return [data['time'] for key, data in edges.items()]
        else:
            data = self.graph.get_edge_data(u, v)
            return [data['time']] if data else []

    def compute_sigma_at_one_time(self, u, v, t):
        """ Replicazione 1:1 di compute_sigma_at_one_time in tpSCAN.py """
        Nu_t = self.temporal_neighbors_at_time(u, t)
        Nv_t = self.temporal_neighbors_at_time(v, t)

        lenuadj = len(Nu_t) + 1
        lenvadj = len(Nv_t) + 1
        
        # Pruning di tpSCAN
        if lenuadj < self.eps * self.eps * lenvadj or lenvadj < self.eps * self.eps * lenuadj:
            return 0.0

        intersection = Nu_t.intersection(Nv_t)
        len_v_u = len(intersection) + 2
        
        # In tpSCAN.py restituiscono un valore fittizio (eps + 0.1) se passa la soglia
        if len_v_u < self.eps * sqrt(lenuadj * lenvadj):
            return 0.0
        else:
            return self.eps + 0.1

    def compute_sigma_tpscan(self, u, v):
        """ Replicazione 1:1 di compute_sigma in tpSCAN.py """
        tau_val = 0
        total_t_edges = self.get_temporal_edges_list(u, v)
        
        if len(total_t_edges) < self.tau:
            return 0
            
        for t in total_t_edges:
            edge_set = tuple(sorted([u, v]) + [t])
            
            if edge_set not in self.sigma_cache:
                result = self.compute_sigma_at_one_time(u, v, t)
                self.sigma_cache[edge_set] = result
                if result > self.eps:
                    tau_val += 1
            else:
                if self.sigma_cache[edge_set] > self.eps:
                    tau_val += 1
                    
            if tau_val >= self.tau:
                return tau_val
                
        return 0

    def get_cached_S_eps(self, u, v):
        edge = tuple(sorted([u, v]))
        if edge not in self.S_eps_cache:
            self.S_eps_cache[edge] = self.compute_sigma_tpscan(u, v)
        return self.S_eps_cache[edge]
    
    def eps_stable_similarity(self, u, v, T, eps):
        return self.compute_sigma_tpscan(u, v)
    
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
            self.sigma_cache[key] = self.compute_sigma_at_one_time(u, v, t)
        return self.sigma_cache[key]

    def find_weak_core(self):
        """ Replicazione esatta del metodo SCANW di tpSCAN.py """
        weak_cores = set()
        
        # Inizializza l (cd) e u (cd_bar)
        cd = {u: 0 for u in self.graph.nodes()}
        cd_bar = {u: len(self.detemporal_graph[u]) for u in self.graph.nodes()}
        
        # RICREAZIONE ESATTA DI `self.rank`: ordinamento stabile basato sul grado spaziale 
        ranktemp = {u: cd_bar[u] for u in self.graph.nodes()}
        ranked_nodes = [item[0] for item in sorted(ranktemp.items(), key=lambda item: item[1], reverse=True)]
        
        for u in ranked_nodes:
            if cd[u] >= self.mu:
                weak_cores.add(u)
                
            if cd[u] < self.mu and cd_bar[u] >= self.mu: 
                # Ora iteriamo sulla lista ordinata per inserimento, come in tpSCAN
                for v in self.detemporal_graph[u]:
                    edge = tuple(sorted([u, v]))
                    
                    if edge not in self.S_eps_cache:
                        sigma_val = self.compute_sigma_tpscan(u, v)
                        self.S_eps_cache[edge] = sigma_val
                        
                        if sigma_val >= self.tau:
                            cd[u] += 1
                            cd[v] += 1 
                        else:
                            cd_bar[u] -= 1
                            cd_bar[v] -= 1 
                    
                    if cd[u] >= self.mu:
                        weak_cores.add(u)
                        
                    if cd[u] >= self.mu or cd_bar[u] < self.mu:
                        break
                        
        return weak_cores

    def find_strong_core(self, WC):
        """ Replicazione esatta del metodo SCANS di tpSCAN.py """
        strong_cores = set()
        
        # tpSCAN estrae i WC prima e li itera
        for u in WC:
            candidate_set = []
            for v in self.detemporal_graph[u]:
                if len(self.get_temporal_edges_list(u, v)) >= self.tau:
                    candidate_set.append(v)
                    
            if len(candidate_set) < self.mu:
                continue
                
            candidate_time = {}
            for v in candidate_set:
                for time_item in self.get_temporal_edges_list(u, v):
                    candidate_time[time_item] = candidate_time.get(time_item, 0) + 1
                    
            times_more_than_miu = [t for t, count in candidate_time.items() if count >= self.mu]
            
            if len(times_more_than_miu) < self.tau:
                continue
                
            tau_calculate = 0
            for t in times_more_than_miu:
                miu_calculate = 0
                max_miu_calculate = candidate_time[t]
                
                for v in candidate_set:
                    if tau_calculate >= self.tau:
                        break
                        
                    edge_set_t = tuple(sorted([u, v]) + [t])
                    if edge_set_t not in self.sigma_cache:
                        self.sigma_cache[edge_set_t] = self.compute_sigma_at_one_time(u, v, t)
                        
                    # Riproduciamo fedelmente il comportamento del codice originale, compreso il mancato pruning
                    if self.sigma_cache[edge_set_t] >= self.eps:
                        miu_calculate += 1
                        
                    if miu_calculate >= self.mu:
                        tau_calculate += 1
                        break
                        
                    if max_miu_calculate < self.mu:
                        break
                        
            if tau_calculate >= self.tau:
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
        start = datetime.datetime.now()
        stable_cores = set()
        
        for u in strong_cores:
            bitmaps = {}
            valid_neighbors = []
            Nte = self.get_Nte_neighbors(u)    
            print(f"Nte set for node {u} -> {Nte}")
            for v in Nte:
                bitmap = 0
                for t in range(1, self.T + 1):
                    # Essendo un booleano (eps+0.1), >= eps è sufficiente per attivare il bit
                    if self.get_cached_sigma(u, v, t) >= self.eps:
                        bitmap |= (1 << (t - 1))
                        
                if bin(bitmap).count('1') >= self.tau:
                    bitmaps[v] = bitmap
                    valid_neighbors.append(v)
            
            if len(valid_neighbors) < self.mu:
                continue

            def dfs_mafia(current_size, current_bitmap, candidates):
                if current_size >= self.mu:
                    return True
                if bin(current_bitmap).count('1') < self.tau:
                    return False
                if current_size + len(candidates) < self.mu:
                    return False
                for i, v in enumerate(candidates):
                    new_bitmap = current_bitmap & bitmaps[v]
                    if bin(new_bitmap).count('1') >= self.tau:
                        if dfs_mafia(current_size + 1, new_bitmap, candidates[i+1:]):
                            return True
                return False
                
            initial_bitmap = (1 << self.T) - 1
            if dfs_mafia(0, initial_bitmap, valid_neighbors):
                stable_cores.add(u)
        
        end = datetime.datetime.now()
        interval = (end-start).total_seconds()
        print(f"Running time of StableCore (MAFIA): {interval}")
        return stable_cores

    def find_connected_components(self, core_graph):
        visited = set()
        components = []

        for node in core_graph:
            if node not in visited:
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
        core_graph = {u: set() for u in stable_cores}
        visited_pairs = set()

        for u in stable_cores:
            for v in set(self.detemporal_graph[u]).intersection(stable_cores):
                edge = tuple(sorted([u, v]))
                if edge not in visited_pairs:
                    visited_pairs.add(edge)
                    
                    s_eps = self.get_cached_S_eps(u, v)
                    if s_eps >= self.tau:
                        core_graph[u].add(v)
                        core_graph[v].add(u)

        core_components = self.find_connected_components(core_graph)

        final_clusters = []
        for core_component in core_components:
            cluster = set(core_component) 
            
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

    numarchi = 0

    with open("graph.txt","w") as f:
        for u, v, key, data in edges:
            t = data.get('time')
            print(f"Edge: {u} - {v}, Time: {t}")
            numarchi += 1
            f.write(f"Edge: {u} - {v}, Time: {t}\n")
        f.write(f"#Archi: {numarchi}")
    

def print_detemporal_graph(detemporal_graph):
    for node, neighbors in detemporal_graph.items():
        print(f"Node: {node}, Neighbors: {neighbors}")

def create_graph_from_ds(dataset):
    G = nx.MultiGraph()
    for line in dataset:
        parts = line.strip().split()
        if len(parts) != 3:
            continue
        u, v, t = parts
        G.add_edge(u, v, time=int(t))
    return G

def read_dataset_from_file(file_path):
    dataset = []
    with open(file_path, 'r') as f:
        for line in f:
            dataset.append(line.strip())
    name = file_path.split('/')[-1].split('.')[0]
    return dataset, name

def test_TSCANF():
    ds = 1
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
    for idx2, cluster2 in enumerate(clusters2):
        print(f"  Cluster {idx2 + 1}: {cluster2}")

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

if __name__ == "__main__":
    start_main = datetime.datetime.now()
    test_TSCANF()
    end_main = datetime.datetime.now()
    interval = (end_main-start_main).total_seconds()
    print(f"Running time of StableCore (MAFIA): {interval}")