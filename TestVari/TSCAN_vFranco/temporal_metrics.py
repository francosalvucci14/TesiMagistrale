from itertools import combinations
from typing import Dict, Iterable, List, Sequence, Set, Tuple

import networkx as nx


def iter_temporal_edges(graph: nx.Graph) -> Iterable[Tuple[str, str, int]]:
    """Yield temporal edges as (u, v, t) for both MultiGraph and simple Graph."""
    if isinstance(graph, (nx.MultiGraph, nx.MultiDiGraph)):
        edges = graph.edges(keys=True, data=True)
    else:
        edges = ((u, v, None, data) for u, v, data in graph.edges(data=True))

    for u, v, key, data in edges:
        if u == v:
            continue
        t = data.get("time")
        if t is None:
            t = 1
        yield (u, v, int(t))


def _edge_degree_within_cluster(graph: nx.Graph, node: str, cluster: Set[str]) -> int:
    degree = 0
    for u, v, t in iter_temporal_edges(graph):
        if u == node and v in cluster:
            degree += 1
        elif v == node and u in cluster:
            degree += 1
    return degree


def _edge_count_inside_cluster(graph: nx.Graph, cluster: Set[str]) -> int:
    count = 0
    for u, v, t in iter_temporal_edges(graph):
        if u in cluster and v in cluster:
            count += 1
    return count


def average_separability(graph: nx.Graph, clusters: Sequence[Set[str]]) -> float:
    """AS: average ratio internal temporal edges / external temporal edges."""
    scores = []
    for cluster in clusters:
        if not cluster:
            continue
        internal = 0
        external = 0
        for u, v, t in iter_temporal_edges(graph):
            inside_u = u in cluster
            inside_v = v in cluster
            if inside_u and inside_v:
                internal += 1
            elif inside_u or inside_v:
                external += 1
        if external == 0:
            score = float("inf") if internal > 0 else 0.0
        else:
            score = internal / external
        scores.append(score)
    return sum(scores) / len(scores) if scores else 0.0


def average_density(graph: nx.Graph, clusters: Sequence[Set[str]]) -> float:
    """AD: average node degree within each cluster, using temporal edges."""
    scores = []
    for cluster in clusters:
        if not cluster:
            continue
        total_degree = sum(_edge_degree_within_cluster(graph, node, cluster) for node in cluster)
        scores.append(total_degree / len(cluster))
    return sum(scores) / len(scores) if scores else 0.0


def _conductance(graph: nx.Graph, subset: Set[str], cluster: Set[str]) -> float:
    if not subset or len(subset) == len(cluster):
        return 0.0

    complement = cluster - subset
    if not complement:
        return 0.0

    cut = 0
    for u, v, t in iter_temporal_edges(graph):
        if (u in subset and v in complement) or (v in subset and u in complement):
            cut += 1

    vol_subset = sum(_edge_degree_within_cluster(graph, node, cluster) for node in subset)
    vol_complement = sum(_edge_degree_within_cluster(graph, node, cluster) for node in complement)

    if min(vol_subset, vol_complement) == 0:
        return 0.0
    return cut / min(vol_subset, vol_complement)


def average_cohesiveness(graph: nx.Graph, clusters: Sequence[Set[str]]) -> float:
    """AC: average of the maximum conductance over all non-trivial subsets of each cluster."""
    scores = []
    for cluster in clusters:
        if not cluster or len(cluster) < 2:
            scores.append(0.0)
            continue

        nodes = list(cluster)
        best = 0.0
        # Exhaustive search is feasible for the small clusters used by this script.
        if len(nodes) <= 18:
            for size in range(1, len(nodes)):
                for subset_nodes in combinations(nodes, size):
                    subset = set(subset_nodes)
                    phi = _conductance(graph, subset, cluster)
                    if phi > best:
                        best = phi
        else:
            # Fallback: evaluate singletons and their complements for larger clusters.
            for node in nodes:
                phi = _conductance(graph, {node}, cluster)
                if phi > best:
                    best = phi
        scores.append(best)
    return sum(scores) / len(scores) if scores else 0.0


def average_clustering_coefficient(graph: nx.Graph, clusters: Sequence[Set[str]]) -> float:
    """ACC: average local clustering coefficient-like score over nodes in each cluster."""
    scores = []
    for cluster in clusters:
        if not cluster:
            continue
        node_scores = []
        for node in cluster:
            neighbors = set()
            for u, v, t in iter_temporal_edges(graph):
                if u == node and v in cluster:
                    neighbors.add(v)
                elif v == node and u in cluster:
                    neighbors.add(u)

            degree = len(neighbors)
            if degree == 0:
                node_scores.append(0.0)
                continue

            closed_edges = 0
            for u, v, t in iter_temporal_edges(graph):
                if u in cluster and v in cluster and u != node and v != node and u in neighbors and v in neighbors:
                    closed_edges += 1
            node_scores.append(closed_edges / degree)
        scores.append(sum(node_scores) / len(node_scores))
    return sum(scores) / len(scores) if scores else 0.0


def summarize_temporal_metrics(graph: nx.Graph, clusters: Sequence[Set[str]]) -> Dict[str, float]:
    return {
        "AS": average_separability(graph, clusters),
        "AD": average_density(graph, clusters),
        #"AC": average_cohesiveness(graph, clusters),
        "ACC": average_clustering_coefficient(graph, clusters),
    }
