#!/usr/bin/python3

import json
import sys

sys.setrecursionlimit(2000)

with open("dump.json", "r") as f:
    dump = json.load(f)

infos = dump["infos"]
lengths = dump["lengths"]
roots = dump["roots"]
edges = dump["edges"]

graph = {}
states = []

for cl in roots:
    graph[cl] = set()

for cl in lengths:
    graph[cl] = set()

    if infos[cl] == "Agdazm2zi6zi2zm5S8jzzZZ41nCkBPCXkfRBNdd_AgdaziTypeCheckingziMonadziBase_PersistentTCSt_con_info":
        states.append(cl)

for edge in edges:
    cl_from = edge["from"]
    cl_to = edge["to"]
    graph[cl_to].add(cl_from)

for state in states:
    print(state)

    for pred1 in graph[state]:
        print("  {} {}".format(pred1, infos[pred1]))

        for pred2 in graph[pred1]:
            print("    {} {}".format(pred2, infos[pred2]))

            for pred3 in graph[pred2]:
                print("        {} {}".format(pred3, infos[pred3]))
