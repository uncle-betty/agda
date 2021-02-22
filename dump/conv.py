#!/usr/bin/python3

import json

sym_tab = {}

with open("info-symb.txt", "r") as f:
    for line in f:
        toks = line.strip().split(" ")
        sym_tab[toks[0]] = toks[1]

roots = []
edges = []

len_tab = {}
info_tab = {}

with open("dump.txt", "r") as f:
    for line in f:
        if line.startswith("### root "):
            roots.append(line.strip()[11:])

        elif line.startswith("### visit "):
            toks = line.strip().split(" ")

            toks_to = toks[2].split(":")
            toks_from = toks[4].split(":")

            clos_to = toks_to[0][2:]
            clos_from = toks_from[0][2:]

            len_to = int(toks_to[1])
            len_from = int(toks_from[1])

            addr_to = toks_to[2][2:]
            addr_from = toks_from[2][2:]

            info_to = "???" if addr_to not in sym_tab else sym_tab[addr_to]
            info_from = "???" if addr_from not in sym_tab else sym_tab[addr_from]

            edges.append({"from": clos_from, "to": clos_to})

            if clos_from in len_tab:
                assert len_tab[clos_from] == len_from

            if clos_to in len_tab:
                assert len_tab[clos_to] == len_to

            len_tab[clos_from] = len_from
            len_tab[clos_to] = len_to

            if clos_from in info_tab:
                assert info_tab[clos_from] == info_from

            if clos_to in info_tab:
                assert info_tab[clos_to] == info_to

            info_tab[clos_from] = info_from
            info_tab[clos_to] = info_to

with open("dump.json", "w") as f:
    json.dump({
        "infos": info_tab,
        "lengths": len_tab,
        "roots": roots,
        "edges": edges
    }, f, indent = 1)
