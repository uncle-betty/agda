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

UNSEEN = 1
SEEN = 2
LOOP_CHECK = 3

for cl in roots:
    graph[cl] = (UNSEEN, set(), set())

# this also overwrites those roots that do have children
for cl, sz in lengths.items():
    graph[cl] = (UNSEEN, set(), set())

for edge in edges:
    cl_from = edge["from"]
    cl_to = edge["to"]
    graph[cl_from][2].add(cl_to)

def traverse(cl, path):
    if cl in infos and infos[cl] == "Agdazm2zi6zi2zm5S8jzzZZ41nCkBPCXkfRBNdd_AgdaziInteractionziOptionsziBase_PragmaOptions_con_info":
        return {cl}

    node = graph[cl]

    state = node[0]
    found = node[1]
    kids = node[2]

    if state == LOOP_CHECK:
        return {}

    if state == SEEN:
        return found

    graph[cl] = (LOOP_CHECK, found, kids)
    old_len = len(found)
    len_list = []

    for kid in kids:
        found_kid = traverse(kid, path + [cl])
        found = found.union(found_kid)
        len_list.append(len(found_kid))

    graph[cl] = (SEEN, found, kids)
    new_len = len(found)

    if new_len >= 32:
        print("XXX {} {} {} {} {} {} {}".format(len(path), cl, old_len, new_len, len_list, kids, path))
        return set()

    return found

for root in roots:
    traverse(root, [])
