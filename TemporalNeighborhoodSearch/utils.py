import networkx_temporal as tx
import networkx as nx

def build_graph():
    G = tx.temporal_graph()
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
    G.add_edge('G', 'I', time=9)

    G.add_edge('I', 'A', time=4)
    G.add_edge('I', 'C', time=6)
    G.add_edge('I', 'E', time=8)

    return G 

def create_graph_from_ds(dataset):
    G = tx.temporal_graph()
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

def sort_graph(graph_file):
    edges = []

    with open(graph_file,'r') as f:

        for line in f:
            u,v,t = map(int,line.split())
            edges.append((u,v,t))

    edges.sort(key=lambda x: (x[0],x[2]))

    with open(graph_file+"_sort","w") as f:
        for u,v,t in edges:
            f.write(f"{u} {v} {t}\n")
    