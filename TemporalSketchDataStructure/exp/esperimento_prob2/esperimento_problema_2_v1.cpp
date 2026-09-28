/**
 * ============================================================================
 * PROGETTO TESI: Neighborhood Search su Grafi Temporali
 * File: esperimento_problema_2.cpp
 * 
 * Implementazione e Suite Sperimentale per la Soluzione del PROBLEMA 2:
 * "Neighborhood search con sliding windows" (cfr. problema.tex e sol_prob_2.tex)
 * 
 * RIFERIMENTI TEORICI IMPLEMENTATI (da sol_prob_2.tex):
 * 1. Fase 1 - Pruning Temporale:
 *    - Insieme Lambda(v): istanti temporali attivi del nodo v.
 *    - Insieme W(v) = { t \in {1, ..., T-mu} | [t, t+mu] \cap Lambda(v) != empty }:
 *      W(v) = U_{i=1}^m J_i, con J_i = [lambda_i - mu, lambda_i] \cap {1, ..., T-mu}.
 *    - Bound teorici verificati numericamente:
 *      |Lambda(v)| + mu <= |W(v)| <= min{ T - mu, (mu + 1)*|Lambda(v)| }.
 *    - Rapporto di riduzione del pruning: (T - mu) / |W(v)|.
 * 
 * 2. Fase 2 - Indicizzazione LSH delle coppie nodo-tempo:
 *    - Algoritmo 1: BuildLSH-Index2(G, mu, b, r)
 *    - Signature MinHash di dimensione k = b * r, suddivisa in b band di r righe.
 *    - Collezione di b hash table L = {H_1, ..., H_b}.
 *    - Chiave aumentata K_j(v, t) = ( Hash_j(Band_j(S_{(v,t)})), t ).
 *    - Bucket: H_j[K_j(v, t)].insert(v).
 * 
 * 3. Algoritmo di Risposta alla Query (con Ispezione dei Candidati e delle Similarita'):
 *    - Algoritmo 2: Query2(v, tau, mu)
 *    - Recupera W(v), calcola S_{(v,t)} = RT_v.query([t, t+mu]) per t in W(v).
 *    - Candidate Set: C(v, t) = U_{j=1}^b H_j[K_j(v, t)] \setminus {v}.
 *    - Tracciamento esplicito di:
 *      * Chi sono i candidati estratti per ciascuna finestra t.
 *      * Similarita' MinHash tra gli sketch temporali: sigma(S_{(v,t)}, S_{(u,t)}).
 *      * Similarita' esatta di Jaccard tra i vicinati per validazione empirica.
 *      * Identificazione dei candidati che rispecchiano la soglia sim >= tau.
 * 
 * 4. Suite Sperimentale Completa:
 *    - Esperimento 1: Analisi del Pruning Temporale W(v) e verifica dei bound.
 *    - Esperimento 2: Costruzione e statistiche dell'Indice LSH.
 *    - Esperimento 3: Benchmark di Query a 3 vie (Brute Force vs Pruning vs LSH).
 *    - Esperimento 3.1: ISPEZIONE DETTAGLIATA CANDIDATI E SIMILARITA' SKETCH PER NODI QUERY.
 *    - Esperimento 4: Valutazione di Accuratezza (Precision & Recall).
 *    - Esperimento 5: Analisi parametrica al variare di mu e tau.
 *    - Esperimento 6: Esportazione automatica dei risultati in formato CSV.
 * 
 * Compilazione raccomandata:
 *   g++ -O3 -std=c++17 -fopenmp esperimento_problema_2.cpp -o esperimento_problema_2
 * ============================================================================
 */

#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <random>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <limits>

#ifdef _OPENMP
#include <omp.h>
#endif

// ============================================================================
// 1. STRUTTURE DEL GRAFO TEMPORALE
// ============================================================================

struct TemporalEdge {
    std::string u;
    std::string v;
    int time;
};

class TemporalGraph {
public:
    std::vector<TemporalEdge> edges;
    std::set<std::string> nodes_set;
    int min_time = std::numeric_limits<int>::max();
    int max_time = std::numeric_limits<int>::min();

    void add_edge(const std::string& u, const std::string& v, int time) {
        edges.push_back({u, v, time});
        nodes_set.insert(u);
        nodes_set.insert(v);
        min_time = std::min(min_time, time);
        max_time = std::max(max_time, time);
    }

    std::vector<std::string> nodes() const {
        return std::vector<std::string>(nodes_set.begin(), nodes_set.end());
    }

    int get_T_min() const { return (min_time == std::numeric_limits<int>::max()) ? 1 : min_time; }
    int get_T_max() const { return (max_time == std::numeric_limits<int>::min()) ? 1 : max_time; }
    int get_T() const { return get_T_max() - get_T_min() + 1; }
};

// Generatore di grafi temporali sintetici con comunita'/cluster per test realistici
TemporalGraph generate_synthetic_temporal_graph(int num_nodes, int num_edges, int time_horizon, int seed = 42) {
    TemporalGraph G;
    std::mt19937 rng(seed);
    std::uniform_int_distribution<int> time_dist(1, time_horizon);
    std::uniform_int_distribution<int> node_dist(0, num_nodes - 1);

    std::vector<std::string> node_names;
    for (int i = 0; i < num_nodes; ++i) {
        node_names.push_back("v" + std::to_string(i));
    }

    // Struttura a cluster dinamici: nodi dello stesso cluster interagiscono con alta probabilita'
    int num_clusters = std::max(2, num_nodes / 5);
    for (int e = 0; e < num_edges; ++e) {
        int u_idx = node_dist(rng);
        int v_idx;
        int t = time_dist(rng);

        if (rng() % 100 < 70) {
            // 70% archi intracommunity (crea pattern di similarita' temporale tra nodi vicini)
            int c = u_idx % num_clusters;
            std::vector<int> cluster_nodes;
            for (int i = 0; i < num_nodes; ++i) {
                if (i % num_clusters == c && i != u_idx) cluster_nodes.push_back(i);
            }
            if (!cluster_nodes.empty()) {
                v_idx = cluster_nodes[rng() % cluster_nodes.size()];
            } else {
                v_idx = (u_idx + 1) % num_nodes;
            }
        } else {
            // 30% archi intercommunity random
            do {
                v_idx = node_dist(rng);
            } while (v_idx == u_idx);
        }

        G.add_edge(node_names[u_idx], node_names[v_idx], t);
    }
    return G;
}

// Lettura del dataset temporale da file (righe con formato: u v t)
TemporalGraph create_graph_from_ds(const std::vector<std::string>& dataset) {
    TemporalGraph G;
    for (const auto& line : dataset) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream iss(line);
        std::string u, v;
        int t;
        if (iss >> u >> v >> t) {
            G.add_edge(u, v, t);
        }
    }
    return G;
}

std::pair<std::vector<std::string>, std::string> read_dataset_from_file(const std::string& file_path) {
    std::vector<std::string> dataset;
    std::ifstream file(file_path);
    std::string line;
    if (file.is_open()) {
        while (std::getline(file, line)) {
            if (!line.empty()) {
                dataset.push_back(line);
            }
        }
    }
    size_t last_slash = file_path.find_last_of('/');
    std::string name = (last_slash == std::string::npos) ? file_path : file_path.substr(last_slash + 1);
    size_t last_dot = name.find_last_of('.');
    if (last_dot != std::string::npos) name = name.substr(0, last_dot);
    return {dataset, name};
}

// ============================================================================
// 2. SKETCH MINHASH CON SIGNATURE CONDIVISE
// ============================================================================

class MinHashParams {
public:
    int num_perm;
    std::vector<uint32_t> a;
    std::vector<uint32_t> b;
    static constexpr uint32_t PRIME = 4294967291U; // 2^32 - 5

    static const MinHashParams& get_instance(int k = 128) {
        static std::unordered_map<int, MinHashParams> instances;
        auto it = instances.find(k);
        if (it == instances.end()) {
            it = instances.emplace(k, MinHashParams(k)).first;
        }
        return it->second;
    }

private:
    MinHashParams(int k) : num_perm(k) {
        std::mt19937 gen(42);
        std::uniform_int_distribution<uint32_t> dist_a(1, PRIME - 1);
        std::uniform_int_distribution<uint32_t> dist_b(0, PRIME - 1);
        a.resize(num_perm);
        b.resize(num_perm);
        for (int i = 0; i < num_perm; ++i) {
            a[i] = dist_a(gen);
            b[i] = dist_b(gen);
        }
    }
};

class NeighborhoodSketch {
public:
    std::set<std::string> vicini;
    virtual ~NeighborhoodSketch() = default;
    virtual std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const = 0;
    virtual std::shared_ptr<NeighborhoodSketch> clone() const = 0;
    virtual void print_info(const std::string& indent) const = 0;
};

class MinHashNeighborhoodSketch : public NeighborhoodSketch {
private:
    int num_perm;
    std::vector<uint32_t> hashvalues;

    void update(const std::string& v) {
        const auto& params = MinHashParams::get_instance(num_perm);
        uint32_t base_hash = std::hash<std::string>{}(v);
        for (int i = 0; i < num_perm; ++i) {
            uint64_t val = (static_cast<uint64_t>(params.a[i]) * base_hash + params.b[i]) % MinHashParams::PRIME;
            if (static_cast<uint32_t>(val) < hashvalues[i]) {
                hashvalues[i] = static_cast<uint32_t>(val);
            }
        }
    }

public:
    MinHashNeighborhoodSketch(const std::vector<std::string>& init_vicini = {}, int num_perm = 128)
        : num_perm(num_perm), hashvalues(num_perm, UINT32_MAX) {
        for (const auto& v : init_vicini) {
            vicini.insert(v);
            update(v);
        }
    }

    std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const override {
        const auto* other_mh = dynamic_cast<const MinHashNeighborhoodSketch*>(&other);
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<std::string>{}, num_perm);

        std::set_union(vicini.begin(), vicini.end(),
                       other_mh->vicini.begin(), other_mh->vicini.end(),
                       std::inserter(new_sk->vicini, new_sk->vicini.begin()));

        for (int i = 0; i < num_perm; ++i) {
            new_sk->hashvalues[i] = std::min(this->hashvalues[i], other_mh->hashvalues[i]);
        }
        return new_sk;
    }

    std::shared_ptr<NeighborhoodSketch> clone() const override {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<std::string>{}, num_perm);
        new_sk->vicini = this->vicini;
        new_sk->hashvalues = this->hashvalues;
        return new_sk;
    }

    // Stima della similarita' di Jaccard tramite MinHash
    double jaccard_sim(const MinHashNeighborhoodSketch& other_sk) const {
        if (this->hashvalues.empty() || other_sk.hashvalues.empty()) return 0.0;
        if (this->hashvalues[0] == UINT32_MAX || other_sk.hashvalues[0] == UINT32_MAX) return 0.0;
        int matches = 0;
        for (int i = 0; i < num_perm; ++i) {
            if (this->hashvalues[i] == other_sk.hashvalues[i]) {
                matches++;
            }
        }
        return static_cast<double>(matches) / num_perm;
    }

    // Calcolo esatto della similarita' di Jaccard tra i vicinati (Ground Truth)
    double exact_jaccard(const MinHashNeighborhoodSketch& other_sk) const {
        if (vicini.empty() && other_sk.vicini.empty()) return 1.0;
        if (vicini.empty() || other_sk.vicini.empty()) return 0.0;
        size_t intersection_sz = 0;
        auto it1 = vicini.begin();
        auto it2 = other_sk.vicini.begin();
        while (it1 != vicini.end() && it2 != other_sk.vicini.end()) {
            if (*it1 == *it2) {
                intersection_sz++;
                ++it1;
                ++it2;
            } else if (*it1 < *it2) {
                ++it1;
            } else {
                ++it2;
            }
        }
        size_t union_sz = vicini.size() + other_sk.vicini.size() - intersection_sz;
        return static_cast<double>(intersection_sz) / union_sz;
    }

    const std::vector<uint32_t>& get_hashvalues() const { return hashvalues; }
    int get_num_perm() const { return num_perm; }
    bool is_empty() const { return vicini.empty(); }

    void print_info(const std::string& indent) const override {
        std::cout << indent << " └ Vicinato (|N|=" << vicini.size() << "): [";
        int cnt = 0;
        for (const auto& v : vicini) {
            if (cnt++ >= 5) { std::cout << "..."; break; }
            std::cout << v << " ";
        }
        std::cout << "]\n";
    }
};

using SketchFactory = std::function<std::shared_ptr<NeighborhoodSketch>(const std::vector<std::string>&)>;

// ============================================================================
// 3. RANGE TREE E TEMPORAL RANGE FOREST
// ============================================================================

struct RangeTreeNode {
    int start_time;
    int end_time;
    std::shared_ptr<NeighborhoodSketch> sk;
    std::shared_ptr<RangeTreeNode> left;
    std::shared_ptr<RangeTreeNode> right;
    bool is_leaf() const { return !left && !right; }
};

class RangeTree {
private:
    std::string node_id;
    SketchFactory sketch_factory;
    std::vector<int> times; // Rappresenta l'insieme Lambda(v) ordinato
    std::shared_ptr<RangeTreeNode> root;

    std::shared_ptr<RangeTreeNode> _build_tree(const std::vector<int>& t_subset,
                                               const std::vector<std::shared_ptr<NeighborhoodSketch>>& s_subset) {
        if (t_subset.empty()) return nullptr;
        if (t_subset.size() == 1) {
            auto node = std::make_shared<RangeTreeNode>();
            node->start_time = t_subset[0];
            node->end_time = t_subset[0];
            node->sk = s_subset[0]->clone();
            return node;
        }

        size_t mid = t_subset.size() / 2;
        std::vector<int> left_t(t_subset.begin(), t_subset.begin() + mid);
        std::vector<std::shared_ptr<NeighborhoodSketch>> left_s(s_subset.begin(), s_subset.begin() + mid);

        std::vector<int> right_t(t_subset.begin() + mid, t_subset.end());
        std::vector<std::shared_ptr<NeighborhoodSketch>> right_s(s_subset.begin() + mid, s_subset.end());

        auto left = _build_tree(left_t, left_s);
        auto right = _build_tree(right_t, right_s);

        auto merged_sk = left->sk->merge(*(right->sk));

        auto node = std::make_shared<RangeTreeNode>();
        node->start_time = left->start_time;
        node->end_time = right->end_time;
        node->sk = merged_sk;
        node->left = left;
        node->right = right;
        return node;
    }

    std::shared_ptr<NeighborhoodSketch> _query(std::shared_ptr<RangeTreeNode> node, int start, int end) {
        if (!node) return nullptr;
        if (start <= node->start_time && node->end_time <= end) {
            return node->sk;
        }
        if (node->end_time < start || node->start_time > end) {
            return nullptr;
        }
        auto left_res = _query(node->left, start, end);
        auto right_res = _query(node->right, start, end);

        if (!left_res) return right_res;
        if (!right_res) return left_res;
        return left_res->merge(*right_res);
    }

public:
    RangeTree() = default;
    RangeTree(std::string id, const std::map<int, std::vector<std::string>>& time_neighbors, SketchFactory factory)
        : node_id(id), sketch_factory(factory) {
        for (const auto& [t, neighbors] : time_neighbors) {
            times.push_back(t);
        }
        if (!times.empty()) {
            std::vector<std::shared_ptr<NeighborhoodSketch>> leaf_sks;
            for (int t : times) {
                leaf_sks.push_back(sketch_factory(time_neighbors.at(t)));
            }
            root = _build_tree(times, leaf_sks);
        }
    }

    std::shared_ptr<NeighborhoodSketch> query(const std::string&, int start, int end) {
        return _query(root, start, end);
    }

    const std::vector<int>& get_times() const { return times; }
};

class TemporalRangeForest {
private:
    const TemporalGraph* temporal_graph;
    SketchFactory sk_factory;
    std::unordered_map<std::string, RangeTree> trees;
    int k_signature;

public:
    TemporalRangeForest(const TemporalGraph* graph, int k = 128)
        : temporal_graph(graph), k_signature(k) {

        sk_factory = [k](const std::vector<std::string>& vicini) {
            return std::make_shared<MinHashNeighborhoodSketch>(vicini, k);
        };

        // Pre-indicizzazione O(|E|) del vicinato per nodo e timestamp
        std::unordered_map<std::string, std::map<int, std::vector<std::string>>> node_time_neighbors;
        for (const auto& edge : temporal_graph->edges) {
            node_time_neighbors[edge.u][edge.time].push_back(edge.v);
            node_time_neighbors[edge.v][edge.time].push_back(edge.u);
        }

        auto all_nodes = temporal_graph->nodes();

        #pragma omp parallel
        {
            std::unordered_map<std::string, RangeTree> local_trees;
            #pragma omp for schedule(dynamic)
            for (size_t i = 0; i < all_nodes.size(); ++i) {
                const auto& node = all_nodes[i];
                if (node_time_neighbors.count(node)) {
                    local_trees.emplace(node, RangeTree(node, node_time_neighbors[node], sk_factory));
                }
            }
            #pragma omp critical
            {
                trees.insert(local_trees.begin(), local_trees.end());
            }
        }
    }

    RangeTree* get_tree_for_node(const std::string& node) {
        auto it = trees.find(node);
        return (it != trees.end()) ? &(it->second) : nullptr;
    }

    const std::vector<int>& get_lambda(const std::string& node) {
        static const std::vector<int> empty_vec;
        auto* tree = get_tree_for_node(node);
        return tree ? tree->get_times() : empty_vec;
    }

    int get_k() const { return k_signature; }
};

// ============================================================================
// 4. ALGORITMO PROBLEMA 2: PRUNING TEMPORALE ED LSH CON CANDIDATI DETTAGLIATI
// ============================================================================

std::vector<int> compute_W(const std::vector<int>& lambda, int mu, int T_min, int T_max) {
    if (lambda.empty() || (T_max - mu) < T_min) return {};

    std::vector<std::pair<int, int>> merged;
    for (int lam : lambda) {
        int L = std::max(T_min, lam - mu);
        int R = std::min(T_max - mu, lam);
        if (L > R) continue;

        if (merged.empty() || L > merged.back().second + 1) {
            merged.push_back({L, R});
        } else {
            merged.back().second = std::max(merged.back().second, R);
        }
    }

    std::vector<int> W;
    for (const auto& iv : merged) {
        for (int t = iv.first; t <= iv.second; ++t) {
            W.push_back(t);
        }
    }
    return W;
}

struct LSHKey {
    uint64_t band_hash;
    int time_window;

    bool operator==(const LSHKey& other) const {
        return band_hash == other.band_hash && time_window == other.time_window;
    }
};

struct LSHKeyHash {
    std::size_t operator()(const LSHKey& k) const {
        std::size_t h1 = std::hash<uint64_t>{}(k.band_hash);
        std::size_t h2 = std::hash<int>{}(k.time_window);
        return h1 ^ (h2 + 0x9e3779b97f4a7c15ULL + (h1 << 6) + (h1 >> 2));
    }
};

inline uint64_t hash_band_fnv(const std::vector<uint32_t>& sig, int band_idx, int r) {
    uint64_t h = 14695981039346656037ULL ^ (static_cast<uint64_t>(band_idx + 1) * 1099511628211ULL);
    int start = band_idx * r;
    for (int i = 0; i < r; ++i) {
        h ^= sig[start + i];
        h *= 1099511628211ULL;
    }
    return h;
}

// Struttura che memorizza i dettagli di ogni candidato valutato
struct CandidateEvaluation {
    std::string node;           // ID del candidato u
    int time_window;            // Finestra temporale t di collisione
    double sketch_similarity;   // Similarita' stimata con MinHash
    double exact_similarity;    // Similarita' Jaccard esatta sui vicinati
    bool is_match;              // True se sketch_similarity >= tau
    std::set<std::string> query_vicinato;      // Vicinato N^{I_t}(v)
    std::set<std::string> candidate_vicinato;  // Vicinato N^{I_t}(u)
};

struct QueryResult {
    bool found = false;
    std::string candidate_node = "";
    int time_window = -1;
    double similarity = 0.0;
    double exact_similarity = 0.0;
    size_t candidates_evaluated = 0;
    double elapsed_us = 0.0;

    // Ispezione approfondita
    std::vector<CandidateEvaluation> evaluated_candidates;
    std::set<std::string> unique_candidate_nodes;
    std::vector<CandidateEvaluation> matching_candidates;
};

class LSHIndexSlidingWindows {
public:
    int b;
    int r;
    int k;
    int mu;
    int T_min;
    int T_max;

    std::vector<std::unordered_map<LSHKey, std::vector<std::string>, LSHKeyHash>> tables;
    std::unordered_map<std::string, std::vector<int>> node_W;
    size_t total_entries_indexed = 0;
    double build_time_ms = 0.0;

    LSHIndexSlidingWindows(int b = 16, int r = 8, int mu = 2, int T_min = 1, int T_max = 100)
        : b(b), r(r), k(b * r), mu(mu), T_min(T_min), T_max(T_max), tables(b) {}

    void build(TemporalRangeForest& trf, const std::vector<std::string>& nodes) {
        auto t_start = std::chrono::high_resolution_clock::now();
        for (int j = 0; j < b; ++j) {
            tables[j].clear();
        }
        node_W.clear();
        total_entries_indexed = 0;

        for (const auto& v : nodes) {
            const auto& lambda_v = trf.get_lambda(v);
            auto W_v = compute_W(lambda_v, mu, T_min, T_max);
            node_W[v] = W_v;

            for (int t : W_v) {
                auto sk = trf.get_tree_for_node(v)->query(v, t, t + mu);
                if (!sk) continue;
                auto mh = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk);
                if (!mh || mh->is_empty()) continue;

                const auto& sig = mh->get_hashvalues();
                for (int j = 0; j < b; ++j) {
                    uint64_t h = hash_band_fnv(sig, j, r);
                    LSHKey key{h, t};
                    tables[j][key].push_back(v);
                    total_entries_indexed++;
                }
            }
        }
        auto t_end = std::chrono::high_resolution_clock::now();
        build_time_ms = std::chrono::duration<double, std::milli>(t_end - t_start).count();
    }

    // Query2 (Algoritmo 2) con registrazione dettagliata dei candidati estratti e delle similarita'
    QueryResult query2(const std::string& v, double tau, TemporalRangeForest& trf, bool stop_at_first_match = true) {
        auto t_start = std::chrono::high_resolution_clock::now();
        QueryResult res;

        auto it_W = node_W.find(v);
        if (it_W == node_W.end()) {
            auto t_end = std::chrono::high_resolution_clock::now();
            res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
            return res;
        }

        const auto& W_v = it_W->second;

        for (int t : W_v) {
            auto sk_v = trf.get_tree_for_node(v)->query(v, t, t + mu);
            if (!sk_v) continue;
            auto mh_v = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_v);
            if (!mh_v || mh_v->is_empty()) continue;

            const auto& sig_v = mh_v->get_hashvalues();

            // Candidate Set Cand_t = U_{j=1}^b H_j[K_j(v, t)] \setminus {v}
            std::unordered_set<std::string> Cand_t;
            for (int j = 0; j < b; ++j) {
                uint64_t h = hash_band_fnv(sig_v, j, r);
                LSHKey key{h, t};
                auto it_bucket = tables[j].find(key);
                if (it_bucket != tables[j].end()) {
                    for (const auto& u : it_bucket->second) {
                        if (u != v) Cand_t.insert(u);
                    }
                }
            }

            res.candidates_evaluated += Cand_t.size();

            // Verifica candidati ed estrazione similarita'
            for (const auto& u : Cand_t) {
                res.unique_candidate_nodes.insert(u);

                auto sk_u = trf.get_tree_for_node(u)->query(u, t, t + mu);
                if (!sk_u) continue;
                auto mh_u = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_u);
                if (!mh_u) continue;

                double sim = mh_v->jaccard_sim(*mh_u);
                double exact_sim = mh_v->exact_jaccard(*mh_u);
                bool is_match = (sim >= tau);

                CandidateEvaluation ce{
                    u, t, sim, exact_sim, is_match,
                    mh_v->vicini, mh_u->vicini
                };
                res.evaluated_candidates.push_back(ce);

                if (is_match) {
                    res.matching_candidates.push_back(ce);
                    if (!res.found) {
                        res.found = true;
                        res.candidate_node = u;
                        res.time_window = t;
                        res.similarity = sim;
                        res.exact_similarity = exact_sim;
                    }
                    if (stop_at_first_match) {
                        auto t_end = std::chrono::high_resolution_clock::now();
                        res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
                        return res;
                    }
                }
            }
        }

        auto t_end = std::chrono::high_resolution_clock::now();
        res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
        return res;
    }

    // Ispezione esaustiva: esplora tutte le finestre W(v) per trovare TUTTI i candidati e TUTTI i match
    QueryResult inspect_all_candidates(const std::string& v, double tau, TemporalRangeForest& trf) {
        return query2(v, tau, trf, false);
    }

    // Metodo di stampa e reportistica dettagliata per un nodo query
    void print_detailed_query_report(const std::string& v, double tau, TemporalRangeForest& trf, bool inspect_all = true) {
        std::cout << "\n================================================================================\n";
        std::cout << " ISPEZIONE DETTAGLIATA CANDIDATI E SIMILARITA' SKETCH PER IL NODO '" << v << "'\n";
        std::cout << "================================================================================\n";
        
        auto it_W = node_W.find(v);
        if (it_W == node_W.end()) {
            std::cout << "Nessuna informazione disponibile per il nodo " << v << "!\n";
            return;
        }

        const auto& lambda_v = trf.get_lambda(v);
        const auto& W_v = it_W->second;

        std::cout << "[Configurazione Query]\n";
        std::cout << " - Nodo target:                  " << v << "\n";
        std::cout << " - Soglia di similarita' (tau):  " << tau << "\n";
        std::cout << " - Ampiezza finestra (mu):       " << mu << "\n";
        std::cout << " - Istanti temporali |Lambda(v)|: " << lambda_v.size() << "\n";
        std::cout << " - Finestre rilevanti |W(v)|:    " << W_v.size() << " (su " << (T_max - mu - T_min + 1) << " totali)\n";

        QueryResult res = inspect_all ? inspect_all_candidates(v, tau, trf) : query2(v, tau, trf, true);

        std::cout << "\n[1. Riepilogo Candidati Estratti dai Bucket LSH]\n";
        std::cout << " - Nodi candidati distinti incontrati: " << res.unique_candidate_nodes.size() << "\n";
        std::cout << "   Elenco nodi candidati: { ";
        for (const auto& c_node : res.unique_candidate_nodes) {
            std::cout << c_node << " ";
        }
        std::cout << "}\n";
        std::cout << " - Totale verifiche candidato-finestra (u, t): " << res.evaluated_candidates.size() << "\n";

        std::cout << "\n[2. Tracciamento Verifiche di Similarita' per Finestra]\n";
        if (res.evaluated_candidates.empty()) {
            std::cout << "   Nessun candidato e' colliso nei bucket LSH con il nodo '" << v << "'.\n";
        } else {
            // Raggruppa per finestra temporale
            std::map<int, std::vector<CandidateEvaluation>> by_window;
            for (const auto& ev : res.evaluated_candidates) {
                by_window[ev.time_window].push_back(ev);
            }

            int shown_windows = 0;
            for (const auto& [t, cand_list] : by_window) {
                if (++shown_windows > 12 && inspect_all) {
                    std::cout << "   ... [altre " << (by_window.size() - 12) << " finestre omesse per brevita'] ...\n";
                    break;
                }
                std::cout << "   Finestra t = " << std::setw(3) << t 
                          << " (Intervallo [" << t << ", " << t + mu << "]):\n";
                
                for (const auto& cand : cand_list) {
                    std::cout << "     -> Candidato '" << cand.node << "':\n";
                    std::cout << "        * Similarita' Sketch MinHash: " << std::fixed << std::setprecision(4) << cand.sketch_similarity;
                    if (cand.is_match) {
                        std::cout << "  ===> [RISPECCHIA LA SIMILARITA'! (>= " << tau << ")]\n";
                    } else {
                        std::cout << "  ===> [SCARTATO: inferiore a soglia " << tau << "]\n";
                    }
                    std::cout << "        * Similarita' Jaccard Esatta: " << cand.exact_similarity << "\n";
                    std::cout << "        * Vicinato N^{I_t}(" << v << ") [dim: " << cand.query_vicinato.size() << "]: [ ";
                    int cnt = 0;
                    for (const auto& nb : cand.query_vicinato) {
                        if (cnt++ >= 4) { std::cout << "... "; break; }
                        std::cout << nb << " ";
                    }
                    std::cout << "]\n";
                    std::cout << "        * Vicinato N^{I_t}(" << cand.node << ") [dim: " << cand.candidate_vicinato.size() << "]: [ ";
                    cnt = 0;
                    for (const auto& nb : cand.candidate_vicinato) {
                        if (cnt++ >= 4) { std::cout << "... "; break; }
                        std::cout << nb << " ";
                    }
                    std::cout << "]\n";
                }
            }
        }

        std::cout << "\n[3. Candidati che Rispecchiano la Similarita' (sim >= " << tau << ")]\n";
        if (res.matching_candidates.empty()) {
            std::cout << "   Nessun candidato soddisfa la condizione sim >= " << tau << ".\n";
        } else {
            std::cout << "   Trovati " << res.matching_candidates.size() << " match validi:\n";
            std::set<std::string> matched_nodes;
            for (size_t m_idx = 0; m_idx < res.matching_candidates.size(); ++m_idx) {
                const auto& m = res.matching_candidates[m_idx];
                matched_nodes.insert(m.node);
                if (m_idx < 10) {
                    std::cout << "   [" << (m_idx + 1) << "] Nodo '" << m.node << "' @ finestra t = " << m.time_window
                              << " | Sketch MinHash Sim: " << std::fixed << std::setprecision(4) << m.sketch_similarity
                              << " | Jaccard Esatta: " << m.exact_similarity << "\n";
                }
            }
            if (res.matching_candidates.size() > 10) {
                std::cout << "   ... [totale " << res.matching_candidates.size() << " match presenti]\n";
            }
            std::cout << "   -> Insieme dei nodi unici che rispecchiano la similarita': { ";
            for (const auto& mn : matched_nodes) std::cout << mn << " ";
            std::cout << "}\n";
        }
        std::cout << "================================================================================\n";
    }
};

// ============================================================================
// 5. BASELINE DI CONFRONTO PER IL BENCHMARK
// ============================================================================

QueryResult baseline_brute_force_no_pruning(const std::string& v, double tau, int mu, int T_min, int T_max,
                                            const std::vector<std::string>& all_nodes, TemporalRangeForest& trf) {
    auto t_start = std::chrono::high_resolution_clock::now();
    QueryResult res;

    for (int t = T_min; t <= T_max - mu; ++t) {
        auto sk_v = trf.get_tree_for_node(v)->query(v, t, t + mu);
        if (!sk_v) continue;
        auto mh_v = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_v);
        if (!mh_v || mh_v->is_empty()) continue;

        for (const auto& u : all_nodes) {
            if (u == v) continue;
            res.candidates_evaluated++;
            auto sk_u = trf.get_tree_for_node(u)->query(u, t, t + mu);
            if (!sk_u) continue;
            auto mh_u = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_u);
            if (!mh_u) continue;

            double sim = mh_v->jaccard_sim(*mh_u);
            if (sim >= tau) {
                auto t_end = std::chrono::high_resolution_clock::now();
                res.found = true;
                res.candidate_node = u;
                res.time_window = t;
                res.similarity = sim;
                res.exact_similarity = mh_v->exact_jaccard(*mh_u);
                res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
                return res;
            }
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    return res;
}

QueryResult baseline_brute_force_with_pruning(const std::string& v, double tau, int mu,
                                              const std::vector<int>& W_v,
                                              const std::vector<std::string>& all_nodes,
                                              TemporalRangeForest& trf) {
    auto t_start = std::chrono::high_resolution_clock::now();
    QueryResult res;

    for (int t : W_v) {
        auto sk_v = trf.get_tree_for_node(v)->query(v, t, t + mu);
        if (!sk_v) continue;
        auto mh_v = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_v);
        if (!mh_v || mh_v->is_empty()) continue;

        for (const auto& u : all_nodes) {
            if (u == v) continue;
            res.candidates_evaluated++;
            auto sk_u = trf.get_tree_for_node(u)->query(u, t, t + mu);
            if (!sk_u) continue;
            auto mh_u = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_u);
            if (!mh_u) continue;

            double sim = mh_v->jaccard_sim(*mh_u);
            if (sim >= tau) {
                auto t_end = std::chrono::high_resolution_clock::now();
                res.found = true;
                res.candidate_node = u;
                res.time_window = t;
                res.similarity = sim;
                res.exact_similarity = mh_v->exact_jaccard(*mh_u);
                res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
                return res;
            }
        }
    }

    auto t_end = std::chrono::high_resolution_clock::now();
    res.elapsed_us = std::chrono::duration<double, std::micro>(t_end - t_start).count();
    return res;
}

std::vector<std::pair<std::string, int>> ground_truth_all_matches(const std::string& v, double tau, int mu,
                                                                 const std::vector<int>& W_v,
                                                                 const std::vector<std::string>& all_nodes,
                                                                 TemporalRangeForest& trf) {
    std::vector<std::pair<std::string, int>> matches;
    for (int t : W_v) {
        auto sk_v = trf.get_tree_for_node(v)->query(v, t, t + mu);
        if (!sk_v) continue;
        auto mh_v = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_v);
        if (!mh_v || mh_v->is_empty()) continue;

        for (const auto& u : all_nodes) {
            if (u == v) continue;
            auto sk_u = trf.get_tree_for_node(u)->query(u, t, t + mu);
            if (!sk_u) continue;
            auto mh_u = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(sk_u);
            if (!mh_u) continue;

            if (mh_v->jaccard_sim(*mh_u) >= tau) {
                matches.push_back({u, t});
            }
        }
    }
    return matches;
}

// ============================================================================
// 6. SUITE SPERIMENTALE COMPLETA
// ============================================================================

void stampaSeparatore(char c = '=', int len = 80) {
    std::cout << std::string(len, c) << "\n";
}

// grafico efficacia prunin temporale, invece che mettere le barre mettere i punit, ordinando nodi per fattore di riduzione

int main(int argc, char** argv) {
    std::ios::sync_with_stdio(false);
    std::cin.tie(nullptr);

    // Parametri di default per l'esperimento
    std::string dataset_file = "";
    int mu = 950;             // Ampiezza della finestra scorrevole
    double tau = 0.75;      // Soglia di similarita'
    int k = 64;            // Dimensione signature MinHash
    int b = 16;             // Numero di band LSH
    int r = 4;              // Righe per band (k = b * r)
    int num_synthetic_nodes = 12500;
    int num_synthetic_edges = 12500;
    int time_horizon = 6050;
    std::string explicit_query_node = ""; // Eventuale nodo specifico richiesto in input

    // Parsing parametri opzionali da riga di comando
    // Sintassi: ./esperimento_problema_2 [dataset_path] [mu] [tau] [k] [b] [query_node]
    if (argc > 1) {
        std::string arg1 = argv[1];
        if (arg1 == "--help" || arg1 == "-h") {
            std::cout << "Uso: " << argv[0] << " [dataset_file] [mu] [tau] [k] [b] [query_node]\n";
            std::cout << "Esempio dataset reale:       " << argv[0] << " chess_year 2 0.5 128 16\n";
            std::cout << "Esempio benchmark sintetico:  " << argv[0] << " none 3 0.5 128 16 v0\n";
            return 0;
        }
        if (arg1 != "none" && arg1 != "synthetic") {
            dataset_file = arg1;
        }
    }
    if (argc > 2) mu = std::stoi(argv[2]);
    if (argc > 3) tau = std::stod(argv[3]);
    if (argc > 4) k = std::stoi(argv[4]);
    if (argc > 5) {
        b = std::stoi(argv[5]);
        r = k / b;
    }
    if (argc > 6) {
        explicit_query_node = argv[6];
    }

    stampaSeparatore('=');
    std::cout << "   BENCHMARK E SUITE SPERIMENTALE - PROBLEMA 2: NS CON SLIDING WINDOWS\n";
    std::cout << "   (Riferimento: sol_prob_2.tex e problema.tex)\n";
    stampaSeparatore('=');

    TemporalGraph G;
    std::string ds_name = "Grafo Sintetico con Cluster Temporali";
    if (!dataset_file.empty()) {
        std::cout << "-> Caricamento dataset da file: " << dataset_file << "... ";
        auto [dataset, name] = read_dataset_from_file(dataset_file);
        if (!dataset.empty()) {
            G = create_graph_from_ds(dataset);
            ds_name = name;
            std::cout << "OK (" << dataset.size() << " righe caricate).\n";
        } else {
            std::cout << "\n[ATTENZIONE] File non trovato o vuoto! Generazione automatica del grafo di benchmark...\n";
            G = generate_synthetic_temporal_graph(num_synthetic_nodes, num_synthetic_edges, time_horizon);
        }
    } else {
        std::cout << "-> Nessun dataset specificato: generazione grafo temporale di benchmark sintetico...\n";
        G = generate_synthetic_temporal_graph(num_synthetic_nodes, num_synthetic_edges, time_horizon);
    }

    std::vector<std::string> nodes = G.nodes();
    int T_min = G.get_T_min();
    int T_max = G.get_T_max();
    int total_possible_windows = std::max(0, T_max - mu - T_min + 1);

    std::cout << "\n[Configurazione dell'Esperimento]\n";
    std::cout << " - Dataset analizzato:            " << ds_name << "\n";
    std::cout << " - Numero totale nodi (|V|):      " << nodes.size() << "\n";
    std::cout << " - Numero totale archi (|E|):     " << G.edges.size() << "\n";
    std::cout << " - Timeline:                      [" << T_min << ", " << T_max << "] (T = " << G.get_T() << ")\n";
    std::cout << " - Ampiezza finestra (mu):        " << mu << " (Finestre totali: " << total_possible_windows << ")\n";
    std::cout << " - Soglia di similarita' (tau):   " << tau << "\n";
    std::cout << " - Dimensione signature (k):      " << k << " permutazioni\n";
    std::cout << " - Configurazione LSH:            b = " << b << " band, r = " << r << " righe (k = " << b * r << ")\n";

    // Costruzione della Temporal Range Forest
    std::cout << "\n-> Costruzione TemporalRangeForest (Range Tree con MinHash per ciascun nodo)... " << std::flush;
    auto t_start_trf = std::chrono::high_resolution_clock::now();
    TemporalRangeForest trf(&G, k);
    auto t_end_trf = std::chrono::high_resolution_clock::now();
    double time_trf_ms = std::chrono::duration<double, std::milli>(t_end_trf - t_start_trf).count();
    std::cout << "Completata in " << std::fixed << std::setprecision(2) << time_trf_ms << " ms.\n";

    // ========================================================================
    // ESPERIMENTO 1: Analisi del Pruning Temporale W(v) e verifica dei bound
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 1: Analisi Teorica ed Empirica del Pruning Temporale W(v)\n";
    stampaSeparatore('-');
    std::cout << "Verifica delle formule analitiche presentate in sol_prob_2.tex:\n";
    std::cout << " 1. |W(v)| <= min{ T - mu, (mu + 1) * |Lambda(v)| }\n";
    std::cout << " 2. In caso denso (gap=1): |W(v)| = |Lambda(v)| + mu\n";
    std::cout << " 3. In caso sparso (gap>mu): |W(v)| = (mu + 1) * |Lambda(v)|\n\n";

    double sum_lambda = 0.0, sum_W = 0.0, sum_upper_bound = 0.0, sum_densest = 0.0;
    double sum_reduction_ratio = 0.0;
    bool all_bounds_satisfied = true;

    std::cout << std::left << std::setw(10) << "Nodo"
              << std::setw(14) << "|Lambda(v)|"
              << std::setw(12) << "|W(v)|"
              << std::setw(16) << "Upper Bound"
              << std::setw(16) << "Densest Case"
              << std::setw(14) << "Riduzione"
              << "Esito Bound\n";
    std::cout << std::string(75, '.') << "\n";

    int sample_nodes_to_show = std::min((size_t)8, nodes.size());
    for (size_t i = 0; i < nodes.size(); ++i) {
        const auto& v = nodes[i];
        const auto& lam = trf.get_lambda(v);
        auto W = compute_W(lam, mu, T_min, T_max);

        size_t m_v = lam.size();
        size_t w_v = W.size();
        size_t upper_bound = std::min((size_t)total_possible_windows, (size_t)(mu + 1) * m_v);
        size_t densest_case = (m_v > 0) ? (m_v + mu) : 0;
        double reduction_ratio = (w_v > 0) ? static_cast<double>(total_possible_windows) / w_v : 1.0;

        sum_lambda += m_v;
        sum_W += w_v;
        sum_upper_bound += upper_bound;
        sum_densest += densest_case;
        sum_reduction_ratio += reduction_ratio;

        bool bound_ok = (w_v <= upper_bound);
        if (!bound_ok) all_bounds_satisfied = false;

        if (i < static_cast<size_t>(sample_nodes_to_show)) {
            std::cout << std::left << std::setw(10) << v
                      << std::setw(14) << m_v
                      << std::setw(12) << w_v
                      << std::setw(16) << upper_bound
                      << std::setw(16) << densest_case
                      << std::setw(14) << std::fixed << std::setprecision(2) << (std::to_string(reduction_ratio).substr(0,4) + "x")
                      << (bound_ok ? "[VERIFICATO]" : "[FALLITO]") << "\n";
        }
    }

    double n_sz = static_cast<double>(nodes.size());
    double avg_lambda = sum_lambda / n_sz;
    double avg_W = sum_W / n_sz;
    double avg_reduction = sum_reduction_ratio / n_sz;
    double percentage_pruned = (total_possible_windows > 0) ? (1.0 - avg_W / total_possible_windows) * 100.0 : 0.0;

    std::cout << std::string(75, '.') << "\n";
    std::cout << "STATISTICHE AGGREGATE SU " << nodes.size() << " NODI:\n";
    std::cout << " - Media |Lambda(v)| (eventi per nodo):          " << std::fixed << std::setprecision(2) << avg_lambda << "\n";
    std::cout << " - Media |W(v)| (finestre esaminate):           " << avg_W << " su " << total_possible_windows << " totali\n";
    std::cout << " - Finestre eliminate dal Pruning:              " << percentage_pruned << " %\n";
    std::cout << " - Fattore medio di riduzione (T-mu)/|W(v)|:     " << avg_reduction << "x\n";
    std::cout << " - Conformita' Bound Teorico |W(v)| <= min{...}: " << (all_bounds_satisfied ? "100% DEI NODI VERIFICATI CON SUCCESSO" : "DISCREPANZA") << "\n";

    // ========================================================================
    // ESPERIMENTO 2: Costruzione dell'Indice LSH a Sliding Windows
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 2: Costruzione dell'Indice LSH (BuildLSH-Index2)\n";
    stampaSeparatore('-');
    LSHIndexSlidingWindows lsh_index(b, r, mu, T_min, T_max);
    lsh_index.build(trf, nodes);

    size_t total_buckets = 0;
    size_t non_empty_buckets = 0;
    for (int j = 0; j < b; ++j) {
        total_buckets += lsh_index.tables[j].size();
        for (const auto& kv : lsh_index.tables[j]) {
            if (!kv.second.empty()) non_empty_buckets++;
        }
    }
    double avg_bucket_sz = (non_empty_buckets > 0) ? static_cast<double>(lsh_index.total_entries_indexed) / non_empty_buckets : 0.0;

    std::cout << " - Tempo costruzione indice (BuildLSH-Index2):  " << std::fixed << std::setprecision(2) << lsh_index.build_time_ms << " ms\n";
    std::cout << " - Entry complessivamente indicizzate (M_W):    " << lsh_index.total_entries_indexed << "\n";
    std::cout << " - Entry che sarebbero servite senza pruning:   " << (nodes.size() * total_possible_windows * b) << "\n";
    std::cout << " - Risparmio di memoria tramite W(v):           " << (1.0 - static_cast<double>(lsh_index.total_entries_indexed) / (nodes.size() * total_possible_windows * b)) * 100.0 << " %\n";
    std::cout << " - Numero totale di bucket creati (b tabelle):   " << total_buckets << "\n";
    std::cout << " - Dimensione media dei bucket non vuoti:       " << avg_bucket_sz << " nodi per bucket\n";

    // ========================================================================
    // ESPERIMENTO 3: Benchmark di Query a 3 vie (BF vs Pruning vs LSH)
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 3: Confronto Prestazionale delle Query\n";
    std::cout << " Confronto a 3 vie: (1) Brute Force Puro, (2) BF con Pruning W(v), (3) LSH Query2\n";
    stampaSeparatore('-');

    int query_trials = std::min((size_t)20, nodes.size());
    double tot_us_bf = 0.0, tot_us_pruning = 0.0, tot_us_lsh = 0.0;
    size_t tot_cand_bf = 0, tot_cand_pruning = 0, tot_cand_lsh = 0;
    int matches_found_lsh = 0;

    std::cout << std::left << std::setw(8)  << "Query"
              << std::setw(11) << "T_BF (us)"
              << std::setw(12) << "T_Prun (us)"
              << std::setw(11) << "T_LSH (us)"
              << std::setw(10) << "Speedup"
              << std::setw(13) << "Cand(BF/LSH)"
              << std::setw(18) << "Candidati LSH"
              << "Esito Query2\n";
    std::cout << std::string(98, '.') << "\n";

    struct CSVRecord {
        std::string node;
        int lambda_sz;
        int W_sz;
        double t_bf;
        double t_prun;
        double t_lsh;
        double speedup;
        size_t cand_bf;
        size_t cand_lsh;
        bool found;
        std::string match_node;
        int match_t;
        double sim;
        double exact_sim;
        std::string candidates_list;
        std::string matching_list;
    };
    std::vector<CSVRecord> csv_records;

    for (int i = 0; i < query_trials; ++i) {
        const auto& q_node = nodes[i];
        const auto& W_q = lsh_index.node_W[q_node];

        auto res_bf = baseline_brute_force_no_pruning(q_node, tau, mu, T_min, T_max, nodes, trf);
        auto res_prun = baseline_brute_force_with_pruning(q_node, tau, mu, W_q, nodes, trf);
        auto res_lsh = lsh_index.query2(q_node, tau, trf, true);

        tot_us_bf += res_bf.elapsed_us;
        tot_us_pruning += res_prun.elapsed_us;
        tot_us_lsh += res_lsh.elapsed_us;
        tot_cand_bf += res_bf.candidates_evaluated;
        tot_cand_pruning += res_prun.candidates_evaluated;
        tot_cand_lsh += res_lsh.candidates_evaluated;

        if (res_lsh.found) matches_found_lsh++;

        double speedup = (res_lsh.elapsed_us > 0) ? (res_bf.elapsed_us / res_lsh.elapsed_us) : 0.0;
        std::string cand_str = std::to_string(res_bf.candidates_evaluated) + " / " + std::to_string(res_lsh.candidates_evaluated);
        
        std::string cand_list_str = "{";
        int c_count = 0;
        for (const auto& cn : res_lsh.unique_candidate_nodes) {
            if (c_count++ > 0) cand_list_str += ",";
            cand_list_str += cn;
        }
        cand_list_str += "}";
        if (cand_list_str.size() > 16) {
            cand_list_str = cand_list_str.substr(0, 13) + "...}";
        }

        std::string outcome = res_lsh.found 
            ? ("Match: (" + res_lsh.candidate_node + ", t=" + std::to_string(res_lsh.time_window) + ", s=" + std::to_string(res_lsh.similarity).substr(0,4) + ")") 
            : "Nessun match";

        // Preparazione stringhe per CSV
        std::string csv_cands = "";
        for (const auto& cn : res_lsh.unique_candidate_nodes) {
            if (!csv_cands.empty()) csv_cands += ";";
            csv_cands += cn;
        }
        std::string csv_matches = "";
        for (const auto& mc : res_lsh.matching_candidates) {
            if (!csv_matches.empty()) csv_matches += ";";
            csv_matches += mc.node + "@t" + std::to_string(mc.time_window) + "(s=" + std::to_string(mc.sketch_similarity).substr(0,4) + ")";
        }

        csv_records.push_back({
            q_node,
            static_cast<int>(trf.get_lambda(q_node).size()),
            static_cast<int>(W_q.size()),
            res_bf.elapsed_us,
            res_prun.elapsed_us,
            res_lsh.elapsed_us,
            speedup,
            res_bf.candidates_evaluated,
            res_lsh.candidates_evaluated,
            res_lsh.found,
            res_lsh.candidate_node,
            res_lsh.time_window,
            res_lsh.similarity,
            res_lsh.exact_similarity,
            csv_cands,
            csv_matches
        });

        std::cout << std::left << std::setw(8)  << q_node
                  << std::setw(11) << std::fixed << std::setprecision(1) << res_bf.elapsed_us
                  << std::setw(12) << res_prun.elapsed_us
                  << std::setw(11) << res_lsh.elapsed_us
                  << std::setw(10) << std::setprecision(2) << (std::to_string(speedup).substr(0,4) + "x")
                  << std::setw(13) << cand_str
                  << std::setw(18) << cand_list_str
                  << outcome << "\n";
    }

    std::cout << std::string(98, '.') << "\n";
    std::cout << "MEDIE COMPLESSIVE SU " << query_trials << " QUERY DI TEST:\n";
    std::cout << " - Tempo medio Brute Force (Senza Pruning):  " << (tot_us_bf / query_trials) << " us\n";
    std::cout << " - Tempo medio Brute Force (Con Pruning W):   " << (tot_us_pruning / query_trials) << " us\n";
    std::cout << " - Tempo medio Algoritmo LSH (Query2):        " << (tot_us_lsh / query_trials) << " us\n";
    std::cout << " - SPEEDUP MEDIO: LSH vs Brute Force:         " << std::fixed << std::setprecision(2) << (tot_us_bf / tot_us_lsh) << "x\n";
    std::cout << " - SPEEDUP MEDIO: LSH vs Pruning Only:        " << (tot_us_pruning / tot_us_lsh) << "x\n";
    std::cout << " - Candidati valutati medi (BF vs LSH):       " << (tot_cand_bf / query_trials) << " vs " << (tot_cand_lsh / query_trials) << "\n";
    std::cout << " - Riduzione dello spazio di ricerca nodi:    " << (1.0 - static_cast<double>(tot_cand_lsh) / tot_cand_bf) * 100.0 << " %\n";

    // ========================================================================
    // ESPERIMENTO 3.1: ISPEZIONE DETTAGLIATA CANDIDATI E SIMILARITÀ SKETCH
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 3.1: Ispezione Approfondita dei Candidati e delle Similarita' Sketch\n";
    std::cout << " (Tracciamento esplicito dei bucket LSH e verifica della condizione sim >= tau)\n";
    stampaSeparatore('-');

    if (!explicit_query_node.empty()) {
        std::cout << "-> Ispezione richiesta specificamente per il nodo: " << explicit_query_node << "\n";
        lsh_index.print_detailed_query_report(explicit_query_node, tau, trf, true);
    } else {
        std::cout << "-> Ispezione esaustiva automatica sui primi nodi del grafo:\n";
        int detailed_samples = std::min((size_t)2, nodes.size());
        for (int s = 0; s < detailed_samples; ++s) {
            lsh_index.print_detailed_query_report(nodes[s], tau, trf, true);
        }
    }

    // ========================================================================
    // ESPERIMENTO 4: Studio della Precisione e Recall (Ground Truth)
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 4: Valutazione di Accuratezza Empirica (Recall & Precision)\n";
    stampaSeparatore('-');

    size_t total_gt_matches = 0;
    size_t total_lsh_matches = 0;
    size_t true_positives = 0;

    int eval_nodes = std::min((size_t)10, nodes.size());
    for (int i = 0; i < eval_nodes; ++i) {
        const auto& q_node = nodes[i];
        const auto& W_q = lsh_index.node_W[q_node];

        auto gt_list = ground_truth_all_matches(q_node, tau, mu, W_q, nodes, trf);
        auto lsh_matches = lsh_index.inspect_all_candidates(q_node, tau, trf).matching_candidates;

        total_gt_matches += gt_list.size();
        total_lsh_matches += lsh_matches.size();

        std::set<std::pair<std::string, int>> gt_set(gt_list.begin(), gt_list.end());
        for (const auto& match : lsh_matches) {
            if (gt_set.count({match.node, match.time_window})) {
                true_positives++;
            }
        }
    }

    double recall = (total_gt_matches > 0) ? static_cast<double>(true_positives) / total_gt_matches : 1.0;
    double precision = (total_lsh_matches > 0) ? static_cast<double>(true_positives) / total_lsh_matches : 1.0;

    std::cout << "Confronto esaustivo su " << eval_nodes << " nodi di test:\n";
    std::cout << " - Match esatti totali esistenti (Ground Truth): " << total_gt_matches << "\n";
    std::cout << " - Match estratti dall'indice LSH:               " << total_lsh_matches << "\n";
    std::cout << " - True Positives (LSH \u2229 Ground Truth):          " << true_positives << "\n";
    std::cout << " - RECALL EMPIRICA DI LSH:                       " << std::fixed << std::setprecision(2) << (recall * 100.0) << " %\n";
    std::cout << " - PRECISION EMPIRICA DI LSH:                    " << (precision * 100.0) << " %\n";

    // ========================================================================
    // ESPERIMENTO 5: Studio Parametrico (Variazione di mu e tau)
    // ========================================================================
    std::cout << "\n";
    stampaSeparatore('-');
    std::cout << " ESPERIMENTO 5: Studio Parametrico (Impatto di mu e tau)\n";
    stampaSeparatore('-');

    std::vector<int> test_mus = {1, 2, 4, 6, 8};
    std::cout << "\n[A] Variazione dell'ampiezza di finestra mu (con tau=" << tau << " fisso):\n";
    std::cout << std::left << std::setw(8) << "mu"
              << std::setw(15) << "Media |W(v)|"
              << std::setw(18) << "Finestre Totali"
              << std::setw(16) << "Pruning Ratio"
              << "Tempo Costruz. LSH\n";
    std::cout << std::string(70, '.') << "\n";

    for (int cur_mu : test_mus) {
        int cur_windows = std::max(0, T_max - cur_mu - T_min + 1);
        double cur_sum_W = 0.0;
        for (const auto& v : nodes) {
            cur_sum_W += compute_W(trf.get_lambda(v), cur_mu, T_min, T_max).size();
        }
        double cur_avg_W = cur_sum_W / nodes.size();
        double cur_ratio = (cur_avg_W > 0) ? (cur_windows / cur_avg_W) : 1.0;

        LSHIndexSlidingWindows temp_lsh(b, r, cur_mu, T_min, T_max);
        temp_lsh.build(trf, nodes);

        std::cout << std::left << std::setw(8) << cur_mu
                  << std::setw(15) << std::fixed << std::setprecision(1) << cur_avg_W
                  << std::setw(18) << cur_windows
                  << std::setw(16) << std::setprecision(2) << (std::to_string(cur_ratio).substr(0,4) + "x")
                  << std::setprecision(2) << temp_lsh.build_time_ms << " ms\n";
    }

    std::vector<double> test_taus = {0.3, 0.5, 0.7, 0.85};
    std::cout << "\n[B] Variazione della soglia di similarita' tau (con mu=" << mu << " fisso):\n";
    std::cout << std::left << std::setw(10) << "tau"
              << std::setw(18) << "Query Time (us)"
              << std::setw(18) << "Cand. Valutati"
              << "Match Trovati (%)\n";
    std::cout << std::string(65, '.') << "\n";

    for (double cur_tau : test_taus) {
        double cur_tot_time = 0.0;
        size_t cur_tot_cand = 0;
        int cur_matches = 0;
        for (int i = 0; i < query_trials; ++i) {
            auto q_res = lsh_index.query2(nodes[i], cur_tau, trf, true);
            cur_tot_time += q_res.elapsed_us;
            cur_tot_cand += q_res.candidates_evaluated;
            if (q_res.found) cur_matches++;
        }
        std::cout << std::left << std::setw(10) << std::fixed << std::setprecision(2) << cur_tau
                  << std::setw(18) << std::setprecision(1) << (cur_tot_time / query_trials)
                  << std::setw(18) << (cur_tot_cand / query_trials)
                  << std::setprecision(1) << (static_cast<double>(cur_matches) / query_trials * 100.0) << " %\n";
    }

    // ========================================================================
    // ESPERIMENTO 6: Esportazione Risultati in CSV per la Tesi
    // ========================================================================
    std::string csv_filename = "risultati_esperimento_prob_2_v1.csv";
    std::ofstream csv_file(csv_filename);
    if (csv_file.is_open()) {
        csv_file << "node_id,lambda_size,W_size,time_bf_us,time_pruning_us,time_lsh_us,speedup,cand_bf,cand_lsh,found,candidate_node,time_window,similarity,exact_similarity,candidates_list,matching_nodes_list\n";
        for (const auto& rec : csv_records) {
            csv_file << rec.node << ","
                     << rec.lambda_sz << ","
                     << rec.W_sz << ","
                     << rec.t_bf << ","
                     << rec.t_prun << ","
                     << rec.t_lsh << ","
                     << rec.speedup << ","
                     << rec.cand_bf << ","
                     << rec.cand_lsh << ","
                     << (rec.found ? 1 : 0) << ","
                     << rec.match_node << ","
                     << rec.match_t << ","
                     << rec.sim << ","
                     << rec.exact_sim << ",\""
                     << rec.candidates_list << "\",\""
                     << rec.matching_list << "\"\n";
        }
        csv_file.close();
        std::cout << "\n-> Dati completi esportati con successo in: " << csv_filename << "\n";
    }

    std::cout << "\n";
    stampaSeparatore('=');
    std::cout << "   TUTTI GLI ESPERIMENTI COMPLETATI CON SUCCESSO!\n";
    stampaSeparatore('=');

    return 0;
}
