#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <unordered_set>
#include <algorithm>
#include <limits>
#include <iomanip>
#include <cstdint>
#include <filesystem>

#if defined(__linux__)
#include <malloc.h>
#endif

namespace fs = std::filesystem;

using TimeStamp = uint64_t; // Supporta sia timestamp discreti che epoch Unix estesi
using EdgeCount = uint64_t;

// Estrae il nome base del file (es. "path/to/email-Eu.txt" -> "email-Eu")
std::string extract_dataset_name(const std::string& path_str) {
    fs::path p(path_str);
    return p.stem().string();
}

void process_dataset(const std::string& filepath, const std::string& output_dir) {
    std::string dataset_name = extract_dataset_name(filepath);
    std::cout << "\n=======================================================\n";
    std::cout << "Analisi dataset: " << dataset_name << " (" << filepath << ")\n";
    std::cout << "=======================================================\n";

    std::ifstream in(filepath);
    if (!in.is_open()) {
        std::cerr << "[ERRORE] Impossibile aprire il file: " << filepath << "\n";
        return;
    }

    EdgeCount num_edges = 0;
    TimeStamp lifetime_min = std::numeric_limits<TimeStamp>::max();
    TimeStamp lifetime_max = 0;

    // Tracciamento dei soli identificatori dei vertici per conteggio |V|
    std::unordered_set<std::string> unique_nodes;
    unique_nodes.reserve(200000);

    std::string line;
    std::string u, v;
    TimeStamp t;

    // Lettura in streaming riga per riga: nessun arco memorizzato in RAM
    while (std::getline(in, line)) {
        if (line.empty() || line[0] == '#' || line[0] == '%') continue;
        std::istringstream iss(line);
        if (iss >> u >> v >> t) {
            num_edges++;
            if (t < lifetime_min) lifetime_min = t;
            if (t > lifetime_max) lifetime_max = t;
            unique_nodes.insert(std::move(u));
            unique_nodes.insert(std::move(v));
        }
    }
    in.close();

    if (num_edges == 0) {
        std::cerr << "[ATTENZIONE] Nessun arco valido trovato in: " << filepath << "\n";
        return;
    }

    size_t num_nodes = unique_nodes.size();

    // Calcolo dei 3 valori di mu
    // Se lifetime_min == 0, mu_1 viene impostato ad almeno 1 per consistenza negli algoritmi
    TimeStamp mu_1 = (lifetime_min > 0) ? (lifetime_min / 2) : 1;
    TimeStamp mu_2 = std::max<TimeStamp>(1, (lifetime_max-lifetime_min) / 2);
    TimeStamp mu_3 = std::max<TimeStamp>(1, (lifetime_max-lifetime_min) / 4);

    // Salvataggio nel file CSV dedicato in dataset_analysis/<dataset_name>.csv
    std::string out_csv_path = output_dir + "/" + dataset_name + ".csv";
    std::ofstream out_csv(out_csv_path);
    if (!out_csv.is_open()) {
        std::cerr << "[ERRORE] Impossibile scrivere in: " << out_csv_path << "\n";
    } else {
        out_csv << "dataset_name,num_nodes,num_edges,lifetime_min,lifetime_max,mu_min_div_2,mu_max_div_2,mu_max_div_4\n";
        out_csv << dataset_name << ","
                << num_nodes << ","
                << num_edges << ","
                << lifetime_min << ","
                << lifetime_max << ","
                << mu_1 << ","
                << mu_2 << ","
                << mu_3 << "\n";
        out_csv.close();
        std::cout << "-> Dati salvati con successo in: " << out_csv_path << "\n";
    }

    // Stampa a video dei risultati
    std::cout << " - Nodi (|V|):                  " << num_nodes << "\n";
    std::cout << " - Archi (|E|):                  " << num_edges << "\n";
    std::cout << " - Timestamp Minimo (t_min):     " << lifetime_min << "\n";
    std::cout << " - Timestamp Massimo (t_max):     " << lifetime_max << "\n";
    std::cout << " - mu_1 (lifetime_min / 2):      " << mu_1 << "\n";
    std::cout << " - mu_2 (lifetime_max / 2):      " << mu_2 << "\n";
    std::cout << " - mu_3 (lifetime_max / 4):      " << mu_3 << "\n";

    // ========================================================================
    // DEALLOCAZIONE FORZATA DELLA MEMORIA
    // ========================================================================
    // Lo swap con un'istanza vuota rilascia i bucket della tabella hash
    std::unordered_set<std::string>().swap(unique_nodes);

#if defined(__linux__)
    // Restituisce fisicamente le pagine di memoria non usate al kernel
    malloc_trim(0);
#endif

    std::cout << "-> RAM rilasciata con successo per questo dataset.\n";
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cout << "Uso: " << argv[0] << " <dataset_1> [dataset_2] ... [dataset_X]\n";
        std::cout << "Esempio: " << argv[0] << " sorted_datasets/*.txt\n";
        return 1;
    }

    std::string output_dir = "dataset_analysis";
    fs::create_directories(output_dir);

    std::vector<std::string> datasets;
    for (int i = 1; i < argc; ++i) {
        datasets.push_back(argv[i]);
    }

    std::cout << "Inizio analisi di " << datasets.size() << " dataset...\n";

    for (const auto& ds_path : datasets) {
        process_dataset(ds_path, output_dir);
    }

    std::cout << "\nAnalisi completata per tutti i dataset. Risultati in '" << output_dir << "/'.\n";
    return 0;
}
