#include <iostream>
#include <vector>
#include <set>
#include <map>
#include <memory>
#include <string>
#include <algorithm>
#include <stdexcept>
#include <functional>
#include <fstream>
#include <sstream>
#include <random>
#include <cmath>
#include <chrono>
#ifdef BUILD_PYTHON_MODULE
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
namespace py = pybind11;
#endif

// ============================================================================
// STRUTTURE DEL GRAFO E UTILITIES
// ============================================================================

struct TemporalEdge {
    std::string u;
    std::string v;
    int time;
};

class TemporalGraph {
public:
    std::vector<TemporalEdge> edges;
    std::set<std::string> nodes_set;

    void add_edge(const std::string& u, const std::string& v, int time) {
        edges.push_back({u, v, time});
        nodes_set.insert(u);
        nodes_set.insert(v);
    }

    std::vector<std::string> nodes() const {
        return std::vector<std::string>(nodes_set.begin(), nodes_set.end());
    }
};

void stampaTempoAuto(std::chrono::nanoseconds durata, std::string label){
    double tempo = durata.count();
    std::string text = "";

    if (label == "grafo"){
        text = "per la costruzione del grafo";
    }
    else if (label == "rt")
    {
        text = "per la costruzione dei Range Trees";
    }
    else {
        text = "per la query";
    }
    

    if (tempo >= 1'000'000'000){
        std::cout << "Tempo di esecuzione (secondi) " << text << ": " << tempo / 1'000'000'000 << " s\n";
    }
    else if (tempo >= 1'000'000){
        std::cout << "Tempo di esecuzione (millisecondi) " << text << ":" << tempo / 1'000'000 << " ms\n";
    }
    else if (tempo >= 1'000){
        std::cout << "Tempo di esecuzione (microsecondi) " << text << ": " << tempo / 1'000 << " us\n";
    }
    else {
        std::cout << "Tempo di esecuzione (nanosecondi) " << text << ": " << tempo << " ns\n";
    }
}

TemporalGraph build_graph_one_v(){
    TemporalGraph G;
    G.add_edge("V", "A", 1);
    G.add_edge("V", "A", 2);

    G.add_edge("V", "B", 3);
    
    G.add_edge("V", "C", 1);
    G.add_edge("V", "C", 5);

    G.add_edge("V", "D", 6);
    G.add_edge("V", "D", 7);

    G.add_edge("V", "E", 3);
    G.add_edge("V", "E", 4);
    G.add_edge("V", "E", 5);

    G.add_edge("A", "W", 10);

    return G;
}

// Crea il grafo a partire da un dataset stringa[cite: 2]
TemporalGraph create_graph_from_ds(const std::vector<std::string>& dataset) {
    TemporalGraph G;
    for (const auto& line : dataset) {
        std::istringstream iss(line);
        std::string u, v;
        int t;
        if (iss >> u >> v >> t) {
            G.add_edge(u, v, t);
        }
    }
    return G;
}

// Legge le righe del dataset da un file di testo[cite: 2]
std::pair<std::vector<std::string>, std::string> read_dataset_from_file(const std::string& file_path) {
    std::vector<std::string> dataset;
    std::ifstream file(file_path);
    std::string line;
    
    if (file.is_open()) {
        while (std::getline(file, line)) {
            if (!line.empty()) {
                dataset.push_back(line);
            }
        }
    } else {
        std::cerr << "Impossibile aprire il file: " << file_path << "\n";
    }

    size_t last_slash = file_path.find_last_of('/');
    std::string name = (last_slash == std::string::npos) ? file_path : file_path.substr(last_slash + 1);
    size_t last_dot = name.find_last_of('.');
    if (last_dot != std::string::npos) name = name.substr(0, last_dot);

    return {dataset, name};
}

// Ordina il grafo per u e poi per time e sovrascrive su un nuovo file[cite: 2]
void sort_graph(const std::string& graph_file) {
    struct TmpEdge { int u, v, t; };
    std::vector<TmpEdge> edges;
    std::ifstream infile(graph_file);
    std::string line;

    while (std::getline(infile, line)) {
        std::istringstream iss(line);
        int u, v, t;
        if (iss >> u >> v >> t) {
            edges.push_back({u, v, t});
        }
    }

    std::sort(edges.begin(), edges.end(), [](const TmpEdge& a, const TmpEdge& b) {
        if (a.u != b.u) return a.u < b.u;
        return a.t < b.t;
    });

    std::ofstream outfile(graph_file + "_sort");
    for (const auto& edge : edges) {
        outfile << edge.u << " " << edge.v << " " << edge.t << "\n";
    }
}

// ============================================================================
// SKETCH ASTRATTO E IMPLEMENTAZIONI
// ============================================================================

class NeighborhoodSketch {
public:
    std::set<std::string> vicini;

    virtual ~NeighborhoodSketch() = default;
    virtual std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const = 0;
    virtual std::shared_ptr<NeighborhoodSketch> clone() const = 0;
    virtual void print_info(const std::string& indent) const = 0;
};

// Implementazione di MinHash
class MinHashNeighborhoodSketch : public NeighborhoodSketch {
private:
    int num_perm;
    std::vector<uint32_t> hashvalues;
    
    // Costanti per le funzioni hash: h(x) = (a*x + b) % c
    static const uint32_t PRIME = 4294967291; // 2^32 - 5
    std::vector<uint32_t> a, b;

    void init_hash_functions() {
        std::mt19937 gen(42); // Seed fisso per consistenza tra diversi sketch
        std::uniform_int_distribution<uint32_t> dist_a(1, PRIME - 1);
        std::uniform_int_distribution<uint32_t> dist_b(0, PRIME - 1);
        
        for (int i = 0; i < num_perm; ++i) {
            a.push_back(dist_a(gen));
            b.push_back(dist_b(gen));
        }
    }

    void update(const std::string& v) {
        uint32_t base_hash = std::hash<std::string>{}(v);
        for (int i = 0; i < num_perm; ++i) {
            uint64_t hash_val = (static_cast<uint64_t>(a[i]) * base_hash + b[i]) % PRIME;
            if (hash_val < hashvalues[i]) {
                hashvalues[i] = static_cast<uint32_t>(hash_val);
            }
        }
    }

public:
    MinHashNeighborhoodSketch(const std::vector<std::string>& init_vicini = {}, int num_perm = 128) 
        : num_perm(num_perm), hashvalues(num_perm, UINT32_MAX) {
        init_hash_functions();
        for (const auto& v : init_vicini) {
            vicini.insert(v);
            update(v);
        }
    }

    std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const override {
        const auto* other_mh = dynamic_cast<const MinHashNeighborhoodSketch*>(&other);
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<std::string>{}, num_perm);
        
        std::set_union(vicini.begin(), vicini.end(),
                       other_mh->vicini.begin(), other_mh->vicini.end(),
                       std::inserter(new_sk->vicini, new_sk->vicini.begin()));

        for (int i = 0; i < num_perm; ++i) {
            new_sk->hashvalues[i] = std::min(this->hashvalues[i], other_mh->hashvalues[i]);
        }
        return new_sk;
    }

    std::shared_ptr<NeighborhoodSketch> clone() const override {
        auto new_sk = std::make_shared<MinHashNeighborhoodSketch>(std::vector<std::string>{}, num_perm);
        new_sk->vicini = this->vicini;
        new_sk->hashvalues = this->hashvalues;
        return new_sk;
    }

    double jaccard_sim(const MinHashNeighborhoodSketch& other_sk) const {
        int matches = 0;
        for (int i = 0; i < num_perm; ++i) {
            if (this->hashvalues[i] == other_sk.hashvalues[i]) {
                matches++;
            }
        }
        return static_cast<double>(matches) / num_perm;
    }

    void print_info(const std::string& indent) const override {
        std::cout << indent << " └ Vicinato: [";
        for (const auto& v : vicini) std::cout << v << " ";
        std::cout << "]\n";
        std::cout << indent << " └ Sketch (primi 5 hash): ";
        for (size_t i = 0; i < std::min((size_t)5, hashvalues.size()); i++) {
            std::cout << hashvalues[i] << " ";
        }
        std::cout << "...\n";
    }
};

// Implementazione di HyperLogLog
class HyperLogLogNeighborhoodSketch : public NeighborhoodSketch {
private:
    int precision;
    int m; // Numero di registri (2^precision)
    std::vector<uint8_t> registers;

    // Funzione helper per contare i leading zeros
    uint8_t count_leading_zeros(uint32_t val) const {
        if (val == 0) return 32;
        return __builtin_clz(val); // GCC/Clang built-in per efficienza
    }

    void update(const std::string& v) {
        uint32_t hash = std::hash<std::string>{}(v);
        // Estrai l'indice del registro (primi 'precision' bit)
        uint32_t idx = hash >> (32 - precision);
        
        // Estrai la parte restante dell'hash per calcolare rho
        uint32_t w = hash << precision; 
        uint8_t rho = count_leading_zeros(w) + 1;
        
        // Se w = 0, rho = 32 - precision + 1
        if (w == 0) rho = 33 - precision;

        if (rho > registers[idx]) {
            registers[idx] = rho;
        }
    }

public:
    HyperLogLogNeighborhoodSketch(const std::vector<std::string>& init_vicini = {}, int precision = 8) 
        : precision(precision), m(1 << precision), registers(1 << precision, 0) {
        
        for (const auto& v : init_vicini) {
            vicini.insert(v);
            update(v);
        }
    }

    std::shared_ptr<NeighborhoodSketch> merge(const NeighborhoodSketch& other) const override {
        const auto* other_hll = dynamic_cast<const HyperLogLogNeighborhoodSketch*>(&other);
        auto new_sk = std::make_shared<HyperLogLogNeighborhoodSketch>(std::vector<std::string>{}, precision);
        
        std::set_union(vicini.begin(), vicini.end(),
                       other_hll->vicini.begin(), other_hll->vicini.end(),
                       std::inserter(new_sk->vicini, new_sk->vicini.begin()));

        for (int i = 0; i < m; ++i) {
            new_sk->registers[i] = std::max(this->registers[i], other_hll->registers[i]);
        }
        return new_sk;
    }

    std::shared_ptr<NeighborhoodSketch> clone() const override {
        auto new_sk = std::make_shared<HyperLogLogNeighborhoodSketch>(std::vector<std::string>{}, precision);
        new_sk->vicini = this->vicini;
        new_sk->registers = this->registers;
        return new_sk;
    }

    int get_estimated_card() const {
        double z = 0.0;
        for (int i = 0; i < m; ++i) {
            z += std::pow(2.0, -registers[i]);
        }
        
        // Calcolo della costante alpha_m
        double alpha_m;
        if (m == 16) alpha_m = 0.673;
        else if (m == 32) alpha_m = 0.697;
        else if (m == 64) alpha_m = 0.709;
        else alpha_m = 0.7213 / (1.0 + 1.079 / m);

        double estimate = alpha_m * m * m / z;
        return static_cast<int>(std::round(estimate));
    }

    void print_info(const std::string& indent) const override {
        std::cout << indent << " └ Vicinato: [";
        for (const auto& v : vicini) std::cout << v << " ";
        std::cout << "]\n";
        std::cout << indent << " └ Sketch (cardinalità stimata HLL): " << get_estimated_card() << "\n";
    }
};

using SketchFactory = std::function<std::shared_ptr<NeighborhoodSketch>(const std::vector<std::string>&)>;

struct RangeTreeNode {
    int start_time;
    int end_time;
    std::shared_ptr<NeighborhoodSketch> sk;
    
    std::shared_ptr<RangeTreeNode> left;
    std::shared_ptr<RangeTreeNode> right;

    bool is_leaf() const {
        return !left && !right;
    }
};

class RangeTree {
private:
    std::string node_id;
    const TemporalGraph* temporal_graph; // <-- ORA È UN PUNTATORE
    SketchFactory sketch_factory;
    std::vector<int> times;
    std::shared_ptr<RangeTreeNode> root;

    std::vector<int> _extract_times() {
        std::set<int> unique_times;
        for (const auto& edge : temporal_graph->edges) { // Riferimento al puntatore del grafo
            // Condizione aggiunta: l'arco deve essere incidente al nostro nodo v
            if (edge.u == node_id || edge.v == node_id) {
                unique_times.insert(edge.time);
            }
        }
        return std::vector<int>(unique_times.begin(), unique_times.end());
    }

    std::vector<std::string> _vicini_at_time(int t) {
        std::set<std::string> vicini;
        for (const auto& edge : temporal_graph->edges) { // <-- Usa -> invece del punto
            if (edge.time != t) continue;
            if (edge.u == node_id) vicini.insert(edge.v);
            else if (edge.v == node_id) vicini.insert(edge.u);
        }
        return std::vector<std::string>(vicini.begin(), vicini.end());
    }

    std::vector<std::shared_ptr<NeighborhoodSketch>> _build_leaf_sks() {
        std::vector<std::shared_ptr<NeighborhoodSketch>> sks;
        for (int t : times) {
            std::vector<std::string> vicinato_t = _vicini_at_time(t);
            sks.push_back(sketch_factory(vicinato_t));
        }
        return sks;
    }

    std::shared_ptr<RangeTreeNode> _build_tree(const std::vector<int>& t_subset, 
                                               const std::vector<std::shared_ptr<NeighborhoodSketch>>& s_subset) {
        if (t_subset.empty()) return nullptr;

        if (t_subset.size() == 1) {
            auto node = std::make_shared<RangeTreeNode>();
            node->start_time = t_subset[0];
            node->end_time = t_subset[0];
            node->sk = s_subset[0]->clone();
            return node;
        }

        size_t mid = t_subset.size() / 2;

        std::vector<int> left_t(t_subset.begin(), t_subset.begin() + mid);
        std::vector<std::shared_ptr<NeighborhoodSketch>> left_s(s_subset.begin(), s_subset.begin() + mid);
        
        std::vector<int> right_t(t_subset.begin() + mid, t_subset.end());
        std::vector<std::shared_ptr<NeighborhoodSketch>> right_s(s_subset.begin() + mid, s_subset.end());

        auto left = _build_tree(left_t, left_s);
        auto right = _build_tree(right_t, right_s);

        auto merged_sk = left->sk->merge(*(right->sk)); // Composizione dello sketch dei vicinati[cite: 1]

        auto node = std::make_shared<RangeTreeNode>();
        node->start_time = left->start_time;
        node->end_time = right->end_time;
        node->sk = merged_sk;
        node->left = left;
        node->right = right;
        
        return node;
    }

    std::shared_ptr<NeighborhoodSketch> _query(std::shared_ptr<RangeTreeNode> node, int start, int end) {
        if (!node) return nullptr;

        // Intervallo completamente contenuto
        if (start <= node->start_time && node->end_time <= end) {
            std::cout << "Nodo canonico recuperato -> [" << node->start_time << "," << node->end_time << "]\n";
            return node->sk; // Ritorna lo sketch dell'intervallo[cite: 1]
        }

        // Intervallo disgiunto
        if (node->end_time < start || node->start_time > end) {
            return nullptr;
        }

        auto left_res = _query(node->left, start, end);
        auto right_res = _query(node->right, start, end);

        if (!left_res) return right_res;
        if (!right_res) return left_res;

        return left_res->merge(*right_res);
    }

public:
    RangeTree(std::string id, const TemporalGraph* graph, SketchFactory factory) 
        : node_id(id), temporal_graph(graph), sketch_factory(factory) {
        
        times = _extract_times();
        if(!times.empty()) {
            std::cout << "Costruzione del RangeTree per il nodo: " << node_id << "\n";
            auto leaf_sks = _build_leaf_sks();
            root = _build_tree(times, leaf_sks);
        }
    }

    std::shared_ptr<NeighborhoodSketch> query(const std::string& label, int start, int end) {
        std::cout << "\nQuery per il RangeTree di nodo " << label << " del Grafo Temporale\n";
        return _query(root, start, end);
    }

    void print_tree(std::shared_ptr<RangeTreeNode> node = nullptr, int level = 0) {
        if (!node && level == 0) node = root;
        if (!node) return;

        std::string indent = "";
        for(int i=0; i<level; ++i) indent += "   ";

        std::cout << indent << "[" << node->start_time << "," << node->end_time << "]\n";
        node->sk->print_info(indent);
        
        if (node->left) print_tree(node->left, level + 1);
        if (node->right) print_tree(node->right, level + 1);
    }
};

void print_graph(const TemporalGraph& G) {
    std::cout << "Grafo Temporale:\n";
    for (const auto& edge : G.edges) {
        std::cout << edge.u << " -- " << edge.v << " @ " << edge.time << "\n";
    }
}

class TemporalRangeForest {
private:
    const TemporalGraph* temporal_graph;
    SketchFactory sk_factory;
    std::map<std::string, RangeTree> trees;

    std::vector<std::string> _extract_nodes() {
        std::set<std::string> nodes;
        for (const auto& edge : temporal_graph->edges) {
            nodes.insert(edge.u);
            nodes.insert(edge.v);
        }
        return std::vector<std::string>(nodes.begin(), nodes.end());
    }

public:
    // Il costruttore ora accetta il grafo e una stringa (es. "MH" o "HLL")
    TemporalRangeForest(const TemporalGraph* graph, const std::string& sketch_type = "MH") 
        : temporal_graph(graph) {
        
        // Imposta la factory corretta in base alla stringa passata
        if (sketch_type == "HLL") {
            sk_factory = [](const std::vector<std::string>& vicini) {
                return std::make_shared<HyperLogLogNeighborhoodSketch>(vicini, 8);
            };
        } else {
            sk_factory = [](const std::vector<std::string>& vicini) {
                return std::make_shared<MinHashNeighborhoodSketch>(vicini, 5);
            };
        }

        auto nodes = _extract_nodes();
        for (const auto& node : nodes) {
            trees.emplace(node, RangeTree(node, temporal_graph, sk_factory));
        }
    }

    RangeTree* get_tree_for_node(const std::string& node) {
        return &trees.at(node);
    }
};

#ifndef BUILD_PYTHON_MODULE
int main() {
    // Calcolo del tempo di esecuzione per la costruzione del grafo e del RangeTree

    auto start_time_graph = std::chrono::high_resolution_clock::now();

    std::cout << "Creazione del grafo tramite build_graph()\n";
    //TemporalGraph G = build_graph(); // Utilizzo della tua funzione[cite: 2]
    // Creazione del grafo a partire da un dataset esterno
    auto [dataset, name] = read_dataset_from_file("lkml_sort");
    TemporalGraph G = create_graph_from_ds(dataset);

    //TemporalGraph G = build_graph_one_v(); // Utilizzo della tua funzione[cite: 2]

    auto end_time_graph = std::chrono::high_resolution_clock::now();
    auto duration_graph = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_graph - start_time_graph);
    print_graph(G);
    int num_nodes = G.nodes().size();
    std::cout << "Numero di nodi nel grafo: " << num_nodes << "\n";
    // Factory per MinHash (usa HLL Factory per testare l'altra)
    // using SketchFactory = std::function<std::shared_ptr<NeighborhoodSketch>(const std::vector<std::string>&)>;

    // SketchFactory mh_factory = [](const std::vector<std::string>& vicini) {
    //     return std::make_shared<MinHashNeighborhoodSketch>(vicini, 128);
    // };
    
    // SketchFactory hll_factory = [](const std::vector<std::string>& vicini) {
    //     return std::make_shared<HyperLogLogNeighborhoodSketch>(vicini, 8);
    // };
    
    auto start_time_range_tree = std::chrono::high_resolution_clock::now();

    std::cout << "Starting the TemporalRangeForest construction\n";
    
    TemporalRangeForest TRF(&G, "MH");

    auto end_time_range_tree = std::chrono::high_resolution_clock::now();
    auto duration_range_tree = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_range_tree - start_time_range_tree);

    // int num_nodes = G.nodes().size();
    // std::cout << "Numero di nodi nel grafo: " << num_nodes << "\n";
    int random_node_index = rand() % num_nodes;
    std::string random_node = G.nodes()[random_node_index];
    std::cout << "Nodo casuale selezionato per la query: " << random_node << "\n";

    TRF.get_tree_for_node(random_node)->print_tree(); // Stampa del RangeTree del nodo "A"

    auto start_time_query = std::chrono::high_resolution_clock::now();
    // Esempio di query per l'intervallo [2000, 2004] sul RangeTree del nodo "862"
    // auto result_sketch = range_trees.at("I").query("I", 5,8); // Intervallo [2, 9] per il nodo "A"
    auto result_sketch = TRF.get_tree_for_node(random_node)->query(random_node, 1998, 2002);
    if (result_sketch) {
        std::cout << "\nSketch risultante per l'intervallo [1998, 2002]:\n";
        result_sketch->print_info("   ");
    } else {
        std::cout << "Nessun vicinato trovato per l'intervallo [1998, 2002].\n";
    }
    auto end_time_query = std::chrono::high_resolution_clock::now();
    auto duration_query = std::chrono::duration_cast<std::chrono::nanoseconds>(end_time_query - start_time_query);

    stampaTempoAuto(duration_graph,"grafo");
    stampaTempoAuto(duration_range_tree,"rt");
    stampaTempoAuto(duration_query,"query");

    return 0;
}
#endif

// "temporal_forest" sarà il nome del modulo Python che andrai ad importare
#ifdef BUILD_PYTHON_MODULE
PYBIND11_MODULE(temporal_forest, m) {
    m.doc() = "Modulo C++ per la Temporal Range Forest";

    py::class_<TemporalGraph>(m, "TemporalGraph")
        .def(py::init<>()) 
        .def("add_edge", &TemporalGraph::add_edge)
        .def("nodes", &TemporalGraph::nodes);

    py::class_<NeighborhoodSketch, std::shared_ptr<NeighborhoodSketch>>(m, "NeighborhoodSketch");
    
    // Esponi HLL per poter leggere la cardinalità
    py::class_<HyperLogLogNeighborhoodSketch, NeighborhoodSketch, std::shared_ptr<HyperLogLogNeighborhoodSketch>>(m, "HyperLogLogSketch")
        .def("get_estimated_card", &HyperLogLogNeighborhoodSketch::get_estimated_card);

    // Esponi MH per poter leggere la Jaccard
    py::class_<MinHashNeighborhoodSketch, NeighborhoodSketch, std::shared_ptr<MinHashNeighborhoodSketch>>(m, "MinHashSketch")
        .def("jaccard_sim", &MinHashNeighborhoodSketch::jaccard_sim);

    // Ora pybind11 sa che accetti un Grafo e una stringa opzionale
    py::class_<TemporalRangeForest>(m, "TemporalRangeForest")
        .def(py::init<const TemporalGraph*, const std::string&>(), py::arg("graph"), py::arg("sketch_type") = "MH")
        .def("query", [](TemporalRangeForest& trf, const std::string& node, int start, int end) {
            return trf.get_tree_for_node(node)->query(node, start, end);
        });
}
#endif