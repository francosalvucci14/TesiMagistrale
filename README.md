# TesiMagistrale

Per effettuare i test sul prob2 eseguire questa guida step-by-step

1. Creare un `venv` di python con i seguenti moduli:
    * `matplotlib`
    * `pandas`
    * `numpy`
2. Entrare nella cartella `TemporalSketchDataStructure`
3. Entrare nella cartella `exp/exp_prob2_completo`
4. Prima di eseguire lo script `run_all.sh` assicurarsi di eseguire i seguenti operazioni
    * eliminare la cartella `discretized_datasets`
    * eliminare la cartella `plot_analysis`
    * eliminare la cartella `tables`
    * eliminare la cartella `results`
5. Eseguire lo script `./sort_dataset.sh` passando, da terminale, il dataset da modificare 
    * questo script riordina il file del dataset secondo i timestamp, ordinandoli in senso crescente.
    * poi inserisce il dataset aggiornato nella cartella `sorted_datasets`
6. Eseguire lo script `run_all.sh`, questo script fa le seguenti cose:
    * Compila i due codici c++ necessari, `discretize_timestamps.cpp` e `esperimento_opt.cpp`
    * La fase di discretizzazione si occupa di discretizzare i timestamp di tutti i dataset che trova nella cartella `sorted_datasets`, tramite formula $$t_{\text{discreto}} = 1 + \left\lfloor \frac{t_{\text{UNIX}} - t_{\min}}{\Delta} \right\rfloor.$$
    * Dopo aver effettuato la discretizzazione, viene eseguito l'esperimento vero e proprio
        * nel dettaglio, viene eseguito questo comando `./exp_opt "$ds_path" "$ds_name" "$mu" "$TAU" "$K" "$B" "$NUM_QUERIES"` per ogni dataset normalizzato
        * tutte le variaibli sono configurabili direttamente da `run_all.sh`, compresi quali dataset usare tramite l'array `DATASET` 
7. Al termine dell'esecuzione, lo script eseguirà direttamente i due codici python `plot_analysis.py` e `process_result.py`
    * Il primo genera i plot di analisi dell'insieme $W(v)$ per ogni dataset, analizzando i file `{dataset_name}_wv_analysis.csv`, ed inserisce i plot nella cartella `plot_analysis/{dataset_name}_pruning_analysis.png`
    * Il secondo elabora i file `{dataset_name}_summary.csv` e genera, nella cartella `tables`, le due tabelle richieste più un plot riassuntivo delle prestazioni dei 3 algoritmi

**P.S**: I dataset di grandi dimensioni non posso caricarli su GitHub   