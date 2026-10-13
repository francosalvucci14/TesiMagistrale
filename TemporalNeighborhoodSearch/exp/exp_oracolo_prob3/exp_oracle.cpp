// =============================================================================
//  exp_oracle.cpp
//  Esperimento sull'oracolo del primo blocco per NS con intervalli ad ampiezza
//  variabile (Variante 2).
//
//  Implementa:
//    - Algoritmo di generazione dell'indice (Sez. "Algoritmo di generazione
//      dell'indice"): h_v(t), pila monotona per L_p/R_p, rettangoli R_{v,p},
//      liste L(v) ordinate per span decrescente, dizionario H[x] -> range tree
//      a 4 livelli D_x.
//    - Algoritmo di query SENZA la verifica di similarita' (niente RT_u/RT_v):
//      per ogni rettangolo rilevante di v si chiama Report(H[x]; a,b,mu; inf)
//      e si raccolgono i candidati C(v,mu).
//
//  Input : uno o piu' file di dataset (edge list temporale, righe "u v t").
//  Output: CSV in --out (queries.csv, rects_per_node.csv, datasets.csv).
//
//  Compilazione:  g++ -O2 -std=c++17 -o exp_oracle exp_oracle.cpp
//  Uso:           ./exp_oracle [opzioni] data1.txt [data2.txt ...]
// =============================================================================
#include <bits/stdc++.h>
using namespace std;

typedef long long ll;
typedef uint32_t u32;

// ----------------------------------------------------------------------------
//  Punto 4D di un rettangolo: k = (l, p, r, s=r-l), v = nodo proprietario
// ----------------------------------------------------------------------------
struct Pt {
    int k[4];
    int v;
};

// ----------------------------------------------------------------------------
//  Range tree a 4 livelli D_x
//    livello 1: albero bilanciato sulla coordinata l
//    livello 2: (per ogni nodo del livello 1) albero sulla coordinata p
//    livello 3: (per ogni nodo del livello 2) albero sulla coordinata r
//    livello 4: (per ogni nodo del livello 3) lista ordinata per span s decr.
//  Ottimizzazione pratica: i nodi con <= LEAF punti non hanno struttura
//  associata, ma vengono scansionati linearmente controllando tutte e 4 le
//  condizioni (il risultato e' identico, cambia solo la memoria).
// ----------------------------------------------------------------------------
class RangeTree4D {
public:
    static int LEAF;

    vector<Pt> pts;
    size_t entries = 0;  // numero di u32/int memorizzati (stima dello spazio)

    explicit RangeTree4D(vector<Pt> points) : pts(std::move(points)) {
        vector<u32> ids(pts.size());
        iota(ids.begin(), ids.end(), 0);
        sort(ids.begin(), ids.end(), [&](u32 a, u32 b) { return pts[a].k[0] < pts[b].k[0]; });
        root = buildTree(0, ids);
    }

    // Report(D_x; a,b,mu; inf): tutti i punti nel box
    //   l <= b ; p in [a, b+mu] ; r >= a+mu ; s >= mu
    // I punti riportati sono appesi a `out` (id nel vettore pts).
    void report(ll a, ll b, ll mu, vector<u32>& out) const {
        qlo[0] = INT_MIN; qhi[0] = b;
        qlo[1] = a;       qhi[1] = b + mu;
        qlo[2] = a + mu;  qhi[2] = INT_MAX;
        qlo[3] = mu;      qhi[3] = INT_MAX;
        queryTree(root, out);
    }

private:
    struct TNode {
        int lo, hi;        // intervallo di posizioni [lo,hi) in ids dell'albero
        int left = -1, right = -1;
        int assoc = -1;    // indice in trees (dim<2) o in lists (dim==2)
    };
    struct Tree {
        int dim;
        vector<int> keys;  // chiavi ordinate (coordinata `dim`)
        vector<u32> ids;
        vector<TNode> nodes;
    };

    vector<Tree> trees;
    vector<vector<u32>> lists;
    int root = -1;
    mutable ll qlo[4], qhi[4];

    int buildTree(int dim, const vector<u32>& ids) {
        Tree T;
        T.dim = dim;
        T.ids = ids;
        T.keys.resize(ids.size());
        for (size_t i = 0; i < ids.size(); i++) T.keys[i] = pts[ids[i]].k[dim];
        entries += 2 * ids.size();
        buildNode(T, 0, (int)ids.size());
        trees.push_back(std::move(T));
        return (int)trees.size() - 1;
    }

    int buildNode(Tree& T, int lo, int hi) {
        int idx = (int)T.nodes.size();
        T.nodes.push_back(TNode());
        T.nodes[idx].lo = lo;
        T.nodes[idx].hi = hi;
        if (hi - lo > LEAF) {
            int mid = (lo + hi) / 2;
            int l = buildNode(T, lo, mid);
            int r = buildNode(T, mid, hi);
            T.nodes[idx].left = l;
            T.nodes[idx].right = r;
            vector<u32> sub(T.ids.begin() + lo, T.ids.begin() + hi);
            if (T.dim < 2) {
                int nd = T.dim + 1;
                sort(sub.begin(), sub.end(), [&](u32 a, u32 b) { return pts[a].k[nd] < pts[b].k[nd]; });
                int a = buildTree(nd, sub);
                T.nodes[idx].assoc = a;
            } else {
                sort(sub.begin(), sub.end(), [&](u32 a, u32 b) { return pts[a].k[3] > pts[b].k[3]; });
                entries += sub.size();
                lists.push_back(std::move(sub));
                T.nodes[idx].assoc = (int)lists.size() - 1;
            }
        }
        return idx;
    }

    inline bool inBox(const Pt& p) const {
        for (int d = 0; d < 4; d++)
            if (p.k[d] < qlo[d] || p.k[d] > qhi[d]) return false;
        return true;
    }

    void queryTree(int ti, vector<u32>& out) const {
        const Tree& T = trees[ti];
        if (T.ids.empty()) return;
        int d = T.dim;
        int lo = (int)(lower_bound(T.keys.begin(), T.keys.end(), (int)max<ll>(qlo[d], INT_MIN)) - T.keys.begin());
        int hi = (int)(upper_bound(T.keys.begin(), T.keys.end(), (int)min<ll>(qhi[d], INT_MAX)) - T.keys.begin());
        if (lo < hi) queryNode(T, 0, lo, hi, out);
    }

    void queryNode(const Tree& T, int ni, int i, int j, vector<u32>& out) const {
        const TNode& n = T.nodes[ni];
        if (j <= n.lo || n.hi <= i) return;
        if (n.hi - n.lo <= LEAF) {  // foglia: scansione lineare
            int from = max(n.lo, i), to = min(n.hi, j);
            for (int q = from; q < to; q++)
                if (inBox(pts[T.ids[q]])) out.push_back(T.ids[q]);
            return;
        }
        if (i <= n.lo && n.hi <= j) {  // nodo canonico
            if (T.dim < 2) {
                queryTree(n.assoc, out);
            } else {
                for (u32 id : lists[n.assoc]) {  // lista per span decrescente
                    if (pts[id].k[3] < qlo[3]) break;
                    out.push_back(id);
                }
            }
            return;
        }
        queryNode(T, n.left, i, j, out);
        queryNode(T, n.right, i, j, out);
    }
};
int RangeTree4D::LEAF = 8;

// ----------------------------------------------------------------------------
//  Rettangolo R_{v,p} = [l,p] x [p,r], etichetta x = h_v(p)
// ----------------------------------------------------------------------------
struct Rect {
    int l, p, r, x, v;
    int span() const { return r - l; }
};

// ----------------------------------------------------------------------------
//  Grafo temporale + indice
// ----------------------------------------------------------------------------
struct TemporalGraph {
    string name;
    int n = 0;
    ll m = 0;     // numero di contatti (u,v,t) letti
    int T = 0;    // istanti 1..T
    vector<ll> orig;                 // id originale dei nodi
    vector<double> tOrig;            // tOrig[t] = timestamp originale (inizio bucket) dell'istante t
    vector<int> evOff;               // CSR eventi
    vector<pair<int,int>> ev;        // (t, vicino)
};

struct Index {
    vector<int> pi;                       // pi[u] in [1..n]
    vector<int> actOff, actT, actH;       // CSR: istanti attivi e h_v(t)
    vector<Rect> rects;                   // rettangoli, contigui per nodo
    vector<int> rectOff;                  // rectOff[v]..rectOff[v+1]  = L(v)
    vector<unique_ptr<RangeTree4D>> H;    // dizionario H[x], x in [1..n]
    // statistiche
    double ms_h = 0, ms_rect = 0, ms_tree = 0;
    size_t entries = 0;
    int distinctValues = 0;
};

static double nowMs() {
    return chrono::duration<double, milli>(chrono::steady_clock::now().time_since_epoch()).count();
}

// ---------------------------------------------------------------------------
//  Lettura dataset: righe "u v t" (separatori: spazi, tab, virgole, ';').
//  Righe che iniziano con # o % vengono ignorate.
// ---------------------------------------------------------------------------
static bool loadGraph(const string& path, const string& timeMode, double bucket, TemporalGraph& G) {
    ifstream in(path);
    if (!in) { cerr << "Impossibile aprire " << path << "\n"; return false; }
    struct Raw { ll u, v; double t; };
    vector<Raw> raw;
    string line;
    while (getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        for (char& c : line) if (c == ',' || c == ';' || c == '\t') c = ' ';
        const char* s = line.c_str();
        char* e;
        ll u = strtoll(s, &e, 10); if (e == s) continue; s = e;
        ll v = strtoll(s, &e, 10); if (e == s) continue; s = e;
        double t = strtod(s, &e);  if (e == s) continue;
        if (u == v) continue;  // self-loop
        raw.push_back({u, v, t});
    }
    if (raw.empty()) { cerr << "Nessun contatto valido in " << path << "\n"; return false; }

    // mappa nodi -> [0,n)
    unordered_map<ll,int> id;
    id.reserve(raw.size() / 2 + 16);
    auto getId = [&](ll x) {
        auto it = id.find(x);
        if (it != id.end()) return it->second;
        int k = (int)G.orig.size();
        id[x] = k; G.orig.push_back(x);
        return k;
    };
    vector<int> U(raw.size()), V(raw.size());
    for (size_t i = 0; i < raw.size(); i++) { U[i] = getId(raw[i].u); V[i] = getId(raw[i].v); }
    G.n = (int)G.orig.size();
    G.m = (ll)raw.size();

    // discretizzazione del tempo in 1..T
    vector<int> Tt(raw.size());
    if (timeMode == "rank") {
        vector<double> ts(raw.size());
        for (size_t i = 0; i < raw.size(); i++) ts[i] = raw[i].t;
        sort(ts.begin(), ts.end());
        ts.erase(unique(ts.begin(), ts.end()), ts.end());
        for (size_t i = 0; i < raw.size(); i++)
            Tt[i] = (int)(lower_bound(ts.begin(), ts.end(), raw[i].t) - ts.begin()) + 1;
        G.T = (int)ts.size();
        G.tOrig.assign(G.T + 1, 0);
        for (int i = 0; i < G.T; i++) G.tOrig[i + 1] = ts[i];
    } else {  // raw: bucket di ampiezza `bucket`
        double tmin = raw[0].t;
        for (auto& r : raw) tmin = min(tmin, r.t);
        int mx = 0;
        for (size_t i = 0; i < raw.size(); i++) {
            Tt[i] = (int)floor((raw[i].t - tmin) / bucket) + 1;
            mx = max(mx, Tt[i]);
        }
        G.T = mx;
        G.tOrig.assign(G.T + 1, 0);
        for (int i = 1; i <= G.T; i++) G.tOrig[i] = tmin + (i - 1) * bucket;
    }

    // CSR degli eventi (entrambe le direzioni)
    G.evOff.assign(G.n + 1, 0);
    for (size_t i = 0; i < raw.size(); i++) { G.evOff[U[i] + 1]++; G.evOff[V[i] + 1]++; }
    for (int v = 0; v < G.n; v++) G.evOff[v + 1] += G.evOff[v];
    G.ev.resize(2 * raw.size());
    vector<int> pos(G.evOff.begin(), G.evOff.end() - 1);
    for (size_t i = 0; i < raw.size(); i++) {
        G.ev[pos[U[i]]++] = {Tt[i], V[i]};
        G.ev[pos[V[i]]++] = {Tt[i], U[i]};
    }
    return true;
}

// ---------------------------------------------------------------------------
//  BuildIndex3
// ---------------------------------------------------------------------------
static void buildIndex(TemporalGraph& G, Index& I, uint64_t seed) {
    int n = G.n, T = G.T;
    // permutazione pi di V (rango in [1..n])
    I.pi.resize(n);
    {
        vector<int> perm(n);
        iota(perm.begin(), perm.end(), 1);
        mt19937_64 rng(seed);
        shuffle(perm.begin(), perm.end(), rng);
        for (int u = 0; u < n; u++) I.pi[u] = perm[u];
    }

    // --- h_v(t) = min pi(N_t(v)), solo sugli istanti attivi Lambda(v) ---
    double t0 = nowMs();
    I.actOff.assign(n + 1, 0);
    I.actT.clear(); I.actH.clear();
    for (int v = 0; v < n; v++) {
        vector<pair<int,int>> e;  // (t, pi[vicino])
        e.reserve(G.evOff[v + 1] - G.evOff[v]);
        for (int q = G.evOff[v]; q < G.evOff[v + 1]; q++) e.push_back({G.ev[q].first, I.pi[G.ev[q].second]});
        sort(e.begin(), e.end());
        for (size_t q = 0; q < e.size(); q++) {
            if (q == 0 || e[q].first != e[q - 1].first) {
                I.actT.push_back(e[q].first);
                I.actH.push_back(e[q].second);  // primo = minimo (ordinato)
            }
        }
        I.actOff[v + 1] = (int)I.actT.size();
    }
    I.ms_h = nowMs() - t0;

    // --- L_p, R_p con pila monotona; rettangoli e lista L(v) ---
    // Lavorando solo sugli istanti attivi si ottengono gli stessi L_p, R_p
    // dell'algoritmo sugli istanti [T]: gli istanti non attivi hanno
    // b=(+inf,t) e non sono mai < di un punto con h finito.
    // Ordine totale b: (h,t). Per j<i: b_j<b_i <=> h_j<=h_i ; per j>i: <=> h_j<h_i.
    t0 = nowMs();
    I.rects.clear();
    I.rectOff.assign(n + 1, 0);
    vector<int> Lq, Rq, stk;
    for (int v = 0; v < n; v++) {
        int s = I.actOff[v], e = I.actOff[v + 1], m = e - s;
        Lq.assign(m, 0); Rq.assign(m, T + 1);
        stk.clear();
        for (int i = 0; i < m; i++) {
            while (!stk.empty() && I.actH[s + stk.back()] > I.actH[s + i]) stk.pop_back();
            Lq[i] = stk.empty() ? 0 : I.actT[s + stk.back()];
            stk.push_back(i);
        }
        stk.clear();
        for (int i = m - 1; i >= 0; i--) {
            while (!stk.empty() && I.actH[s + stk.back()] >= I.actH[s + i]) stk.pop_back();
            Rq[i] = stk.empty() ? T + 1 : I.actT[s + stk.back()];
            stk.push_back(i);
        }
        size_t base = I.rects.size();
        for (int i = 0; i < m; i++)
            I.rects.push_back({Lq[i] + 1, I.actT[s + i], Rq[i] - 1, I.actH[s + i], v});
        // ordina L(v) per span decrescente
        sort(I.rects.begin() + base, I.rects.end(),
             [](const Rect& a, const Rect& b) { return a.span() > b.span(); });
        I.rectOff[v + 1] = (int)I.rects.size();
    }
    I.ms_rect = nowMs() - t0;

    // --- Q_x e range tree D_x ---
    t0 = nowMs();
    vector<int> cnt(n + 2, 0);
    for (auto& r : I.rects) cnt[r.x]++;
    vector<vector<Pt>> dummy;  // (non usato: costruiamo un x alla volta)
    vector<int> off(n + 2, 0);
    for (int x = 1; x <= n; x++) off[x + 1] = off[x] + cnt[x];
    vector<int> byX(I.rects.size());
    vector<int> fill(off.begin(), off.end());
    for (size_t i = 0; i < I.rects.size(); i++) byX[fill[I.rects[i].x]++] = (int)i;
    I.H.clear();
    I.H.resize(n + 1);
    I.entries = 0;
    I.distinctValues = 0;
    for (int x = 1; x <= n; x++) {
        if (cnt[x] == 0) continue;
        vector<Pt> P;
        P.reserve(cnt[x]);
        for (int q = off[x]; q < off[x + 1]; q++) {
            const Rect& r = I.rects[byX[q]];
            P.push_back({{r.l, r.p, r.r, r.r - r.l}, r.v});
        }
        I.H[x].reset(new RangeTree4D(std::move(P)));
        I.entries += I.H[x]->entries;
        I.distinctValues++;
    }
    I.ms_tree = nowMs() - t0;
}

// ---------------------------------------------------------------------------
//  Query3 senza verifica: ritorna le statistiche dei candidati C(v,mu)
// ---------------------------------------------------------------------------
struct QRes {
    int kRel = 0;        // numero di rettangoli rilevanti di v (iterazioni)
    ll pairs = 0;        // M = |C(v,mu)| = sum_u |I_u|
    int nodes = 0;       // numero di nodi u distinti con almeno un candidato
    double us = 0;       // tempo di query (microsecondi)
};

// Se `cl` != nullptr, per ogni rettangolo R_u riportato appende (u, t_lo, t_hi) con
// [t_lo,t_hi] = I_u = [a,b] ∩ X_{R_u}(mu): sono tutti e soli i t tali che (u,t) in C(v,mu).
static QRes queryOracle(const Index& I, int T, int v, ll mu, vector<int>& stamp, int stampId,
                        vector<u32>& buf, vector<array<int,3>>* cl = nullptr) {
    QRes res;
    double t0 = nowMs();
    if (mu >= T) return res;
    for (int q = I.rectOff[v]; q < I.rectOff[v + 1]; q++) {
        const Rect& R = I.rects[q];
        if (R.r - R.l < mu) break;  // gli span sono decrescenti
        res.kRel++;
        ll a = max<ll>(R.l, R.p - mu), b = min<ll>(R.p, R.r - mu);
        const RangeTree4D& D = *I.H[R.x];
        buf.clear();
        D.report(a, b, mu, buf);
        for (u32 id : buf) {
            const Pt& P = D.pts[id];
            if (P.v == v) continue;
            ll lo = max<ll>(a, max<ll>(P.k[0], P.k[1] - mu));
            ll hi = min<ll>(b, min<ll>(P.k[1], P.k[2] - mu));
            ll c = hi - lo + 1;  // |I_u| = |[a,b] ∩ X_{R_u}(mu)|
            if (c <= 0) { cerr << "ERRORE interno: candidato con intersezione vuota\n"; exit(2); }
            res.pairs += c;
            if (cl) cl->push_back({P.v, (int)lo, (int)hi});
            if (stamp[P.v] != stampId) { stamp[P.v] = stampId; res.nodes++; }
        }
    }
    res.us = (nowMs() - t0) * 1000.0;
    return res;
}

// ---------------------------------------------------------------------------
//  Verifica (opzionale): calcolo brute force di C(v,mu) con finestre scorrevoli
// ---------------------------------------------------------------------------
static bool bruteForce(const TemporalGraph& G, const Index& I, int v, int mu, ll& pairs, int& nodes,
                       set<pair<int,int>>& S) {
    int n = G.n, T = G.T;
    const int INF = INT_MAX;
    auto windowMin = [&](int u) {  // h_u([t,t+mu]) per t=1..T-mu
        vector<int> h(T + 2, INF);
        for (int q = I.actOff[u]; q < I.actOff[u + 1]; q++) h[I.actT[q]] = I.actH[q];
        vector<int> res(T - mu + 1, INF);
        deque<int> dq;
        for (int i = 1; i <= T; i++) {
            while (!dq.empty() && h[dq.back()] >= h[i]) dq.pop_back();
            dq.push_back(i);
            if (dq.front() <= i - mu - 1) dq.pop_front();
            if (i >= mu + 1) res[i - mu] = h[dq.front()];
        }
        return res;
    };
    vector<int> hv = windowMin(v);
    pairs = 0; nodes = 0; S.clear();
    for (int u = 0; u < n; u++) {
        if (u == v) continue;
        vector<int> hu = windowMin(u);
        ll c = 0;
        for (int t = 1; t <= T - mu; t++)
            if (hv[t] != INF && hv[t] == hu[t]) { c++; S.insert({u, t}); }
        pairs += c;
        if (c > 0) nodes++;
    }
    return true;
}

// ---------------------------------------------------------------------------
//  CLI
// ---------------------------------------------------------------------------
static vector<double> parseList(const string& s) {
    vector<double> r;
    stringstream ss(s);
    string tok;
    while (getline(ss, tok, ',')) if (!tok.empty()) r.push_back(stod(tok));
    return r;
}
static string stem(const string& p) {
    size_t a = p.find_last_of("/\\");
    string f = (a == string::npos) ? p : p.substr(a + 1);
    size_t d = f.find_last_of('.');
    return d == string::npos ? f : f.substr(0, d);
}

int main(int argc, char** argv) {
    string outDir = "results", timeMode = "raw";
    double bucket = 1.0;
    int Q = 100, verifyQ = 0;
    string dump = "intervals";  // none | intervals | expand
    uint64_t seed = 42;
    vector<double> muFrac = {0.0, 0.01, 0.05, 0.1, 0.25};
    vector<double> muAbs;
    vector<string> files;

    for (int i = 1; i < argc; i++) {
        string a = argv[i];
        auto next = [&]() -> string {
            if (i + 1 >= argc) { cerr << "Valore mancante per " << a << "\n"; exit(1); }
            return argv[++i];
        };
        if (a == "--out") outDir = next();
        else if (a == "--queries") Q = stoi(next());
        else if (a == "--mu-frac") muFrac = parseList(next());
        else if (a == "--mu") muAbs = parseList(next());
        else if (a == "--bucket") bucket = stod(next());
        else if (a == "--time-mode") timeMode = next();
        else if (a == "--seed") seed = stoull(next());
        else if (a == "--leaf") RangeTree4D::LEAF = stoi(next());
        else if (a == "--verify") verifyQ = stoi(next());
        else if (a == "--dump") dump = next();
        else if (a == "-h" || a == "--help") {
            cout <<
"Uso: exp_oracle [opzioni] dataset1 [dataset2 ...]\n"
"  Dataset: righe 'u v t' (# e % = commenti)\n"
"  --out DIR          cartella output (default results)\n"
"  --queries Q        nodi v campionati per dataset (default 100)\n"
"  --mu-frac a,b,..   valori di mu come frazione di (T-1)  (default 0,0.01,0.05,0.1,0.25)\n"
"  --mu a,b,..        valori assoluti di mu (si sommano a --mu-frac)\n"
"  --time-mode M      raw (bucket di ampiezza --bucket) | rank (rango dei timestamp distinti)\n"
"  --bucket B         ampiezza del bucket temporale in modalita' raw (default 1)\n"
"  --seed S           seed (permutazione pi e campionamento)\n"
"  --leaf L           soglia di foglia del range tree (default 8)\n"
"  --dump M           candidati (u,t): none | intervals (default, (u,t_lo,t_hi)) | expand (una riga per (u,t))\n"
"  --verify K         confronta con brute force le prime K query (solo grafi piccoli)\n";
            return 0;
        } else if (!a.empty() && a[0] == '-') { cerr << "Opzione sconosciuta " << a << "\n"; return 1; }
        else files.push_back(a);
    }
    if (files.empty()) { cerr << "Nessun dataset. Usa --help.\n"; return 1; }
    if (dump != "none" && dump != "intervals" && dump != "expand") { cerr << "--dump non valido\n"; return 1; }
    if (timeMode != "raw" && timeMode != "rank") { cerr << "time-mode non valido\n"; return 1; }
    if (system(("mkdir -p '" + outDir + "'").c_str()) != 0) { cerr << "mkdir fallita\n"; return 1; }

    ofstream fq(outDir + "/queries.csv"), fr(outDir + "/rects_per_node.csv"), fd(outDir + "/datasets.csv");
    ofstream fc;
    if (dump != "none") fc.open(outDir + "/candidates.csv");
    if (dump == "intervals") fc << "dataset,v,mu,u,t_lo,t_hi,t_orig_lo,t_orig_hi\n";
    else if (dump == "expand") fc << "dataset,v,mu,u,t,t_orig\n";
    fq << "dataset,n,T,v,mu,mu_over_T,num_rects_v,k_rel_rects,cand_pairs,cand_nodes,cand_nodes_pct,query_us\n";
    fr << "dataset,node,num_rects,num_active_times,queried\n";
    fd << "dataset,n,contacts,T,active_pairs,num_rects,distinct_values,build_h_ms,build_rects_ms,build_trees_ms,index_entries\n";

    for (const string& path : files) {
        TemporalGraph G;
        G.name = stem(path);
        cerr << "[" << G.name << "] caricamento...\n";
        if (!loadGraph(path, timeMode, bucket, G)) continue;
        cerr << "[" << G.name << "] n=" << G.n << " contatti=" << G.m << " T=" << G.T << "\n";

        Index I;
        buildIndex(G, I, seed);
        int n = G.n, T = G.T;
        size_t N = I.rects.size();
        cerr << "[" << G.name << "] rettangoli N=" << N << " valori distinti=" << I.distinctValues
             << " build(h/rect/trees ms)=" << I.ms_h << "/" << I.ms_rect << "/" << I.ms_tree << "\n";
        fd << G.name << "," << n << "," << G.m << "," << T << "," << I.actT.size() << "," << N << ","
           << I.distinctValues << "," << I.ms_h << "," << I.ms_rect << "," << I.ms_tree << ","
           << I.entries << "\n";

        // --- scelta dei nodi v: campione uniforme tra i nodi con >=1 rettangolo ---
        vector<int> cand;
        for (int v = 0; v < n; v++) if (I.rectOff[v + 1] > I.rectOff[v]) cand.push_back(v);
        mt19937_64 rng(seed + 1);
        shuffle(cand.begin(), cand.end(), rng);
        if ((int)cand.size() > Q) cand.resize(Q);

        // --- valori di mu ---
        set<ll> muSet;
        for (double f : muFrac) muSet.insert((ll)floor(f * (T - 1)));
        for (double m : muAbs) if (m >= 0 && m < T) muSet.insert((ll)m);
        vector<ll> mus(muSet.begin(), muSet.end());

        vector<char> queried(n, 0);
        vector<int> stamp(n, -1);
        vector<u32> buf;
        int stampId = 0, verified = 0, mismatches = 0;
        for (int v : cand) {
            queried[v] = 1;
            for (ll mu : mus) {
                vector<array<int,3>> cl;
                bool needCl = (dump != "none") || (verified < verifyQ);
                QRes r = queryOracle(I, T, v, mu, stamp, stampId++, buf, needCl ? &cl : nullptr);
                if (dump == "intervals") {
                    for (auto& c : cl)
                        fc << G.name << "," << G.orig[v] << "," << mu << "," << G.orig[c[0]] << "," << c[1] << "," << c[2]
                           << "," << G.tOrig[c[1]] << "," << G.tOrig[c[2]] << "\n";
                } else if (dump == "expand") {
                    for (auto& c : cl)
                        for (int t = c[1]; t <= c[2]; t++)
                            fc << G.name << "," << G.orig[v] << "," << mu << "," << G.orig[c[0]] << "," << t << "," << G.tOrig[t] << "\n";
                }
                double pct = (n > 1) ? 100.0 * r.nodes / (n - 1) : 0.0;
                fq << G.name << "," << n << "," << T << "," << G.orig[v] << "," << mu << ","
                   << (double)mu / T << "," << (I.rectOff[v + 1] - I.rectOff[v]) << "," << r.kRel << ","
                   << r.pairs << "," << r.nodes << "," << pct << "," << r.us << "\n";
                if (verified < verifyQ && (ll)n * T <= 20000000LL) {
                    ll bp; int bn; set<pair<int,int>> bs;
                    bruteForce(G, I, v, (int)mu, bp, bn, bs);
                    // insieme (u,t) prodotto dall'indice (espandendo gli intervalli); t in [T-mu]
                    set<pair<int,int>> is; bool dup = false;
                    for (auto& c : cl)
                        for (int t = c[1]; t <= c[2]; t++) {
                            if (c[1] < 1 || t > T - (int)mu) dup = true;
                            if (!is.insert({c[0], t}).second) dup = true;  // ogni coppia una sola volta
                        }
                    if (dup || is != bs || bp != r.pairs || bn != r.nodes) {
                        mismatches++;
                        cerr << "  MISMATCH v=" << G.orig[v] << " mu=" << mu << ": indice (pairs,nodes)=("
                             << r.pairs << "," << r.nodes << ") brute=(" << bp << "," << bn << ")"
                             << (dup ? " [duplicati/t fuori range]" : "") << (is != bs ? " [insiemi (u,t) diversi]" : "") << "\n";
                    }
                    verified++;
                }
            }
        }
        if (verifyQ > 0)
            cerr << "[" << G.name << "] verifica: " << verified << " query confrontate, "
                 << mismatches << " discrepanze\n";

        for (int v = 0; v < n; v++)
            fr << G.name << "," << G.orig[v] << "," << (I.rectOff[v + 1] - I.rectOff[v]) << ","
               << (I.actOff[v + 1] - I.actOff[v]) << "," << (int)queried[v] << "\n";
        cerr << "[" << G.name << "] fatto.\n";
    }
    cerr << "Risultati in " << outDir << "/\n";
    return 0;
}