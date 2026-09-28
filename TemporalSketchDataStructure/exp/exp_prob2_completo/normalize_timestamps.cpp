#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <unordered_map>
#include <cstdint>
#include <filesystem>

namespace fs = std::filesystem;

// ============================================================================
// Normalizzazione rank-based dei timestamp
//
// Dato un file nel formato "u v t", produce un file con gli stessi archi
// ma i timestamp rimpiazzati dal loro rango (0, 1, 2, ...) nell'ordinamento
// crescente dei timestamp distinti.
//
// Questo preserva:
// - L'ordinamento temporale degli archi
// - Le relazioni di vicinato (chi è vicino a chi in quale timestamp)
// - La semantica degli sketch MinHash e dei RangeTree
//
// Il parametro mu va ri-calibrato come percentuale dei timestamp distinti.
// ============================================================================

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Uso: " << argv[0] << " <dataset_1> [dataset_2] ...\n";
        std::cout << "Esempio: " << argv[0] << " sorted_datasets/*.txt sorted_datasets/*.edges\n";
        std::cout << "\nOutput: normalized_datasets/<basename>\n";
        return 1;
    }

    std::string output_dir = "normalized_datasets";
    fs::create_directories(output_dir);

    // File riassuntivo con le info di normalizzazione per tutti i dataset
    std::string info_path = output_dir + "/normalization_info.csv";
    std::ofstream info_file(info_path);
    info_file << "dataset_name,original_t_min,original_t_max,original_range,"
              << "num_distinct_timestamps,normalized_t_max,"
              << "compression_ratio,"
              << "suggested_mu_small,suggested_mu_medium,suggested_mu_large\n";

    for (int arg_i = 1; arg_i < argc; ++arg_i) {
        std::string input_path = argv[arg_i];
        fs::path p(input_path);
        std::string basename = p.stem().string();

        std::cout << "\n=== Normalizzazione: " << basename << " ===\n";

        // ---- Passata 1: raccolta timestamp distinti ----
        std::ifstream in1(input_path);
        if (!in1.is_open()) {
            std::cerr << "[ERRORE] Impossibile aprire: " << input_path << "\n";
            continue;
        }

        std::vector<uint64_t> all_timestamps;
        all_timestamps.reserve(1000000);

        std::string line;
        while (std::getline(in1, line)) {
            if (line.empty() || line[0] == '#' || line[0] == '%') continue;
            std::istringstream iss(line);
            std::string u, v;
            uint64_t t;
            if (iss >> u >> v >> t) {
                all_timestamps.push_back(t);
            }
        }
        in1.close();

        if (all_timestamps.empty()) {
            std::cerr << "[ATTENZIONE] Nessun arco trovato in: " << input_path << "\n";
            continue;
        }

        // Ordina e rimuovi duplicati per ottenere i timestamp distinti
        std::sort(all_timestamps.begin(), all_timestamps.end());
        all_timestamps.erase(
            std::unique(all_timestamps.begin(), all_timestamps.end()),
            all_timestamps.end()
        );

        size_t num_distinct = all_timestamps.size();
        uint64_t original_min = all_timestamps.front();
        uint64_t original_max = all_timestamps.back();
        uint64_t original_range = original_max - original_min;

        std::cout << "  Timestamp distinti: " << num_distinct << "\n";
        std::cout << "  Range originale: [" << original_min << ", " << original_max
                  << "] (ampiezza " << original_range << ")\n";
        std::cout << "  Range normalizzato: [0, " << (num_distinct - 1) << "]\n";

        double compression = (num_distinct > 1)
            ? static_cast<double>(original_range) / (num_distinct - 1)
            : 1.0;
        std::cout << "  Rapporto di compressione: " << compression << "x\n";

        // Costruisci la mappa timestamp -> rango
        std::unordered_map<uint64_t, uint32_t> ts_to_rank;
        ts_to_rank.reserve(num_distinct);
        for (size_t i = 0; i < num_distinct; ++i) {
            ts_to_rank[all_timestamps[i]] = static_cast<uint32_t>(i);
        }

        // Libera il vettore dei timestamp (non serve più)
        { std::vector<uint64_t>().swap(all_timestamps); }

        // ---- Passata 2: riscrittura con timestamp normalizzati ----
        std::ifstream in2(input_path);
        std::string out_path = output_dir + "/" + basename;
        std::ofstream out(out_path);

        if (!out.is_open()) {
            std::cerr << "[ERRORE] Impossibile scrivere: " << out_path << "\n";
            continue;
        }

        while (std::getline(in2, line)) {
            if (line.empty() || line[0] == '#' || line[0] == '%') {
                out << line << "\n";
                continue;
            }
            std::istringstream iss(line);
            std::string u, v;
            uint64_t t;
            if (iss >> u >> v >> t) {
                auto it = ts_to_rank.find(t);
                if (it != ts_to_rank.end()) {
                    out << u << " " << v << " " << it->second << "\n";
                }
            }
        }
        in2.close();
        out.close();

        // Calcola mu suggeriti: percentuali del numero di timestamp distinti
        // Corrispondono approssimativamente a: mu_small ~ 5%, mu_medium ~ 25%, mu_large ~ 50%
        uint32_t norm_max = static_cast<uint32_t>(num_distinct - 1);
        uint32_t mu_small  = std::max(1U, norm_max / 20);    // ~5%
        uint32_t mu_medium = std::max(1U, norm_max / 4);     // ~25%
        uint32_t mu_large  = std::max(1U, norm_max / 2);     // ~50%

        std::cout << "  Mu suggeriti (normalizzati): "
                  << mu_small << " (5%), "
                  << mu_medium << " (25%), "
                  << mu_large << " (50%)\n";
        std::cout << "  -> File salvato: " << out_path << "\n";

        info_file << basename << ","
                  << original_min << ","
                  << original_max << ","
                  << original_range << ","
                  << num_distinct << ","
                  << norm_max << ","
                  << compression << ","
                  << mu_small << ","
                  << mu_medium << ","
                  << mu_large << "\n";
    }

    info_file.close();
    std::cout << "\n-> Info di normalizzazione salvate in: " << info_path << "\n";
    std::cout << "Normalizzazione completata.\n";

    return 0;
}
