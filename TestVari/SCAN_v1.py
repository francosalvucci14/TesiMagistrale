from math import sqrt
from collections import deque

def similarity(x,y,g):
    Nx = g[x] | {x}
    Ny = g[y] | {y}

    #print(f"x: {x}, y: {y}")
    #print(f"Nx: {Nx}, Ny: {Ny}")

    comuni = len(Nx & Ny)
    #print(f"Comuni: {comuni}")
    sim = comuni / sqrt(len(Nx) * len(Ny))
    return sim

def eps_vicini(v,g,eps):
    result = []

    for u in g[v]:
        s = similarity(v,u,g)
        #print(f"Similarity between {v} and {u}: {s}")
        if s >= eps:
            result.append(u)
    return result

def is_core(v,g,eps,mu):
    return len(eps_vicini(v,g,eps)) >= mu

UNCLASSIFIED = -1
NONMEMBERS = -2

def SCAN(g,eps=0.5,mu=2):
    cluster_id = 0
    vertex_cluster = {v: UNCLASSIFIED for v in g}

    for v in g:
        if vertex_cluster[v] != UNCLASSIFIED:
            continue
        if not is_core(v,g,eps,mu):
            vertex_cluster[v] = NONMEMBERS
            continue
        vertex_cluster[v] = cluster_id #nuovo cluster
        queue = deque([v])
        while queue:
            x = queue.popleft()
            vicinato = eps_vicini(x,g,eps)
            if len(vicinato) >= mu:
                for y in vicinato:
                    if vertex_cluster[y] in [UNCLASSIFIED, NONMEMBERS]:
                        if vertex_cluster[y] == UNCLASSIFIED:
                            queue.append(y)
                        vertex_cluster[y] = cluster_id
        cluster_id += 1

    return vertex_cluster

def print_clusters(vertex_cluster):
    clusters = {}
    for vertex, cluster_id in vertex_cluster.items():
        if cluster_id not in clusters:
            clusters[cluster_id] = []
        clusters[cluster_id].append(vertex)

    for cluster_id, vertices in clusters.items():
        if cluster_id == NONMEMBERS:
            print(f"Non-members: {vertices}")
        else:
            print(f"Cluster {cluster_id}: {vertices}")

def print_graph(g):
    for v, neighbors in g.items():
        print(f"{v}: {neighbors}")

def class_hub_outlier(g,vertex_cluster):
    hubs = []
    outliers = []
    for v in g:
        if vertex_cluster[v] != -2:
            continue

        neighbors_cluster = set()
        for u in g[v]:
            if vertex_cluster[u] >=0:
                neighbors_cluster.add(vertex_cluster[u])
        
        if len(neighbors_cluster) >=2:
            hubs.append(v)
        else:
            outliers.append(v)
    return hubs, outliers

if __name__ == "__main__":
    graph = {
        0: {1,4,5,6},
        1: {0,5,2},
        2: {1,3,5},
        3: {2,4,5,6},
        4: {0,3,5,6},
        5: {0,1,2,3,4},
        6: {0,4,3,11,7,10},
        7: {6,8,11,12},
        8: {7,9,12},
        9: {8,12,10,13},
        10: {9,11,12,6},
        11: {6,7,10,12},
        12: {7,8,9,10,11},
        13: {9}
    }
    
    print_graph(graph)

    eps = [0.0,0.1,0.2,0.3,0.4,0.5, 0.6, 0.7,0.8,0.9,1.0]
    n_clusters = []
    for e in eps:
        print(f"\nEpsilon: {e}")
        result = SCAN(graph, eps=e, mu=2)
        print_clusters(result)
        
        n_cluster_iter = len(set(c for c in result.values() if c >= 0))
        n_clusters += [(e,n_cluster_iter)]
        print(f"Number of clusters: {n_cluster_iter}")

        hubs, outliers = class_hub_outlier(graph, result)
        print(f"Hubs: {hubs}")
        print(f"Outliers: {outliers}")
    
    best_eps = max(n_clusters, key=lambda x: x[1])[0]
    print(f"\nBest epsilon: {best_eps} with {max(n_clusters, key=lambda x: x[1])[1]} clusters")

    
