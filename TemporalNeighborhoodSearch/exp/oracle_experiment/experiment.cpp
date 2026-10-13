// Complete experiment: 4D first-MinHash oracle + full-signature validation.
// TemporalRangeForest, RangeTree and MinHashParams adapted verbatim from
// the user's esperimento_opt.cpp. No timestamp normalization here.
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <map>
#include <memory>
#include <numeric>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <unordered_map>
#include <vector>
using namespace std;
using U = uint32_t;
using NodeId = uint32_t;
using TimeStamp = uint32_t;
using HashVal = uint32_t;
using Clock = chrono::steady_clock;
static constexpr U INFU = numeric_limits<U>::max();

static double secs_since(const Clock::time_point& t0) {
    return chrono::duration<double>(Clock::now() - t0).count();
}
static uint64_t rss_kb() {
    ifstream f("/proc/self/status");
    string key; uint64_t v;
    while (f >> key) {
        if (key == "VmRSS:") { f >> v; return v; }
        string rem; getline(f, rem);
    }
    return 0;
}
static vector<string> split(const string& s, char d) {
    vector<string> out; string x; stringstream ss(s);
    while (getline(ss, x, d)) if (!x.empty()) out.push_back(x);
    return out;
}
static string csv_escape(const string& s) {
    string out = "\"";
    for (char c : s) { if (c == '\"') out += '\"'; out += c; }
    return out + '\"';
}

// ---------------------------------------------------------------------------
// STEP 0: The SAME TemporalRangeForest and MinHash sketches as in
// esperimento_opt.cpp: one tree per vertex, each internal node holding a
// componentwise MinHash merge over active timestamps.
// ---------------------------------------------------------------------------
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


// First component of MinHashNeighborhoodSketch::signature() for a singleton
// {neighbor}. Reuses EXACTLY the hash scheme of MinHashParams::get_instance(k).
static U first_hash(NodeId neighbor, int k) {
    const auto& prm = MinHashParams::get_instance(k);
    const uint32_t base_hash = neighbor * 2654435761U;
    const uint64_t value = (uint64_t(prm.a[0]) * base_hash + prm.b[0]) % MinHashParams::PRIME;
    return (U)value;
}

// Identical acceptance criterion to esperimento_opt.cpp (sig_match_ge).
static int need_matches(int k, double tau) {
    int m = 0;
    while (m <= k && static_cast<double>(m) / k < tau) ++m;
    return m;
}
static bool sig_match_ge(const HashVal* a, const HashVal* b, int k, int need) {
    int matches = 0;
    for (int i0 = 0; i0 < k; i0 += 16) {
        int i1 = min(k, i0 + 16);
        for (int i = i0; i < i1; ++i) matches += (a[i] == b[i]);
        if (matches + (k - i1) < need) return false;
    }
    return matches >= need;
}

// Used after the oracle has already established signature[0] equality.
// This is exactly the second phase described in the LaTeX: compare k-1 others.
static bool verify_remaining_components(const HashVal* a, const HashVal* b,
                                         int k, int need) {
    if (a[0] != b[0]) return false;
    int matches = 1;
    for (int i = 1; i < k; ++i) {
        matches += (a[i] == b[i]);
        if (matches + (k - 1 - i) < need) return false;
    }
    return matches >= need;
}

struct Event { U v, t, h; };
struct Rect {
    U owner;  // v
    U l, p, r, s, x;  // [l,p] x [p,r], span s=r-l, first MinHash x
};
// ---------- 4D range tree keyed by (l, p, r, s) with report box ----------
class RangeTree4D {
    struct Node {
        U lo = 0, hi = 0;
        U left = INFU, right = INFU;
        U aux = INFU;
        U offset = 0, len = 0;
    };
    vector<Node> nodes;
    vector<U> postings;            // rectangle ids sorted by descending s at level 3
    const vector<Rect>* rects{};
    uint64_t budget = numeric_limits<uint64_t>::max();

    static U coord(const Rect& r, int dim) {
        if (dim == 0) return r.l;
        if (dim == 1) return r.p;
        return r.r;
    }
    void guard() {
        uint64_t used = uint64_t(nodes.capacity()) * sizeof(Node)
                      + uint64_t(postings.capacity()) * sizeof(U);
        if (used > budget) throw runtime_error("Range tree exceeds --max-index-mb");
    }

    U build(vector<U> ids, int dim) {
        sort(ids.begin(), ids.end(), [&](U a, U b) {
            U x = coord((*rects)[a], dim), y = coord((*rects)[b], dim);
            return x != y ? x < y : a < b;
        });

        U idx = (U)nodes.size();
        nodes.emplace_back();
        guard();
        nodes[idx].lo = coord((*rects)[ids.front()], dim);
        nodes[idx].hi = coord((*rects)[ids.back()], dim);

        if (dim == 2) {
            sort(ids.begin(), ids.end(), [&](U a, U b) {
                if ((*rects)[a].s != (*rects)[b].s) return (*rects)[a].s > (*rects)[b].s;
                return a < b;
            });
            U off = (U)postings.size();
            postings.insert(postings.end(), ids.begin(), ids.end());
            guard();
            nodes[idx].offset = off;
            nodes[idx].len = (U)ids.size();
        } else {
            nodes[idx].aux = build(ids, dim + 1);
        }

        if (ids.size() > 1) {
            sort(ids.begin(), ids.end(), [&](U a, U b) {
                U x = coord((*rects)[a], dim), y = coord((*rects)[b], dim);
                return x != y ? x < y : a < b;
            });
            auto mid = ids.begin() + ids.size() / 2;
            vector<U> left(ids.begin(), mid), right(mid, ids.end());
            nodes[idx].left = build(move(left), dim);
            nodes[idx].right = build(move(right), dim);
        }
        return idx;
    }

    template <class F>
    void descend(U idx, int dim, U a, U b, U mu, F& cb) const {
        const Node& n = nodes[idx];
        U low = 0, high = INFU;
        if (dim == 0) { low = 0;     high = b; }
        if (dim == 1) { low = a;     high = b + mu; }
        if (dim == 2) { low = a + mu; high = INFU; }

        if (n.hi < low || n.lo > high) return;
        if (low <= n.lo && n.hi <= high) {
            if (dim < 2) {
                descend(n.aux, dim + 1, a, b, mu, cb);
            } else {
                for (U j = 0; j < n.len; ++j) {
                    U rid = postings[n.offset + j];
                    if ((*rects)[rid].s < mu) break;
                    cb(rid);
                }
            }
            return;
        }
        if (n.left != INFU) {
            descend(n.left, dim, a, b, mu, cb);
            descend(n.right, dim, a, b, mu, cb);
        }
    }
public:
    U root = INFU;
    void init(const vector<Rect>& rects_, vector<U> ids, uint64_t bytes_budget) {
        rects = &rects_;
        budget = bytes_budget;
        nodes.clear(); postings.clear();
        if (!ids.empty()) root = build(move(ids), 0);
    }
    template <class F>
    void query(U a, U b, U mu, F cb) const {
        if (root != INFU) descend(root, 0, a, b, mu, cb);
    }
    uint64_t bytes() const {
        return uint64_t(nodes.capacity()) * sizeof(Node)
             + uint64_t(postings.capacity()) * sizeof(U);
    }
};

struct Index {
    vector<Rect> rects;                 // all finite rectangles
    vector<vector<U>> L;                // per-node rectangle ids, sorted by decreasing span
    unordered_map<U, RangeTree4D> H;    // per-label range tree
    uint64_t tree_bytes = 0;
    U n = 0, T = 0;
};

static vector<U> parse_mus(const string& s, U T) {
    vector<U> out;
    for (const string& token : split(s, ',')) {
        // mu is an INTEGER expressed in the discretized bins of discretize_ts.cpp.
        if (token.find_first_of(".eE-") != string::npos)
            throw runtime_error("mu values must be nonnegative integer bins (not fractions): " + token);
        const uint64_t v = stoull(token);
        if (v >= T) throw runtime_error("mu must be < T=" + to_string(T) + ": " + token);
        out.push_back((U)v);
    }
    if (out.empty()) throw runtime_error("empty mu list");
    sort(out.begin(), out.end());
    out.erase(unique(out.begin(), out.end()), out.end());
    return out;
}

// ---------------------------------------------------------------------------
// ORACLE FILTER: emit every overlap (u, [lo,hi]) of R_v and R_u on D_mu.
// Rectangle partitions guarantee no (u,t) appears twice.
// No (u,t) candidates are stored in memory.
// ---------------------------------------------------------------------------
template <class F>
static uint64_t for_each_filtered_interval(const Index& ix, U v, U mu, F&& callback,
                                           uint64_t* reported_out=nullptr) {
    uint64_t visited = 0, reported = 0;
    for (U rvid : ix.L[v]) {
        const Rect& rv = ix.rects[rvid];
        if (rv.s < mu) break;  // sorted by decreasing span
        ++visited;
        U a = max(rv.l, rv.p > mu ? U(rv.p - mu) : U(0));
        U b = min(rv.p, rv.r >= mu ? U(rv.r - mu) : U(0));
        if (a > b) throw runtime_error("inconsistent span of query rectangle");
        auto it = ix.H.find(rv.x);
        if (it == ix.H.end()) continue;
        it->second.query(a, b, mu, [&](U ruid) {
            ++reported;
            const Rect& ru = ix.rects[ruid];
            if (ru.owner == v) return;
            // X_Ru(mu)=[max(l,p-mu), min(p,r-mu)]
            const U ua = max(ru.l, ru.p > mu ? U(ru.p - mu) : U(0));
            const U ub = min(ru.p, ru.r >= mu ? U(ru.r - mu) : U(0));
            const U lo = max(a, ua), hi = min(b, ub);
            if (lo <= hi) callback(ru.owner, lo, hi);
        });
    }
    if (reported_out) *reported_out = reported;
    return visited;
}

struct QueryMetrics {
    uint64_t filter_nodes = 0, filter_pairs = 0;
    uint64_t verified_nodes = 0, verified_pairs = 0;
    uint64_t visited_rects = 0, reported_rects = 0;
    bool found = false;
    U witness_node = INFU, witness_time = INFU;
    double filter_ms = 0.0, verify_ms = 0.0;
};

// For diagnostic correctness checks on small graphs (optional).
static void exhaustive_validate(const TemporalRangeForest& forest,
    U n, U T, U v, U mu, int k, int need, const QueryMetrics& actual) {
    if (uint64_t(n) * (T - mu) > 200000ULL)
        throw runtime_error("--validate: dataset too large for exhaustive check (>200000 node-window comparisons)");
    vector<U> av(k), au(k);
    vector<uint8_t> filt(n, 0), valid(n, 0);
    uint64_t fp=0, vp=0;
    for (U t=1; t <= T-mu; ++t) {
        if (!forest.get_tree(v)->query_range(t, t+mu, av.data(), 0, k)) continue;
        for (U u=0; u<n; ++u) {
            if (u == v) continue;
            if (!forest.get_tree(u)->query_range(t, t+mu, au.data(), 0, k)) continue;
            if (av[0] != au[0]) continue;
            ++fp; filt[u]=1;
            if (sig_match_ge(av.data(), au.data(), k, need)) { ++vp; valid[u]=1; }
        }
    }
    uint64_t fn=accumulate(filt.begin(),filt.end(),uint64_t(0));
    uint64_t vn=accumulate(valid.begin(),valid.end(),uint64_t(0));
    if (fp != actual.filter_pairs || vp != actual.verified_pairs ||
        fn != actual.filter_nodes || vn != actual.verified_nodes) {
        ostringstream ss;
        ss << "VALIDATION FAIL v="<<v<<" mu="<<mu
           << " expected filter(n,p)="<<fn<<","<<fp
           << " got="<<actual.filter_nodes<<","<<actual.filter_pairs
           << " expected verified(n,p)="<<vn<<","<<vp
           << " got="<<actual.verified_nodes<<","<<actual.verified_pairs;
        throw runtime_error(ss.str());
    }
}

int main(int argc, char** argv) {
    try {
        string dataset_path;
        string out_csv = "results.csv";
        string mus_csv = "0,1,2,4";
        uint64_t max_index_mb = 1024;
        uint64_t seed = 42;
        size_t queries = 100;
        int sketch_k = 64;
        double tau = 0.55;
        bool validate = false;
        for (int i = 1; i < argc; ++i) {
            string a = argv[i];
            if (a == "--validate") { validate = true; continue; }
            if (i + 1 >= argc) throw runtime_error("missing value for " + a);
            string b = argv[++i];
            if (a == "--dataset") dataset_path = b;
            else if (a == "--out") out_csv = b;
            else if (a == "--mus") mus_csv = b;
            else if (a == "--queries") queries = stoull(b);
            else if (a == "--max-index-mb") max_index_mb = stoull(b);
            else if (a == "--seed") seed = stoull(b);
            else if (a == "--k") sketch_k = stoi(b);
            else if (a == "--tau") tau = stod(b);
            else throw runtime_error("unknown argument " + a);
        }
        if (dataset_path.empty()) throw runtime_error("use --dataset PATH");
        if (sketch_k <= 0) throw runtime_error("--k must be positive");
        if (!isfinite(tau) || tau < 0.0 || tau > 1.0) throw runtime_error("--tau must be in [0,1]");
        const int threshold_matches = need_matches(sketch_k, tau);

        auto preprocess_start = Clock::now();
        // [1] Load the already-discretized graph from discretize_ts.cpp (no time remapping).
        TemporalGraph G = load_temporal_graph_from_file(dataset_path);
        const U n = G.num_nodes(), T = G.get_T_max();
        if (!n || !T) throw runtime_error("empty graph");
        if (T >= INFU - 1) throw runtime_error("T is too large for this timestamp type");
        for (const auto& e : G.edges)
            if (e.time < 1 || e.time > T) throw runtime_error("invalid discrete timestamp");
        vector<U> mu_vals = parse_mus(mus_csv, T);
        cerr << "loading complete: n=" << n << " T=" << T << " m=" << G.edges.size()
             << " tau=" << tau << " k=" << sketch_k << "\n";

        // [2] Build REAL complete MinHash sketches using the same forest as the supplied experiment.
        auto trf_start = Clock::now();
        TemporalRangeForest forest(&G, sketch_k);
        const double trf_s = secs_since(trf_start);

        // [3] Compute finite h_v(t) from the EXACT first MinHash component.
        vector<U> hash0(n);
        for (U u=0; u<n; ++u) hash0[u] = first_hash(u, sketch_k);
        vector<Event> events;
        if (G.edges.size() > (size_t(INFU) / 2)) throw runtime_error("too many events for 32-bit rectangle IDs");
        events.reserve(G.edges.size() * 2);
        for (const auto& e : G.edges) {
            events.push_back({e.u, e.time, hash0[e.v]});
            events.push_back({e.v, e.time, hash0[e.u]});
        }
        // All sketch data are in the forest; original raw edges may now be freed.
        vector<TemporalEdge>().swap(G.edges);
        sort(events.begin(), events.end(), [](const Event& a, const Event& b) {
            return tie(a.v,a.t,a.h)<tie(b.v,b.t,b.h);
        });
        size_t keep=0;
        for (size_t i=0;i<events.size();++i)
            if (!keep || events[keep-1].v != events[i].v || events[keep-1].t != events[i].t)
                events[keep++] = events[i];
        events.resize(keep);

        // [4] Build L(v), finite-labeled rectangles R_{v,p}, and the 4D trees H[x].
        Index ix;
        ix.n=n; ix.T=T;
        ix.L.resize(n);
        ix.rects.reserve(events.size());
        vector<U> h(T+1, INFU), Lp(T+1), Rp(T+1), st;
        unordered_map<U, vector<U>> groups;
        groups.reserve(n);
        size_t ei=0;
        for (U v=0;v<n;++v) {
            fill(h.begin(),h.end(),INFU);
            while (ei<events.size() && events[ei].v==v) {
                h[events[ei].t]=events[ei].h;
                ++ei;
            }
            st.clear();
            for (U p=1;p<=T;++p) {
                while (!st.empty() && h[st.back()]>h[p]) st.pop_back();
                Lp[p]=st.empty()?0:st.back();
                st.push_back(p);
            }
            st.clear();
            for (U p=T;p>=1;--p) {
                while (!st.empty() && h[st.back()]>=h[p]) st.pop_back();
                Rp[p]=st.empty()?T+1:st.back();
                st.push_back(p);
                if (p==1) break;
            }
            auto& Lv=ix.L[v];
            for (U p=1;p<=T;++p) {
                if (h[p]==INFU) continue;
                if (ix.rects.size()>=INFU) throw runtime_error("too many rectangles");
                U l=Lp[p]+1, r=Rp[p]-1, rid=(U)ix.rects.size();
                ix.rects.push_back({v,l,p,r,r-l,h[p]});
                Lv.push_back(rid);
                groups[h[p]].push_back(rid);
            }
            sort(Lv.begin(),Lv.end(),[&](U a,U b){
                return ix.rects[a].s!=ix.rects[b].s ? ix.rects[a].s>ix.rects[b].s : a<b;
            });
        }
        vector<Event>().swap(events);
        vector<U>().swap(h); vector<U>().swap(Lp); vector<U>().swap(Rp); vector<U>().swap(st);
        vector<pair<U,size_t>> order;
        for (const auto& p:groups) order.push_back({p.first,p.second.size()});
        sort(order.begin(),order.end(),[](auto a,auto b){return a.second>b.second;});
        ix.H.reserve(groups.size());
        const uint64_t budget=max_index_mb*1024ULL*1024ULL;
        for (auto [x,count]:order) {
            (void)count;
            auto it=groups.find(x);
            if (it==groups.end()) continue;
            if (ix.tree_bytes>=budget) throw runtime_error("range tree memory budget exceeded");
            RangeTree4D tree;
            tree.init(ix.rects,move(it->second),budget-ix.tree_bytes);
            ix.tree_bytes+=tree.bytes();
            ix.H.emplace(x,move(tree));
            groups.erase(it);
        }
        const double index_s=secs_since(preprocess_start);
        cerr << "preprocessing done: TRF_s="<<trf_s
             << " total_preprocess_s="<<index_s<<" rectangles="<<ix.rects.size()
             << " 4D_MiB="<<ix.tree_bytes/1048576.0
             << " RSS_MiB="<<rss_kb()/1024.0<<"\n";

        // [5] Uniform random sample WITHOUT replacement, SAME v for all mu values.
        vector<U> vs(n);
        iota(vs.begin(),vs.end(),0);
        mt19937_64 rng(seed);
        shuffle(vs.begin(),vs.end(),rng);
        if (queries && queries<vs.size()) vs.resize(queries);
        sort(vs.begin(),vs.end());
        bool header=false;
        { ifstream chk(out_csv); header=!chk || chk.peek()==ifstream::traits_type::eof(); }
        ofstream csv(out_csv,ios::app);
        if (!csv) throw runtime_error("cannot write " + out_csv);
        if (header)
            csv << "dataset,node_id,mu,k,tau,filter_nodes,filter_pairs,verified_nodes,verified_pairs,"
                   "found,witness_node,witness_time,visited_rectangles,reported_rectangles,"
                   "filter_ms,verification_ms,query_ms,"
                   "trf_preprocessing_s,total_preprocessing_s,index_mib,rss_kib,vertices,T,rectangles\n";
        vector<U> seen_filter(n,0), seen_verified(n,0);
        U filter_epoch=0, valid_epoch=0;
        vector<HashVal> sv(sketch_k), su(sketch_k);
        for (U mu:mu_vals) {
            for (U v:vs) {
                QueryMetrics m;
                if (++filter_epoch==0) {fill(seen_filter.begin(),seen_filter.end(),0);filter_epoch=1;}
                if (++valid_epoch==0) {fill(seen_verified.begin(),seen_verified.end(),0);valid_epoch=1;}

                // PHASE A: first-MinHash filtering using 4D oracle. Do not touch full sketches.
                auto f0=Clock::now();
                m.visited_rects=for_each_filtered_interval(ix,v,mu,[&](U u,U lo,U hi){
                    m.filter_pairs += uint64_t(hi)-lo+1;
                    if (seen_filter[u]!=filter_epoch) {seen_filter[u]=filter_epoch;++m.filter_nodes;}
                },&m.reported_rects);
                m.filter_ms=secs_since(f0)*1000.0;

                // PHASE B: for ALL filtered (u,t), fetch complete sketches from
                // TemporalRangeForest and apply exact supplied signature threshold.
                // A SECOND streaming traversal avoids storing C(v,mu) in RAM.
                // verification_ms includes the cost of this second range-tree traversal.
                auto v0=Clock::now();
                if (m.filter_pairs) {
                    U cached_t=INFU;
                    bool has_sv=false;
                    for_each_filtered_interval(ix,v,mu,[&](U u,U lo,U hi){
                        for (uint64_t tt=lo;tt<=hi;++tt) {
                            U t=U(tt);
                            if (t != cached_t) {
                                has_sv=forest.get_tree(v)->query_range(t,t+mu,sv.data(),0,sketch_k);
                                cached_t=t;
                            }
                            if (!has_sv) throw runtime_error("oracle output has empty query sketch");
                            if (!forest.get_tree(u)->query_range(t,t+mu,su.data(),0,sketch_k))
                                throw runtime_error("oracle output has empty candidate sketch");
                            if (sv[0]!=su[0]) throw runtime_error("oracle first hash differs from actual sketch[0]");
                            if (verify_remaining_components(sv.data(),su.data(),sketch_k,threshold_matches)) {
                                if (!m.found) { m.found = true; m.witness_node = u; m.witness_time = t; }
                                ++m.verified_pairs;
                                if (seen_verified[u]!=valid_epoch) {
                                    seen_verified[u]=valid_epoch; ++m.verified_nodes;
                                }
                            }
                        }
                    });
                }
                m.verify_ms=secs_since(v0)*1000.0;
                if (m.verified_nodes>m.filter_nodes || m.verified_pairs>m.filter_pairs || m.filter_nodes>n-1)
                    throw runtime_error("invalid candidate counts (invariants violated)");
                if (validate) exhaustive_validate(forest,n,T,v,mu,sketch_k,threshold_matches,m);

                csv<<csv_escape(dataset_path)<<','<<csv_escape(G.id_to_name[v])<<','<<mu<<','
                   <<sketch_k<<','<<fixed<<setprecision(6)<<tau<<','
                   <<m.filter_nodes<<','<<m.filter_pairs<<','
                   <<m.verified_nodes<<','<<m.verified_pairs<<','
                   <<int(m.found)<<',';
                if (m.found) csv<<csv_escape(G.id_to_name[m.witness_node]);
                csv<<',';
                if (m.found) csv<<m.witness_time;
                csv<<','<<m.visited_rects<<','<<m.reported_rects<<','
                   <<m.filter_ms<<','<<m.verify_ms<<','<<(m.filter_ms+m.verify_ms)<<','
                   <<trf_s<<','<<index_s<<','<<ix.tree_bytes/1048576.0<<','
                   <<rss_kb()<<','<<n<<','<<T<<','<<ix.rects.size()<<'\n';
            }
            csv.flush();
            cerr<<"mu="<<mu<<" done: "<<vs.size()<<" sampled nodes\n";
        }
    } catch(const exception& e) {
        cerr<<"ERROR: "<<e.what()<<'\n';
        return 1;
    }
    return 0;
}
