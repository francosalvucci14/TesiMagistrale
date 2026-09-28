// =============================================================================
// experiment_mu_variation.cpp
// -----------------------------------------------------------------------------
// Esperimento (Problema 2, variante sliding-window, sol_prob_2.tex) che
// confronta, al VARIARE DI mu, tre soluzioni:
//
//   (A) NAIVE (baseline)  : data v in query, si confrontano TUTTI i nodi
//                            u != v e TUTTI gli intervalli [t,t+mu] con
//                            t in {1,...,T-mu}, verificando la similarita'
//                            degli sketch.
//   (B) MIA SOLUZIONE      : sol_prob_2.tex — Fase 1 (pruning con W(v)) +
//                            Fase 2 con UN SOLO indice LSH L={H_1,...,H_b}
//                            con chiave aumentata K_j(v,t)=(Hash_j(Band_j(v,t)),t).
//   (C) ALTERNATIVA        : come (B), ma invece di un solo indice LSH se ne
//                            istanziano T-mu, uno per ciascun intervallo
//                            [t,t+mu] (query adattata di conseguenza: niente
//                            timestamp nella chiave, si interroga la
//                            struttura L_t specifica per quell'istante,
//                            iterando su TUTTE le T-mu strutture).
//
// INPUT
// -----
//   ./experiment_mu_variation                          -> genera un grafo
//        temporale Erdos-Renyi con parametri di default (n,T,p,seed)
//   ./experiment_mu_variation dataset.txt               -> carica il grafo
//        da file di testo, un arco per riga: "u v t" (spazio-separati)
//   ./experiment_mu_variation --generate n T p [seed]   -> genera un grafo
//        Erdos-Renyi con parametri custom
//
// Il modello Erdos-Renyi usato è quello "a snapshot": per ciascun istante
// t=1..T si genera un grafo G(n,p) indipendente (ogni coppia di nodi e'
// collegata con probabilita' p) e i suoi archi vengono etichettati con quel
// t. E' la naturale estensione del modello di Erdos-Renyi al caso temporale.
//
// mu VIENE FATTO VARIARE in una serie di valori dipendenti da T (che sia
// stato letto dal dataset o dal grafo generato): mu_i = round(f_i * T) per
// una lista di frazioni f_i (vedi MU_FRACTIONS sotto).
//
// Per ciascun valore di mu si eseguono NUM_QUERIES_PER_MU interrogazioni; ad
// ogni iterazione il nodo v e' scelto UNIFORMEMENTE A CASO tra i nodi del
// grafo (lo stesso v e' usato per tutti e tre i metodi, per un confronto
// equo). Il file CSV prodotto contiene ESATTAMENTE:
//   - il tempo di ciascuna query, al variare di mu (colonna query_time_us)
//   - lo spazio occupato da ciascuna soluzione (colonna space_entries)
// (mu, method, query_id, v sono solo le chiavi identificative necessarie a
// organizzare questi due dati, non misure aggiuntive).
//
// Compilazione:
//   g++ -O2 -std=c++17 experiment_mu_variation.cpp -o experiment_mu_variation
// =============================================================================

#include "trf_core.hpp"

#include <iostream>
#include <fstream>
#include <sstream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <set>
#include <string>
#include <random>
#include <chrono>
#include <algorithm>
#include <cmath>
#include <cstdint>

using Clock = std::chrono::high_resolution_clock;
static inline double us_since(const Clock::time_point& t0) {
    return std::chrono::duration<double, std::micro>(Clock::now() - t0).count();
}

// =============================================================================
// 1. CARICAMENTO DATASET ( "u v t" per riga ) / GENERAZIONE ERDOS-RENYI
// =============================================================================

// Carica un grafo temporale da file di testo: ogni riga "u v t".
// Righe vuote o che iniziano con '#' vengono ignorate.
TemporalGraph load_temporal_graph_from_file(const std::string& path, int& T_out) {
    TemporalGraph G;
    std::ifstream in(path);
    if (!in) {
        std::cerr << "Errore: impossibile aprire il file dataset '" << path << "'\n";
        std::exit(1);
    }
    std::string line;
    int max_t = 0;
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#') continue;
        std::istringstream iss(line);
        std::string u, v;
        int t;
        if (!(iss >> u >> v >> t)) continue; // riga malformata: si ignora
        G.add_edge(u, v, t);
        max_t = std::max(max_t, t);
    }
    T_out = max_t;
    return G;
}

// Genera un grafo temporale secondo il modello di Erdos-Renyi "a snapshot":
// per ogni istante t=1..T, un grafo G(n,p) indipendente (ogni coppia di nodi
// collegata con probabilita' p), i cui archi vengono etichettati con quel t.
TemporalGraph generate_er_temporal_graph(int n, int T, double p, unsigned seed) {
    TemporalGraph G;
    std::mt19937 rng(seed);
    std::uniform_real_distribution<double> unif(0.0, 1.0);
    for (int t = 1; t <= T; ++t) {
        for (int i = 0; i < n; ++i) {
            for (int j = i + 1; j < n; ++j) {
                if (unif(rng) < p) {
                    G.add_edge(std::to_string(i), std::to_string(j), t);
                }
            }
        }
    }
    return G;
}

// =============================================================================
// 2. UTILITY CONDIVISE: Lambda(v), W(v), banding LSH
//    (identiche, in sostanza, a quelle del primo esperimento)
// =============================================================================

std::vector<int> compute_lambda(const TemporalGraph& G, const std::string& v) {
    std::set<int> times;
    for (const auto& e : G.edges) {
        if (e.u == v || e.v == v) times.insert(e.time);
    }
    return std::vector<int>(times.begin(), times.end());
}

std::vector<std::pair<int,int>> compute_W_intervals(const std::vector<int>& lambda_v, int mu, int Tmax) {
    std::vector<std::pair<int,int>> intervals;
    for (int lam : lambda_v) {
        int lo = std::max(1, lam - mu);
        int hi = std::min(Tmax, lam);
        if (lo <= hi) intervals.push_back({lo, hi});
    }
    if (intervals.empty()) return {};
    std::sort(intervals.begin(), intervals.end());
    std::vector<std::pair<int,int>> merged;
    merged.push_back(intervals[0]);
    for (size_t i = 1; i < intervals.size(); ++i) {
        if (intervals[i].first <= merged.back().second + 1) {
            merged.back().second = std::max(merged.back().second, intervals[i].second);
        } else {
            merged.push_back(intervals[i]);
        }
    }
    return merged;
}

static inline uint64_t hash_band(const std::vector<uint32_t>& sig, int band_idx, int r) {
    uint64_t h = 1469598103934665603ULL ^ (0x9E3779B97F4A7C15ULL * (uint64_t)(band_idx + 1));
    int base = band_idx * r;
    for (int i = 0; i < r; ++i) {
        h ^= (uint64_t)sig[base + i];
        h *= 1099511628211ULL;
    }
    return h;
}

static inline uint64_t combine_with_time(uint64_t band_hash, int t) {
    uint64_t h = band_hash;
    h ^= (uint64_t)(uint32_t)t * 0x9E3779B97F4A7C15ULL;
    h ^= h >> 29;
    h *= 0xBF58476D1CE4E5B9ULL;
    return h;
}

struct LSHParams { int k; int b; int r; };

// Cache delle chiamate RT_v.query(...) gia' effettuate (una per algoritmo),
// cosi' che, come nello pseudocodice, un valore gia' calcolato in fase di
// build venga riusato gratis in fase di query.
class SketchCache {
    TemporalRangeForest* forest;
    int mu;
    std::unordered_map<std::string, std::shared_ptr<MinHashNeighborhoodSketch>> cache;
public:
    SketchCache(TemporalRangeForest* f, int mu_) : forest(f), mu(mu_) {}
    std::shared_ptr<MinHashNeighborhoodSketch> get(const std::string& v, int t) {
        std::string key = v; key += '#'; key += std::to_string(t);
        auto it = cache.find(key);
        if (it != cache.end()) return it->second;
        RangeTree* rt = forest->get_tree_for_node(v);
        std::shared_ptr<MinHashNeighborhoodSketch> sk;
        if (rt) {
            auto generic = rt->query(v, t, t + mu);
            sk = std::dynamic_pointer_cast<MinHashNeighborhoodSketch>(generic);
        }
        cache.emplace(std::move(key), sk);
        return sk;
    }
};

struct QueryResult {
    bool found = false;
    std::string partner;
    int time = -1;
};

// =============================================================================
// 3. ALGORITMO (A): NAIVE (baseline)
// =============================================================================
class NaiveSolution {
    TemporalGraph* G;
    int mu, Tmax;
    double tau;
    std::vector<std::string> nodes;
    SketchCache cache;
public:
    NaiveSolution(TemporalGraph* g, TemporalRangeForest* forest, int mu_, double tau_, int T_)
        : G(g), mu(mu_), Tmax(T_ - mu_), tau(tau_), cache(forest, mu_) {
        nodes = G->nodes();
    }
    long long space_entries() const { return 0; } // nessuna struttura ausiliaria

    QueryResult query(const std::string& v) {
        QueryResult res;
        for (int t = 1; t <= Tmax; ++t) {
            auto sv = cache.get(v, t);
            if (!sv) continue;
            for (const auto& u : nodes) {
                if (u == v) continue;
                auto su = cache.get(u, t);
                if (!su) continue;
                if (sv->jaccard_sim(*su) >= tau) {
                    res.found = true; res.partner = u; res.time = t;
                    return res;
                }
            }
        }
        return res;
    }
};

// =============================================================================
// 4. ALGORITMO (B): MIA SOLUZIONE — LSH unificato con chiave aumentata dal tempo
// =============================================================================
class MySolution {
    TemporalGraph* G;
    int mu, Tmax;
    double tau;
    LSHParams params;
    SketchCache cache;
    std::unordered_map<std::string, std::vector<std::pair<int,int>>> Wv;
    std::vector<std::unordered_map<uint64_t, std::vector<std::string>>> tables;

public:
    MySolution(TemporalGraph* g, TemporalRangeForest* forest, int mu_, double tau_, int T_, LSHParams p)
        : G(g), mu(mu_), Tmax(T_ - mu_), tau(tau_), params(p), cache(forest, mu_), tables(p.b) {
        // Fase 1 + Fase 2 (BuildLSH-Index2)
        for (const auto& v : G->nodes()) {
            auto lambda_v = compute_lambda(*G, v);
            auto w = compute_W_intervals(lambda_v, mu, Tmax);
            Wv[v] = w;
            for (auto& iv : w) {
                for (int t = iv.first; t <= iv.second; ++t) {
                    auto sk = cache.get(v, t);
                    if (!sk) continue;
                    const auto& sig = sk->signature();
                    for (int j = 0; j < params.b; ++j) {
                        uint64_t bh = hash_band(sig, j, params.r);
                        uint64_t key = combine_with_time(bh, t);
                        tables[j][key].push_back(v);
                    }
                }
            }
        }
    }

    // Query2
    QueryResult query(const std::string& v) {
        QueryResult res;
        auto it = Wv.find(v);
        if (it == Wv.end()) return res;
        for (auto& iv : it->second) {
            for (int t = iv.first; t <= iv.second; ++t) {
                auto sv = cache.get(v, t); // hit garantito (già calcolato in build)
                if (!sv) continue;
                const auto& sig = sv->signature();
                std::unordered_set<std::string> cand;
                for (int j = 0; j < params.b; ++j) {
                    uint64_t bh = hash_band(sig, j, params.r);
                    uint64_t key = combine_with_time(bh, t);
                    auto tit = tables[j].find(key);
                    if (tit != tables[j].end())
                        for (const auto& u : tit->second) if (u != v) cand.insert(u);
                }
                for (const auto& u : cand) {
                    auto su = cache.get(u, t); // hit garantito
                    if (!su) continue;
                    if (sv->jaccard_sim(*su) >= tau) {
                        res.found = true; res.partner = u; res.time = t;
                        return res;
                    }
                }
            }
        }
        return res;
    }

    // "Spazio occupato" = overhead strutturale (numero di hash table
    // effettivamente istanziate: qui sempre b) + volume dei dati indicizzati
    // (numero totale di riferimenti a nodo memorizzati nei bucket).
    // Per "mia soluzione" le hash table sono sempre e solo b, a prescindere
    // da T e mu.
    long long space_entries() const {
        long long tot_refs = 0;
        for (auto& tab : tables) for (auto& kv : tab) tot_refs += (long long)kv.second.size();
        long long num_tables = (long long)params.b;
        return num_tables + tot_refs;
    }
};

// =============================================================================
// 5. ALGORITMO (C): ALTERNATIVA — T-mu strutture LSH indipendenti
// -----------------------------------------------------------------------------
// Costruzione: stessa Fase 1 (W(v)), ma ogni istante t ha la propria
// collezione indipendente di b hash table (niente timestamp nella chiave).
// Query (ADATTATA): si interroga ogni struttura L_t con SOLO l'hash della
// band (nessuna combinazione con t, che qui è implicito nella struttura), e
// si iterano TUTTE le T-mu strutture, unendo i candidati trovati.
// =============================================================================
class AlternativeSolution {
    TemporalGraph* G;
    int mu, Tmax;
    double tau;
    LSHParams params;
    SketchCache cache;
    std::unordered_map<std::string, std::vector<std::pair<int,int>>> Wv;
    // tables[t][j] = hash table della band j per la struttura L_t
    std::vector<std::vector<std::unordered_map<uint64_t, std::vector<std::string>>>> tables;

public:
    AlternativeSolution(TemporalGraph* g, TemporalRangeForest* forest, int mu_, double tau_, int T_, LSHParams p)
        : G(g), mu(mu_), Tmax(T_ - mu_), tau(tau_), params(p), cache(forest, mu_) {
        tables.assign(Tmax + 1, std::vector<std::unordered_map<uint64_t, std::vector<std::string>>>(p.b));
        for (const auto& v : G->nodes()) {
            auto lambda_v = compute_lambda(*G, v);
            auto w = compute_W_intervals(lambda_v, mu, Tmax);
            Wv[v] = w;
            for (auto& iv : w) {
                for (int t = iv.first; t <= iv.second; ++t) {
                    auto sk = cache.get(v, t);
                    if (!sk) continue;
                    const auto& sig = sk->signature();
                    for (int j = 0; j < params.b; ++j) {
                        uint64_t bh = hash_band(sig, j, params.r); // niente t nella chiave: la struttura è già L_t
                        tables[t][j][bh].push_back(v);
                    }
                }
            }
        }
    }

    // Query ADATTATA: itera su TUTTE le T-mu strutture L_1,...,L_{Tmax} e
    // unisce i candidati.
    QueryResult query(const std::string& v) {
        QueryResult res;
        for (int t = 1; t <= Tmax; ++t) {
            auto sv = cache.get(v, t); // può essere un vero "miss" se t non in W(v)
            if (!sv) continue;
            const auto& sig = sv->signature();
            std::unordered_set<std::string> cand;
            for (int j = 0; j < params.b; ++j) {
                uint64_t bh = hash_band(sig, j, params.r);
                auto tit = tables[t][j].find(bh);
                if (tit != tables[t][j].end())
                    for (const auto& u : tit->second) if (u != v) cand.insert(u);
            }
            for (const auto& u : cand) {
                auto su = cache.get(u, t);
                if (!su) continue;
                if (sv->jaccard_sim(*su) >= tau) {
                    res.found = true; res.partner = u; res.time = t;
                    return res;
                }
            }
        }
        return res;
    }

    // "Spazio occupato" = overhead strutturale (numero di hash table
    // effettivamente istanziate: qui b*(T-mu), una collezione indipendente
    // per ciascun istante) + volume dei dati indicizzati (riferimenti a
    // nodo memorizzati nei bucket, in totale identico a "mia soluzione").
    long long space_entries() const {
        long long tot_refs = 0;
        for (auto& per_t : tables) for (auto& tab : per_t) for (auto& kv : tab) tot_refs += (long long)kv.second.size();
        long long num_tables = (long long)params.b * (long long)Tmax;
        return num_tables + tot_refs;
    }
};

// =============================================================================
// 6. MAIN — batteria al variare di mu
// =============================================================================

// Frazioni di T usate per generare i valori di mu da testare (mu dipende dal
// grafo tramite T, come richiesto).
static const std::vector<double> MU_FRACTIONS = {0.02, 0.05, 0.10, 0.15, 0.20, 0.30, 0.40};

int main(int argc, char** argv) {
    // ---- parametri di default per la generazione Erdos-Renyi -------------
    int n = 80, T = 200;
    double p = 0.02;
    unsigned seed = 42;
    const double TAU = 0.30;
    const LSHParams LSH{24, 8, 3};
    const int NUM_QUERIES_PER_MU = 30;

    TemporalGraph G;

    if (argc >= 2 && std::string(argv[1]) == "--generate") {
        if (argc >= 3) n = std::stoi(argv[2]);
        if (argc >= 4) T = std::stoi(argv[3]);
        if (argc >= 5) p = std::stod(argv[4]);
        if (argc >= 6) seed = (unsigned)std::stoul(argv[5]);
        std::cout << "Generazione grafo Erdos-Renyi temporale: n=" << n << ", T=" << T
                   << ", p=" << p << ", seed=" << seed << "\n";
        G = generate_er_temporal_graph(n, T, p, seed);
    } else if (argc >= 2) {
        std::string path = argv[1];
        std::cout << "Caricamento dataset da file: " << path << "\n";
        G = load_temporal_graph_from_file(path, T);
        n = (int)G.nodes().size();
        std::cout << "  -> letti " << G.edges.size() << " archi, " << n
                   << " nodi distinti, T (=max timestamp) = " << T << "\n";
    } else {
        std::cout << "Nessun dataset fornito: generazione grafo Erdos-Renyi temporale "
                     "con parametri di default: n=" << n << ", T=" << T << ", p=" << p
                   << ", seed=" << seed << "\n";
        G = generate_er_temporal_graph(n, T, p, seed);
    }

    auto all_nodes = G.nodes();
    if (all_nodes.empty()) {
        std::cerr << "Errore: il grafo non ha nodi.\n";
        return 1;
    }

    // Costruzione UNA SOLA VOLTA della struttura black-box condivisa
    // (TemporalRangeForest / RangeTree): non dipende da mu.
    TemporalRangeForest forest(&G, LSH.k);

    // Valori di mu da testare, dipendenti da T.
    std::vector<int> mu_values;
    {
        std::set<int> mu_set;
        for (double f : MU_FRACTIONS) {
            int mu = (int)std::round(f * T);
            if (mu >= 1 && mu < T) mu_set.insert(mu);
        }
        mu_values.assign(mu_set.begin(), mu_set.end());
    }
    if (mu_values.empty()) {
        std::cerr << "Errore: nessun valore di mu valido per T=" << T << ".\n";
        return 1;
    }

    std::cout << "Valori di mu testati (dipendenti da T=" << T << "): ";
    for (int mu : mu_values) std::cout << mu << " ";
    std::cout << "\n\n";

    std::ofstream csv("results_mu_variation.csv");
    // Colonne richieste: tempo di query (al variare di mu) e spazio occupato.
    // mu, method, query_id, v sono solo le chiavi identificative necessarie.
    csv << "mu,method,query_id,v,query_time_us,space_entries\n";

    std::mt19937 query_rng(seed + 777);
    std::uniform_int_distribution<size_t> node_pick(0, all_nodes.size() - 1);

    for (int mu : mu_values) {
        int Tmax = T - mu;
        std::cout << "=== mu = " << mu << " (T-mu = " << Tmax << ") ===\n";

        NaiveSolution naive(&G, &forest, mu, TAU, T);
        MySolution mine(&G, &forest, mu, TAU, T, LSH);
        AlternativeSolution alt(&G, &forest, mu, TAU, T, LSH);

        long long space_naive = naive.space_entries();
        long long space_mine = mine.space_entries();
        long long space_alt = alt.space_entries();
        std::cout << "  spazio occupato -> naive: " << space_naive
                   << " | mia_soluzione: " << space_mine
                   << " | alternativa: " << space_alt << "\n";

        double sum_naive = 0, sum_mine = 0, sum_alt = 0;

        for (int q = 0; q < NUM_QUERIES_PER_MU; ++q) {
            // v scelto randomicamente ad ogni iterazione (stesso v per i tre metodi)
            const std::string& v = all_nodes[node_pick(query_rng)];

            auto t0 = Clock::now();
            QueryResult r_naive = naive.query(v);
            double time_naive_us = us_since(t0);
            (void)r_naive;

            t0 = Clock::now();
            QueryResult r_mine = mine.query(v);
            double time_mine_us = us_since(t0);
            (void)r_mine;

            t0 = Clock::now();
            QueryResult r_alt = alt.query(v);
            double time_alt_us = us_since(t0);
            (void)r_alt;

            sum_naive += time_naive_us; sum_mine += time_mine_us; sum_alt += time_alt_us;

            csv << mu << ",naive," << q << "," << v << "," << time_naive_us << "," << space_naive << "\n";
            csv << mu << ",mia_soluzione," << q << "," << v << "," << time_mine_us << "," << space_mine << "\n";
            csv << mu << ",alternativa," << q << "," << v << "," << time_alt_us << "," << space_alt << "\n";
        }

        std::cout << "  tempo medio di query (us) -> naive: " << (sum_naive / NUM_QUERIES_PER_MU)
                   << " | mia_soluzione: " << (sum_mine / NUM_QUERIES_PER_MU)
                   << " | alternativa: " << (sum_alt / NUM_QUERIES_PER_MU) << "\n\n";
    }

    csv.close();
    std::cout << "Fatto. Risultati scritti in results_mu_variation.csv\n";
    return 0;
}