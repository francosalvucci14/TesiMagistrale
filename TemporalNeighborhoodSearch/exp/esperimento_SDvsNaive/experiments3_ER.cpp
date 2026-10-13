// Trick per rinominare il main() contenuto in TemporalRangeForest.cpp
#define main temporal_rf_main
#include "../../TemporalRangeForest.cpp"
#undef main

#include <chrono>
#include <iostream>
#include <fstream>
#include <vector>
#include <random>
#include <map>
#include <string>
#include <algorithm>
#include <iomanip>
#include <cmath>

/**
 * Generazione del grafo di Erdős-Rényi G(n, p) temporale:
 * - N nodi e grado medio delta (p = delta / (num_nodes - 1))
 * - Per ogni arco statico, si sceglie L in [1, n^c] e si aggiungono L etichette random in [1, max_time].
 *
 * Implementato tramite l'algoritmo lineare Batagelj-Brandes O(n + m) per campionamento esatto.
 */
TemporalGraph ER_random_graph(uint32_t num_nodes, uint32_t delta, uint32_t max_time, double c, std::mt19937 &gen)
{
    TemporalGraph Gnp;
    if (num_nodes <= 1)
        return Gnp;

    // Probabilità affinché il grado medio sia delta: delta = p * (num_nodes - 1)
    double p = static_cast<double>(delta) / (num_nodes - 1);
    if (p > 1.0)
        p = 1.0;
    if (p <= 0.0)
        return Gnp;

    // Calcolo limite massimo per L: L in [1, n^c]
    uint32_t max_L = std::max((uint32_t)1, static_cast<uint32_t>(std::pow(num_nodes, c)));
    std::uniform_int_distribution<uint32_t> l_dist(1, max_L);
    std::uniform_int_distribution<uint32_t> time_dist(1, max_time);
    std::uniform_real_distribution<double> u_dist(0.0, 1.0);

    // Salto geometrico ad alta precisione con log1p(-p) = ln(1 - p)
    double log_cp = std::log1p(-p);
    uint32_t v = 1;
    long long w = -1;

    while (v < num_nodes)
    {
        double r = u_dist(gen);
        while (r <= 0.0)
            r = u_dist(gen); // Evita log(0)

        long long step = 1 + static_cast<long long>(std::log(r) / log_cp);
        w += step;
        while (w >= v && v < num_nodes)
        {
            w -= v;
            v++;
        }
        if (v < num_nodes)
        {
            std::string u_str = "N" + std::to_string(v);
            std::string w_str = "N" + std::to_string(w);

            // Per ogni arco scegliamo L in [1, n^c] ed inseriamo L etichette random
            uint32_t L = (max_L > 1) ? l_dist(gen) : 1;
            for (uint32_t l = 0; l < L; ++l)
            {
                uint32_t t = time_dist(gen);
                Gnp.add_edge(u_str, w_str, t);
            }
        }
    }
    return Gnp;
}

// Funzione per generare un grafo temporale fittizio standard
TemporalGraph generate_synthetic_graph(uint32_t num_nodes, uint32_t avg_degree, uint32_t max_time, std::mt19937 &gen)
{
    TemporalGraph G;
    uint32_t total_edges = num_nodes * avg_degree;
    std::uniform_int_distribution<uint32_t> node_dist(0, num_nodes - 1);
    std::uniform_int_distribution<uint32_t> time_dist(1, max_time);

    for (uint32_t i = 0; i < total_edges; ++i)
    {
        uint32_t u_idx = node_dist(gen);
        uint32_t v_idx = node_dist(gen);
        while (v_idx == u_idx && num_nodes > 1)
        {
            v_idx = node_dist(gen);
        }
        std::string u = "N" + std::to_string(u_idx);
        std::string v = "N" + std::to_string(v_idx);
        uint32_t t = time_dist(gen);
        G.add_edge(u, v, t);
    }
    return G;
}

// Funzione per la query naive: per ogni query scansiona il grafo e ricostruisce lo sketch da zero
std::shared_ptr<NeighborhoodSketch> naive_query(
    const TemporalGraph &G,
    const std::string &query_node,
    uint32_t start_time,
    uint32_t end_time,
    uint32_t k)
{
    std::vector<std::string> neighbors;
    for (const auto &edge : G.edges)
    {
        if (edge.time >= start_time && edge.time <= end_time)
        {
            if (edge.u == query_node)
                neighbors.push_back(edge.v);
            else if (edge.v == query_node)
                neighbors.push_back(edge.u);
        }
    }
    return std::make_shared<MinHashNeighborhoodSketch>(neighbors, k);
}

int main(int argc, char *argv[])
{
    //
    // invece che creare tutto il grafo, di N nodi scelgo X <= N nodi, e genero solamete quegli X vicinati temporali tutti con size in media avg_degree 
    //

    // Modalità di default: "er" (Erdos-Renyi) oppure "standard"
    std::string graph_type = "er";
    double c = 0.7; // Esponente per L = [1, n^c] - valori: 0.1,0.2,0.5, 1.0

    // Parsing parametri da riga di comando
    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--graph" || arg == "-g")
        {
            if (i + 1 < argc)
                graph_type = argv[++i];
        }
        else if (arg == "--c" || arg == "-c")
        {
            if (i + 1 < argc)
                c = std::stod(argv[++i]);
        }
        else if (arg == "er" || arg == "erdos" || arg == "erdos-renyi")
        {
            graph_type = "er";
        }
        else if (arg == "standard" || arg == "synthetic")
        {
            graph_type = "standard";
        }
    }

    std::transform(graph_type.begin(), graph_type.end(), graph_type.begin(), ::tolower);
    bool is_er = (graph_type == "er" || graph_type == "erdos" || graph_type == "erdos-renyi");

    // Selezione del file CSV di output in base al grafo scelto
    std::string csv_filename = is_er ? "results/results_interval_er.csv" : "results/results_interval_standard.csv";

    uint32_t num_nodes = 10000;                                             // Numero nodi nel grafo - n=10.000 il c buono è 4-4.5
    uint32_t avg_degree = 32;                                               // Grado medio (delta) - O(radice(n) )
    uint32_t max_time = 30 * static_cast<uint32_t>(std::pow(num_nodes, c)); // Timestamp massimo (T_max)
    std::cout << "Max time: " << max_time << std::endl;
    // exit(0);
    uint32_t k = 128;                  // Dimensione dello sketch
    uint32_t num_queries_per_mu = 100; // Numero di query random da eseguire per ogni ampiezza mu

    std::mt19937 gen(std::random_device{}()); // Random seed

    std::cout << "=== Esperimento: Variazione Ampiezza Intervallo Temporale (mu) ===" << std::endl;
    std::cout << "Configurazione:\n";
    std::cout << " - Tipo Grafo: " << (is_er ? "Erdos-Renyi (ER)" : "Sintetico Standard") << "\n";
    if (is_er)
    {
        std::cout << " - Esponente c per L in [1, n^c]: " << c << " (L_max = "
                  << std::max((uint32_t)1, static_cast<uint32_t>(std::pow(num_nodes, c))) << ")\n";
    }
    std::cout << " - File di output: " << csv_filename << "\n";
    std::cout << " - Orizzonte temporale (T): " << max_time << "\n";
    std::cout << " - Numero nodi: " << num_nodes << ", Grado medio: " << avg_degree << "\n";
    std::cout << " - Dimensione sketch (k): " << k << "\n";
    std::cout << " - Numero query per ampiezza mu: " << num_queries_per_mu << "\n";

    std::cout << "\nGenerazione grafo temporale..." << std::endl;
    TemporalGraph G;
    if (is_er)
    {
        G = ER_random_graph(num_nodes, avg_degree, max_time, c, gen);
    }
    else
    {
        G = generate_synthetic_graph(num_nodes, avg_degree, max_time, gen);
    }

    auto nodes = G.nodes();
    if (nodes.empty())
    {
        std::cerr << "Errore: il grafo non contiene nodi con archi." << std::endl;
        return 1;
    }

    // Selezione casuale del nodo target ad ogni esecuzione
    std::uniform_int_distribution<> node_dist(0, nodes.size() - 1);
    std::string target_node = nodes[node_dist(gen)];
    std::cout << "Nodo target selezionato casualmente: " << target_node << std::endl;

    // --- 1. COSTRUZIONE STRUTTURA TRF (UNA SOLA VOLTA ALL'INIZIO) ---
    std::cout << "Costruzione del Temporal Range Tree per " << target_node << std::endl;

    std::streambuf *old_cout_buf = std::cout.rdbuf(nullptr);

    SketchFactory factory = [k](const std::vector<std::string> &vicini)
    {
        return std::make_shared<MinHashNeighborhoodSketch>(vicini, k);
    };

    auto tree = std::make_shared<RangeTree>(target_node, &G, factory);

    std::cout.rdbuf(old_cout_buf); // Ripristino std::cout
    std::cout << "Struttura TRT pronta. Avvio del benchmark sulle query...\n"
              << std::endl;

    // Insieme dei valori del parametro mu (ampiezza intervallo) da testare
    std::vector<uint32_t> mu_values = {max_time / 32, max_time / 16, max_time / 8, max_time / 4, max_time / 2};

    std::ofstream out(csv_filename);
    out << "mu,max_possible_intervals,trf_time_ns,naive_time_ns,speedup\n";

    double sum_speedup = 0.0;
    uint32_t evaluated_mu_count = 0;

    for (uint32_t mu : mu_values)
    {
        if (mu > max_time)
            continue;

        uint32_t max_t_start = max_time - mu + 1;
        uint32_t max_possible_intervals = max_t_start;

        std::uniform_int_distribution<uint32_t> start_dist(1, max_t_start);

        struct Query
        {
            uint32_t start;
            uint32_t end;
        };
        std::vector<Query> queries;
        for (uint32_t i = 0; i < num_queries_per_mu; ++i)
        {
            uint32_t t_start = start_dist(gen);
            uint32_t t_end = t_start + mu - 1;
            queries.push_back({t_start, t_end});
        }

        // --- 2. QUERY TRF ---
        long long total_trf_time = 0;
        for (const auto &q : queries)
        {
            auto start = std::chrono::high_resolution_clock::now();
            auto res = tree->query(target_node, q.start, q.end);
            auto end = std::chrono::high_resolution_clock::now();
            total_trf_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        }
        long long avg_trf_time = total_trf_time / num_queries_per_mu;

        // --- 3. QUERY NAIVE ---
        long long total_naive_time = 0;
        for (const auto &q : queries)
        {
            auto start = std::chrono::high_resolution_clock::now();
            auto res = naive_query(G, target_node, q.start, q.end, k);
            auto end = std::chrono::high_resolution_clock::now();
            total_naive_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        }
        long long avg_naive_time = total_naive_time / num_queries_per_mu;

        double speedup = (avg_trf_time > 0) ? static_cast<double>(avg_naive_time) / avg_trf_time : 0.0;
        sum_speedup += speedup;
        evaluated_mu_count++;

        out << mu << "," << max_possible_intervals << "," << avg_trf_time << "," << avg_naive_time << "," << speedup << "\n";
        std::cout << "Ampiezza mu = " << std::setw(5) << mu
                  << " (Intervalli possibili: " << std::setw(5) << max_possible_intervals << ")"
                  << " -> TRF: " << std::setw(7) << avg_trf_time << " ns"
                  << " | Naive: " << std::setw(9) << avg_naive_time << " ns"
                  << " | Speedup: " << std::fixed << std::setprecision(2) << speedup << "x" << std::endl;
    }

    out.close();
    std::cout << "\nRisultati salvati con successo in: " << csv_filename << std::endl;

    double avg_speedup_overall = (evaluated_mu_count > 0) ? (sum_speedup / evaluated_mu_count) : 0.0;
    std::cout << "==================================================" << std::endl;
    std::cout << " SPEEDUP MEDIO COMPLESSIVO (Naive / TRF): "
              << std::fixed << std::setprecision(2) << avg_speedup_overall << "x" << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
