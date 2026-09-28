import networkx as nx
from dataclasses import dataclass, field
from typing import List, Set

# -------------------------------------
# Rappresentazione di una community
# -------------------------------------

@dataclass
class TemporalCommunity:
    id: int
    nodes: Set
    start: int
    end: int

    history: List[Set] = field(default_factory=list)

    def lifetime(self):
        return self.end - self.start + 1


# -------------------------------------
# Jaccard similarity
# -------------------------------------

def jaccard(A, B):

    if len(A) == 0 and len(B) == 0:
        return 1.0

    return len(A & B) / len(A | B)


# -------------------------------------
# Estrazione k-core communities
# -------------------------------------

def extract_kcore_communities(G, k):

    """
    Calcola il k-core e restituisce
    le componenti connesse.

    Ogni componente è una community.
    """

    core = nx.k_core(G, k=k)

    communities = []

    for component in nx.connected_components(core):
        communities.append(set(component))

    return communities

# -------------------------------------
# Dynamic Core Tracker
# -------------------------------------

class DynamicKCoreDetector:

    def __init__(
            self,
            k=2,
            similarity_threshold=0.5):

        self.k = k
        self.threshold = similarity_threshold

        self.next_id = 0


    def detect(self, snapshots):

        """
        snapshots:
            lista di grafi networkx

        esempio:

        [
          G1,
          G2,
          G3
        ]

        """
        active = []
        finished = []

        for t, G in enumerate(snapshots):

            print(f"Processing snapshot {t}")

            communities = extract_kcore_communities(
                G,
                self.k
            )
            matched = set()
            new_active = []

            # confronto con comunità precedenti

            for old in active:

                best_match = None
                best_score = 0

                for idx,new_nodes in enumerate(communities):

                    if idx in matched:
                        continue

                    score = jaccard(
                        old.nodes,
                        new_nodes
                    )

                    if score > best_score:
                        best_score = score
                        best_match = idx

                if (
                    best_match is not None
                    and best_score >= self.threshold
                ):
                    nodes = communities[best_match]
                    old.nodes = nodes
                    old.end = t
                    old.history.append(nodes)
                    matched.add(best_match)
                    new_active.append(old)
                else:
                    # community terminata

                    finished.append(old)

            # nuove comunità

            for idx,nodes in enumerate(communities):

                if idx not in matched:

                    c = TemporalCommunity(
                        id=self.next_id,
                        nodes=nodes,
                        start=t,
                        end=t,
                        history=[nodes]
                    )

                    self.next_id += 1
                    new_active.append(c)

            active = new_active

        # aggiungo quelle ancora vive

        finished.extend(active)

        return finished



# -------------------------------------
# Filtering stable communities
# -------------------------------------

def stable_filter(
        communities,
        min_lifetime=3):

    return [
        c for c in communities
        if c.lifetime() >= min_lifetime
    ]



# -------------------------------------
# TEST
# -------------------------------------

def create_snapshots_from_temporalgraph(TG):
    
    """
    TG: networkx temporal graph with "time" attribute on edges
    """

    snapshots = []
    times = set()
    # collect all timestamps from edges; edges may have either a single
    # "time" attribute or a multi-valued "timestamps" list
    for u, v, data in TG.edges(data=True):
        if "time" in data:
            times.add(data.get("time"))
        if "timestamps" in data:
            for tt in data.get("timestamps", []):
                times.add(tt)

    for t in sorted(times):
        G = nx.Graph()
        for u, v, data in TG.edges(data=True):
            # edge present at time t if:
            # - it has a single "time" equal to t, or
            # - it has a "timestamps" list containing t
            if data.get("time", None) == t:
                G.add_edge(u, v)
                continue

            timestamps = data.get("timestamps")
            if timestamps is not None:
                # accept any iterable (list, tuple, set)
                try:
                    if t in timestamps:
                        G.add_edge(u, v)
                except TypeError:
                    # timestamps might be a single int by mistake
                    if timestamps == t:
                        G.add_edge(u, v)

        snapshots.append(G)

    return snapshots

if __name__ == "__main__":


    # # snapshot 0

    # G1 = nx.Graph()

    # G1.add_edges_from([
    #     ("A","B"),
    #     ("B","C"),
    #     ("C","A"),
    #     ("C","D")
    # ])



    # # snapshot 1

    # G2 = nx.Graph()

    # G2.add_edges_from([
    #     ("A","B"),
    #     ("B","C"),
    #     ("C","A"),
    #     ("C","D"),
    #     ("D","E")
    # ])



    # # snapshot 2

    # G3 = nx.Graph()

    # G3.add_edges_from([
    #     ("A","B"),
    #     ("B","C"),
    #     ("C","A"),
    #     ("D","E")
    # ])



    # snapshots=[
    #     G1,
    #     G2,
    #     G3
    # ]

    G = nx.Graph()

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

    snapshots = create_snapshots_from_temporalgraph(G)

    detector = DynamicKCoreDetector(
        k=2,
        similarity_threshold=0.6
    )


    communities = detector.detect(
        snapshots
    )


    stable = stable_filter(
        communities,
        min_lifetime=1
    )


    print("\nStable communities:")


    for c in stable:

        print(
            f"""Community {c.id}
Nodes:
{c.nodes}

Interval:
[{c.start},{c.end}]

Lifetime:
{c.lifetime()}
"""
        )
