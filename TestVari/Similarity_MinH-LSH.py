import networkx as nx
from datasketch import MinHash, MinHashLSH, lsh

def compute_scan_lsh(G,num_perm=128,lsh_threshold=0.6):
    """
    Compute the MinHash LSH for a given graph G.

    Parameters:
    G (networkx.Graph): The input graph.
    num_perm (int): Number of permutations for MinHash.
    lsh_threshold (float): Threshold for LSH.

    Returns:
    dict: A dictionary mapping each node to its corresponding MinHash signature.
    MinHashLSH: The LSH index built from the MinHash signatures.
    """
    lsh = MinHashLSH(threshold=lsh_threshold, num_perm=num_perm)
    minhash_dict = {}

    for node in G.nodes():
        
        scan_neighbors = set(G.neighbors(node))
        scan_neighbors.add(node)  # Include the node itself in the scan
        minhash = MinHash(num_perm=num_perm)

        for neighbor in scan_neighbors:
            minhash.update(str(neighbor).encode('utf8'))

        minhash_dict[node] = minhash
        lsh.insert(node, minhash)

    candidate_pairs = set()

    for node in G.nodes():
        result = lsh.query(minhash_dict[node])
        for candidate in result:
            candidate_node = type(node)(candidate)  # Ensure the candidate is of the same type as node
            if node != candidate_node:
                pair = tuple(sorted((node, candidate_node)))
                candidate_pairs.add(pair)

    print("Candidate Pairs:")
    for pair in candidate_pairs:
        print(f"  {pair}")

    return minhash_dict, candidate_pairs

def verify_scan_similarity(G, candidate_pairs):
    """
    Verify the similarity of candidate pairs using structural similarity.

    Parameters:
    G (networkx.Graph): The input graph.
    candidate_pairs (set): A set of candidate pairs to verify.

    Returns:
    dict: A dictionary mapping each candidate pair to its Jaccard similarity score.
    """
    similarity_scores = {}

    for node1, node2 in candidate_pairs:
        neighbors1 = set(G.neighbors(node1)) | {node1}  # Include the node itself in the scan
        neighbors2 = set(G.neighbors(node2)) | {node2}  # Include the node itself in the scan
        intersection = len(neighbors1 & neighbors2)
        union = (len(neighbors1) * len(neighbors2)) ** 0.5
        structure_similarity = intersection / union if union != 0 else 0.0
        similarity_scores[(node1, node2)] = structure_similarity

    return similarity_scores

if __name__ == "__main__":
    #G = nx.karate_club_graph()

    G = nx.Graph()
    G.add_edge('V1', 'V2')
    #G.add_edge('V1', 'V4')
    G.add_edge('V1', 'V3')
    G.add_edge('V2', 'V3')
    G.add_edge('V2', 'V4')
    G.add_edge('V3', 'V4')
    G.add_edge('V3', 'V5')
    #G.add_edge('V4', 'V5')
    G.add_edge('V5', 'V6')
    G.add_edge('V5', 'V8')
    G.add_edge('V8', 'V6')
    G.add_edge('V8', 'V7')
    G.add_edge('V6', 'V7')

    LSH_THRESHOLD = 0.7

    minhash_dict, candidate_pairs = compute_scan_lsh(G, num_perm=128, lsh_threshold=LSH_THRESHOLD)
    similarity_scores = verify_scan_similarity(G, candidate_pairs)

    print("MinHash Signatures:")
    for node, minhash in minhash_dict.items():
        print(f"Node {node}: {minhash.digest()}")
    
    print("Candidate Pairs:")
    for pair in candidate_pairs:
        print(f"  {pair}")
    
    print("Similarity Scores:")
    for pair, score in similarity_scores.items():
        print(f"  Pair {pair}: Structural Similarity = {score:.4f}")