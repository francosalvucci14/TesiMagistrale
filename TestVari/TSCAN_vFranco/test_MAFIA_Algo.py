class MafiaAlgo:
    def __init__(self,custom_bm, num_transactions, minsup):
        self.minsup = minsup
        self.num_tr = num_transactions
        self.MFI = []

        self.bitmaps = {}

        for item,bitmap in custom_bm.items():
            if self._get_support(bitmap) >= self.minsup:
                self.bitmaps[item] = bitmap 

    #     self._build_trans(transactions)
    
    # def _build_trans(self,transactions):
    #     all_item = sorted(list(set(item for t in transactions for item in t)))

    #     for item in all_item:
    #         bitmap = 0
    #         for idx, t in enumerate(transactions):
    #             if item in t:
    #                 bitmap |= (1 << idx) # imposta il bit idx a 1

    #         # filtro iniziale: teniamo solamente gli item singolarmente frequenti [1-itemset]
    #         if self._get_support(bitmap) >= self.minsup:
    #             self.bitmaps[item] = bitmap
    
    def _get_support(self,bitmap):
        return bin(bitmap).count("1") #conta il numero di bit ad 1, equivalente a popcount() di C
    
    def is_subset_of_any_mfi(self,itemset):
        # controlla se itemset è gia contenuto in un MFI, cosi da "tagliare" l'albero di ricerca
        for mfi in self.MFI:
            if itemset.issubset(mfi):
                return True
        return False
    
    def run(self):
        ordered_it = sorted(self.bitmaps.keys(),key=lambda x:self._get_support(self.bitmaps[x]))

        initial_bitmap = (1 << self.num_tr) -1

        self._mafia(set(),initial_bitmap, ordered_it)
        return self.MFI
    
    def _mafia(self, current_its, current_bm, tail):
        full_union = current_its.union(set(tail))

        if self.is_subset_of_any_mfi(full_union):
            return
        
        if tail:
            tail_bm = current_bm
            for item in tail:
                tail_bm &= self.bitmaps[item]
            
            if self._get_support(tail_bm) >= self.minsup:
                if not self.is_subset_of_any_mfi(full_union):
                    self.MFI.append(full_union)
                return
            
        for i,item in enumerate(tail):
            new_bp = current_bm & self.bitmaps[item]

            if self._get_support(new_bp) >= self.minsup:
                new_is = current_its.union({item})
                new_tail = tail[i+1:]

                self._mafia(new_is,new_bp,new_tail)

        if current_its and not self.is_subset_of_any_mfi(current_its):
            self.MFI.append(current_its)
    
if __name__ == "__main__":
    # Dataset di esempio, ogni riga facciamo che indica un singolo snapshot temporale
    # Grafo temporale preso in considerazione:

    """
    TG2 = tx.temporal_graph()

    TG2.add_edge("A", "B", time=1)
    TG2.add_edge("A", "B", time=2)
    TG2.add_edge("A", "C", time=1)
    TG2.add_edge("B", "C", time=3)
    TG2.add_edge("C", "D", time=4)
    TG2.add_edge("C", "E", time=1)
    TG2.add_edge("C", "E", time=3)
    TG2.add_edge("C", "E", time=4)
    """

    # dataset = [
    #     {"A","B","C","E"},
    #     {"A","B"},
    #     {"B","C","E"},
    #     {"C","D","E"}
    # ]

    bitmap_from_G = {
        "A": 0b0011,
        "B": 0b0111,
        "C": 0b1101,
        "D": 0b1000,
        "E": 0b1101,
    }
    NUM_TR = 4
    MIN_SUP = 3

    mafia = MafiaAlgo(bitmap_from_G,NUM_TR,MIN_SUP)

    MFI = mafia.run()

    print(f"--- MIN_SUP: {MIN_SUP} ---")
    print(f"--- NUM_TR: {NUM_TR} ---")
    print("Frequent Pattern Massimali (MFI) trovati:")
    for mfi in MFI:
        print(f" -> {sorted(list(mfi))}")