from math import sqrt
from collections import deque
import networkx as nx
import numpy as np
import optparse

parser = optparse.OptionParser()
parser.add_option("-e", "--eps",action="store", dest="eps",default=0.5, help="Epsilon threshold for structural similarity (default: 0.5)",type="float")
parser.add_option("-m", "--mu", action="store", dest="mu",default=2, help="Minimum number of epsilon neighbors to be a core vertex (default: 2)",type="int")

options, args = parser.parse_args()

if not options.eps or not options.mu:
    parser.error("Both eps and mu parameters are required.")
    print("Usage: python SCAN_v2.py -e <eps> -m <mu>")
    exit(1)

UNCLASSIFIED = -1
NONMEMBERS = -2

class SCAN:
    def __init__(self, eps=0.5, mu=2):
        self.eps = eps
        self.mu = mu

        self.labels_ = {}
        self.hubs = []
        self.outliers = []
    
    def structural_similarity(self, x, y, g):
        
        Nx = set(g.neighbors(x))
        Ny = set(g.neighbors(y))

        Nx.add(x)
        Ny.add(y)

        comuni = len(Nx & Ny) #intersezione
        return comuni / sqrt(len(Nx) * len(Ny))
    
    def epsilon_vicini(self, v, g):
        result = []
        for u in g.neighbors(v):
            s = self.structural_similarity(v, u, g)
            if s >= self.eps:
                result.append(u)
        return result
    
    def is_core(self, v, g):
        return len(self.epsilon_vicini(v, g)) >= self.mu
    
    def fit(self, g):
        cluster_id = 0
        labels = {v: UNCLASSIFIED for v in g.nodes()}

        for v in g.nodes():
            if labels[v] != UNCLASSIFIED:
                continue
            if not self.is_core(v, g):
                labels[v] = NONMEMBERS
                continue
            
            labels[v] = cluster_id
            queue = deque([v])

            while queue:
                x = queue.popleft()
                vicinato = self.epsilon_vicini(x, g)
                if len(vicinato) >= self.mu:
                    for y in vicinato:
                        if labels[y] in [UNCLASSIFIED, NONMEMBERS]:
                            if labels[y] == UNCLASSIFIED:
                                queue.append(y)
                            labels[y] = cluster_id
            cluster_id += 1
        
        self.labels_ = labels

        # Classificazione hub e outlier
        self._classify_hubs_outliers(g)
        return self
    
    def _classify_hubs_outliers(self, g):

        hubs = []
        outliers = []

        for v in g.nodes():
            if self.labels_[v] != NONMEMBERS:
                continue
            
            neighbors_cluster = set()
            for u in g.neighbors(v):
                if self.labels_[u] >= 0:
                    neighbors_cluster.add(self.labels_[u])
            
            if len(neighbors_cluster) >= 2:
                hubs.append(v)
            else:
                outliers.append(v)

        self.hubs = hubs
        self.outliers = outliers

def print_clusters_networkx(scan):
    clusters = {}
    labels = scan.labels_
    hubs = scan.hubs
    outliers = scan.outliers
    for vertex, cluster_id in labels.items():
        if cluster_id not in clusters:
            clusters[cluster_id] = []
        clusters[cluster_id].append(vertex)

    for cluster_id, vertices in clusters.items():
        if cluster_id != NONMEMBERS:
            print(f"Cluster {cluster_id}: {vertices}")
    
    for cluster_id, vertices in clusters.items():
        if cluster_id == NONMEMBERS:
            print(f"Non-members: {vertices} con\n-------\tHubs: {hubs}\n-------\tOutliers: {outliers}")



def print_graph_networkx(g):
    for v in g.nodes():
        print(f"{v}: {list(g.neighbors(v))}")

if __name__ == "__main__":
    
    
    # #G = nx.erdos_renyi_graph(20,0.6)
    # G = {
    #     1: {2},
    #     2: {1,3},
    #     3: {2,4,5,6},
    #     4: {3},
    #     5: {3,6},
    #     6: {3,7},
    #     7: {6}
    # }
    # # trasformo il dizionario in un grafo di networkx
    # G = nx.Graph(G)
    
    # print("\nRunning SCAN with eps =", options.eps, "and mu =", options.mu, "for graph G\n")

    # print_graph_networkx(G)

    # scan = SCAN(eps=options.eps, mu=options.mu)
    # scan.fit(G)
    
    # print("Cluster labels:", scan.labels_)
    # print("Core Nodes:", [v for v in G.nodes() if scan.is_core(v, G)])
    # #print("Hubs:", scan.hubs)
    # #print("Outliers:", scan.outliers)

    # print_clusters_networkx(scan)

    # print("-\n"*5)

    # G1 = {
    #     1: {2},
    #     2: {1},
    #     3: {5},
    #     4: {},
    #     5: {3},
    #     6: {7},
    #     7: {6}
    # }
    # # trasformo il dizionario in un grafo di networkx
    # G1 = nx.Graph(G1)
    # print("\nRunning SCAN with eps =", options.eps, "and mu =", options.mu, "for graph G1\n")
    
    # print_graph_networkx(G1)

    # scan = SCAN(eps=options.eps, mu=options.mu)
    # scan.fit(G1)
    
    # print("Cluster labels:", scan.labels_)
    # print("Core Nodes: ", [v for v in G1.nodes() if scan.is_core(v, G1)])
    # #print("Hubs:", scan.hubs)
    # #print("Outliers:", scan.outliers)

    # print_clusters_networkx(scan)

    # print("-\n"*5)

    # G2 = {
    #     1: {2},
    #     2: {1,3},
    #     3: {2,4,6},
    #     4: {3},
    #     5: {},
    #     6: {3},
    #     7: {}
    # }
    # # trasformo il dizionario in un grafo di networkx
    # G2 = nx.Graph(G2)
    # print("\nRunning SCAN with eps =", options.eps, "and mu =", options.mu, "for graph G2\n")
    # print_graph_networkx(G2)

    
    # scan = SCAN(eps=options.eps, mu=options.mu)
    # scan.fit(G2)
    
    # print("Cluster labels:", scan.labels_)
    # print("Core Nodes: ", [v for v in G2.nodes() if scan.is_core(v, G2)])
    # #print("Hubs:", scan.hubs)
    # #print("Outliers:", scan.outliers)

    # print_clusters_networkx(scan)

    # print("-\n"*5)

    # G3 = {
    #     1: {},
    #     2: {},
    #     3: {4},
    #     4: {3},
    #     5: {},
    #     6: {7},
    #     7: {6}
    # }
    # # trasformo il dizionario in un grafo di networkx
    # G3 = nx.Graph(G3)
    # print("\nRunning SCAN with eps =", options.eps, "and mu =", options.mu, "for graph G3\n")
    # print_graph_networkx(G3)

    
    # scan = SCAN(eps=options.eps, mu=options.mu)
    # scan.fit(G3)
    
    # print("Cluster labels:", scan.labels_)
    # print("Core Nodes: ", [v for v in G3.nodes() if scan.is_core(v, G3)])
    # #print("Hubs:", scan.hubs)
    # #print("Outliers:", scan.outliers)

    # print_clusters_networkx(scan)
    
    G=nx.Graph()
    G.add_edge('A', 'B')
    G.add_edge('A', 'C')
    G.add_edge('B', 'C')
    G.add_edge('C', 'D')
    G.add_edge('C', 'E')

    print("\nRunning SCAN with eps =", options.eps, "and mu =", options.mu, "for graph G\n")
    print_graph_networkx(G)
    scan = SCAN(eps=options.eps, mu=options.mu)
    scan.fit(G)
    print("Cluster labels:", scan.labels_)
    print("Core Nodes: ", [v for v in G.nodes() if scan.is_core(v, G)])
    print_clusters_networkx(scan)