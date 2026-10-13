// // Trick per rinominare il main() contenuto in TemporalRangeForest.cpp
// #define main temporal_rf_main
// #include "../../TemporalRangeForest.cpp"
// #undef main

// #include <chrono>
// #include <iostream>
// #include <fstream>
// #include <vector>
// #include <random>
// #include <map>
// #include <string>
// #include <algorithm>

// // MODIFICARE GENERAZIONE GRAFO COSÌ:
// // - Genero grafo ER (Erdos-Renyi) con N nodi e grado medio delta
// // - per ogni arco passo scelgo un numero in L = [1, n^c], e ci aggiungo L etichette random

// TemporalGraph ER_random_graph(int num_nodes,int delta, int max_time){
//   TemporalGraph Gnp;
//   int total_edges = num_nodes*delta;

//   return Gnp;
// }

// // Funzione per generare un grafo temporale fittizio
// TemporalGraph generate_synthetic_graph(int num_nodes, int avg_degree, int max_time, std::mt19937& gen) {
//     TemporalGraph G;
//     int total_edges = num_nodes * avg_degree;
//     std::uniform_int_distribution<> node_dist(0, num_nodes - 1);
//     std::uniform_int_distribution<> time_dist(1, max_time);

//     for (int i = 0; i < total_edges; ++i) {
//         int u_idx = node_dist(gen);
//         int v_idx = node_dist(gen);
//         while (v_idx == u_idx && num_nodes > 1) {
//             v_idx = node_dist(gen);
//         }
//         std::string u = "N" + std::to_string(u_idx);
//         std::string v = "N" + std::to_string(v_idx);
//         int t = time_dist(gen);
//         G.add_edge(u, v, t);
//     }
//     return G;
// }

// // Funzione per la query naive: per ogni query scansiona il grafo e ricostruisce lo sketch da zero
// std::shared_ptr<NeighborhoodSketch> naive_query(
//     const TemporalGraph& G, 
//     const std::string& query_node, 
//     int start_time, 
//     int end_time, 
//     int k) 
// {
//     std::vector<std::string> neighbors;
//     for (const auto& edge : G.edges) {
//         if (edge.time >= start_time && edge.time <= end_time) {
//             if (edge.u == query_node) neighbors.push_back(edge.v);
//             else if (edge.v == query_node) neighbors.push_back(edge.u);
//         }
//     }
//     return std::make_shared<MinHashNeighborhoodSketch>(neighbors, k);
// }

// int main() {
//     int num_nodes = 70000;           // Numero nodi nel grafo
//     int avg_degree = 350;            // Grado medio (totale 40.000 archi)
//     int max_time = 5481;            // Timestamp massimo (T_max)
//     int k = 128;                     // Dimensione dello sketch
//     int num_queries_per_mu = 100;   // Numero di query random da eseguire per ogni ampiezza mu

//     std::mt19937 gen(std::random_device{}()); // Random seed per variare ogni esecuzione

//     std::cout << "=== Esperimento: Variazione Ampiezza Intervallo Temporale (mu) ===" << std::endl;
//     std::cout << "Configurazione:\n";
//     std::cout << " - Orizzonte temporale (T): " << max_time << "\n";
//     std::cout << " - Numero nodi: " << num_nodes << ", Grado medio: " << avg_degree << "\n";
//     std::cout << " - Dimensione sketch (k): " << k << "\n";
//     std::cout << " - Numero query per ampiezza mu: " << num_queries_per_mu << "\n";
    
//     std::cout << "Generazione grafo temporale (T_max = " << max_time << ")..." << std::endl;

//     TemporalGraph G = generate_synthetic_graph(num_nodes, avg_degree, max_time, gen);
//     auto nodes = G.nodes();

//     // Selezione casuale del nodo target ad ogni esecuzione
//     std::uniform_int_distribution<> node_dist(0, nodes.size() - 1);
//     std::string target_node = nodes[node_dist(gen)];
//     std::cout << "Nodo target selezionato casualmente: " << target_node << std::endl;

//     // --- 1. COSTRUZIONE STRUTTURA TRF (UNA SOLA VOLTA ALL'INIZIO) ---
//     std::cout << "Costruzione del Temporal Range Tree per " << target_node << std::endl;
    
//     // Silenziamo temporaneamente std::cout durante la costruzione della TRF
//     std::streambuf* old_cout_buf = std::cout.rdbuf(nullptr);

//     SketchFactory factory = [k](const std::vector<std::string>& vicini) {
//         return std::make_shared<MinHashNeighborhoodSketch>(vicini, k);
//     };

//     // La struttura viene istanziata UNA SOLA VOLTA qui
//     auto tree = std::make_shared<RangeTree>(target_node, &G, factory);

//     std::cout.rdbuf(old_cout_buf); // Ripristiniamo std::cout
//     std::cout << "Struttura TRT pronta. Avvio del benchmark sulle query...\n" << std::endl;

//     // Insieme dei valori del parametro mu (ampiezza intervallo) da testare
//     std::vector<int> mu_values = {50, 100, 200, 500, 1000,2000,5000};

//     std::ofstream out("results_interval.csv");
//     out << "mu,max_possible_intervals,trf_time_ns,naive_time_ns\n";

//     for (int mu : mu_values) {
//         // Se mu supera max_time salta
//         if (mu > max_time) continue;

//         // Limite superiore per t_start affinché t_start + mu - 1 <= max_time
//         int max_t_start = max_time - mu + 1; 
//         int max_possible_intervals = max_t_start; // Numero totale di intervalli possibili di ampiezza mu

//         std::uniform_int_distribution<> start_dist(1, max_t_start);

//         // Generazione query casuali per la mu corrente
//         struct Query { int start; int end; };
//         std::vector<Query> queries;
//         for (int i = 0; i < num_queries_per_mu; ++i) {
//             int t_start = start_dist(gen);
//             int t_end = t_start + mu - 1;
//             queries.push_back({t_start, t_end});
//         }

//         // --- 2. QUERY TRF (Esegue solo le query sull'indice pre-costruito) ---
//         long long total_trf_time = 0;
//         for (const auto& q : queries) {
//             auto start = std::chrono::high_resolution_clock::now();
//             auto res = tree->query(target_node, q.start, q.end);
//             auto end = std::chrono::high_resolution_clock::now();
//             total_trf_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
//         }
//         long long avg_trf_time = total_trf_time / num_queries_per_mu;

//         // --- 3. QUERY NAIVE (Scansiona e ricostruisce lo sketch da zero per ogni query) ---
//         long long total_naive_time = 0;
//         for (const auto& q : queries) {
//             auto start = std::chrono::high_resolution_clock::now();
//             auto res = naive_query(G, target_node, q.start, q.end, k);
//             auto end = std::chrono::high_resolution_clock::now();
//             total_naive_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
//         }
//         long long avg_naive_time = total_naive_time / num_queries_per_mu;

//         out << mu << "," << max_possible_intervals << "," << avg_trf_time << "," << avg_naive_time << "\n";
//         std::cout << "Ampiezza mu = " << mu 
//                   << " (Intervalli possibili: " << max_possible_intervals << ")"
//                   << " -> TRF: " << avg_trf_time << " ns"
//                   << " | Naive (Ricalcolo da zero): " << avg_naive_time << " ns" << std::endl;
//     }

//     out.close();
//     std::cout << "\nRisultati salvati con successo in results_interval.csv" << std::endl;
//     return 0;
// }

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

// MODIFICARE GENERAZIONE GRAFO COSÌ:
// - Genero grafo ER (Erdos-Renyi) con N nodi e grado medio delta
// - per ogni arco passo scelgo un numero in L = [1, n^c], e ci aggiungo L etichette random

TemporalGraph ER_random_graph(int num_nodes,int delta, int max_time){
  TemporalGraph Gnp;
  int total_edges = num_nodes*delta;

  return Gnp;
}

// Funzione per generare un grafo temporale fittizio
TemporalGraph generate_synthetic_graph(int num_nodes, int avg_degree, int max_time, std::mt19937& gen) {
    TemporalGraph G;
    int total_edges = num_nodes * avg_degree;
    std::uniform_int_distribution<> node_dist(0, num_nodes - 1);
    std::uniform_int_distribution<> time_dist(1, max_time);

    for (int i = 0; i < total_edges; ++i) {
        int u_idx = node_dist(gen);
        int v_idx = node_dist(gen);
        while (v_idx == u_idx && num_nodes > 1) {
            v_idx = node_dist(gen);
        }
        std::string u = "N" + std::to_string(u_idx);
        std::string v = "N" + std::to_string(v_idx);
        int t = time_dist(gen);
        G.add_edge(u, v, t);
    }
    return G;
}

// Funzione per la query naive: per ogni query scansiona il grafo e ricostruisce lo sketch da zero
std::shared_ptr<NeighborhoodSketch> naive_query(
    const TemporalGraph& G, 
    const std::string& query_node, 
    int start_time, 
    int end_time, 
    int k) 
{
    std::vector<std::string> neighbors;
    for (const auto& edge : G.edges) {
        if (edge.time >= start_time && edge.time <= end_time) {
            if (edge.u == query_node) neighbors.push_back(edge.v);
            else if (edge.v == query_node) neighbors.push_back(edge.u);
        }
    }
    return std::make_shared<MinHashNeighborhoodSketch>(neighbors, k);
}

int main() {
    int num_nodes = 300000;//70000;           // Numero nodi nel grafo
    int avg_degree = 200;//350;            // Grado medio (totale 40.000 archi)
    int max_time = 15000;//6000;            // Timestamp massimo (T_max)
    int k = 64;                     // Dimensione dello sketch
    int num_queries_per_mu = 100;   // Numero di query random da eseguire per ogni ampiezza mu

    std::mt19937 gen(std::random_device{}()); // Random seed per variare ogni esecuzione

    std::cout << "=== Esperimento: Variazione Ampiezza Intervallo Temporale (mu) ===" << std::endl;
    std::cout << "Configurazione:\n";
    std::cout << " - Orizzonte temporale (T): " << max_time << "\n";
    std::cout << " - Numero nodi: " << num_nodes << ", Grado medio: " << avg_degree << "\n";
    std::cout << " - Dimensione sketch (k): " << k << "\n";
    std::cout << " - Numero query per ampiezza mu: " << num_queries_per_mu << "\n";
    
    std::cout << "Generazione grafo temporale (T_max = " << max_time << ")..." << std::endl;

    TemporalGraph G = generate_synthetic_graph(num_nodes, avg_degree, max_time, gen);
    auto nodes = G.nodes();

    // Selezione casuale del nodo target ad ogni esecuzione
    std::uniform_int_distribution<> node_dist(0, nodes.size() - 1);
    std::string target_node = nodes[node_dist(gen)];
    std::cout << "Nodo target selezionato casualmente: " << target_node << std::endl;

    // --- 1. COSTRUZIONE STRUTTURA TRF (UNA SOLA VOLTA ALL'INIZIO) ---
    std::cout << "Costruzione del Temporal Range Tree per " << target_node << std::endl;
    
    // Silenziamo temporaneamente std::cout durante la costruzione della TRF
    std::streambuf* old_cout_buf = std::cout.rdbuf(nullptr);

    SketchFactory factory = [k](const std::vector<std::string>& vicini) {
        return std::make_shared<MinHashNeighborhoodSketch>(vicini, k);
    };

    // La struttura viene istanziata UNA SOLA VOLTA qui
    auto tree = std::make_shared<RangeTree>(target_node, &G, factory);

    std::cout.rdbuf(old_cout_buf); // Ripristiniamo std::cout
    std::cout << "Struttura TRT pronta. Avvio del benchmark sulle query...\n" << std::endl;

    // Insieme dei valori del parametro mu (ampiezza intervallo) da testare
    std::vector<int> mu_values = {50, 100, 200, 500, 1000, 2000, 5000, 10000};

    std::ofstream out("results/results_interval.csv");
    out << "mu,max_possible_intervals,trf_time_ns,naive_time_ns,speedup\n";

    double sum_speedup = 0.0;
    int evaluated_mu_count = 0;

    for (int mu : mu_values) {
        // Se mu supera max_time salta
        if (mu > max_time) continue;

        // Limite superiore per t_start affinché t_start + mu - 1 <= max_time
        int max_t_start = max_time - mu + 1; 
        int max_possible_intervals = max_t_start; // Numero totale di intervalli possibili di ampiezza mu

        std::uniform_int_distribution<> start_dist(1, max_t_start);

        // Generazione query casuali per la mu corrente
        struct Query { int start; int end; };
        std::vector<Query> queries;
        for (int i = 0; i < num_queries_per_mu; ++i) {
            int t_start = start_dist(gen);
            int t_end = t_start + mu - 1;
            queries.push_back({t_start, t_end});
        }

        // --- 2. QUERY TRF (Esegue solo le query sull'indice pre-costruito) ---
        long long total_trf_time = 0;
        for (const auto& q : queries) {
            auto start = std::chrono::high_resolution_clock::now();
            auto res = tree->query(target_node, q.start, q.end);
            auto end = std::chrono::high_resolution_clock::now();
            total_trf_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        }
        long long avg_trf_time = total_trf_time / num_queries_per_mu;

        // --- 3. QUERY NAIVE (Scansiona e ricostruisce lo sketch da zero per ogni query) ---
        long long total_naive_time = 0;
        for (const auto& q : queries) {
            auto start = std::chrono::high_resolution_clock::now();
            auto res = naive_query(G, target_node, q.start, q.end, k);
            auto end = std::chrono::high_resolution_clock::now();
            total_naive_time += std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
        }
        long long avg_naive_time = total_naive_time / num_queries_per_mu;

        // Calcolo dello speedup puntuale: Naive / TRF
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
    std::cout << "\nRisultati salvati con successo in results_interval.csv" << std::endl;

    // Calcolo e stampa a video dello speedup medio complessivo
    double avg_speedup_overall = (evaluated_mu_count > 0) ? (sum_speedup / evaluated_mu_count) : 0.0;
    std::cout << "==================================================" << std::endl;
    std::cout << " SPEEDUP MEDIO COMPLESSIVO (Naive / TRF): " 
              << std::fixed << std::setprecision(2) << avg_speedup_overall << "x" << std::endl;
    std::cout << "==================================================" << std::endl;

    return 0;
}
