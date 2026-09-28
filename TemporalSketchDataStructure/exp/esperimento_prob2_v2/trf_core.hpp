// =============================================================================
// trf_core.hpp
// -----------------------------------------------------------------------------
// Adattamento "silenzioso" (nessuna stampa di debug) di TemporalRangeForest.cpp
// fornito dall'utente. Contiene la struttura dati black-box usata come base per
// TUTTI e tre gli algoritmi confrontati nell'esperimento (naive, soluzione LSH
// unificata con chiave aumentata, soluzione con T-mu LSH distinte):
//
//   - TemporalGraph            : grafo temporale G=(V,E,lambda)
//   - MinHashNeighborhoodSketch: sketch del vicinato N^I(v), usato per LSH
//   - RangeTree / TemporalRangeForest : struttura dati "black box" RT_v che,
//     data una richiesta di intervallo [start,end], ritorna lo sketch
//     S(v,[start,end]) = Sketch(N^{[start,end]}(v)) con costo (assunto)
//     O(k log|Lambda(v)|), esattamente come descritto in sol_prob_2.tex.
//
// Le uniche modifiche rispetto al file originale sono:
//   1) Rimozione di tutte le std::cout di debug (costruzione RangeTree, query,
//      "nodo canonico recuperato", ecc.) che altrimenti renderebbero
//      inutilizzabile qualunque benchmark su tanti nodi/query.
//   2. Rimozione della parte HyperLogLog e del binding pybind11 (non servono
//      per l'esperimento: l'algoritmo LSH del capitolo richiede una signature
//      "bandabile", quindi si usa esclusivamente MinHash).
//   3. Aggiunta di un getter pubblico signature()/size() sullo sketch MinHash,
//      necessario per poter suddividere la signature in b band da r righe.
//
// La complessità e la semantica della struttura dati (RangeTree "black box")
// NON sono state alterate.
// =============================================================================
#pragma once

#include <vector>
#include <set>
#include <map>
#include <unordered_map>
#include <memory>
#include <string>
#include <algorithm>
#include <functional>
#include <random>
#include <cstdint>

// ============================================================================
// STRUTTURE DEL GRAFO
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

    void add_edge(const std::string& u, const std::string& v, int time) {
        edges.push_back({u, v, time});
        nodes_set.insert(u);
        nodes_set.insert(v);
    }

    std::vector<std::string> nodes() const {
        return std::vector<std::string>(nodes_set.begin(), nodes_set.end());
    }
};

// ============================================================================
// SKETCH DEL VICINATO (MinHash)
// ============================================================================

class NeighborhoodSketch {
public:
    std::set<std::string> vicini;

    virtual ~NeighborhoodSketch() = default;
    virtual std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const = 0;
    virtual std::shared_ptr<NeighborhoodSketch> clone() const = 0;
};

class MinHashNeighborhoodSketch : public NeighborhoodSketch {
private:
    int num_perm;
    std::vector<uint32_t> hashvalues;

    static const uint32_t PRIME = 4294967291u; // 2^32 - 5
    std::vector<uint32_t> a, b;

    void init_hash_functions() {
        std::mt19937 gen(42); // Seed fisso per consistenza tra diversi sketch
        std::uniform_int_distribution<uint32_t> dist_a(1, PRIME - 1);
        std::uniform_int_distribution<uint32_t> dist_b(0, PRIME - 1);
        for (int i = 0; i < num_perm; ++i) {
            a.push_back(dist_a(gen));
            b.push_back(dist_b(gen));
        }
    }

    void update(const std::string& v) {
        uint32_t base_hash = std::hash<std::string>{}(v);
        for (int i = 0; i < num_perm; ++i) {
            uint64_t hash_val = (static_cast<uint64_t>(a[i]) * base_hash + b[i]) % PRIME;
            if (hash_val < hashvalues[i]) {
                hashvalues[i] = static_cast<uint32_t>(hash_val);
            }
        }
    }

public:
    MinHashNeighborhoodSketch(const std::vector<std::string>& init_vicini = {}, int num_perm = 128)
        : num_perm(num_perm), hashvalues(num_perm, UINT32_MAX) {
        init_hash_functions();
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

    double jaccard_sim(const MinHashNeighborhoodSketch& other_sk) const {
        int matches = 0;
        for (int i = 0; i < num_perm; ++i) {
            if (this->hashvalues[i] == other_sk.hashvalues[i]) matches++;
        }
        return static_cast<double>(matches) / num_perm;
    }

    // --- Aggiunta rispetto all'originale: necessaria per il banding LSH ---
    const std::vector<uint32_t>& signature() const { return hashvalues; }
    int signature_size() const { return num_perm; }
};

using SketchFactory = std::function<std::shared_ptr<NeighborhoodSketch>(const std::vector<std::string>&)>;

// ============================================================================
// RANGE TREE (una istanza per nodo) + TEMPORAL RANGE FOREST
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
    const TemporalGraph* temporal_graph;
    SketchFactory sketch_factory;
    std::vector<int> times;
    std::shared_ptr<RangeTreeNode> root;

    std::vector<int> _extract_times() {
        std::set<int> unique_times;
        for (const auto& edge : temporal_graph->edges) {
            if (edge.u == node_id || edge.v == node_id) unique_times.insert(edge.time);
        }
        return std::vector<int>(unique_times.begin(), unique_times.end());
    }

    std::vector<std::string> _vicini_at_time(int t) {
        std::set<std::string> vicini;
        for (const auto& edge : temporal_graph->edges) {
            if (edge.time != t) continue;
            if (edge.u == node_id) vicini.insert(edge.v);
            else if (edge.v == node_id) vicini.insert(edge.u);
        }
        return std::vector<std::string>(vicini.begin(), vicini.end());
    }

    std::vector<std::shared_ptr<NeighborhoodSketch>> _build_leaf_sks() {
        std::vector<std::shared_ptr<NeighborhoodSketch>> sks;
        for (int t : times) sks.push_back(sketch_factory(_vicini_at_time(t)));
        return sks;
    }

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
        if (start <= node->start_time && node->end_time <= end) return node->sk;
        if (node->end_time < start || node->start_time > end) return nullptr;

        auto left_res = _query(node->left, start, end);
        auto right_res = _query(node->right, start, end);
        if (!left_res) return right_res;
        if (!right_res) return left_res;
        return left_res->merge(*right_res);
    }

public:
    RangeTree(std::string id, const TemporalGraph* graph, SketchFactory factory)
        : node_id(std::move(id)), temporal_graph(graph), sketch_factory(std::move(factory)) {
        times = _extract_times();
        if (!times.empty()) {
            auto leaf_sks = _build_leaf_sks();
            root = _build_tree(times, leaf_sks);
        }
    }

    // Query per l'intervallo [start,end] (assunta O(k log|Lambda(v)|), come da tesi)
    std::shared_ptr<NeighborhoodSketch> query(const std::string& /*label*/, int start, int end) {
        return _query(root, start, end);
    }

    const std::vector<int>& incident_times() const { return times; }
};

class TemporalRangeForest {
private:
    const TemporalGraph* temporal_graph;
    SketchFactory sk_factory;
    std::map<std::string, RangeTree> trees;

    std::vector<std::string> _extract_nodes() {
        std::set<std::string> nodes;
        for (const auto& edge : temporal_graph->edges) {
            nodes.insert(edge.u);
            nodes.insert(edge.v);
        }
        return std::vector<std::string>(nodes.begin(), nodes.end());
    }

public:
    // k = lunghezza della signature MinHash (= b*r nell'algoritmo LSH)
    TemporalRangeForest(const TemporalGraph* graph, int minhash_k = 64)
        : temporal_graph(graph) {
        sk_factory = [minhash_k](const std::vector<std::string>& vicini) {
            return std::make_shared<MinHashNeighborhoodSketch>(vicini, minhash_k);
        };
        auto nodes = _extract_nodes();
        for (const auto& node : nodes) {
            trees.emplace(node, RangeTree(node, temporal_graph, sk_factory));
        }
    }

    RangeTree* get_tree_for_node(const std::string& node) {
        auto it = trees.find(node);
        if (it == trees.end()) return nullptr;
        return &it->second;
    }
};
