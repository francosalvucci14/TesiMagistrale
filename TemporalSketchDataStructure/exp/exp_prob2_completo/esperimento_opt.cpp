#include <iostream>
#include <vector>
#include <map>
#include <unordered_map>
#include <unordered_set>
#include <memory>
#include <string>
#include <algorithm>
#include <fstream>
#include <sstream>
#include <random>
#include <cmath>
#include <chrono>
#include <iomanip>
#include <limits>
#include <cstdint>
#include <cstddef>
#include <filesystem>

// ============================================================================
// DEFINIZIONE TIPI
// ============================================================================
using NodeId    = uint32_t;
using TimeStamp = uint32_t;
using HashVal   = uint32_t;
using LSHKey    = uint64_t;

using Clock = std::chrono::high_resolution_clock;

static inline double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ============================================================================
// 1. GRAFO TEMPORALE
// ============================================================================

struct TemporalEdge {
    NodeId u, v;
    TimeStamp time;
};

class TemporalGraph {
public:
    std::vector<TemporalEdge> edges;
    std::unordered_map<std::string, NodeId> name_to_id;
    std::vector<std::string> id_to_name;
    TimeStamp min_time = std::numeric_limits<TimeStamp>::max();
    TimeStamp max_time = std::numeric_limits<TimeStamp>::min();

    NodeId get_or_register_node(const std::string& name) {
        auto it = name_to_id.find(name);
        if (it != name_to_id.end()) return it->second;
        NodeId new_id = static_cast<NodeId>(id_to_name.size());
        name_to_id[name] = new_id;
        id_to_name.push_back(name);
        return new_id;
    }

    void add_edge(const std::string& u_str, const std::string& v_str, TimeStamp t) {
        NodeId u = get_or_register_node(u_str);
        NodeId v = get_or_register_node(v_str);
        edges.push_back({u, v, t});
        min_time = std::min(min_time, t);
        max_time = std::max(max_time, t);
    }

    NodeId num_nodes() const { return static_cast<NodeId>(id_to_name.size()); }
    TimeStamp get_T_min() const { return (min_time == std::numeric_limits<TimeStamp>::max()) ? 0 : min_time; }
    TimeStamp get_T_max() const { return (max_time == std::numeric_limits<TimeStamp>::min()) ? 0 : max_time; }
};

TemporalGraph load_temporal_graph_from_file(const std::string& path) {
    TemporalGraph G;
    std::ifstream in(path);
    if (!in.is_open()) {
        std::cerr << "Errore: Impossibile aprire il file '" << path << "'\n";
        std::exit(1);
    }
    std::string line;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream iss(line);
        std::string u, v;
        TimeStamp t;
        if (iss >> u >> v >> t) G.add_edge(u, v, t);
    }
    return G;
}

// ============================================================================
// 2. SKETCH MINHASH (senza vicini — solo signature)
// ============================================================================

class MinHashParams {
public:
    int num_perm;
    std::vector<uint32_t> a, b;
    static constexpr uint32_t PRIME = 4294967291U;

    static const MinHashParams& get_instance(int k = 64) {
        static std::unordered_map<int, MinHashParams> instances;
        auto it = instances.find(k);
        if (it == instances.end()) it = instances.emplace(k, MinHashParams(k)).first;
        return it->second;
    }
private:
    MinHashParams(int k) : num_perm(k) {
        std::mt19937 gen(42);
        std::uniform_int_distribution<uint32_t> dist_a(1, PRIME - 1);
        std::uniform_int_distribution<uint32_t> dist_b(0, PRIME - 1);
        a.resize(num_perm); b.resize(num_perm);
        for (int i = 0; i < num_perm; ++i) { a[i] = dist_a(gen); b[i] = dist_b(gen); }
    }
};

class MinHashNeighborhoodSketch {
private:
    int num_perm;
    std::vector<HashVal> hashvalues;
    bool empty_flag;

    void update(NodeId v) {
        const auto& params = MinHashParams::get_instance(num_perm);
        uint32_t base_hash = v * 2654435761U;
        for (int i = 0; i < num_perm; ++i) {
            uint64_t val = (static_cast<uint64_t>(params.a[i]) * base_hash + params.b[i]) % MinHashParams::PRIME;
            if (static_cast<uint32_t>(val) < hashvalues[i]) hashvalues[i] = static_cast<uint32_t>(val);
        }
    }
public:
    MinHashNeighborhoodSketch(const std::vector<NodeId>& init_vicini = {}, int num_perm = 64)
        : num_perm(num_perm), hashvalues(num_perm, UINT32_MAX), empty_flag(init_vicini.empty()) {
        for (NodeId v : init_vicini) update(v);
    }

    std::shared_ptr<MinHashNeighborhoodSketch> merge(const MinHashNeighborhoodSketch& other) const {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<NodeId>{}, num_perm);
        new_sk->empty_flag = this->empty_flag && other.empty_flag;
        for (int i = 0; i < num_perm; ++i)
            new_sk->hashvalues[i] = std::min(this->hashvalues[i], other.hashvalues[i]);
        return new_sk;
    }

    std::shared_ptr<MinHashNeighborhoodSketch> clone() const {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<NodeId>{}, num_perm);
        new_sk->empty_flag = this->empty_flag;
        new_sk->hashvalues = this->hashvalues;
        return new_sk;
    }

    double jaccard_sim(const MinHashNeighborhoodSketch& other) const {
        if (hashvalues.empty() || other.hashvalues.empty()) return 0.0;
        if (hashvalues[0] == UINT32_MAX || other.hashvalues[0] == UINT32_MAX) return 0.0;
        int matches = 0;
        for (int i = 0; i < num_perm; ++i)
            if (hashvalues[i] == other.hashvalues[i]) matches++;
        return static_cast<double>(matches) / num_perm;
    }

    const std::vector<HashVal>& signature() const { return hashvalues; }
    bool is_empty() const { return empty_flag; }
};

// ============================================================================
// 3. RANGE TREE E TEMPORAL RANGE FOREST (dalla TRF v2, senza OpenMP)
// ============================================================================

struct RangeTreeNode {
    TimeStamp start_time, end_time;
    std::shared_ptr<MinHashNeighborhoodSketch> sk;
    std::unique_ptr<RangeTreeNode> left, right;
};

class RangeTree {
private:
    NodeId node_id;
    int k_perm;
    std::vector<TimeStamp> times;
    std::unique_ptr<RangeTreeNode> root;

    std::unique_ptr<RangeTreeNode> build_tree(
        const std::vector<TimeStamp>& t_sub,
        const std::vector<std::shared_ptr<MinHashNeighborhoodSketch>>& s_sub,
        size_t lo, size_t hi
    ) {
        if (lo >= hi) return nullptr;
        auto n = std::make_unique<RangeTreeNode>();
        if (hi - lo == 1) {
            n->start_time = t_sub[lo]; n->end_time = t_sub[lo];
            n->sk = s_sub[lo];
            return n;
        }
        size_t mid = lo + (hi - lo) / 2;
        n->left = build_tree(t_sub, s_sub, lo, mid);
        n->right = build_tree(t_sub, s_sub, mid, hi);
        n->start_time = n->left->start_time;
        n->end_time = n->right->end_time;
        n->sk = n->left->sk->merge(*(n->right->sk));
        return n;
    }

    std::shared_ptr<MinHashNeighborhoodSketch> query_internal(
        const std::unique_ptr<RangeTreeNode>& n, TimeStamp start, TimeStamp end
    ) const {
        if (!n) return nullptr;
        if (start <= n->start_time && n->end_time <= end) return n->sk;
        if (n->end_time < start || n->start_time > end) return nullptr;
        auto l_res = query_internal(n->left, start, end);
        auto r_res = query_internal(n->right, start, end);
        if (!l_res) return r_res;
        if (!r_res) return l_res;
        return l_res->merge(*r_res);
    }

public:
    RangeTree() = default;
    RangeTree(NodeId id, const std::map<TimeStamp, std::vector<NodeId>>& time_neighbors, int k)
        : node_id(id), k_perm(k) {
        times.reserve(time_neighbors.size());
        for (const auto& kv : time_neighbors) times.push_back(kv.first);
        if (!times.empty()) {
            std::vector<std::shared_ptr<MinHashNeighborhoodSketch>> leaves;
            leaves.reserve(times.size());
            for (TimeStamp t : times)
                leaves.push_back(std::make_shared<MinHashNeighborhoodSketch>(time_neighbors.at(t), k));
            root = build_tree(times, leaves, 0, leaves.size());
        }
    }

    std::shared_ptr<MinHashNeighborhoodSketch> query(TimeStamp start, TimeStamp end) const {
        return query_internal(root, start, end);
    }
    const std::vector<TimeStamp>& get_times() const { return times; }
};

class TemporalRangeForest {
private:
    std::vector<RangeTree> trees;
    int k_signature;
public:
    TemporalRangeForest(const TemporalGraph* G, int k = 64) : k_signature(k) {
        NodeId n = G->num_nodes();
        // Pre-indicizzazione O(|E|)
        std::vector<std::map<TimeStamp, std::vector<NodeId>>> adj(n);
        for (const auto& e : G->edges) {
            adj[e.u][e.time].push_back(e.v);
            adj[e.v][e.time].push_back(e.u);
        }
        trees.resize(n);
        for (NodeId u = 0; u < n; ++u) {
            if (!adj[u].empty()) trees[u] = RangeTree(u, adj[u], k);
        }
    }

    RangeTree* get_tree(NodeId v) {
        if (v < trees.size()) return &trees[v];
        return nullptr;
    }

    const std::vector<TimeStamp>& get_lambda(NodeId v) {
        static const std::vector<TimeStamp> empty_vec;
        auto* t = get_tree(v);
        return t ? t->get_times() : empty_vec;
    }
};

// ============================================================================
// 4. CALCOLO W(v) — RESTITUISCE INTERVALLI FUSI [lo, hi], NON singoli istanti
//
// FIX #1: guardia contro underflow uint32_t su T_max - mu
// FIX #3: rappresentazione compatta come intervalli, non lista di istanti
// ============================================================================

using Interval = std::pair<TimeStamp, TimeStamp>;

// Restituisce gli intervalli fusi di W(v)
std::vector<Interval> compute_W_intervals(
    const std::vector<TimeStamp>& lambda, TimeStamp mu, TimeStamp T_min, TimeStamp T_max
) {
    // FIX #1: guardia underflow
    if (lambda.empty() || T_max < mu || (T_max - mu) < T_min) return {};

    TimeStamp Tmax_safe = T_max - mu;  // sicuro dopo la guardia

    std::vector<Interval> intervals;
    for (TimeStamp lam : lambda) {
        TimeStamp L = (lam > mu + T_min) ? (lam - mu) : T_min;
        TimeStamp R = std::min(Tmax_safe, lam);
        if (L <= R) intervals.push_back({L, R});
    }
    if (intervals.empty()) return {};

    std::sort(intervals.begin(), intervals.end());

    std::vector<Interval> merged;
    merged.push_back(intervals[0]);
    for (size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].first <= merged.back().second + 1)
            merged.back().second = std::max(merged.back().second, intervals[i].second);
        else
            merged.push_back(intervals[i]);
    }
    return merged;
}

// Conta |W(v)| senza materializzare
size_t count_W(const std::vector<Interval>& intervals) {
    size_t total = 0;
    for (const auto& iv : intervals) total += (iv.second - iv.first + 1);
    return total;
}

// Itera su tutti i t in W(v) — usato dove serve l'enumerazione
// (costruzione LSH, query). Non li materializza tutti in un vettore.
template<typename Func>
void for_each_W(const std::vector<Interval>& intervals, Func&& func) {
    for (const auto& iv : intervals) {
        for (TimeStamp t = iv.first; t <= iv.second; ++t) func(t);
    }
}

// ============================================================================
// 5. LSH UTILITIES
// ============================================================================

static inline uint64_t hash_band_fnv(const std::vector<HashVal>& sig, int band_idx, int r) {
    uint64_t h = 14695981039346656037ULL ^ (static_cast<uint64_t>(band_idx + 1) * 1099511628211ULL);
    int start = band_idx * r;
    for (int i = 0; i < r; ++i) { h ^= sig[start + i]; h *= 1099511628211ULL; }
    return h;
}

static inline LSHKey combine_with_time(uint64_t band_hash, TimeStamp t) {
    uint64_t h = band_hash;
    h ^= static_cast<uint64_t>(t) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h;
}

struct LSHParams { int k, b, r; };
struct QueryResult { bool found = false; NodeId node = 0; TimeStamp time = 0; };

// ============================================================================
// 6. SKETCH ON-THE-FLY (nessuna cache — la TRF è veloce)
//
// FIX #4: nessuna unordered_map illimitata, nessuna frammentazione con OMP
// ============================================================================

class SketchProvider {
    TemporalRangeForest* forest;
    TimeStamp mu;
public:
    SketchProvider(TemporalRangeForest* f, TimeStamp mu_) : forest(f), mu(mu_) {}

    std::shared_ptr<MinHashNeighborhoodSketch> get(NodeId v, TimeStamp t) const {
        auto* tree = forest->get_tree(v);
        if (!tree) return nullptr;
        return tree->query(t, t + mu);
    }
};

// ============================================================================
// 7. LE 3 SOLUZIONI ALGORITMICHE
// ============================================================================

// (A) BASELINE BRUTE FORCE
// FIX #1: guardia underflow su Tmax
class NaiveSolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    NodeId n_nodes;
    const SketchProvider& provider;
public:
    NaiveSolution(NodeId num_nodes, const SketchProvider& sp, TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax)
        : mu(mu_), T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : 0),  // FIX #1
          tau(tau_), n_nodes(num_nodes), provider(sp) {}

    double auxiliary_memory_mb() const { return 0.0; }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (Tmax < T_min) return res;  // range vuoto
        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            auto sv = provider.get(v, t);
            if (!sv || sv->is_empty()) continue;
            for (NodeId u = 0; u < n_nodes; ++u) {
                if (u == v) continue;
                auto su = provider.get(u, t);
                if (!su || su->is_empty()) continue;
                if (sv->jaccard_sim(*su) >= tau) {
                    res.found = true; res.node = u; res.time = t;
                    return res;
                }
            }
        }
        return res;
    }
};

// (B) MIA SOLUZIONE (LSH Unificato + Pruning W(v))
// FIX #1: guardia underflow
// FIX #3: Wv come intervalli, non come vettori di istanti
class MySolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    const SketchProvider& provider;

    // FIX #3: W(v) come intervalli fusi invece che lista esplicita
    std::vector<std::vector<Interval>> Wv_intervals;
    std::vector<std::unordered_map<LSHKey, std::vector<NodeId>>> tables;

public:
    MySolution(NodeId num_nodes, TemporalRangeForest* f, const SketchProvider& sp,
               TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : 0),  // FIX #1
          tau(tau_), params(p), provider(sp), tables(p.b) {

        Wv_intervals.resize(num_nodes);

        for (NodeId v = 0; v < num_nodes; ++v) {
            const auto& lam = f->get_lambda(v);
            Wv_intervals[v] = compute_W_intervals(lam, mu, tmin, tmax);

            for_each_W(Wv_intervals[v], [&](TimeStamp t) {
                auto sk = provider.get(v, t);
                if (!sk || sk->is_empty()) return;
                const auto& sig = sk->signature();
                for (int j = 0; j < params.b; ++j) {
                    uint64_t bh = hash_band_fnv(sig, j, params.r);
                    LSHKey key = combine_with_time(bh, t);
                    tables[j][key].push_back(v);
                }
            });
        }
    }

    double auxiliary_memory_mb() const {
        size_t bytes = 0;
        // Wv_intervals: molto più compatto di Wv esplicito
        bytes += Wv_intervals.capacity() * sizeof(std::vector<Interval>);
        for (const auto& iv : Wv_intervals)
            bytes += iv.capacity() * sizeof(Interval);
        // LSH tables
        bytes += tables.capacity() * sizeof(std::unordered_map<LSHKey, std::vector<NodeId>>);
        for (const auto& tab : tables) {
            bytes += tab.bucket_count() * sizeof(void*);
            for (const auto& kv : tab) {
                bytes += sizeof(LSHKey) + sizeof(std::vector<NodeId>) + (3 * sizeof(void*));
                bytes += kv.second.capacity() * sizeof(NodeId);
            }
        }
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (v >= Wv_intervals.size()) return res;

        for (const auto& iv : Wv_intervals[v]) {
            for (TimeStamp t = iv.first; t <= iv.second; ++t) {
                auto sv = provider.get(v, t);
                if (!sv || sv->is_empty()) continue;
                const auto& sig = sv->signature();
                std::unordered_set<NodeId> cand;
                for (int j = 0; j < params.b; ++j) {
                    uint64_t bh = hash_band_fnv(sig, j, params.r);
                    LSHKey key = combine_with_time(bh, t);
                    auto tit = tables[j].find(key);
                    if (tit != tables[j].end())
                        for (NodeId u : tit->second) if (u != v) cand.insert(u);
                }
                for (NodeId u : cand) {
                    auto su = provider.get(u, t);
                    if (!su || su->is_empty()) continue;
                    if (sv->jaccard_sim(*su) >= tau) {
                        res.found = true; res.node = u; res.time = t;
                        return res;
                    }
                }
            }
        }
        return res;
    }
};

// (C) ALTERNATIVA (T-mu strutture LSH indipendenti)
// FIX #1: guardia underflow
// FIX #2: usa unordered_map<TimeStamp, ...> SPARSO invece di array denso
class AlternativeSolution {
    using BandTables = std::vector<std::unordered_map<uint64_t, std::vector<NodeId>>>;

    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    const SketchProvider& provider;

    // FIX #2: mappa sparsa timestamp → LSH, anziché vettore denso [0..T-mu]
    std::unordered_map<TimeStamp, std::unique_ptr<BandTables>> tables;

public:
    AlternativeSolution(NodeId num_nodes, TemporalRangeForest* f, const SketchProvider& sp,
                        TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : 0),  // FIX #1
          tau(tau_), params(p), provider(sp) {

        for (NodeId v = 0; v < num_nodes; ++v) {
            const auto& lam = f->get_lambda(v);
            auto w_ivs = compute_W_intervals(lam, mu, tmin, tmax);

            for_each_W(w_ivs, [&](TimeStamp t) {
                if (t < T_min || t > Tmax) return;
                auto sk = provider.get(v, t);
                if (!sk || sk->is_empty()) return;
                const auto& sig = sk->signature();

                // FIX #2: allocazione lazy per timestamp
                auto& slot = tables[t];
                if (!slot) slot = std::make_unique<BandTables>(params.b);

                for (int j = 0; j < params.b; ++j) {
                    uint64_t bh = hash_band_fnv(sig, j, params.r);
                    (*slot)[j][bh].push_back(v);
                }
            });
        }
    }

    double auxiliary_memory_mb() const {
        size_t bytes = 0;
        bytes += tables.bucket_count() * sizeof(void*);
        for (const auto& [ts, ptr] : tables) {
            if (!ptr) continue;
            bytes += sizeof(TimeStamp) + sizeof(std::unique_ptr<BandTables>) + 3 * sizeof(void*);
            const auto& bands = *ptr;
            bytes += bands.capacity() * sizeof(std::unordered_map<uint64_t, std::vector<NodeId>>);
            for (const auto& tab : bands) {
                bytes += tab.bucket_count() * sizeof(void*);
                for (const auto& kv : tab) {
                    bytes += sizeof(uint64_t) + sizeof(std::vector<NodeId>) + 3 * sizeof(void*);
                    bytes += kv.second.capacity() * sizeof(NodeId);
                }
            }
        }
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (Tmax < T_min) return res;

        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            auto sv = provider.get(v, t);
            if (!sv || sv->is_empty()) continue;
            const auto& sig = sv->signature();

            auto map_it = tables.find(t);
            if (map_it == tables.end() || !map_it->second) continue;

            const BandTables& bands = *(map_it->second);
            std::unordered_set<NodeId> cand;
            for (int j = 0; j < params.b; ++j) {
                uint64_t bh = hash_band_fnv(sig, j, params.r);
                auto tit = bands[j].find(bh);
                if (tit != bands[j].end())
                    for (NodeId u : tit->second) if (u != v) cand.insert(u);
            }
            for (NodeId u : cand) {
                auto su = provider.get(u, t);
                if (!su || su->is_empty()) continue;
                if (sv->jaccard_sim(*su) >= tau) {
                    res.found = true; res.node = u; res.time = t;
                    return res;
                }
            }
        }
        return res;
    }
};

// ============================================================================
// MAIN
// ============================================================================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Uso: " << argv[0] << " <dataset_path> <dataset_name> [mu] [tau] [k] [b] [num_queries]\n";
        return 1;
    }

    std::string dataset_path = argv[1];
    std::string dataset_name = argv[2];
    TimeStamp mu  = (argc > 3) ? static_cast<TimeStamp>(std::stoul(argv[3])) : 5;
    double    tau = (argc > 4) ? std::stod(argv[4]) : 0.40;
    int k         = (argc > 5) ? std::stoi(argv[5]) : 64;
    int b         = (argc > 6) ? std::stoi(argv[6]) : 8;
    int num_queries = (argc > 7) ? std::stoi(argv[7]) : 30;
    int r = k / b;

    std::filesystem::create_directories("results");

    std::cout << "===============================================================\n";
    std::cout << " ESPERIMENTO PROBLEMA 2 - DATASET: " << dataset_name << "\n";
    std::cout << "===============================================================\n";

    // --- Caricamento grafo ---
    TemporalGraph G = load_temporal_graph_from_file(dataset_path);
    NodeId total_nodes = G.num_nodes();
    TimeStamp T_min = G.get_T_min();
    TimeStamp T_max = G.get_T_max();

    // FIX #1: calcolo sicuro total_windows
    TimeStamp total_windows = (T_max >= mu + T_min) ? (T_max - mu - T_min + 1) : 0;

    std::cout << "Archi: " << G.edges.size() << " | Nodi: " << total_nodes
              << " | T_min: " << T_min << " | T_max: " << T_max
              << " | mu: " << mu
              << " | Finestre totali (T-mu): " << total_windows << "\n";

    if (total_windows == 0) {
        std::cerr << "[ATTENZIONE] mu >= range temporale. Nessuna finestra generabile.\n";
    }

    // --- Costruzione TRF (dalla tua v2, senza OpenMP) ---
    std::cout << "Costruzione TemporalRangeForest...\n";
    auto t_trf = Clock::now();
    TemporalRangeForest trf(&G, k);
    std::cout << "  TRF costruita in " << us_since(t_trf) / 1000.0 << " ms\n";

    // ========================================================================
    // 1. ANALISI W(v) E FATTORE DI GUADAGNO
    // ========================================================================
    std::string wv_filename = "results/" + dataset_name + "_wv_analysis.csv";
    std::ofstream wv_file(wv_filename);
    wv_file << "node_id,lambda_size,w_size,upper_bound,lower_bound,no_pruning_windows,gain_factor\n";

    for (NodeId v = 0; v < total_nodes; ++v) {
        const auto& lam = trf.get_lambda(v);
        auto w_ivs = compute_W_intervals(lam, mu, T_min, T_max);
        size_t m_v = lam.size();
        size_t w_v = count_W(w_ivs);

        size_t ub = std::min(static_cast<size_t>(total_windows), static_cast<size_t>(mu + 1) * m_v);
        size_t lb = (m_v > 0) ? std::min(static_cast<size_t>(total_windows), m_v + mu) : 0;
        double gain = (w_v > 0) ? static_cast<double>(total_windows) / w_v : 1.0;

        wv_file << G.id_to_name[v] << "," << m_v << "," << w_v << ","
                << ub << "," << lb << "," << total_windows << "," << gain << "\n";
    }
    wv_file.close();
    std::cout << "-> File W(v) salvato in: " << wv_filename << "\n";

    // ========================================================================
    // 2. COSTRUZIONE DELLE 3 STRUTTURE
    // ========================================================================
    std::cout << "Costruzione indici e strutture...\n";
    SketchProvider provider(&trf, mu);

    auto t_build = Clock::now();
    NaiveSolution naive(total_nodes, provider, mu, tau, T_min, T_max);
    std::cout << "  NaiveSolution: " << us_since(t_build) / 1000.0 << " ms\n";

    t_build = Clock::now();
    MySolution mine(total_nodes, &trf, provider, mu, tau, T_min, T_max, {k, b, r});
    std::cout << "  MySolution: " << us_since(t_build) / 1000.0 << " ms\n";

    t_build = Clock::now();
    AlternativeSolution alt(total_nodes, &trf, provider, mu, tau, T_min, T_max, {k, b, r});
    std::cout << "  AlternativeSolution: " << us_since(t_build) / 1000.0 << " ms\n";

    double mem_naive_mb = naive.auxiliary_memory_mb();
    double mem_mine_mb  = mine.auxiliary_memory_mb();
    double mem_alt_mb   = alt.auxiliary_memory_mb();

    // Liberazione memoria del grafo
    { std::vector<TemporalEdge>().swap(G.edges); }
    { std::unordered_map<std::string, NodeId>().swap(G.name_to_id); }
    { std::vector<std::string>().swap(G.id_to_name); }

    // ========================================================================
    // 3. ESECUZIONE QUERY
    // ========================================================================
    std::mt19937 rng(42);
    std::uniform_int_distribution<NodeId> dist(0, total_nodes - 1);
    int queries_to_run = std::min(static_cast<NodeId>(num_queries), total_nodes);

    double tot_time_naive = 0.0, tot_time_mine = 0.0, tot_time_alt = 0.0;

    for (int q = 0; q < queries_to_run; ++q) {
        NodeId q_node = dist(rng);

        auto t0 = Clock::now();
        naive.query(q_node);
        tot_time_naive += us_since(t0);

        t0 = Clock::now();
        mine.query(q_node);
        tot_time_mine += us_since(t0);

        t0 = Clock::now();
        alt.query(q_node);
        tot_time_alt += us_since(t0);
    }

    double avg_time_naive = tot_time_naive / queries_to_run;
    double avg_time_mine  = tot_time_mine / queries_to_run;
    double avg_time_alt   = tot_time_alt / queries_to_run;

    // ========================================================================
    // 4. ESPORTAZIONE
    // ========================================================================
    std::string summary_filename = "results/" + dataset_name + "_summary.csv";
    std::ofstream sum_file(summary_filename);
    sum_file << "dataset,algorithm,avg_query_time_us,memory_mb\n";
    sum_file << dataset_name << ",BruteForce,"   << avg_time_naive << "," << mem_naive_mb << "\n";
    sum_file << dataset_name << ",MiaSoluzione," << avg_time_mine  << "," << mem_mine_mb  << "\n";
    sum_file << dataset_name << ",Alternativa,"  << avg_time_alt   << "," << mem_alt_mb   << "\n";
    sum_file.close();

    std::cout << "-> File metriche salvato in: " << summary_filename << "\n";
    std::cout << "Esecuzione completata per il dataset: " << dataset_name << "\n";

    return 0;
}
