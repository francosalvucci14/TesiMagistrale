from __future__ import annotations
from dataclasses import dataclass
from typing import Any,Dict,List,Optional
from datasketch import MinHash
import networkx_temporal as tx
from utils import build_graph
import optparse

parser = optparse.OptionParser()
#parser.add_option("-e", "--eps",action="store", dest="eps",default=0.5, help="Epsilon threshold for structural similarity (default: 0.5)",type="float")
#parser.add_option("-m", "--mu", action="store", dest="mu",default=2, help="Minimum number of epsilon neighbors to be a core vertex (default: 2)",type="int")
#parser.add_option("-t", "--tau", action="store", dest="tau",default=2, help="Size of the intervals [t,t+tau] (default: 2)",type="int")
#parser.add_option("-a","--algorithm", action="store", dest="algorithm",default="MAFIA", help="Algorithm to use for stable core extraction: MAFIA or Nte (default: MAFIA)",type="string")
parser.add_option("-S","--sksize",action="store",dest="num_perm",default=128,help="Size of the Sketch (number of permutation)",type="int")
parser.add_option("-i","--start_time",action="store",dest="start_time",default=1,help="Interval starting time")
parser.add_option("-f","--end_time",action="store",dest="end_time",help="Interval ending time")
options, args = parser.parse_args()

#NUM_PERM = options.sksize

# Sketch Astratto

class NeighborhoodSketch:
    # Per ora esempio, sketch del vicinato basato su MinHash - Da astrarre a qualunque sketch

    def __init__(self,vicini=None,num_perm=128):
        self.num_perm=num_perm
        self.minhash=MinHash(num_perm=num_perm)
        self.vicini = set(vicini or [])

        if vicini is not None:
            for vicino in self.vicini:
                self.minhash.update(str(vicino).encode("utf-8"))

    def merge(self, other_sk: "NeighborhoodSketch") -> "NeighborhoodSketch":
        # Return nuovo sketch ottenuto dalla composizione dei due passati

        new = NeighborhoodSketch()

        new.vicini = self.vicini | other_sk.vicini

        new.minhash = self.minhash.copy()
        new.minhash.merge(other_sk.minhash)
        return new

    def copy(self):
        new = NeighborhoodSketch(num_perm=self.num_perm)
        new.minhash = self.minhash.copy()
        return new

    def jaccard_sim(self,other_sk: "NeighborhoodSketch") -> float:
        # Similarità di Jaccard

        return self.minhash.jaccard(other_sk.minhash)

    def __repr__(self):
        return f"MinHashSketch(num_perm={self.num_perm}) -> {self.minhash.hashvalues}"

@dataclass
class RangeTreeNode:
    start_time: int
    end_time: int

    sk: NeighborhoodSketch

    left: Optional["RangeTreeNode"] = None
    right: Optional["RangeTreeNode"] = None

    @property
    def is_leaf(self):
        return self.left is None and self.right is None

    def __repr__(self):
        return (
            f"RangeTreeNode(Interval -> [{self.start_time,self.end_time}],Sketch -> {self.sk})"
        )

@dataclass
class RangeTree:
    def __init__(self, node_id, temporal_graph,num_perm):
        self.node_id = node_id
        self.temporal_graph = temporal_graph
        self.num_perm = num_perm
        self.times = self._extract_times()

        leaf_sks = self._build_leaf_sks()

        self.root = self._build_tree(self.times, leaf_sks)

    def _extract_times(self):

        times = set()

        for _,_,data in self.temporal_graph.temporal_edges(data=True):
            times.add(data["time"])

        return sorted(times)

    def _vicini_at_time(self,t):
        # Return vicinato del nodo al tempo t

        # Gt = self.snapshots[t]

        # if self.node_id not in Gt:
        #     return []

        # return list(Gt.neighbors(self.node_id))

        vicini = set()

        for u,v,data in self.temporal_graph.temporal_edges(data=True):
            if data["time"] != t:
                continue
            if u == self.node_id:
                vicini.add(v)
            elif v == self.node_id:
                vicini.add(u)

        return list(vicini)

    def _build_leaf_sks(self):

        sks = []

        for t in self.times:
            vicinato_t = self._vicini_at_time(t)
            sks.append(NeighborhoodSketch(vicinato_t,self.num_perm))

        return sks

    def _build_tree(self,times,sks):
        if len(times) == 1:
            return RangeTreeNode(start_time = times[0],end_time = times[0],sk=sks[0])

        mid = len(times)//2

        left = self._build_tree(times[:mid],sks[:mid])
        right = self._build_tree(times[mid:],sks[mid:])

        merged = left.sk.merge(right.sk)

        return RangeTreeNode(
            start_time=left.start_time,
            end_time=right.end_time,
            sk=merged,
            left=left,
            right=right
        )

    def query(self,label, start, end):
        # Return sketch dell'intervallo [start,end]
        print(f"\nQuery per il RangeTree di nodo {label} del Grafo Temporale\n")
        return self._query(self.root,start,end)

    def _query(self,node,start,end):
        if node is None:
            return None

        #intervallo completamente contenuto

        if start <= node.start_time and node.end_time <= end:
            print(f"Nodo canonico recuperato -> [{node.start_time,node.end_time}]")
            return node.sk

        #intervallo disgiunto

        if node.end_time < start or node.start_time > end:
            return None

        left = self._query(node.left,start, end)
        right = self._query(node.right,start,end)

        if left is None:
            return right

        if right is None:
            return left

        return left.merge(right)

    def print_tree(self,node=None, level=0):
        if node is None:
            node = self.root

        indent  ="   "*level

        print(f"{indent}[{node.start_time},{node.end_time}]")
        print(f"{indent} └ Vicinato {sorted(node.sk.vicini)}")
        print(f"{indent} └ Sketch (primi 5 hash) {node.sk.minhash.hashvalues[:5]} ... ") #mettere hashvalues[:N] per stampare solo i primi N valori
        
        if node.left:
            self.print_tree(node.left,level+1)
        if node.right:
            self.print_tree(node.right,level+1)

class TemporalRangeForest:
    # Temporal forest composta da un RangeTree per ogni nodo

    def __init__(self,temporal_graph,num_perm):
        self.temporal_graph = temporal_graph

        self.trees = {}

        nodes = self._extract_nodes()

        for node in nodes:
            self.trees[node] = RangeTree(node,temporal_graph,num_perm)

    def _extract_nodes(self):
        nodes = set()
        
        # for G in snapshots:
        #     nodes.update(G.nodes())

        for u,v,_ in self.temporal_graph.temporal_edges(data=True):
            nodes.add(u)
            nodes.add(v)

        return sorted(nodes)

    def get_tree_for_node(self,node):
        return self.trees[node]

if __name__ == "__main__":
    G = build_graph()


    #print(type(G))
#
    #print(next(iter(G.edges(data=True,keys=True))))
    #print(dir(G))

    num_perm = options.num_perm
    start_T = options.start_time
    end_T = options.end_time

    if start_T > end_T:
        #raise ValueError("Starting time need to be greater or equal to ending time")
        print("Starting time need to be greater or equal to ending time")
        exit()

    TRF = TemporalRangeForest(G,num_perm)
    nodes = list(G.nodes()[0])
    
    # for node in nodes:
        # tree = TRF.get_tree_for_node(node)
        # print(f"RangeTree per nodo {node}:\n")
        # print(f"\t {tree.print_tree()}\n") 
    # print("RangeTree del nodo G: \n")
    # print(f"\t{TRF.get_tree_for_node("G").print_tree()}")

    print("RangeTree del nodo A: \n")
    print(f"\t{TRF.get_tree_for_node("A").print_tree()}")

    print("RangeTree del nodo C: \n")
    print(f"\t{TRF.get_tree_for_node("C").print_tree()}")

    # Risposta a query, dati due nodi A e F quanto vale la similarità fra i due vicinati, nell'intervallo [2,5]

    RT_a = TRF.get_tree_for_node("A").query("A",start_T,end_T)
    RT_b = TRF.get_tree_for_node("C").query("C",start_T,end_T)

    print("\n")

    print(RT_a.minhash.hashvalues[:5])
    print(RT_b.minhash.hashvalues[:5])

    print(f"\nJaccard Similarity: {RT_a.jaccard_sim(RT_b)}")