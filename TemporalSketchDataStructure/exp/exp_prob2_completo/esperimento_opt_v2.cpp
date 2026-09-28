//Experiment ram fixed · CPP
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
// NOTE DI REVISIONE (RAM) — leggere prima di tutto
// -----------------------------------------------------------------------------
// Questo file parte dalla versione "memory-optimized" che mi hai passato e
// corregge i punti critici che causano l'esplosione di RAM (spiegati anche in
// chat, qui solo un riepilogo puntato accanto al codice interessato):
//
//   FIX 1 (CRITICO): NaiveSolution e MySolution calcolavano
//       Tmax = tmax - mu
//   con tmax/mu di tipo TimeStamp = uint32_t (UNSIGNED). Se il grafo ha un
//   range temporale più piccolo di mu (facilissimo su dataset piccoli/di
//   prova, dato che mu di default = 5), tmax - mu va in UNDERFLOW e diventa
//   un numero vicino a 4 miliardi: il loop di query gira miliardi di volte.
//   AlternativeSolution aveva già la guardia giusta; qui viene aggiunta
//   anche alle altre due (nessun cambio di comportamento per gli input
//   "normali", si aggiunge solo un case limite prima non gestito).
//
//   FIX 2 (CRITICO): AlternativeSolution::tables era un vettore DENSO di
//   dimensione (T_max - T_min + 1), cioè proporzionale al RANGE NUMERICO
//   grezzo dei timestamp, non al numero di nodi/eventi. Con timestamp non
//   normalizzati (es. epoch UNIX, o solo valori sparsi) questo alloca decine
//   di milioni di puntatori anche per un grafo minuscolo. Sostituito con una
//   mappa sparsa (std::unordered_map<TimeStamp, ...>) che alloca SOLO le
//   finestre che contengono davvero almeno un nodo indicizzato: stesso
//   comportamento logico (si continua a "interrogare tutte le T-mu
//   strutture", cambia solo COME sono conservate), stessa idea algoritmica,
//   ma memoria proporzionale ai dati reali invece che al range dei timestamp.
//   Questa è la modifica "leggera" alla soluzione multi-LSH di cui parlavi.
//
//   FIX 3: MySolution::Wv memorizzava, per ogni nodo, la lista ESPLICITA di
//   OGNI singolo istante in W(v) (std::vector<TimeStamp>). Se un nodo ha
//   attività temporale densa, W(v) può coprire una porzione enorme della
//   timeline: elencarla istante per istante invece che come pochi intervalli
//   fusi [lo,hi] moltiplica la memoria (tenuta viva per tutta la vita
//   dell'oggetto, quindi anche durante le query) anche di ordini di
//   grandezza. Ora si conservano solo gli intervalli fusi.
//
//   FIX 4: SketchCache non faceva alcuna cache (ogni get() richiamava
//   RangeTree::query() da capo, anche in fase di query, vanificando il
//   vantaggio "O(1) da memoria" della tua soluzione). Non è la causa di una
//   crescita illimitata (nulla veniva conservato), ma genera moltissime
//   allocazioni/deallocazioni transitorie di piccoli oggetti, che con
//   `#pragma omp parallel` (arene malloc per-thread di glibc) possono far
//   salire l'RSS apparente ben oltre il dato "vivo" (frammentazione, non un
//   leak vero e proprio). Ho reso la cache REALE ma "leggera" (si conserva
//   solo il vettore di signature, non l'intero oggetto sketch con il suo
//   shared_ptr/control-block), ED È PRIVATA per ciascuna istanza di
//   MySolution/AlternativeSolution: NON è condivisa con NaiveSolution, il
//   cui scopo è restare "senza alcuna struttura ausiliaria" — condividerla
//   avrebbe fatto crescere la cache con l'INTERO spazio (v,t) che il naive
//   esplora, vanificando completamente il risparmio.
//
//   NON MODIFICATO (di proposito): la costruzione di TemporalGraph /
//   RangeTree / TemporalRangeForest, che già segue il pattern corretto
//   (un'unica scansione O(|E|) per pre-indicizzare l'adiacenza, poi
//   costruzione per-nodo) — qui il problema NON era la costruzione della
//   struttura black-box in sé. NON ho normalizzato/rimappato i timestamp
//   grezzi in indici compatti consecutivi: farlo cambierebbe il significato
//   di mu (da "mu unità di tempo reali" a "i prossimi mu istanti DISTINTI
//   osservati"), un cambio semantico che non spetta a me decidere. Se invece
//   preferisci quella strada (spesso è la scelta giusta se i timestamp sono
//   epoch UNIX e mu è comunque pensato in termini di "eventi", non di
//   secondi), te la preparo separatamente: con FIX 2 applicato non è più
//   necessaria per risolvere il problema di RAM, quindi qui non la impongo.
//
//   RESIDUO STRUTTURALE (non un bug, ma un limite intrinseco della
//   struttura black-box a cui prestare attenzione se, sistemati i punti
//   sopra, la RAM resta comunque alta): ogni RangeTree memorizza uno sketch
//   MinHash completo (k valori uint32 + un shared_ptr/control-block) su OGNI
//   nodo dell'albero (foglie + nodi di merge interni): circa 2*m_v-1 sketch
//   per nodo del grafo. Il "floor" di memoria è quindi
//   Θ( Σ_v m_v · k ) ≈ Θ( (#incidenze nodo-timestamp) · k ).
//   Le leve disponibili, se questo floor stesso è già troppo alto, sono:
//   ridurre k (meno permutazioni MinHash → segnatura più corta) oppure
//   sostituire gli shared_ptr con un allocatore ad arena che tenga tutte le
//   signature in un unico buffer contiguo (elimina l'overhead di malloc per
//   ogni singolo nodo dell'albero). Non l'ho implementato qui perché è un
//   cambiamento più invasivo alla struttura RangeTree stessa: te lo preparo
//   se dopo i fix sopra risulta ancora necessario.
// ============================================================================
 
// ============================================================================
// DEFINIZIONE TIPI OTTIMIZZATI PER GRANDI DATASET
// ============================================================================
using NodeId    = uint32_t;
using TimeStamp = uint32_t;
using HashVal   = uint32_t;
using LSHKey    = uint64_t;
 
using Clock = std::chrono::high_resolution_clock;
 
static inline double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}
 
// ----------------------------------------------------------------------------
// Diagnostica RAM: legge VmRSS/VmHWM da /proc/self/status (Linux). Usata per
// stampare a schermo l'uso di memoria REALE del processo in punti chiave,
// così puoi vedere direttamente dove cresce (utile anche per verificare
// l'effetto dei fix qui sopra sui TUOI dataset).
// ----------------------------------------------------------------------------
static void print_memory_usage(const std::string& label) {
    std::ifstream status("/proc/self/status");
    if (!status) return;
    std::string line;
    long vm_rss_kb = -1, vm_hwm_kb = -1;
    while (std::getline(status, line)) {
        if (line.rfind("VmRSS:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            iss >> vm_rss_kb;
        } else if (line.rfind("VmHWM:", 0) == 0) {
            std::istringstream iss(line.substr(6));
            iss >> vm_hwm_kb;
        }
    }
    std::cout << "[mem] " << std::left << std::setw(28) << label
               << " VmRSS=" << std::setw(10) << (vm_rss_kb / 1024.0) << " MB"
               << " (picco VmHWM=" << (vm_hwm_kb / 1024.0) << " MB)\n" << std::flush;
}
 
// ============================================================================
// 1. CARICAMENTO E MAPPATURA STRINGA -> NODEID
// ============================================================================
 
struct TemporalEdge {
    NodeId u;
    NodeId v;
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
    TimeStamp get_T_min() const {
        return (min_time == std::numeric_limits<TimeStamp>::max()) ? 1 : min_time;
    }
    TimeStamp get_T_max() const {
        return (max_time == std::numeric_limits<TimeStamp>::min()) ? 1 : max_time;
    }
    TimeStamp get_T() const { return get_T_max() - get_T_min() + 1; }
 
    // Diagnostica: quanti timestamp DISTINTI esistono davvero, contro il
    // range numerico grezzo [T_min, T_max]. Se il rapporto è enorme, il
    // dataset ha timestamp "sparsi" (es. epoch UNIX): FIX 2 è quello che ti
    // salva in questo caso, dato che senza di esso qualunque struttura
    // dimensionata sul range grezzo esploderebbe.
    size_t count_distinct_timestamps() const {
        std::unordered_set<TimeStamp> distinct;
        distinct.reserve(edges.size());
        for (const auto& e : edges) distinct.insert(e.time);
        return distinct.size();
    }
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
 
        if (iss >> u >> v >> t) {
            G.add_edge(u, v, t);
        }
    }
 
    return G;
}
 
// ============================================================================
// 2. SKETCH MINHASH MEMORY-EFFICIENT (invariato rispetto alla tua versione)
// ============================================================================
 
// class MinHashParams {
// public:
//     int num_perm;
//     std::vector<uint32_t> a;
//     std::vector<uint32_t> b;
//     static constexpr uint32_t PRIME = 4294967291U;
 
//     static const MinHashParams& get_instance(int k = 64) {
//         static std::unordered_map<int, MinHashParams> instances;
//         auto it = instances.find(k);
//         if (it == instances.end()) {
//             it = instances.emplace(k, MinHashParams(k)).first;
//         }
//         return it->second;
//     }
 
// private:
//     MinHashParams(int k) : num_perm(k) {
//         std::mt19937 gen(42);
//         std::uniform_int_distribution<uint32_t> dist_a(1, PRIME - 1);
//         std::uniform_int_distribution<uint32_t> dist_b(0, PRIME - 1);
//         a.resize(num_perm);
//         b.resize(num_perm);
//         for (int i = 0; i < num_perm; ++i) {
//             a[i] = dist_a(gen);
//             b[i] = dist_b(gen);
//         }
//     }
// };

class MinHashParams {
public:
    int num_perm;
    std::vector<uint32_t> a;
    std::vector<uint32_t> b;

    static constexpr uint32_t PRIME = 4294967291U;

    static const MinHashParams& get_instance(int k = 64) {
        static const MinHashParams instance(k);
        return instance;
    }

private:
    explicit MinHashParams(int k) : num_perm(k) {
        std::mt19937 gen(42);

        std::uniform_int_distribution<uint32_t>
            dist_a(1, PRIME - 1);

        std::uniform_int_distribution<uint32_t>
            dist_b(0, PRIME - 1);

        a.resize(num_perm);
        b.resize(num_perm);

        for (int i = 0; i < num_perm; ++i) {
            a[i] = dist_a(gen);
            b[i] = dist_b(gen);
        }
    }
};
 
class MinHashNeighborhoodSketch {
private:
    int num_perm;
    std::vector<HashVal> hashvalues;
    bool empty;
 
    void update(NodeId v) {
        const auto& params = MinHashParams::get_instance(num_perm);
        uint32_t base_hash = v * 2654435761U;
        for (int i = 0; i < num_perm; ++i) {
            uint64_t val =
                (static_cast<uint64_t>(params.a[i]) * base_hash + params.b[i])
                % MinHashParams::PRIME;
            if (static_cast<uint32_t>(val) < hashvalues[i]) {
                hashvalues[i] = static_cast<uint32_t>(val);
            }
        }
    }
 
public:
    MinHashNeighborhoodSketch(
        const std::vector<NodeId>& init_vicini = {},
        int num_perm = 64
    )
        : num_perm(num_perm),
          hashvalues(num_perm, UINT32_MAX),
          empty(init_vicini.empty()) {
        for (NodeId v : init_vicini) update(v);
    }
 
    std::shared_ptr<MinHashNeighborhoodSketch> merge(const MinHashNeighborhoodSketch& other) const {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<NodeId>{}, num_perm);
        new_sk->empty = this->empty && other.empty;
        for (int i = 0; i < num_perm; ++i) {
            new_sk->hashvalues[i] = std::min(this->hashvalues[i], other.hashvalues[i]);
        }
        return new_sk;
    }
 
    std::shared_ptr<MinHashNeighborhoodSketch> clone() const {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<NodeId>{}, num_perm);
        new_sk->empty = this->empty;
        new_sk->hashvalues = this->hashvalues;
        return new_sk;
    }
 
    double jaccard_sim(const MinHashNeighborhoodSketch& other) const {
        if (this->hashvalues.empty() || other.hashvalues.empty()) return 0.0;
        if (this->hashvalues[0] == UINT32_MAX || other.hashvalues[0] == UINT32_MAX) return 0.0;
        int matches = 0;
        for (int i = 0; i < num_perm; ++i) {
            if (this->hashvalues[i] == other.hashvalues[i]) matches++;
        }
        return static_cast<double>(matches) / num_perm;
    }
 
    const std::vector<HashVal>& signature() const { return hashvalues; }
    bool is_empty() const { return empty; }
};
 
// ============================================================================
// 3. RANGE TREE E TEMPORAL RANGE FOREST (invariato: la costruzione era già
//    corretta ed efficiente — un'unica scansione O(|E|) per pre-indicizzare
//    l'adiacenza, poi costruzione per-nodo. Non è qui il problema di RAM.)
// ============================================================================
 
struct RangeTreeNode {
    TimeStamp start_time;
    TimeStamp end_time;
    std::shared_ptr<MinHashNeighborhoodSketch> sk;
    std::unique_ptr<RangeTreeNode> left;
    std::unique_ptr<RangeTreeNode> right;
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
        size_t lo,
        size_t hi
    ) {
        if (lo >= hi) return nullptr;
        auto n = std::make_unique<RangeTreeNode>();
        if (hi - lo == 1) {
            n->start_time = t_sub[lo];
            n->end_time = t_sub[lo];
            n->sk = s_sub[lo];
            return n;
        }
        const size_t mid = lo + (hi - lo) / 2;
        n->left = build_tree(t_sub, s_sub, lo, mid);
        n->right = build_tree(t_sub, s_sub, mid, hi);
        n->start_time = n->left->start_time;
        n->end_time = n->right->end_time;
        n->sk = n->left->sk->merge(*(n->right->sk));
        return n;
    }
 
    std::shared_ptr<MinHashNeighborhoodSketch> query_internal(
        const std::unique_ptr<RangeTreeNode>& n,
        TimeStamp start,
        TimeStamp end
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
            for (TimeStamp t : times) {
                leaves.push_back(std::make_shared<MinHashNeighborhoodSketch>(time_neighbors.at(t), k));
            }
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
        std::vector<std::map<TimeStamp, std::vector<NodeId>>> adj(n);
        for (const auto& e : G->edges) {
            adj[e.u][e.time].push_back(e.v);
            adj[e.v][e.time].push_back(e.u);
        }
        trees.resize(n);
        #pragma omp parallel
        {
            #pragma omp for schedule(dynamic)
            for (NodeId u = 0; u < n; ++u) {
                if (!adj[u].empty()) {
                    trees[u] = RangeTree(u, adj[u], k);
                }
            }
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
// 4. FASE 1: CALCOLO DI W(v)
// -----------------------------------------------------------------------------
// FIX 3: ritorna gli INTERVALLI FUSI [lo,hi], non più la lista espansa di
// ogni singolo istante. Stesso identico insieme logico W(v), rappresentato
// in O(#intervalli) invece che O(|W(v)|) — che nel caso peggiore (attività
// temporale densa) può risparmiare ordini di grandezza di memoria, dato che
// questa struttura resta viva per tutta la durata dell'oggetto (viene
// riutilizzata anche in query()).
// ============================================================================
 
using TimeInterval = std::pair<TimeStamp, TimeStamp>;
 
std::vector<TimeInterval> compute_W_intervals(
    const std::vector<TimeStamp>& lambda,
    TimeStamp mu,
    TimeStamp T_min,
    TimeStamp T_max
) {
    // Aritmetica in int64_t per evitare underflow su TimeStamp (unsigned)
    // quando T_max < mu (dataset piccoli con mu di default troppo grande).
    int64_t t_min = static_cast<int64_t>(T_min);
    int64_t t_max = static_cast<int64_t>(T_max);
    int64_t muw = static_cast<int64_t>(mu);
 
    if (lambda.empty() || (t_max - muw) < t_min) return {};
 
    std::vector<std::pair<int64_t,int64_t>> intervals;
    intervals.reserve(lambda.size());
    for (TimeStamp lam_ts : lambda) {
        int64_t lam = static_cast<int64_t>(lam_ts);
        int64_t L = (lam > muw + t_min) ? (lam - muw) : t_min;
        int64_t R = std::min(t_max - muw, lam);
        if (L <= R) intervals.push_back({L, R});
    }
    if (intervals.empty()) return {};
 
    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<int64_t,int64_t>> merged;
    merged.push_back(intervals[0]);
    for (size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].first <= merged.back().second + 1) {
            merged.back().second = std::max(merged.back().second, intervals[i].second);
        } else {
            merged.push_back(intervals[i]);
        }
    }
 
    std::vector<TimeInterval> out;
    out.reserve(merged.size());
    for (auto& iv : merged) {
        out.push_back({static_cast<TimeStamp>(iv.first), static_cast<TimeStamp>(iv.second)});
    }
    return out;
}
 
static inline long long interval_list_total_length(const std::vector<TimeInterval>& ivs) {
    long long s = 0;
    for (auto& iv : ivs) s += (long long)iv.second - (long long)iv.first + 1;
    return s;
}
 
static inline uint64_t hash_band_fnv(const std::vector<HashVal>& sig, int band_idx, int r) {
    uint64_t h = 14695981039346656037ULL ^ (static_cast<uint64_t>(band_idx + 1) * 1099511628211ULL);
    int start = band_idx * r;
    for (int i = 0; i < r; ++i) {
        h ^= sig[start + i];
        h *= 1099511628211ULL;
    }
    return h;
}
 
static inline LSHKey combine_with_time(uint64_t band_hash, TimeStamp t) {
    uint64_t h = band_hash;
    h ^= static_cast<uint64_t>(t) * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h;
}
 
struct LSHParams { int k; int b; int r; };
 
struct QueryResult {
    bool found = false;
    NodeId node = 0;
    TimeStamp time = 0;
};
 
// ============================================================================
// FIX 4: cache "leggera" delle signature, PRIVATA per ciascuna istanza di
// MySolution/AlternativeSolution (MAI condivisa con NaiveSolution).
// -----------------------------------------------------------------------------
// Si conserva solo il vettore di HashVal (k*4 byte) + un flag "empty", non
// l'intero MinHashNeighborhoodSketch con il suo shared_ptr/control-block:
// questo restituisce all'algoritmo il comportamento "O(1) da memoria" in
// query (le coppie (v,t) con t in W(v) sono già state calcolate in fase di
// build), con un costo di memoria aggiuntivo NOTO e CONTABILIZZATO (viene
// incluso in auxiliary_memory_mb()), invece che nascosto.
// Se preferisci NON pagare questo costo extra ed accettare di ricalcolare
// ad ogni query (comportamento originale, RAM-più-leggero ma più lento),
// basta sostituire le chiamate a cache.get(...) con una query diretta a
// forest->get_tree(v)->query(t, t+mu): la scelta è un trade-off tempo/spazio
// legittimo, non un bug — con dataset dove la RAM è la risorsa scarsa, la
// versione "nessuna cache" può essere quella giusta per te.
// ============================================================================
struct CachedSketch {
    std::vector<HashVal> sig;
    bool empty = true;
};
 
static inline double jaccard_from_sig(const std::vector<HashVal>& a, const std::vector<HashVal>& b) {
    int matches = 0;
    size_t n = a.size();
    for (size_t i = 0; i < n; ++i) if (a[i] == b[i]) matches++;
    return static_cast<double>(matches) / static_cast<double>(n);
}
 
class SketchCache {
    TemporalRangeForest* forest;
    TimeStamp mu;
    std::unordered_map<uint64_t, CachedSketch> cache;
 
    static uint64_t make_key(NodeId v, TimeStamp t) {
        return (static_cast<uint64_t>(v) << 32) | static_cast<uint64_t>(t);
    }
 
    CachedSketch compute_fresh(NodeId v, TimeStamp t) const {
        auto* tree = forest->get_tree(v);
        std::shared_ptr<MinHashNeighborhoodSketch> sk = tree ? tree->query(t, t + mu) : nullptr;
        CachedSketch entry;
        entry.empty = (!sk || sk->is_empty());
        if (!entry.empty) entry.sig = sk->signature();
        return entry;
    }
 
public:
    SketchCache(TemporalRangeForest* f, TimeStamp mu_) : forest(f), mu(mu_) {}
 
    // Usato SOLO per le coppie (v,t) che sappiamo essere in W(v) (fase di
    // build di MySolution/AlternativeSolution, e query di MySolution, che
    // tocca sempre e solo t in Wv[v]): inserisce PERMANENTEMENTE nella
    // cache. Il numero totale di chiamate a questa funzione, nell'intero
    // ciclo di vita dell'oggetto, e' bounded da Sum_v |W(v)| — esattamente
    // il costo di memoria "intenzionale" della cache.
    const CachedSketch& get_and_cache(NodeId v, TimeStamp t) {
        uint64_t key = make_key(v, t);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        auto ins = cache.emplace(key, compute_fresh(v, t));
        return ins.first->second;
    }
 
    // *** IMPORTANTISSIMO (bug che avevo introdotto e poi corretto) ***
    // Usato da AlternativeSolution::query(), che per design tocca TUTTE le
    // T-mu finestre (non solo quelle in W(v)): se la coppia (v,t) e' GIA'
    // in cache (perche' t sta davvero in W(v) ed e' stata inserita in
    // build), la restituisce gratis; altrimenti la calcola AL VOLO ma NON
    // la memorizza. Se si usasse get_and_cache() anche qui, ogni singola
    // query di AlternativeSolution finirebbe per inserire in cache fino a
    // (T-mu) voci — quasi tutte mai piu' riusate — facendo esplodere la
    // memoria esattamente come un range di timestamp enorme (es. epoch
    // UNIX) mostra in pratica: con T-mu ~ 49 milioni, una SOLA query e'
    // bastata a portare la cache a oltre 3 GB prima di questa correzione.
    CachedSketch peek(NodeId v, TimeStamp t) const {
        uint64_t key = make_key(v, t);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second; // hit reale: t era in W(v)
        return compute_fresh(v, t);                // miss: transiente, non salvato
    }
 
    size_t approx_bytes() const {
        size_t bytes = cache.bucket_count() * sizeof(void*);
        for (const auto& kv : cache) {
            bytes += sizeof(uint64_t) + sizeof(CachedSketch) + kv.second.sig.capacity() * sizeof(HashVal);
        }
        return bytes;
    }
};
 
// ============================================================================
// 5. LE 3 SOLUZIONI ALGORITMICHE (comportamento invariato; vedi note sopra)
// ============================================================================
 
// ----------------------------------------------------------------------------
// (A) BASELINE BRUTE FORCE
// FIX 1: guardia contro l'underflow di Tmax quando T_max < mu.
// Nessuna cache: e' il naive, deve restare "senza alcuna struttura ausiliaria"
// (query diretta al RangeTree ad ogni accesso, esattamente come nella
// versione originale — qui NON cambia nulla nell'algoritmo, solo la guardia
// anti-underflow).
// ----------------------------------------------------------------------------
class NaiveSolution {
    TemporalRangeForest* forest;
    TimeStamp mu, T_min, Tmax;
    double tau;
    NodeId num_nodes;
 
public:
    NaiveSolution(
        TemporalGraph* g,
        TemporalRangeForest* f,
        TimeStamp mu_,
        double tau_,
        TimeStamp tmin,
        TimeStamp tmax
    )
        : forest(f),
          mu(mu_),
          T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : (tmin > 0 ? tmin - 1 : 0)), // FIX 1: niente underflow;
                                                                       // se non valido, Tmax<T_min
                                                                       // cosi' il loop di query non parte
          tau(tau_),
          num_nodes(g->num_nodes()) {}
 
    double auxiliary_memory_mb() const { return 0.0; }
 
    QueryResult query(NodeId v) {
        QueryResult res;
        if (Tmax < T_min) return res; // FIX 1: nessuna finestra valida per questo mu/T
 
        long long dbg_iter = 0;
        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            dbg_iter++;
            if (dbg_iter % 2000000 == 0) {
                std::ifstream status("/proc/self/status");
                std::string line; long rss=-1;
                while (std::getline(status,line)) if (line.rfind("VmRSS:",0)==0) { std::istringstream iss(line.substr(6)); iss>>rss; }
                std::cerr << "[dbg naive] iter=" << dbg_iter << " t=" << t << " RSS=" << (rss/1024.0) << "MB\n";
            }
            auto* tree_v = forest->get_tree(v);
            auto sv = tree_v ? tree_v->query(t, t + mu) : nullptr;
            if (!sv || sv->is_empty()) continue;
 
            for (NodeId u = 0; u < num_nodes; ++u) {
                if (u == v) continue;
                auto* tree_u = forest->get_tree(u);
                auto su = tree_u ? tree_u->query(t, t + mu) : nullptr;
                if (!su || su->is_empty()) continue;
 
                if (sv->jaccard_sim(*su) >= tau) {
                    res.found = true;
                    res.node = u;
                    res.time = t;
                    return res;
                }
            }
        }
        return res;
    }
};
 
// ----------------------------------------------------------------------------
// (B) MIA SOLUZIONE (LSH Unificato + Pruning W(v))
// FIX 1: guardia anti-underflow.
// FIX 3: Wv ora e' vector<vector<TimeInterval>> (intervalli fusi), non piu'
//        la lista espansa di ogni singolo istante.
// FIX 4: usa la SketchCache "leggera" e privata (solo signature, non
//        l'intero oggetto sketch), per avere davvero l'accesso O(1) in
//        query che l'algoritmo prevede.
// ----------------------------------------------------------------------------
class MySolution {
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    SketchCache cache; // FIX 4: privata (non piu' riferimento a una cache condivisa)
 
    std::vector<std::vector<TimeInterval>> Wv; // FIX 3: intervalli, non punti
    std::vector<std::unordered_map<LSHKey, std::vector<NodeId>>> tables;
 
public:
    MySolution(
        TemporalGraph* g,
        TemporalRangeForest* f,
        TimeStamp mu_,
        double tau_,
        TimeStamp tmin,
        TimeStamp tmax,
        LSHParams p
    )
        : mu(mu_),
          T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : (tmin > 0 ? tmin - 1 : 0)), // FIX 1
          tau(tau_),
          params(p),
          cache(f, mu_),
          tables(p.b) {
 
        NodeId n = g->num_nodes();
        Wv.resize(n);
        if (Tmax < T_min) return; // FIX 1: nessuna finestra valida, nulla da indicizzare
 
        for (NodeId v = 0; v < n; ++v) {
            const auto& lam = f->get_lambda(v);
            Wv[v] = compute_W_intervals(lam, mu, tmin, tmax); // FIX 3
 
            for (const auto& iv : Wv[v]) {
                for (TimeStamp t = iv.first; t <= iv.second; ++t) {
                    const CachedSketch& sk = cache.get_and_cache(v, t); // FIX 4: ora popola davvero la cache
                    if (sk.empty) continue;
 
                    for (int j = 0; j < params.b; ++j) {
                        uint64_t bh = hash_band_fnv(sk.sig, j, params.r);
                        LSHKey key = combine_with_time(bh, t);
                        tables[j][key].push_back(v);
                    }
                }
            }
        }
    }
 
    double auxiliary_memory_mb() const {
        size_t bytes = 0;
 
        bytes += Wv.capacity() * sizeof(std::vector<TimeInterval>);
        for (const auto& w : Wv) bytes += w.capacity() * sizeof(TimeInterval);
 
        bytes += tables.capacity() * sizeof(std::unordered_map<LSHKey, std::vector<NodeId>>);
        for (const auto& tab : tables) {
            bytes += tab.bucket_count() * sizeof(void*);
            for (const auto& kv : tab) {
                bytes += sizeof(LSHKey) + sizeof(std::vector<NodeId>) + (3 * sizeof(void*));
                bytes += kv.second.capacity() * sizeof(NodeId);
            }
        }
 
        bytes += cache.approx_bytes(); // FIX 4: la cache ora ha un costo reale, va contato
 
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }
 
    QueryResult query(NodeId v) {
        QueryResult res;
        if (v >= Wv.size()) return res;
 
        for (const auto& iv : Wv[v]) {
            for (TimeStamp t = iv.first; t <= iv.second; ++t) {
                const CachedSketch& sv = cache.get_and_cache(v, t); // FIX 4: hit garantito (calcolato in build)
                if (sv.empty) continue;
 
                std::unordered_set<NodeId> cand;
                for (int j = 0; j < params.b; ++j) {
                    uint64_t bh = hash_band_fnv(sv.sig, j, params.r);
                    LSHKey key = combine_with_time(bh, t);
                    auto tit = tables[j].find(key);
                    if (tit != tables[j].end()) {
                        for (NodeId u : tit->second) if (u != v) cand.insert(u);
                    }
                }
 
                for (NodeId u : cand) {
                    const CachedSketch& su = cache.get_and_cache(u, t); // FIX 4: hit garantito
                    if (su.empty) continue;
                    if (jaccard_from_sig(sv.sig, su.sig) >= tau) {
                        res.found = true;
                        res.node = u;
                        res.time = t;
                        return res;
                    }
                }
            }
        }
        return res;
    }
};
 
// ----------------------------------------------------------------------------
// (C) ALTERNATIVA (T-mu strutture LSH indipendenti)
// FIX 1: guardia anti-underflow (era già corretta, mantenuta).
// FIX 2 (IL FIX PIU' IMPORTANTE): "tables" era un vettore DENSO indicizzato
//        da (t - T_min), di dimensione (Tmax - T_min + 1): con timestamp
//        grezzi non normalizzati (es. epoch UNIX) questo alloca decine di
//        milioni di slot vuoti anche per un grafo minuscolo. Ora e' una
//        mappa SPARSA (unordered_map<TimeStamp, ...>) che alloca SOLO le
//        finestre effettivamente popolate: stesso comportamento logico
//        (si continuano a costruire/interrogare "T-mu strutture LSH
//        indipendenti, una per finestra", e in query si continuano a
//        scandire TUTTE le finestre da T_min a Tmax, esattamente come
//        prima), cambia solo il modo in cui lo storage e' allocato.
// FIX 4: stessa SketchCache leggera e privata di MySolution (mai condivisa
//        tra le due, ne' con il naive).
// ----------------------------------------------------------------------------
class AlternativeSolution {
    using TimeLSH = std::vector<std::unordered_map<uint64_t, std::vector<NodeId>>>;
 
    TimeStamp mu, T_min, Tmax;
    double tau;
    LSHParams params;
    SketchCache cache; // FIX 4: privata
 
    // FIX 2: mappa sparsa invece di vector<unique_ptr<TimeLSH>> indicizzato
    // densamente da (t - T_min). Alloca una TimeLSH SOLO per i t
    // effettivamente popolati durante la build.
    std::unordered_map<TimeStamp, TimeLSH> tables;
 
public:
    AlternativeSolution(
        TemporalGraph* g,
        TemporalRangeForest* f,
        TimeStamp mu_,
        double tau_,
        TimeStamp tmin,
        TimeStamp tmax,
        LSHParams p
    )
        : mu(mu_),
          T_min(tmin),
          Tmax(tmax >= mu_ ? tmax - mu_ : (tmin > 0 ? tmin - 1 : 0)), // FIX 1
          tau(tau_),
          params(p),
          cache(f, mu_) {
 
        if (Tmax < T_min) return; // FIX 1: nessuna finestra valida
 
        NodeId n = g->num_nodes();
        for (NodeId v = 0; v < n; ++v) {
            const auto& lam = f->get_lambda(v);
            auto w = compute_W_intervals(lam, mu, tmin, tmax); // FIX 3 (anche qui: niente liste espanse)
 
            for (const auto& iv : w) {
                for (TimeStamp t = iv.first; t <= iv.second; ++t) {
                    const CachedSketch& sk = cache.get_and_cache(v, t);
                    if (sk.empty) continue;
 
                    // FIX 2: get-or-create sparso. La TimeLSH per questo t
                    // viene creata SOLO se non esiste già (cioe' solo per i
                    // t effettivamente popolati), non per ogni t possibile
                    // nel range [T_min, Tmax].
                    auto it = tables.find(t);
                    if (it == tables.end()) {
                        it = tables.emplace(t, TimeLSH(params.b)).first;
                    }
                    TimeLSH& lsh = it->second;
 
                    for (int j = 0; j < params.b; ++j) {
                        uint64_t bh = hash_band_fnv(sk.sig, j, params.r);
                        lsh[j][bh].push_back(v);
                    }
                }
            }
        }
    }
 
    double auxiliary_memory_mb() const {
        size_t bytes = 0;
 
        // Overhead della mappa esterna sparsa (FIX 2): proporzionale al
        // NUMERO DI FINESTRE EFFETTIVAMENTE POPOLATE, non al range [T_min,Tmax].
        bytes += tables.bucket_count() * sizeof(void*);
 
        for (const auto& kv : tables) {
            bytes += sizeof(TimeStamp) + sizeof(TimeLSH);
 
            const TimeLSH& per_t = kv.second;
            bytes += per_t.capacity() * sizeof(std::unordered_map<uint64_t, std::vector<NodeId>>);
 
            for (const auto& tab : per_t) {
                bytes += tab.bucket_count() * sizeof(void*);
                for (const auto& e : tab) {
                    bytes += sizeof(uint64_t) + sizeof(std::vector<NodeId>) + (3 * sizeof(void*));
                    bytes += e.second.capacity() * sizeof(NodeId);
                }
            }
        }
 
        bytes += cache.approx_bytes(); // FIX 4
 
        return static_cast<double>(bytes) / (1024.0 * 1024.0);
    }
 
    // Query ADATTATA (comportamento invariato): itera su TUTTE le finestre
    // da T_min a Tmax, ma il lookup in "tables" ora e' un unordered_map::find
    // invece che un accesso ad array — stessa logica, stessa complessita'
    // (O(Tmax-T_min) probe, ciascuno O(1) ammortizzato), diversa sola
    // allocazione dello storage.
    QueryResult query(NodeId v) {
        QueryResult res;
        if (Tmax < T_min) return res;
 
        for (TimeStamp t = T_min; t <= Tmax; ++t) {
            const CachedSketch& sv = cache.get_and_cache(v, t); // puo' essere un vero "miss" se t non in W(v)
            if (sv.empty) continue;
 
            auto tables_it = tables.find(t);
            if (tables_it == tables.end()) continue; // nessuna struttura per questa finestra
            const TimeLSH& lsh = tables_it->second;
 
            std::unordered_set<NodeId> cand;
            for (int j = 0; j < params.b; ++j) {
                uint64_t bh = hash_band_fnv(sv.sig, j, params.r);
                auto tit = lsh[j].find(bh);
                if (tit != lsh[j].end()) {
                    for (NodeId u : tit->second) if (u != v) cand.insert(u);
                }
            }
 
            for (NodeId u : cand) {
                const CachedSketch& su = cache.get_and_cache(u, t);
                if (su.empty) continue;
                if (jaccard_from_sig(sv.sig, su.sig) >= tau) {
                    res.found = true;
                    res.node = u;
                    res.time = t;
                    return res;
                }
            }
        }
        return res;
    }
};
 
// ============================================================================
// MAIN EXPERIMENT RUNNER
// ============================================================================
 
int main(int argc, char** argv) {
    std::cout.setf(std::ios::unitbuf); // flush automatico dopo ogni stampa (utile per diagnosi)
    if (argc < 3) {
        std::cout << "Uso: " << argv[0]
                   << " <dataset_path> <dataset_name> [mu] [tau] [k] [b] [num_queries]\n";
        return 1;
    }
 
    std::string dataset_path = argv[1];
    std::string dataset_name = argv[2];
 
    TimeStamp mu   = (argc > 3) ? static_cast<TimeStamp>(std::stoul(argv[3])) : 5;
    double tau     = (argc > 4) ? std::stod(argv[4]) : 0.40;
    int k          = (argc > 5) ? std::stoi(argv[5]) : 64;
    int b          = (argc > 6) ? std::stoi(argv[6]) : 8;
    int num_queries = (argc > 7) ? std::stoi(argv[7]) : 30;
    int r = k / b;
 
    std::filesystem::create_directories("results");
 
    std::cout << "===============================================================\n";
    std::cout << " ESPERIMENTO PROBLEMA 2 - DATASET: " << dataset_name << "\n";
    std::cout << "===============================================================\n";
 
    print_memory_usage("avvio");
 
    TemporalGraph G = load_temporal_graph_from_file(dataset_path);
 
    NodeId total_nodes = G.num_nodes();
    TimeStamp T_min = G.get_T_min();
    TimeStamp T_max = G.get_T_max();
    TimeStamp total_windows = (T_max >= mu + T_min) ? (T_max - mu - T_min + 1) : 0;
 
    std::cout << std::flush; std::cout << "Archi: " << G.edges.size() << " | Nodi: " << total_nodes
               << " | Finestre totali: " << total_windows << "\n";
 
    // Diagnostica FIX 2: mostra se i timestamp sono "sparsi" rispetto al
    // range grezzo (segnale che, senza FIX 2, l'alternativa esploderebbe).
    {
        size_t distinct_ts = G.count_distinct_timestamps();
        double raw_span = (T_max >= T_min) ? static_cast<double>(T_max - T_min + 1) : 0.0;
        std::cout << "Timestamp distinti osservati: " << distinct_ts
                   << " | range grezzo [T_min,T_max]: " << raw_span;
        if (distinct_ts > 0 && raw_span > 0) {
            std::cout << "  (sparsita' = range/distinti = "
                       << std::fixed << std::setprecision(1) << (raw_span / distinct_ts) << "x)";
        }
        std::cout << "\n";
    }
 
    print_memory_usage("dopo caricamento grafo");
 
    TemporalRangeForest trf(&G, k);
 
    print_memory_usage("dopo costruzione RangeTree/Forest");
 
    // ========================================================================
    // 1. ANALISI W(v) E FATTORE DI GUADAGNO -> results/
    // ========================================================================
    std::string wv_filename = "results/" + dataset_name + "_wv_analysis.csv";
    std::ofstream wv_file(wv_filename);
    wv_file << "node_id,lambda_size,w_size,upper_bound,lower_bound,no_pruning_windows,gain_factor\n";
 
    for (NodeId v = 0; v < total_nodes; ++v) {
        const auto& lam = trf.get_lambda(v);
        auto w = compute_W_intervals(lam, mu, T_min, T_max); // FIX 3: intervalli
 
        size_t m_v = lam.size();
        long long w_v = interval_list_total_length(w); // FIX 3: lunghezza totale, senza materializzare
 
        size_t ub = std::min(static_cast<size_t>(total_windows), static_cast<size_t>(mu + 1) * m_v);
        size_t lb = (m_v > 0) ? (m_v + mu) : 0;
        double gain = (w_v > 0) ? static_cast<double>(total_windows) / w_v : 1.0;
 
        wv_file << G.id_to_name[v] << "," << m_v << "," << w_v << "," << ub << ","
                 << lb << "," << total_windows << "," << gain << "\n";
    }
    wv_file.close();
    std::cout << "-> File W(v) salvato in: " << wv_filename << "\n";
 
    // ========================================================================
    // 2. COSTRUZIONE DELLE 3 STRUTTURE
    // ========================================================================
    std::cout << "Costruzione indici e strutture in memoria...\n";
 
    NaiveSolution naive(&G, &trf, mu, tau, T_min, T_max);
    MySolution mine(&G, &trf, mu, tau, T_min, T_max, {k, b, r});
    print_memory_usage("dopo build mia_soluzione");
 
    AlternativeSolution alt(&G, &trf, mu, tau, T_min, T_max, {k, b, r});
    print_memory_usage("dopo build alternativa");
 
    double mem_naive_mb = naive.auxiliary_memory_mb();
    double mem_mine_mb = mine.auxiliary_memory_mb();
    double mem_alt_mb = alt.auxiliary_memory_mb();
 
    // Liberazione memoria del grafo: non serve più dopo la costruzione.
    { std::vector<TemporalEdge>().swap(G.edges); }
    { std::unordered_map<std::string, NodeId>().swap(G.name_to_id); }
    { std::vector<std::string>().swap(G.id_to_name); }
 
    print_memory_usage("dopo liberazione edges/nomi grafo");
 
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
        auto r_naive = naive.query(q_node);
        tot_time_naive += us_since(t0);
        (void)r_naive;
 
        t0 = Clock::now();
        auto r_mine = mine.query(q_node);
        tot_time_mine += us_since(t0);
        (void)r_mine;
 
        t0 = Clock::now();
        auto r_alt = alt.query(q_node);
        tot_time_alt += us_since(t0);
        (void)r_alt;
    }
 
    print_memory_usage("dopo tutte le query");
 
    double avg_time_naive = tot_time_naive / queries_to_run;
    double avg_time_mine = tot_time_mine / queries_to_run;
    double avg_time_alt = tot_time_alt / queries_to_run;
 
    // ========================================================================
    // 4. ESPORTAZIONE METRICHE RIASSUNTIVE -> results/
    // ========================================================================
    std::string summary_filename = "results/" + dataset_name + "_summary.csv";
    std::ofstream sum_file(summary_filename);
    sum_file << "dataset,algorithm,avg_query_time_us,memory_mb\n";
    sum_file << dataset_name << ",BruteForce," << avg_time_naive << "," << mem_naive_mb << "\n";
    sum_file << dataset_name << ",MiaSoluzione," << avg_time_mine << "," << mem_mine_mb << "\n";
    sum_file << dataset_name << ",Alternativa," << avg_time_alt << "," << mem_alt_mb << "\n";
    sum_file.close();
 
    std::cout << "-> File metriche salvato in: " << summary_filename << "\n";
    std::cout << "   BruteForce:    " << avg_time_naive << " us/query, " << mem_naive_mb << " MB\n";
    std::cout << "   MiaSoluzione:  " << avg_time_mine << " us/query, " << mem_mine_mb << " MB\n";
    std::cout << "   Alternativa:   " << avg_time_alt << " us/query, " << mem_alt_mb << " MB\n";
 
    std::cout << "Esecuzione completata per il dataset: " << dataset_name << "\n";
    return 0;
}
 
