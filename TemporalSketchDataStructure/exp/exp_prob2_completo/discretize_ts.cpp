#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
#include <limits>
#include <cstdint>
#include <filesystem>
#include <iomanip>

namespace fs = std::filesystem;

// Numero di snapshot temporali discreti desiderati (target standard per grafi temporali)
constexpr uint64_t DEFAULT_TARGET_BINS = 2000;

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Uso: " << argv[0] << " <dataset_1> [dataset_2] ... [dataset_X]\n";
        std::cout << "Esempio: " << argv[0] << " sorted_datasets/*.txt\n";
        std::cout << "Output: discretized_datasets/<basename>\n";
        return 1;
    }

    std::string output_dir = "discretized_datasets";
    fs::create_directories(output_dir);

    // File riepilogativo con le informazioni di discretizzazione per tutti i dataset
    std::string info_path = output_dir + "/discretization_info.csv";
    std::ofstream info_file(info_path);
    info_file << "dataset_name,original_t_min,original_t_max,original_span_seconds,"
              << "target_bins,actual_t_max,bin_duration_seconds,"
              << "suggested_mu_small,suggested_mu_medium,suggested_mu_large\n";

    for (int arg_i = 1; arg_i < argc; ++arg_i) {
        std::string input_path = argv[arg_i];
        fs::path p(input_path);
        std::string basename = p.stem().string();

        std::cout << "\n=======================================================\n";
        std::cout << "Discretizzazione Dataset [" << arg_i << "/" << (argc - 1) << "]: " << basename << "\n";
        std::cout << "=======================================================\n";

        // ---- PASSATA 1: Lettura in streaming per identificare t_min, t_max ed edge_count ----
        std::ifstream in1(input_path);
        if (!in1.is_open()) {
            std::cerr << "[ERRORE] Impossibile aprire: " << input_path << "\n";
            continue;
        }

        uint64_t t_min = std::numeric_limits<uint64_t>::max();
        uint64_t t_max = 0;
        uint64_t edge_count = 0;

        std::string line, u, v;
        uint64_t t;
        while (std::getline(in1, line)) {
            if (line.empty() || line[0] == '#' || line[0] == '%') continue;
            std::istringstream iss(line);
            if (iss >> u >> v >> t) {
                if (t < t_min) t_min = t;
                if (t > t_max) t_max = t;
                edge_count++;
            }
        }
        in1.close();

        if (edge_count == 0) {
            std::cerr << "[ATTENZIONE] Nessun arco valido trovato in: " << input_path << "\n";
            continue;
        }

        uint64_t span = (t_max >= t_min) ? (t_max - t_min) : 0;
        uint64_t target_bins = DEFAULT_TARGET_BINS;

        // Se l'orizzonte temporale naturale è già inferiore al target, non raggruppa
        uint64_t delta = (span > target_bins) ? std::max<uint64_t>(1, (span + target_bins - 1) / target_bins) : 1;

        std::cout << " - Archi letti:                 " << edge_count << "\n";
        std::cout << " - UNIX Range grezzo:           [" << t_min << ", " << t_max << "] (durata: " << span << " s)\n";
        std::cout << " - Durata singolo bin temporale: " << delta << " secondi (" 
                  << std::fixed << std::setprecision(2) << (delta / 3600.0) << " ore)\n";

        // ---- PASSATA 2: Riscrittura riga per riga con timestamp discretizzati ----
        std::ifstream in2(input_path);
        std::string out_path = output_dir + "/" + basename + ".txt";
        std::ofstream out(out_path);

        if (!out.is_open()) {
            std::cerr << "[ERRORE] Impossibile creare: " << out_path << "\n";
            continue;
        }

        uint64_t actual_t_max = 1;
        while (std::getline(in2, line)) {
            if (line.empty() || line[0] == '#' || line[0] == '%') continue;
            std::istringstream iss(line);
            if (iss >> u >> v >> t) {
                uint64_t norm_t = 1 + (t - t_min) / delta;
                out << u << " " << v << " " << norm_t << "\n";
                if (norm_t > actual_t_max) actual_t_max = norm_t;
            }
        }
        in2.close();
        out.close();

        // Calcolo valori di mu calibrati sulla timeline discretizzata
        uint64_t mu_small  = std::max<uint64_t>(1, actual_t_max / 50); // ~2% di T
        uint64_t mu_medium = std::max<uint64_t>(1, actual_t_max / 10); // ~10% di T
        uint64_t mu_large  = std::max<uint64_t>(1, actual_t_max / 4);  // ~25% di T
        uint64_t mu_large_half  = std::max<uint64_t>(1, actual_t_max / 2);  // ~50% di T

        std::cout << " - Timeline finale [1, T]:       [1, " << actual_t_max << "]\n";
        std::cout << " - Valori di mu suggeriti:        "
                  << mu_small << " (~2%), "
                  << mu_medium << " (~10%), "
                  << mu_large << " (~25%),  "
                  << mu_large_half << " (~50%)\n";
        std::cout << " -> File salvato in: " << out_path << "\n";

        // Scrittura riga nel file CSV di riepilogo
        info_file << basename << ","
                  << t_min << ","
                  << t_max << ","
                  << span << ","
                  << target_bins << ","
                  << actual_t_max << ","
                  << delta << ","
                  << mu_small << ","
                  << mu_medium << ","
                  << mu_large << "\n";
    }

    info_file.close();
    std::cout << "\n=======================================================\n";
    std::cout << "Processo terminato. Informazioni salvate in: " << info_path << "\n";
    return 0;
}