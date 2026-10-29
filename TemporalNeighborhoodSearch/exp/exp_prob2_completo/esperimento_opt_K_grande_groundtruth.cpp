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
#include <cstring>
#include <cstdlib>

using NodeId    = uint32_t;
using TimeStamp = uint32_t;
using HashVal   = uint32_t;
using LSHKey    = uint64_t;

using Clock = std::chrono::high_resolution_clock;

static inline double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// ============================================================================
// 1. CARICAMENTO GRAFO (Legge direttamente l'output di normalize_timestamps.cpp)
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
    std::string line, u, v;
    TimeStamp t;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream iss(line);
        if (iss >> u >> v >> t) G.add_edge(u, v, t);
    }
    return G;
}

// ============================================================================
// 2. MINHASH CON SIGNATURE COMPATTE
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

    const std::vector<HashVal>& signature() const { return hashvalues; }
    bool is_empty() const { return empty_flag; }
};

// ============================================================================
// 3. RANGE TREE SU STRUTTURA AD ALBERO OTTIMIZZATA
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

    void collect(const RangeTreeNode* n, TimeStamp start, TimeStamp end,
                 HashVal* out, int off, int len, bool& any) const {
        if (!n) return;
        if (n->end_time < start || n->start_time > end) return;
        if (start <= n->start_time && n->end_time <= end) {
            const HashVal* s = n->sk->signature().data() + off;
            if (!any) { std::memcpy(out, s, sizeof(HashVal) * len); any = true; }
            else for (int i = 0; i < len; ++i) out[i] = std::min(out[i], s[i]);
            return;
        }
        collect(n->left.get(), start, end, out, off, len, any);
        collect(n->right.get(), start, end, out, off, len, any);
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

    bool query_range(TimeStamp start, TimeStamp end, HashVal* out, int off, int len) const {
        bool any = false;
        collect(root.get(), start, end, out, off, len, any);
        return any;
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

    const RangeTree* get_tree(NodeId v) const {
        return (v < trees.size()) ? &trees[v] : nullptr;
    }

    const std::vector<TimeStamp>& get_lambda(NodeId v) const {
        static const std::vector<TimeStamp> empty_vec;
        return (v < trees.size()) ? trees[v].get_times() : empty_vec;
    }
};

// Vicinati REALI del grafo temporale: usati solo per valutare la ground truth
// e le risposte LSH. L'indice tiene (tempo, vicino) per nodo e non materializza
// i vicinati di tutte le possibili finestre, che occuperebbero molta RAM.
class ExactTemporalNeighborhoods {
    using TimedNeighbor = std::pair<TimeStamp, NodeId>;
    std::vector<std::vector<TimedNeighbor>> adjacency;

public:
    explicit ExactTemporalNeighborhoods(const TemporalGraph& graph)
        : adjacency(graph.num_nodes()) {
        for (const auto& e : graph.edges) {
            adjacency[e.u].emplace_back(e.time, e.v);
            adjacency[e.v].emplace_back(e.time, e.u);
        }
        for (auto& a : adjacency) std::sort(a.begin(), a.end());
    }

    void neighborhood(NodeId v, TimeStamp t, TimeStamp mu,
                      std::vector<NodeId>& output) const {
        output.clear();
        if (v >= adjacency.size()) return;
        const auto& a = adjacency[v];
        auto it = std::lower_bound(
            a.begin(), a.end(), TimedNeighbor{t, 0});
        const uint64_t end = static_cast<uint64_t>(t) + mu;
        for (; it != a.end() && it->first <= end; ++it)
            output.push_back(it->second);
        std::sort(output.begin(), output.end());
        output.erase(std::unique(output.begin(), output.end()), output.end());
    }

    static bool similar(const std::vector<NodeId>& a,
                        const std::vector<NodeId>& b, double tau) {
        // Per coerenza con le query originali, le finestre prive di vicini
        // non rappresentano risultati; evita J(empty, empty) ambiguo.
        if (a.empty() || b.empty()) return false;
        size_t intersection = 0;
        auto ia = a.begin(), ib = b.begin();
        while (ia != a.end() && ib != b.end()) {
            if (*ia == *ib) { ++intersection; ++ia; ++ib; }
            else if (*ia < *ib) ++ia;
            else ++ib;
        }
        const size_t cardinality_union = a.size() + b.size() - intersection;
        return static_cast<double>(intersection) /
                   static_cast<double>(cardinality_union) >= tau;
    }
};

// ============================================================================
// 4. CALCOLO W(v) CON INTERVALLI COMPATTI
// ============================================================================

using Interval = std::pair<TimeStamp, TimeStamp>;

std::vector<Interval> compute_W_intervals(
    const std::vector<TimeStamp>& lambda, TimeStamp mu, TimeStamp T_min, TimeStamp T_max
) {
    if (lambda.empty() || T_max < mu || (T_max - mu) < T_min) return {};
    TimeStamp Tmax_safe = T_max - mu;

    std::vector<Interval> intervals;
    for (TimeStamp lam : lambda) {
        TimeStamp L = (lam >= mu + T_min) ? (lam - mu) : T_min;
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

size_t count_W(const std::vector<Interval>& intervals) {
    size_t total = 0;
    for (const auto& iv : intervals) total += (iv.second - iv.first + 1);
    return total;
}

struct WindowCursor {
    const std::vector<TimeStamp>& ts;
    size_t lo = 0, hi = 0;
    explicit WindowCursor(const std::vector<TimeStamp>& v) : ts(v) {}
    void advance(TimeStamp t, TimeStamp mu) {
        const size_t n = ts.size();
        while (lo < n && ts[lo] < t) ++lo;
        if (hi < lo) hi = lo;
        const uint64_t end = static_cast<uint64_t>(t) + mu;
        while (hi < n && ts[hi] <= end) ++hi;
    }
};

// ============================================================================
// 5. UTILITY LSH ED ENTRY COMPATTE (12 BYTE)
// ============================================================================

static inline uint64_t hash_band_ptr(const HashVal* band, int band_idx, int r) {
    uint64_t h = 14695981039346656037ULL ^ (static_cast<uint64_t>(band_idx + 1) * 1099511628211ULL);
    for (int i = 0; i < r; ++i) { h ^= band[i]; h *= 1099511628211ULL; }
    return h;
}

static inline uint64_t hash_band_fnv(const HashVal* sig, int band_idx, int r) {
    return hash_band_ptr(sig + static_cast<size_t>(band_idx) * r, band_idx, r);
}

static inline LSHKey combine_with_time(uint64_t band_hash, TimeStamp t) {
    uint64_t h = band_hash + static_cast<uint64_t>(t) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 30; h *= 0xBF58476D1CE4E5B9ULL;
    h ^= h >> 27; h *= 0x94D049BB133111EBULL;
    h ^= h >> 31;
    return h;
}

struct LSHParams { int k, b, r; };
struct QueryResult { bool found = false; NodeId node = 0; TimeStamp time = 0; };

static inline int need_matches(int k, double tau) {
    int m = 0;
    while (m <= k && static_cast<double>(m) / k < tau) ++m;
    return m;
}

static inline bool sig_match_ge(const HashVal* a, const HashVal* b, int k, int need) {
    int m = 0;
    for (int i0 = 0; i0 < k; i0 += 16) {
        const int i1 = std::min(k, i0 + 16);
        for (int i = i0; i < i1; ++i) m += (a[i] == b[i]);
        if (m + (k - i1) < need) return false;
    }
    return m >= need;
}

#pragma pack(push, 4)
struct LSHEntry { uint64_t key; NodeId node; };
struct AltEntry { uint64_t hash; NodeId node; };
#pragma pack(pop)

// Indice Piatto per la Mia Soluzione (LSH Unificato)
class FlatBandIndex {
    int shift = 63;
    std::vector<uint32_t> dir;
    std::vector<LSHEntry> ents;
public:
    void build(std::vector<LSHEntry>&& es) {
        const size_t N = es.size();
        if (N == 0) return;
        int p = 1;
        while (p < 28 && (static_cast<size_t>(1) << p) < N / 4 + 1) ++p;
        shift = 64 - p;
        const size_t B = static_cast<size_t>(1) << p;
        dir.assign(B + 1, 0);
        for (const auto& e : es) ++dir[(e.key >> shift) + 1];
        for (size_t i = 0; i < B; ++i) dir[i + 1] += dir[i];
        {
            std::vector<uint32_t> pos(dir.begin(), dir.end() - 1);
            ents.resize(N);
            for (const auto& e : es) ents[pos[e.key >> shift]++] = e;
        }
        std::vector<LSHEntry>().swap(es);
        for (size_t i = 0; i < B; ++i) {
            const uint32_t lo = dir[i], hi = dir[i + 1];
            if (hi - lo > 1) {
                std::sort(ents.begin() + lo, ents.begin() + hi,
                          [](const LSHEntry& x, const LSHEntry& y) { return x.key < y.key; });
            }
        }
    }

    template <class F>
    bool for_each(uint64_t key, F&& f) const {
        if (ents.empty()) return false;
        const size_t bkt = key >> shift;
        uint32_t lo = dir[bkt], hi = dir[bkt + 1];
        if (hi - lo > 16) {
            lo = static_cast<uint32_t>(std::lower_bound(ents.begin() + lo, ents.begin() + hi, key,
                    [](const LSHEntry& e, uint64_t k) { return e.key < k; }) - ents.begin());
        }
        for (uint32_t i = lo; i < hi; ++i) {
            if (ents[i].key > key) break;
            if (ents[i].key == key && f(ents[i].node)) return true;
        }
        return false;
    }

    size_t memory_bytes() const {
        return ents.capacity() * sizeof(LSHEntry) + dir.capacity() * sizeof(uint32_t);
    }
};

// Indice CSR Piatto per l'Alternativa (T-mu strutture LSH senza overhead di hash map)
class FlatAlternativeIndex {
    std::vector<uint32_t> offsets;
    std::vector<AltEntry> entries;
public:
    void build_from_raw(uint32_t num_windows, std::vector<std::pair<uint32_t, AltEntry>>& raw_entries) {
        offsets.assign(num_windows + 1, 0);
        for (const auto& re : raw_entries) {
            if (re.first < num_windows) offsets[re.first + 1]++;
        }
        for (size_t i = 0; i < num_windows; ++i) offsets[i + 1] += offsets[i];

        entries.resize(raw_entries.size());
        std::vector<uint32_t> cur_pos(offsets.begin(), offsets.end() - 1);
        for (const auto& re : raw_entries) {
            if (re.first < num_windows) {
                entries[cur_pos[re.first]++] = re.second;
            }
        }
        std::vector<std::pair<uint32_t, AltEntry>>().swap(raw_entries);

        for (size_t w = 0; w < num_windows; ++w) {
            uint32_t lo = offsets[w], hi = offsets[w + 1];
            if (hi - lo > 1) {
                std::sort(entries.begin() + lo, entries.begin() + hi,
                          [](const AltEntry& x, const AltEntry& y) { return x.hash < y.hash; });
            }
        }
    }

    template <class F>
    bool for_each_in_window(uint32_t w, uint64_t hash, F&& f) const {
        if (w >= offsets.size() - 1) return false;
        uint32_t lo = offsets[w], hi = offsets[w + 1];
        if (lo >= hi) return false;

        auto it_lo = std::lower_bound(entries.begin() + lo, entries.begin() + hi, hash,
                                      [](const AltEntry& e, uint64_t h) { return e.hash < h; });
        for (auto it = it_lo; it != entries.begin() + hi && it->hash == hash; ++it) {
            if (f(it->node)) return true;
        }
        return false;
    }

    size_t memory_bytes() const {
        return offsets.capacity() * sizeof(uint32_t) + entries.capacity() * sizeof(AltEntry);
    }
};

class SketchProvider {
    const TemporalRangeForest* forest;
    TimeStamp mu;
    int k;
public:
    SketchProvider(const TemporalRangeForest* f, TimeStamp mu_, int k_) : forest(f), mu(mu_), k(k_) {}
    bool get_into(NodeId v, TimeStamp t, HashVal* out) const {
        const auto* tree = forest->get_tree(v);
        return tree && tree->query_range(t, t + mu, out, 0, k);
    }
};

// ============================================================================
// 6. LE 3 SOLUZIONI ALGORITMICHE (ZERO-OOM GUARANTEED)
// ============================================================================

// (A) BASELINE BRUTE FORCE
class NaiveSolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    NodeId n_nodes;
    int k, need;
    const SketchProvider& provider;
    mutable std::vector<HashVal> sv, su;
public:
    NaiveSolution(NodeId num_nodes, const SketchProvider& sp, TimeStamp mu_, double tau_,
                  TimeStamp tmin, TimeStamp tmax, int k_)
        : mu(mu_), T_min(tmin), Tmax(tmax >= mu_ ? tmax - mu_ : 0),
          tau(tau_), n_nodes(num_nodes), k(k_), need(need_matches(k_, tau_)),
          provider(sp), sv(k_), su(k_) {}

    double auxiliary_memory_mb() const { return 0.0; }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (Tmax < T_min) return res;
        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            if (!provider.get_into(v, t, sv.data())) continue;
            for (NodeId u = 0; u < n_nodes; ++u) {
                if (u == v) continue;
                if (!provider.get_into(u, t, su.data())) continue;
                if (sig_match_ge(sv.data(), su.data(), k, need)) {
                    res.found = true; res.node = u; res.time = t;
                    return res;
                }
            }
        }
        return res;
    }

    // Ground truth ESATTA sulla Jaccard dei vicinati temporali ORIGINALI:
    // enumera tutte le coppie (u,t), u != v, senza fermarsi alla prima.
    // Conserviamo solo la cardinalita' per evitare un insieme O(n*T) in RAM.
    uint64_t count_all(NodeId v, const ExactTemporalNeighborhoods& exact) const {
        if (Tmax < T_min) return 0;
        uint64_t matches = 0;
        std::vector<NodeId> nv, nu;
        for (uint64_t ti = T_min; ti <= Tmax; ++ti) {
            const TimeStamp t = static_cast<TimeStamp>(ti);
            exact.neighborhood(v, t, mu, nv);
            if (nv.empty()) continue;
            for (NodeId u = 0; u < n_nodes; ++u) {
                if (u == v) continue;
                exact.neighborhood(u, t, mu, nu);
                if (ExactTemporalNeighborhoods::similar(nv, nu, tau)) ++matches;
            }
        }
        return matches;
    }
};

// (B) MIA SOLUZIONE (LSH Unificato + Pruning W(v))
class MySolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    int need;
    const TemporalRangeForest* forest;
    const SketchProvider& provider;

    std::vector<Interval> W_flat;
    std::vector<size_t>   W_off;
    std::vector<FlatBandIndex> index;

    mutable std::vector<HashVal> sv, su;
    mutable std::vector<uint64_t> bh;
    mutable std::vector<uint32_t> seen;
    mutable uint32_t epoch = 0;

public:
    MySolution(NodeId num_nodes, const TemporalRangeForest* f, const SketchProvider& sp,
               TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin), Tmax(tmax >= mu_ ? tmax - mu_ : 0),
          tau(tau_), params(p), need(need_matches(p.k, tau_)),
          forest(f), provider(sp), index(p.b),
          sv(p.k), su(p.k), bh(p.b), seen(num_nodes, 0) {

        W_off.assign(static_cast<size_t>(num_nodes) + 1, 0);
        size_t total_pairs = 0;
        for (NodeId v = 0; v < num_nodes; ++v) {
            auto ivs = compute_W_intervals(f->get_lambda(v), mu, tmin, tmax);
            for (const auto& iv : ivs) total_pairs += (iv.second - iv.first + 1);
            W_flat.insert(W_flat.end(), ivs.begin(), ivs.end());
            W_off[v + 1] = W_flat.size();
        }

        std::vector<HashVal> band_buf(params.r);
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        for (int j = 0; j < params.b; ++j) {
            std::vector<LSHEntry> es;
            es.reserve(total_pairs);
            for (NodeId v = 0; v < num_nodes; ++v) {
                if (W_off[v] == W_off[v + 1]) continue;
                const auto* tree = f->get_tree(v);
                WindowCursor cur(f->get_lambda(v));
                size_t plo = NPOS, phi = NPOS;
                bool ok = false; uint64_t bh_cur = 0;
                for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
                    for (TimeStamp t = W_flat[i].first; t <= W_flat[i].second; ++t) {
                        cur.advance(t, mu);
                        if (cur.lo != plo || cur.hi != phi) {
                            plo = cur.lo; phi = cur.hi;
                            ok = tree->query_range(t, t + mu, band_buf.data(), j * params.r, params.r);
                            if (ok) bh_cur = hash_band_ptr(band_buf.data(), j, params.r);
                        }
                        if (ok) es.push_back({combine_with_time(bh_cur, t), v});
                    }
                }
            }
            index[j].build(std::move(es));
        }
    }

    double auxiliary_memory_mb() const {
        size_t bytes = W_flat.capacity() * sizeof(Interval) + W_off.capacity() * sizeof(size_t)
                     + seen.capacity() * sizeof(uint32_t);
        for (const auto& ix : index) bytes += ix.memory_bytes();
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (v + 1 >= W_off.size()) return res;
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        WindowCursor cur(forest->get_lambda(v));
        size_t plo = NPOS, phi = NPOS;
        bool ok = false;

        for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
            for (TimeStamp t = W_flat[i].first; t <= W_flat[i].second; ++t) {
                cur.advance(t, mu);
                if (cur.lo != plo || cur.hi != phi) {
                    plo = cur.lo; phi = cur.hi;
                    ok = provider.get_into(v, t, sv.data());
                    if (ok) for (int j = 0; j < params.b; ++j)
                        bh[j] = hash_band_fnv(sv.data(), j, params.r);
                }
                if (!ok) continue;

                if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }
                for (int j = 0; j < params.b; ++j) {
                    const LSHKey key = combine_with_time(bh[j], t);
                    const bool hit = index[j].for_each(key, [&](NodeId u) -> bool {
                        if (u == v || seen[u] == epoch) return false;
                        seen[u] = epoch;
                        if (!provider.get_into(u, t, su.data())) return false;
                        if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                            res.found = true; res.node = u; res.time = t;
                            return true;
                        }
                        return false;
                    });
                    if (hit) return res;
                }
            }
        }
        return res;
    }

    // Conteggio di TUTTE le risposte ammissibili (u,t) trovate dall'LSH.
    // L'epoch elimina le ripetizioni tra bande per la STESSA finestra.
    uint64_t count_all(NodeId v, const ExactTemporalNeighborhoods& exact) const {
        if (v + 1 >= W_off.size()) return 0;
        uint64_t matches = 0;
        std::vector<NodeId> nv, nu;
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        WindowCursor cur(forest->get_lambda(v));
        size_t plo = NPOS, phi = NPOS;
        bool ok = false;

        for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
            for (uint64_t ti = W_flat[i].first; ti <= W_flat[i].second; ++ti) {
                const TimeStamp t = static_cast<TimeStamp>(ti);
                cur.advance(t, mu);
                if (cur.lo != plo || cur.hi != phi) {
                    plo = cur.lo; phi = cur.hi;
                    ok = provider.get_into(v, t, sv.data());
                    if (ok) for (int j = 0; j < params.b; ++j)
                        bh[j] = hash_band_fnv(sv.data(), j, params.r);
                }
                if (!ok) continue;
                exact.neighborhood(v, t, mu, nv);
                if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }
                for (int j = 0; j < params.b; ++j) {
                    const LSHKey key = combine_with_time(bh[j], t);
                    index[j].for_each(key, [&](NodeId u) -> bool {
                        if (u == v || seen[u] == epoch) return false;
                        seen[u] = epoch;
                        if (!provider.get_into(u, t, su.data())) return false;
                        if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                            exact.neighborhood(u, t, mu, nu);
                            if (ExactTemporalNeighborhoods::similar(nv, nu, tau)) ++matches;
                        }
                        return false;  // continua: la recall richiede TUTTE le coppie
                    });
                }
            }
        }
        return matches;
    }
};

// (C) MIA SOLUZIONE SENZA PRUNING W(v)  [ablation]
// Stessa logica di MySolution: stessi parametri k, b, r, stesso indice FlatBandIndex per banda,
// stessa chiave combine_with_time(hash_banda, t), stessa verifica dei candidati.
// NON calcola, NON memorizza e NON usa W(v). Differisce solo in:
//   - creazione delle chiavi: ogni nodo scorre TUTTE le finestre [T_min, Tmax]
//   - query: scorre TUTTE le finestre [T_min, Tmax] invece dei soli intervalli di W(v).
// Le differenze misurabili sono quindi tempo di costruzione, tempo di query e (di poco) memoria
// (MySolution paga lo spazio di W_flat/W_off, questa no).
class MySolutionNoWv {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    int need;
    const TemporalRangeForest* forest;
    const SketchProvider& provider;

    std::vector<FlatBandIndex> index;
    mutable std::vector<HashVal>   sv, su;
    mutable std::vector<uint64_t>  bh;
    mutable std::vector<uint32_t>  seen;
    mutable uint32_t epoch = 0;

public:
    MySolutionNoWv(NodeId num_nodes, const TemporalRangeForest* f, const SketchProvider& sp,
                   TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin), Tmax(tmax >= mu_ ? tmax - mu_ : 0),
          tau(tau_), params(p), need(need_matches(p.k, tau_)),
          forest(f), provider(sp), index(p.b),
          sv(p.k), su(p.k), bh(p.b), seen(num_nodes, 0) {

        if (Tmax < T_min) return;

        std::vector<HashVal> band_buf(params.r);
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        for (int j = 0; j < params.b; ++j) {
            std::vector<LSHEntry> es;
            // Nessun reserve: senza W(v) il numero di entry non e' noto a priori (e' proprio
            // sum|W(v)|). MySolution invece lo conosce e fa reserve esatto: e' un vantaggio reale di W(v).
            for (NodeId v = 0; v < num_nodes; ++v) {
                const auto* tree = f->get_tree(v);
                if (!tree) continue;
                WindowCursor cur(f->get_lambda(v));
                size_t plo = NPOS, phi = NPOS;
                bool ok = false; uint64_t bh_cur = 0;
                for (TimeStamp t = T_min; t <= Tmax; ++t) {
                    cur.advance(t, mu);
                    if (cur.lo != plo || cur.hi != phi) {
                        plo = cur.lo; phi = cur.hi;
                        ok = tree->query_range(t, t + mu, band_buf.data(), j * params.r, params.r);
                        if (ok) bh_cur = hash_band_ptr(band_buf.data(), j, params.r);
                    }
                    if (ok) es.push_back({combine_with_time(bh_cur, t), v});
                }
            }
            index[j].build(std::move(es));
        }
    }

    double auxiliary_memory_mb() const {
        size_t bytes = seen.capacity() * sizeof(uint32_t);
        for (const auto& ix : index) bytes += ix.memory_bytes();
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (Tmax < T_min) return res;
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        WindowCursor cur(forest->get_lambda(v));
        size_t plo = NPOS, phi = NPOS;
        bool ok = false;

        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            cur.advance(t, mu);
            if (cur.lo != plo || cur.hi != phi) {
                plo = cur.lo; phi = cur.hi;
                ok = provider.get_into(v, t, sv.data());
                if (ok) for (int j = 0; j < params.b; ++j)
                    bh[j] = hash_band_fnv(sv.data(), j, params.r);
            }
            //if (!ok) continue;

            if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }
            for (int j = 0; j < params.b; ++j) {
                const LSHKey key = combine_with_time(bh[j], t);
                const bool hit = index[j].for_each(key, [&](NodeId u) -> bool {
                    if (u == v || seen[u] == epoch) return false;
                    seen[u] = epoch;
                    if (!provider.get_into(u, t, su.data())) return false;
                    if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                        res.found = true; res.node = u; res.time = t;
                        return true;
                    }
                    return false;
                });
                if (hit) return res;
            }
        }
        return res;
    }
};

// (D) ALTERNATIVA (T-mu LSH indipendenti, uno per finestra, strutture piatte CSR)
//
// NOTA DI EQUITA': per un confronto controllato con MySolution, l'alternativa sfrutta W(v)
// esattamente nello stesso modo: W(v) e' precalcolato UNA sola volta e usato
//   - in costruzione: v viene inserito solo negli indici delle finestre t in W(v);
//   - in query:       si interrogano solo le finestre t in W(v) (NON tutte le T-mu).
// L'unica differenza rispetto a MySolution e' quindi la struttura di indicizzazione
// (T-mu indici indipendenti vs un unico indice globale con chiave (banda, tempo)).
class AlternativeSolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    int need;
    const TemporalRangeForest* forest;
    const SketchProvider& provider;
    uint32_t num_windows = 0;

    std::vector<Interval> W_flat;
    std::vector<size_t>   W_off;

    std::vector<FlatAlternativeIndex> band_indices;
    mutable std::vector<HashVal> sv, su;
    mutable std::vector<uint64_t> bh;
    mutable std::vector<uint32_t> seen;
    mutable uint32_t epoch = 0;

public:
    AlternativeSolution(NodeId num_nodes, const TemporalRangeForest* f, const SketchProvider& sp,
                        TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin), Tmax(tmax >= mu_ ? tmax - mu_ : 0),
          tau(tau_), params(p), need(need_matches(p.k, tau_)),
          forest(f), provider(sp), band_indices(p.b),
          sv(p.k), su(p.k), bh(p.b), seen(num_nodes, 0) {

        num_windows = (Tmax >= T_min) ? (Tmax - T_min + 1) : 0;

        // W(v) calcolato una volta sola (prima veniva ricalcolato per ogni banda)
        W_off.assign(static_cast<size_t>(num_nodes) + 1, 0);
        size_t total_pairs = 0;
        for (NodeId v = 0; v < num_nodes; ++v) {
            auto ivs = compute_W_intervals(f->get_lambda(v), mu, tmin, tmax);
            for (const auto& iv : ivs) total_pairs += (iv.second - iv.first + 1);
            W_flat.insert(W_flat.end(), ivs.begin(), ivs.end());
            W_off[v + 1] = W_flat.size();
        }
        if (num_windows == 0) return;

        std::vector<HashVal> band_buf(params.r);
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();

        // Costruzione banda per banda: deallocazione immediata ad ogni step
        for (int j = 0; j < params.b; ++j) {
            std::vector<std::pair<uint32_t, AltEntry>> raw_entries;
            raw_entries.reserve(total_pairs);
            for (NodeId v = 0; v < num_nodes; ++v) {
                if (W_off[v] == W_off[v + 1]) continue;
                const auto* tree = f->get_tree(v);
                if (!tree) continue;
                WindowCursor cur(f->get_lambda(v));
                size_t plo = NPOS, phi = NPOS;
                bool ok = false; uint64_t bh_cur = 0;

                for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
                    for (TimeStamp t = W_flat[i].first; t <= W_flat[i].second; ++t) {
                        cur.advance(t, mu);
                        if (cur.lo != plo || cur.hi != phi) {
                            plo = cur.lo; phi = cur.hi;
                            ok = tree->query_range(t, t + mu, band_buf.data(), j * params.r, params.r);
                            if (ok) bh_cur = hash_band_ptr(band_buf.data(), j, params.r);
                        }
                        if (ok) {
                            uint32_t w = t - T_min;
                            raw_entries.push_back({w, {bh_cur, v}});
                        }
                    }
                }
            }
            band_indices[j].build_from_raw(num_windows, raw_entries);
        }
    }

    double auxiliary_memory_mb() const {
        // Include W(v) (come in MySolution) per un confronto di memoria equo.
        size_t bytes = W_flat.capacity() * sizeof(Interval) + W_off.capacity() * sizeof(size_t)
                     + seen.capacity() * sizeof(uint32_t);
        for (const auto& idx : band_indices) bytes += idx.memory_bytes();
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (num_windows == 0 || v + 1 >= W_off.size()) return res;
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        WindowCursor cur(forest->get_lambda(v));
        size_t plo = NPOS, phi = NPOS;
        bool ok = false;

        // Si interrogano solo le finestre in W(v) (stesso ciclo di MySolution)
        for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
            for (TimeStamp t = W_flat[i].first; t <= W_flat[i].second; ++t) {
                cur.advance(t, mu);
                if (cur.lo != plo || cur.hi != phi) {
                    plo = cur.lo; phi = cur.hi;
                    ok = provider.get_into(v, t, sv.data());
                    if (ok) for (int j = 0; j < params.b; ++j)
                        bh[j] = hash_band_fnv(sv.data(), j, params.r);
                }
                if (!ok) continue;

                uint32_t w = t - T_min;
                if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }

                for (int j = 0; j < params.b; ++j) {
                    const bool hit = band_indices[j].for_each_in_window(w, bh[j], [&](NodeId u) -> bool {
                        if (u == v || seen[u] == epoch) return false;
                        seen[u] = epoch;
                        if (!provider.get_into(u, t, su.data())) return false;
                        if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                            res.found = true; res.node = u; res.time = t;
                            return true;
                        }
                        return false;
                    });
                    if (hit) return res;
                }
            }
        }
        return res;
    }

    // Stessa enumerazione della soluzione unificata, ma nelle T-mu
    // strutture LSH indipendenti: NON e' la ground truth.
    uint64_t count_all(NodeId v, const ExactTemporalNeighborhoods& exact) const {
        if (num_windows == 0 || v + 1 >= W_off.size()) return 0;
        uint64_t matches = 0;
        std::vector<NodeId> nv, nu;
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();
        WindowCursor cur(forest->get_lambda(v));
        size_t plo = NPOS, phi = NPOS;
        bool ok = false;

        for (size_t i = W_off[v]; i < W_off[v + 1]; ++i) {
            for (uint64_t ti = W_flat[i].first; ti <= W_flat[i].second; ++ti) {
                const TimeStamp t = static_cast<TimeStamp>(ti);
                cur.advance(t, mu);
                if (cur.lo != plo || cur.hi != phi) {
                    plo = cur.lo; phi = cur.hi;
                    ok = provider.get_into(v, t, sv.data());
                    if (ok) for (int j = 0; j < params.b; ++j)
                        bh[j] = hash_band_fnv(sv.data(), j, params.r);
                }
                if (!ok) continue;
                exact.neighborhood(v, t, mu, nv);
                const uint32_t w = t - T_min;
                if (++epoch == 0) { std::fill(seen.begin(), seen.end(), 0); epoch = 1; }
                for (int j = 0; j < params.b; ++j) {
                    band_indices[j].for_each_in_window(w, bh[j], [&](NodeId u) -> bool {
                        if (u == v || seen[u] == epoch) return false;
                        seen[u] = epoch;
                        if (!provider.get_into(u, t, su.data())) return false;
                        if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                            exact.neighborhood(u, t, mu, nu);
                            if (ExactTemporalNeighborhoods::similar(nv, nu, tau)) ++matches;
                        }
                        return false;
                    });
                }
            }
        }
        return matches;
    }
};

// (E) Alternativa con un indice LSH indipendente per ciascuna finestra temporale.
// Costruzione e query visitano l'intero intervallo temporale.
class AlternativeSolutionNoWv {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    int need;
    const TemporalRangeForest* forest;
    const SketchProvider& provider;
    uint32_t num_windows = 0;

    std::vector<FlatAlternativeIndex> band_indices;
    mutable std::vector<HashVal> sv, su;
    mutable std::vector<uint64_t> bh;
    mutable std::vector<uint32_t> seen;
    mutable uint32_t epoch = 0;

public:
    AlternativeSolutionNoWv(NodeId num_nodes, const TemporalRangeForest* f, const SketchProvider& sp,
                            TimeStamp mu_, double tau_, TimeStamp tmin, TimeStamp tmax, LSHParams p)
        : mu(mu_), T_min(tmin), Tmax(tmax >= mu_ ? tmax - mu_ : 0),
          tau(tau_), params(p), need(need_matches(p.k, tau_)),
          forest(f), provider(sp), band_indices(p.b),
          sv(p.k), su(p.k), bh(p.b), seen(num_nodes, 0) {

        num_windows = (Tmax >= T_min) ? (Tmax - T_min + 1) : 0;
        if (num_windows == 0) return;

        std::vector<HashVal> band_buf(params.r);
        constexpr size_t NPOS = std::numeric_limits<size_t>::max();

        for (int j = 0; j < params.b; ++j) {
            std::vector<std::pair<uint32_t, AltEntry>> raw_entries;
            for (NodeId v = 0; v < num_nodes; ++v) {
                const auto* tree = f->get_tree(v);
                if (!tree) continue;

                WindowCursor cur(f->get_lambda(v));
                size_t plo = NPOS, phi = NPOS;
                bool ok = false;
                uint64_t bh_cur = 0;
                for (TimeStamp t = T_min; t <= Tmax; ++t) {
                    cur.advance(t, mu);
                    if (cur.lo != plo || cur.hi != phi) {
                        plo = cur.lo;
                        phi = cur.hi;
                        ok = tree->query_range(t, t + mu, band_buf.data(),
                                               j * params.r, params.r);
                        if (ok) bh_cur = hash_band_ptr(band_buf.data(), j, params.r);
                    }
                    if (ok) {
                        const uint32_t w = t - T_min;
                        raw_entries.push_back({w, {bh_cur, v}});
                    }
                }
            }
            band_indices[j].build_from_raw(num_windows, raw_entries);
        }
    }

    double auxiliary_memory_mb() const {
        size_t bytes = seen.capacity() * sizeof(uint32_t);
        for (const auto& idx : band_indices) bytes += idx.memory_bytes();
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }

    QueryResult query(NodeId v) const {
        QueryResult res;
        if (num_windows == 0) return res;

        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            if (!provider.get_into(v, t, sv.data())) continue;
            for (int j = 0; j < params.b; ++j)
                bh[j] = hash_band_fnv(sv.data(), j, params.r);

            const uint32_t w = t - T_min;
            if (++epoch == 0) {
                std::fill(seen.begin(), seen.end(), 0);
                epoch = 1;
            }

            for (int j = 0; j < params.b; ++j) {
                const bool hit = band_indices[j].for_each_in_window(w, bh[j], [&](NodeId u) -> bool {
                    if (u == v || seen[u] == epoch) return false;
                    seen[u] = epoch;
                    if (!provider.get_into(u, t, su.data())) return false;
                    if (sig_match_ge(sv.data(), su.data(), params.k, need)) {
                        res.found = true;
                        res.node = u;
                        res.time = t;
                        return true;
                    }
                    return false;
                });
                if (hit) return res;
            }
        }
        return res;
    }
};

// ============================================================================
// MAIN RUNNER
// ============================================================================

int main(int argc, char** argv) {
    if (argc < 3) {
        std::cout << "Uso: " << argv[0] << " <dataset_path> <dataset_name> [mu] [tau] [k] [b] [num_queries]\n";
        return 1;
    }

    std::string dataset_path = argv[1];
    std::string dataset_name = argv[2];
    TimeStamp mu  = (argc > 3) ? static_cast<TimeStamp>(std::stoul(argv[3])) : 10;
    double    tau = (argc > 4) ? std::stod(argv[4]) : 0.40;
    int k         = (argc > 5) ? std::stoi(argv[5]) : 64;
    int b         = (argc > 6) ? std::stoi(argv[6]) : 8;
    int num_queries = (argc > 7) ? std::stoi(argv[7]) : 30;
    if (k <= 0 || b <= 0 || k % b != 0 || tau < 0.0 || tau > 1.0) {
        std::cerr << "[ERRORE] Servono k>0, b>0, k multiplo di b e tau in [0,1].\n";
        return 1;
    }
    int r = k / b;

    std::filesystem::create_directories("results_K_grande_gt");

    std::cout << "===============================================================\n";
    std::cout << " BENCHMARK PROBLEMA 2 - DATASET: " << dataset_name << "\n";
    std::cout << "===============================================================\n";

    // 1. Caricamento del dataset normalizzato
    TemporalGraph G = load_temporal_graph_from_file(dataset_path);
    NodeId total_nodes = G.num_nodes();
    TimeStamp T_min = G.get_T_min();
    TimeStamp T_max = G.get_T_max();
    TimeStamp total_windows = (T_max >= mu + T_min) ? (T_max - mu - T_min + 1) : 0;

    std::cout << "Archi: " << G.edges.size() << " | Nodi: " << total_nodes
              << " | Timeline normalizzata [" << T_min << ", " << T_max << "] | Finestre (T-mu): " << total_windows << "\n";

    if (total_windows == 0) {
        std::cerr << "[ERRORE] mu (" << mu << ") e' >= dell'ampiezza temporale (" << (T_max - T_min) << "). Nessuna finestra.\n";
        return 1;
    }

    // 2. Costruzione Temporal Range Forest
    std::cout << "Costruzione TemporalRangeForest...\n";
    auto t_trf = Clock::now();
    TemporalRangeForest trf(&G, k);
    std::cout << "  Costruzione TRF completata in " << us_since(t_trf) / 1000.0 << " ms\n";

    // 3. Analisi W(v) e Salvataggio CSV
    std::string wv_filename = "results_K_grande_gt/" + dataset_name + "_wv_analysis.csv";
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
    std::cout << "-> Metriche W(v) salvate in: " << wv_filename << "\n";

    // L'indice compatto conserva SOLO gli eventi temporali reali. Serve
    // a verificare l'esatta Jaccard; non e' usato per costruire gli LSH.
    std::cout << "Costruzione indice dei vicinati esatti per la ground truth...\n";
    ExactTemporalNeighborhoods exact_neighborhoods(G);

    // Liberazione memoria archi non più necessari
    { std::vector<TemporalEdge>().swap(G.edges); }

    // 4. Provider (non alloca memoria rilevante, solo puntatori)
    SketchProvider provider(&trf, mu, k);

    // Pre-genera un campione di nodi sorgente DISTINTI (seed fisso).
    // Cosi' ogni risposta positiva corrisponde a un nodo diverso, non a una
    // ripetizione della stessa query. Campionamento senza rimpiazzo in O(Q)
    // memoria, senza costruire un vettore grande quanto tutto il grafo.
    if (num_queries <= 0) {
        std::cerr << "[ERRORE] num_queries deve essere > 0.\n";
        return 1;
    }
    const size_t queries_to_run = std::min<size_t>(static_cast<size_t>(num_queries), total_nodes);
    std::mt19937 rng(42);
    std::unordered_set<NodeId> selected;
    selected.reserve(queries_to_run);
    std::vector<NodeId> query_nodes;
    query_nodes.reserve(queries_to_run);
    for (uint64_t j = static_cast<uint64_t>(total_nodes) - queries_to_run;
         j < static_cast<uint64_t>(total_nodes); ++j) {
        std::uniform_int_distribution<NodeId> dist(0, static_cast<NodeId>(j));
        const NodeId candidate = dist(rng);
        const NodeId chosen = selected.insert(candidate).second
                                  ? candidate : static_cast<NodeId>(j);
        if (chosen != candidate) selected.insert(chosen);
        query_nodes.push_back(chosen);
    }
    std::shuffle(query_nodes.begin(), query_nodes.end(), rng);

    // Per ogni nodo query contiamo TUTTE le coppie ammissibili (u,t):
    // ground truth con BF e Jaccard esatta sui vicinati reali,
    // mentre LSH propone i candidati, successivamente convalidati su Jaccard esatta.
    // I tre vettori richiedono solo O(Q) memoria.
    std::vector<uint64_t> groundtruth_counts(queries_to_run, 0);
    std::vector<uint64_t> mine_counts(queries_to_run, 0);
    std::vector<uint64_t> alt_counts(queries_to_run, 0);
    double avg_groundtruth_enumeration_us = 0.0;

    // Risultati accumulati (ogni soluzione vive e muore nel proprio scope)
    // double avg_time_naive = 0.0, mem_naive_mb = 0.0, build_naive_ms = 0.0;
    double avg_time_mine = 0.0, mem_mine_mb = 0.0, build_mine_ms = 0.0;
    double avg_time_mine_nowv = 0.0, mem_mine_nowv_mb = 0.0, build_mine_nowv_ms = 0.0;
    double avg_time_alt = 0.0, mem_alt_mb = 0.0, build_alt_ms = 0.0;
    // double avg_time_alt_nowv = 0.0, mem_alt_nowv_mb = 0.0, build_alt_nowv_ms = 0.0;
    // std::vector<QueryResult> res_mine;   // per verificare che la variante senza W(v) dia gli stessi risultati

    // ---- (A) Ground truth: Brute Force ESAUSTIVA ----
    // Non e' una query booleana con early-stop: contiamo ogni (u,t) valido.
    // Eseguita separatamente e distrutta prima di costruire gli indici LSH.
    {
        std::cout << "Ground Truth: Brute Force esaustiva (tutte le coppie u,t)...\n";
        NaiveSolution sol(total_nodes, provider, mu, tau, T_min, T_max, k);
        double tot = 0.0;
        for (size_t qi = 0; qi < queries_to_run; ++qi) {
            const auto t0 = Clock::now();
            groundtruth_counts[qi] = sol.count_all(query_nodes[qi], exact_neighborhoods);
            tot += us_since(t0);
            std::cout << "  Query " << (qi + 1) << '/' << queries_to_run << " -> " << groundtruth_counts[qi] << " coppie ammissibili\n";
        }
        avg_groundtruth_enumeration_us = tot / queries_to_run;
        std::cout << "  Tempo medio per enumerare la ground truth: "
                  << avg_groundtruth_enumeration_us << " us\n";
    }

    // ---- (B) Mia Soluzione (LSH Unificato + W(v)) ----
    {
        std::cout << "Costruzione MySolution (LSH Unificato + Pruning Temporale)...\n";
        auto t_build = Clock::now();
        MySolution sol(total_nodes, &trf, provider, mu, tau, T_min, T_max, {k, b, r});
        build_mine_ms = us_since(t_build) / 1000.0;
        std::cout << "  Costruita in " << build_mine_ms << " ms\n";
        mem_mine_mb = sol.auxiliary_memory_mb();

        std::cout << "  Esecuzione " << queries_to_run << " query...\n";
        double tot = 0.0;
        for (size_t qi = 0; qi < queries_to_run; ++qi) {
            auto t0 = Clock::now();
            const QueryResult result = sol.query(query_nodes[qi]);
            tot += us_since(t0);
            (void)result;
        }
        avg_time_mine = tot / queries_to_run;
        std::cout << "  Enumerazione delle coppie per la recall (fuori dal benchmark)...\n";
        for (size_t qi = 0; qi < queries_to_run; ++qi)
            mine_counts[qi] = sol.count_all(query_nodes[qi], exact_neighborhoods);
        std::cout << "  Tempo medio: " << avg_time_mine << " us | Mem: " << mem_mine_mb << " MB\n";
        std::cout << "  Tempo totale di esecuzione: " << tot << "us\n";
    }  // <- MySolution distrutta qui

    // ---- (C) Mia Soluzione SENZA Pruning W(v) [variante di controllo] ----
    {
        std::cout << "Costruzione MySolutionNoWv (LSH Unificato, NO Pruning W(v))...\n";
        auto t_build = Clock::now();
        MySolutionNoWv sol(total_nodes, &trf, provider, mu, tau, T_min, T_max, {k, b, r});
        build_mine_nowv_ms = us_since(t_build) / 1000.0;
        std::cout << "  Costruita in " << build_mine_nowv_ms << " ms\n";
        mem_mine_nowv_mb = sol.auxiliary_memory_mb();

        std::cout << "  Esecuzione " << queries_to_run << " query...\n";
        double tot = 0.0;
        for (NodeId qn : query_nodes) {
            auto t0 = Clock::now();
            (void)sol.query(qn);
            tot += us_since(t0);
        }
        avg_time_mine_nowv = tot / queries_to_run;
        std::cout << "  Tempo medio: " << avg_time_mine_nowv << " us | Mem: " << mem_mine_nowv_mb << " MB\n";
        std::cout << "  Tempo totale di esecuzione: " << tot << "us\n";
    }  // <- MySolutionNoWv distrutta qui

    // ---- (D) Alternativa (T-mu LSH Indipendenti + W(v)) ----
    {
        std::cout << "Costruzione AlternativeSolution (T-mu LSH Indipendenti)...\n";
        auto t_build = Clock::now();
        AlternativeSolution sol(total_nodes, &trf, provider, mu, tau, T_min, T_max, {k, b, r});
        build_alt_ms = us_since(t_build) / 1000.0;
        std::cout << "  Costruita in " << build_alt_ms << " ms\n";
        mem_alt_mb = sol.auxiliary_memory_mb();

        std::cout << "  Esecuzione " << queries_to_run << " query...\n";
        double tot = 0.0;
        for (size_t qi = 0; qi < queries_to_run; ++qi) {
            auto t0 = Clock::now();
            const QueryResult result = sol.query(query_nodes[qi]);
            tot += us_since(t0);
            (void)result;
        }
        avg_time_alt = tot / queries_to_run;
        std::cout << "  Enumerazione delle coppie per la recall (fuori dal benchmark)...\n";
        for (size_t qi = 0; qi < queries_to_run; ++qi)
            alt_counts[qi] = sol.count_all(query_nodes[qi], exact_neighborhoods);
        std::cout << "  Tempo medio: " << avg_time_alt << " us | Mem: " << mem_alt_mb << " MB\n";
        std::cout << "  Tempo totale di esecuzione: " << tot << "us\n";
    }  // <- AlternativeSolution distrutta qui


    // 5. Recall rispetto alla ground truth ottenuta con la BRUTE FORCE.
    // Un elemento della ground truth e' una coppia (nodo u, inizio finestra t).
    // La brute force applica Jaccard ESATTA ai veri vicinati temporali.
    // Le soluzioni LSH generano candidati e superano il loro filtro MinHash,
    // ma sono contate solo se soddisfano anche l'esatta Jaccard.
    // Le coppie LSH valide sono quindi un sottoinsieme della ground truth,
    // e i rispettivi conteggi sono veri positivi, non semplici candidati.
    // Memorizziamo solo i conteggi, senza materializzare la ground truth in RAM.
    const std::string validity_filename = "results_K_grande_gt/" + dataset_name + "_validity.csv";
    std::ofstream validity_file(validity_filename);
    if (!validity_file) {
        std::cerr << "[ERRORE] Impossibile creare: " << validity_filename << "\n";
        return 1;
    }
    validity_file << "query_index,source_node_id,"
                     "groundtruth_pairs,mia_soluzione_pairs,alternativa_pairs,"
                     "groundtruth_found,mia_soluzione_found,alternativa_found,"
                     "groundtruth_cumulative,mia_soluzione_cumulative,alternativa_cumulative,"
                     "groundtruth_positive_cumulative,mia_positive_cumulative,alternativa_positive_cumulative,"
                     "mia_false_negative_cumulative,alternativa_false_negative_cumulative,"
                     "mia_recall_pairs_pct,alternativa_recall_pairs_pct\n";

    uint64_t gt_pairs = 0, mine_pairs = 0, alt_pairs = 0;
    uint64_t gt_positive_queries = 0, mine_positive_queries = 0, alt_positive_queries = 0;
    uint64_t mine_fn_pairs = 0, alt_fn_pairs = 0;
    uint64_t mine_tp_queries = 0, alt_tp_queries = 0;
    auto recall_csv = [](uint64_t tp, uint64_t total) -> std::string {
        if (total == 0) return "";  // recall non definita; non scrivere 100%
        std::ostringstream ss;
        ss << std::fixed << std::setprecision(6)
           << (100.0 * static_cast<double>(tp) / static_cast<double>(total));
        return ss.str();
    };

    for (size_t qi = 0; qi < queries_to_run; ++qi) {
        const uint64_t gt = groundtruth_counts[qi];
        const uint64_t my = mine_counts[qi];
        const uint64_t alt = alt_counts[qi];
        if (my > gt || alt > gt) {
            std::cerr << "[ERRORE] Query " << qi + 1
                      << ": un LSH ha piu' coppie ammissibili della ground truth."
                      << " Verificare query_all/chiavi/caching.\n";
            return 1;
        }

        const bool gt_found = gt > 0;
        const bool my_found = my > 0;
        const bool alt_found = alt > 0;
        gt_pairs += gt;
        mine_pairs += my;
        alt_pairs += alt;
        mine_fn_pairs += gt - my;
        alt_fn_pairs += gt - alt;
        gt_positive_queries += gt_found;
        mine_positive_queries += my_found;
        alt_positive_queries += alt_found;
        mine_tp_queries += gt_found && my_found;
        alt_tp_queries += gt_found && alt_found;

        validity_file << (qi + 1) << ',' << query_nodes[qi] << ','
                      << gt << ',' << my << ',' << alt << ','
                      << static_cast<int>(gt_found) << ','
                      << static_cast<int>(my_found) << ','
                      << static_cast<int>(alt_found) << ','
                      << gt_pairs << ',' << mine_pairs << ',' << alt_pairs << ','
                      << gt_positive_queries << ',' << mine_positive_queries << ','
                      << alt_positive_queries << ','
                      << mine_fn_pairs << ',' << alt_fn_pairs << ','
                      << recall_csv(my, gt) << ',' << recall_csv(alt, gt) << '\n';
    }
    validity_file.close();

    const std::string mine_recall_pairs = recall_csv(mine_pairs, gt_pairs);
    const std::string alt_recall_pairs = recall_csv(alt_pairs, gt_pairs);
    const std::string mine_recall_query = recall_csv(mine_tp_queries, gt_positive_queries);
    const std::string alt_recall_query = recall_csv(alt_tp_queries, gt_positive_queries);

    // Una sola riga per dataset/parametri, adatta anche a confronti successivi.
    const std::string recall_summary_filename =
        "results_K_grande_gt/" + dataset_name + "_recall_summary.csv";
    std::ofstream recall_sum(recall_summary_filename);
    recall_sum << "dataset,mu,tau,k,b,r,queries,groundtruth_pairs,"
                  "mia_soluzione_pairs,alternativa_pairs,mia_false_negatives,"
                  "alternativa_false_negatives,mia_recall_pairs_pct,alternativa_recall_pairs_pct,"
                  "groundtruth_positive_queries,mia_positive_queries,alternativa_positive_queries,"
                  "mia_recall_query_pct,alternativa_recall_query_pct,"
                  "groundtruth_avg_enumeration_us\n";
    recall_sum << dataset_name << ',' << mu << ',' << tau << ',' << k << ','
               << b << ',' << r << ',' << queries_to_run << ','
               << gt_pairs << ',' << mine_pairs << ',' << alt_pairs << ','
               << mine_fn_pairs << ',' << alt_fn_pairs << ','
               << mine_recall_pairs << ',' << alt_recall_pairs << ','
               << gt_positive_queries << ',' << mine_positive_queries << ','
               << alt_positive_queries << ','
               << mine_recall_query << ',' << alt_recall_query << ','
               << avg_groundtruth_enumeration_us << '\n';
    recall_sum.close();

    std::cout << "[GROUND TRUTH BF] Coppie (u,t) ammissibili: " << gt_pairs
              << " | Query positive: " << gt_positive_queries
              << " / " << queries_to_run << '\n';
    std::cout << "[RECALL COPPIE] Mia soluzione: "
              << (mine_recall_pairs.empty() ? "N/D" : mine_recall_pairs + "%")
              << " (FN=" << mine_fn_pairs << ") | Alternativa: "
              << (alt_recall_pairs.empty() ? "N/D" : alt_recall_pairs + "%")
              << " (FN=" << alt_fn_pairs << ")\n";
    std::cout << "[RECALL QUERY BOOLEANE] Mia soluzione: "
              << (mine_recall_query.empty() ? "N/D" : mine_recall_query + "%")
              << " | Alternativa: "
              << (alt_recall_query.empty() ? "N/D" : alt_recall_query + "%") << '\n';
    std::cout << "-> Controllo validita' salvato in: " << validity_filename << "\n";
    std::cout << "-> Riepilogo recall salvato in: " << recall_summary_filename << "\n";

    // 6. Salvataggio Sommario Metriche (invariato)
    std::string summary_filename = "results_K_grande_gt/" + dataset_name + "_summary.csv";
    std::ofstream sum_file(summary_filename);
    sum_file << "dataset,algorithm,avg_query_time_us,memory_mb,build_time_ms\n";
    // sum_file << dataset_name << ",BruteForce,"   << avg_time_naive << "," << mem_naive_mb << "," << build_naive_ms << "\n";
    sum_file << dataset_name << ",MiaSoluzione,"      << avg_time_mine      << "," << mem_mine_mb      << "," << build_mine_ms      << "\n";
    sum_file << dataset_name << ",MiaSoluzione_NoWv," << avg_time_mine_nowv << "," << mem_mine_nowv_mb << "," << build_mine_nowv_ms << "\n";
    sum_file << dataset_name << ",Alternativa,"  << avg_time_alt   << "," << mem_alt_mb   << "," << build_alt_ms   << "\n";
    // sum_file << dataset_name << ",Alternativa_NoWv," << avg_time_alt_nowv << "," << mem_alt_nowv_mb << "," << build_alt_nowv_ms << "\n";
    sum_file.close();

    std::cout << "-> Summary salvato in: " << summary_filename << "\n";
    std::cout << "Esperimento completato con successo.\n";

    return 0;
}
