import networkx as nx
from collections import defaultdict
from math import sqrt
import optparse

parser = optparse.OptionParser()
parser.add_option("-e", "--eps",action="store", dest="eps",default=0.5, help="Epsilon threshold for structural similarity (default: 0.5)",type="float")
parser.add_option("-m", "--mu", action="store", dest="mu",default=2, help="Minimum number of epsilon neighbors to be a core vertex (default: 2)",type="int")
parser.add_option("-t", "--tau", action="store", dest="tau",default=2, help="Size of the intervals [t,t+tau] (default: 2)",type="int")

options, args = parser.parse_args()

def structural_similarity(x, y, graph):
    """Compute structural similarity exactly like SCAN from a timestamp snapshot."""
    Nx = set(graph.get(x, [])) | {x}
    Ny = set(graph.get(y, [])) | {y}
    if len(Nx) == 0 or len(Ny) == 0:
        return 0.0
    return len(Nx & Ny) / sqrt(len(Nx) * len(Ny))


def build_temporal_snapshots(G):
    """Convert a networkx graph with edge timestamps into per-timestamp adjacency snapshots."""
    temporal_edges = defaultdict(lambda: defaultdict(set))
    timestamps = set()

    for u, v, data in G.edges(data=True):
        ts = data.get('timestamps', [])
        if ts is None:
            continue
        if isinstance(ts, (int, float)):
            ts = [int(ts)]
        for t in ts:
            if not isinstance(t, int):
                continue
            timestamps.add(t)
            temporal_edges[t][u].add(v)
            temporal_edges[t][v].add(u)

    return temporal_edges, timestamps


def build_index_similarity(G, tau, eps, mu, t_min=1):
    """Build the Index-Similarity structure for each interval [t, t+tau]."""
    if tau < 0:
        raise ValueError("tau must be non-negative")
    if mu < 1:
        raise ValueError("mu must be at least 1")

    temporal_edges, timestamps = build_temporal_snapshots(G)
    if not timestamps:
        return {}

    max_ts = max(timestamps)
    if t_min > max_ts:
        return {}

    last_start = max_ts - tau
    if last_start < t_min:
        last_start = t_min

    intervals = []
    for start in range(t_min, last_start + 1):
        end = start + tau
        if end > max_ts:
            continue
        intervals.append((start, end))

    index = {}
    for start, end in intervals:
        node_index = defaultdict(lambda: defaultdict(lambda: {
            'neighbor': None,
            'timestamps': [],
            'timestamp_similarity': {},
            'avg_similarity': 0.0,
        }))

        for t in range(start, end + 1):
            graph_t = temporal_edges.get(t)
            if not graph_t:
                continue

            for v, neighbors in graph_t.items():
                for u in neighbors:
                    if u == v:
                        continue
                    entry = node_index[v][u]
                    entry['neighbor'] = u
                    entry['timestamps'].append(t)
                    if t not in entry['timestamp_similarity']:
                        entry['timestamp_similarity'][t] = structural_similarity(v, u, graph_t)

        interval_nodes = {}
        for v, neighbors in node_index.items():
            entries = []
            for u, entry in neighbors.items():
                if not entry['timestamps']:
                    continue
                entry['timestamps'].sort()
                entry['avg_similarity'] = sum(entry['timestamp_similarity'].values()) / len(entry['timestamp_similarity'])
                if all(sim >= eps for sim in entry['timestamp_similarity'].values()):
                    entries.append(entry)
            if entries:
                interval_nodes[v] = {
                    'core': len(entries) >= mu,
                    'neighbors': entries,
                }

        index[(start, end)] = {
            'interval': (start, end),
            'nodes': interval_nodes,
        }

    return index


def print_index(index):
    """Pretty print the Index-Similarity structure."""
    for _, data in index.items():
        start, end = data['interval']
        print(f"Interval [{start},{end}]")
        for v, node_data in sorted(data['nodes'].items()):
            print(f"  {v}: core={node_data['core']}") 
            for entry in node_data['neighbors']:
                timestamps = entry['timestamps']
                interval_str = f"[{timestamps[0]},{timestamps[-1]}]" if len(timestamps) > 1 else f"[{timestamps[0]}]"
                avg_sim = entry['avg_similarity']
                print(f"    ts={timestamps}, sim={avg_sim:.4f}, neighbor={entry['neighbor']}")
        print()


if __name__ == '__main__':
    G = nx.Graph()
    # G.add_edge('A', 'B', timestamps=[1, 2])
    # G.add_edge('A', 'C', timestamps=[1])
    # G.add_edge('B', 'C', timestamps=[3])
    # G.add_edge('C', 'D', timestamps=[4])
    # G.add_edge('C', 'E', timestamps=[1, 3, 4])

    # tau = 3
    # eps = 0.5
    # mu = 2
    # index = build_index_similarity(G, tau=tau, eps=eps, mu=mu, t_min=1)
    # print_index(index)

    G.add_edge('V1', 'V2', timestamps=[2,3,4])
    G.add_edge('V1', 'V4', timestamps=[4,5,6])
    G.add_edge('V1', 'V3', timestamps=[1,2,3,4])
    G.add_edge('V2', 'V3', timestamps=[1,2,3,7])
    G.add_edge('V2', 'V4', timestamps=[2, 3, 4])
    G.add_edge('V3', 'V4', timestamps=[1,2,3,7])
    G.add_edge('V3', 'V5', timestamps=[1,2,7])
    G.add_edge('V4', 'V5', timestamps=[7])
    G.add_edge('V5', 'V6', timestamps=[2,3,4,5])
    G.add_edge('V5', 'V8', timestamps=[2,4,6,7])
    G.add_edge('V8', 'V6', timestamps=[2,3,5,6,7])
    G.add_edge('V8', 'V7', timestamps=[2,5,6,7])
    G.add_edge('V6', 'V7', timestamps=[2,3,5])

    #tau = 6
    #eps = 0.0
    #mu = 1
    eps = options.eps
    tau = options.tau
    mu = options.mu

    index = build_index_similarity(G, tau=tau, eps=eps, mu=mu, t_min=1)
    print_index(index)