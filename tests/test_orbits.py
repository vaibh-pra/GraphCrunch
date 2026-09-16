#!/usr/bin/env python3
"""Brute-force validation of the twin-contraction lift.

For each test graph the true vertex and edge orbits are computed by enumerating
the entire automorphism group, then compared against what structure() returns
after contracting, solving the coloured quotient, and lifting. Any disagreement
is a bug in the contraction, the colouring, or the lift.

Coverage:
  every labelled graph on 2..5 vertices          (exhaustive)
  random graphs on 6 and 7 vertices              (sampled)
  lexicographic blow-ups with large twin classes (sampled, the interesting case)

Run:  make test        or        python3 tests/test_orbits.py
"""
import itertools, random, sys
from collections import defaultdict
from itertools import combinations, permutations
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "src"))
from orbits_serial import structure, find_binary


def brute_force(n, ue):
    """True vertex and edge orbits, by enumerating Aut(G). Exponential, so only
    ever called on the small graphs in this file."""
    es = set(map(frozenset, ue))
    auts = [p for p in permutations(range(n))
            if all(frozenset((p[u], p[v])) in es for u, v in ue)]

    def orbits(items, act):
        seen, out = set(), set()
        for x in items:
            if x in seen:
                continue
            o = frozenset(act(g, x) for g in auts)
            out.add(o); seen |= o
        return out

    vo = orbits(range(n), lambda g, x: g[x])
    eo = orbits([tuple(sorted(e)) for e in ue],
                lambda g, e: tuple(sorted((g[e[0]], g[e[1]]))))
    return vo, eo


def check(n, ue, binary):
    adj = defaultdict(set)
    for u, v in ue:
        adj[u].add(v); adj[v].add(u)
    for v in range(n):
        adj[v]
    vlift, elift, _, _ = structure(n, adj, sorted(ue), binary)
    got_v = {frozenset(o) for o in vlift}
    got_e = {frozenset(tuple(sorted(e)) for e in g) for g in elift}
    want_v, want_e = brute_force(n, ue)
    if got_v != want_v:
        return "vertex orbits differ"
    if got_e != want_e:
        return "edge orbits differ"
    return None


def all_graphs(n):
    P = list(combinations(range(n), 2))
    for mask in range(1 << len(P)):
        yield [P[i] for i in range(len(P)) if mask >> i & 1]


def blowup(sizes, base_edges, rnd):
    off, c = [], 0
    for s in sizes:
        off.append(c); c += s
    E = []
    for a, b in base_edges:
        E += [(off[a] + i, off[b] + j)
              for i in range(sizes[a]) for j in range(sizes[b])]
    for k, s in enumerate(sizes):
        if rnd.random() < 0.5:
            E += [(off[k] + i, off[k] + j) for i, j in combinations(range(s), 2)]
    return c, sorted({(min(u, v), max(u, v)) for u, v in E})


def main():
    try:
        binary = find_binary()
    except SystemExit as e:
        print(e); print("SKIPPED, solver not built"); return 0
    print(f"solver: {binary}")
    rnd = random.Random(20260916)
    failures = tested = 0

    for n in range(2, 6):
        for ue in all_graphs(n):
            tested += 1
            why = check(n, ue, binary)
            if why:
                failures += 1
                print(f"FAIL n={n} E={ue}: {why}")
        print(f"n={n}: all {1 << (n * (n - 1) // 2)} labelled graphs checked", flush=True)

    for n, reps in ((6, 60), (7, 40)):
        P = list(combinations(range(n), 2))
        for _ in range(reps):
            ue = sorted(e for e in P if rnd.random() < rnd.choice([.2, .4, .6, .8]))
            tested += 1
            why = check(n, ue, binary)
            if why:
                failures += 1
                print(f"FAIL n={n} E={ue}: {why}")
        print(f"n={n}: {reps} random graphs checked", flush=True)

    base = list(combinations(range(4), 2))
    done = 0
    while done < 60:
        sizes = [rnd.choice([1, 2, 3]) for _ in range(4)]
        if sum(sizes) > 8:
            continue
        nn, ue = blowup(sizes, [e for e in base if rnd.random() < 0.5], rnd)
        if nn < 2:
            continue
        done += 1; tested += 1
        why = check(nn, ue, binary)
        if why:
            failures += 1
            print(f"FAIL blow-up sizes={sizes}: {why}")
    print(f"blow-ups: {done} graphs with large twin classes checked", flush=True)

    print(f"\n{tested} graphs checked, {failures} failures")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
