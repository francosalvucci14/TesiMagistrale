import networkx as nx
from itertools import combinations


def _canonical(u, v):
    return (u, v) if u <= v else (v, u)

def structural_sim(G,u,v):
    
    Nu = set(G.neighbors(u))
    Nv = set(G.neighbors(v))

    Nu.add(u)
    Nv.add(v)

    intersect = len(Nu & Nv)

    return intersect/((len(Nu)*len(Nv))**0.5)

def all_sim(G, sim=None, only_edge=False):
    if sim is None:
        sim = {}

    # build set of current pairs in canonical form
    if only_edge:
        raw_pairs = G.edges()
    else:
        raw_pairs = combinations(G.nodes(), 2)

    current_pairs = {_canonical(u, v) for u, v in raw_pairs}

    # Ensure existing sim keys are canonical and mark removed pairs as 0
    for key in list(sim.keys()):
        try:
            u, v = key
        except Exception:
            continue
        can = _canonical(u, v)
        if can not in current_pairs:
            sim[can] = 0.0
            if key != can and key in sim:
                # remove old non-canonical key if present
                sim.pop(key, None)
        else:
            # move to canonical key if needed
            if key != can:
                sim[can] = sim.pop(key)

    # Compute similarities for pairs present now (skip cached)
    for u, v in current_pairs:
        if (u, v) in sim:
            print(f"Skip nodes: {(u,v)}. Similarity already computed")
            continue
        sim[(u, v)] = structural_sim(G, u, v)

    return sim


def _print_sims(sims, title=None):
    if title:
        print(title)
    for pair, simv in sorted(sims.items()):
        print(f"Similarity for {pair}:{simv:.4f}")


def process_snapshots(snapshots, sim=None, only_edge=True, verbose=True):
    if sim is None:
        sim = {}
    caches = []
    for idx, Gs in enumerate(snapshots, start=1):
        if verbose:
            print(f"\nProcessing snapshot {idx}")
            print("Cache before updating:")
            _print_sims(sim)
        sim = all_sim(Gs, sim, only_edge)
        if verbose:
            print("Cache after updating:")
            _print_sims(sim)
        caches.append(sim.copy())
    return caches

if __name__ == "__main__":
    
    G = nx.Graph()

    G.add_edges_from(
        [
            (1,2),
            (2,3),
            (3,4),
            (3,5),
            (3,6),
            (5,6),
            (6,7)
        ]
    )

    G_temp1 = nx.Graph()

    G_temp1.add_edges_from(
        [
            (1,2),
            (3,5),
            (6,7)
        ]
    )
    
    G_temp2 = nx.Graph()

    G_temp2.add_edges_from(
        [
            (1,2),
            (2,3),
            (3,6)
        ]
    )

    sims = all_sim(G, {}, True)
    print("Structural Similarity (SCAN)\n")
    _print_sims(sims)

    print("\nStructural Similarity (SCAN) for temporal snapshots\n")

    snapshots = [G_temp1, G_temp2]
    process_snapshots(snapshots, sims, True, verbose=True)