#!/usr/bin/env python3
"""
GraphCrunch. Automorphism vertex and edge orbits via twin contraction.

    orbits_serial.py --edges EDGES.tsv [--out ORBITS.txt] [--raw]

Stages:
  1. twin classes: identical open OR closed neighbourhoods, provably interchangeable
  2. quotient, each quotient vertex COLOURED BY (class size, is it a clique)
  3. the nauty-based solver on the coloured quotient
  4. lift: an orbit of G is the union of the twin classes whose representatives
     share an orbit of the quotient, and edge orbits lift by the two-family rule
     in lift_edge_orbits below

--raw additionally runs the solver on the uncontracted graph and checks the two
partitions are identical, which is the only thing that makes the timing meaningful.

The solver binary is located in this order.
  1. --binary on the command line
  2. the GRAPHCRUNCH_BIN environment variable
  3. automorph_serial_col next to this file, or one directory above it
  4. automorph_serial_col anywhere on PATH
"""
import argparse, hashlib, json, os, re, shutil, subprocess, sys, tempfile, time
from collections import defaultdict
from contextlib import contextmanager
from pathlib import Path

BIN_NAME = "automorph_serial_col"


def find_binary(explicit=None):
    """Locate the solver. See the module docstring for the search order."""
    if explicit:
        return Path(explicit)
    env = os.environ.get("GRAPHCRUNCH_BIN")
    if env:
        return Path(env)
    here = Path(__file__).resolve().parent
    for cand in (here / BIN_NAME, here.parent / BIN_NAME):
        if cand.is_file():
            return cand
    found = shutil.which(BIN_NAME)
    if found:
        return Path(found)
    raise SystemExit(
        f"{BIN_NAME} not found. Build it with `make`, or set GRAPHCRUNCH_BIN, "
        f"or pass --binary /path/to/{BIN_NAME}."
    )


@contextmanager
def workdir(path=None):
    """Scratch space for the solver. A private temporary directory unless the
    caller names one, so nothing is written beside the user's data."""
    if path is not None:
        yield Path(path)
    else:
        with tempfile.TemporaryDirectory(prefix="graphcrunch-") as d:
            yield Path(d)

def read_edges(path):
    """One edge per line, tab or whitespace separated, arbitrary vertex names.
    Blank lines and lines beginning with # are ignored. Extra columns are
    ignored, so a weighted or typed edge list is accepted as is."""
    out = []
    for line in Path(path).read_text(errors="ignore").splitlines():
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        p = line.split("\t") if "\t" in line else line.split()
        if len(p) >= 2: out.append((p[0], p[1]))
    return out

def twin_classes(n, adj):
    """Classes of vertices with identical open OR closed neighbourhoods, with a
    clique flag per class.

    The flag is free and never needs testing. A class built from equal OPEN
    neighbourhoods is always an independent set, because N(u) = N(v) with u
    adjacent to v would put u inside N(u) and force a self loop. A class built
    from equal CLOSED neighbourhoods is always a clique, because u lies in
    N[u] = N[v]. So the table a class came from decides it, and the O(sum |c|^2)
    pairwise test the caller used to run is unnecessary.

    Returns (classes, is_clique), parallel lists.
    """
    byo, byc = defaultdict(list), defaultdict(list)
    for v in range(n):
        byo[frozenset(adj[v])].append(v)
        byc[frozenset(adj[v] | {v})].append(v)
    seen, cls, clq = set(), [], []
    for tbl, flag in ((byo, False), (byc, True)):
        for g in tbl.values():
            g = [v for v in g if v not in seen]
            if len(g) > 1:
                cls.append(sorted(g)); clq.append(flag); seen.update(g)
    for v in range(n):
        if v not in seen: cls.append([v]); clq.append(False)
    return cls, clq

def call(binary, el, colour, out):
    t0 = time.time()
    r = subprocess.run([str(binary), str(el)] + ([str(colour)] if colour else [""])
                       + [str(out)], capture_output=True, text=True)
    if r.returncode != 0:
        raise RuntimeError((r.stderr or r.stdout)[:300])
    orbits = parse(Path(out).read_text())
    dt = time.time() - t0          # includes parsing the solver's output
    return orbits, dt

def parse(txt):
    lines = txt.splitlines()
    i = next(k for k, L in enumerate(lines) if L.strip() == "Vertex Classes")
    cnt = int(lines[i + 1]); out = []
    for L in lines[i + 2:]:
        L = L.strip()
        if not L.startswith("[") or len(out) >= cnt: break
        out.append([int(t) for t in L.strip("[]").split(",")])
    return out

def parse_edges(txt):
    """The binary's Edge Classes section: lines of [(a, b), (c, d), ...]."""
    lines = txt.splitlines()
    try:
        i = next(k for k, L in enumerate(lines) if L.strip() == "Edge Classes")
    except StopIteration:
        return []
    cnt = int(lines[i + 1]); out = []
    for L in lines[i + 2:]:
        L = L.strip()
        if not L.startswith("[") or len(out) >= cnt: break
        out.append([tuple(int(x) for x in pr.split(","))
                    for pr in re.findall(r"\(([^)]*)\)", L)])
    return out

def lift_edge_orbits(cls, is_clique, qvert_orbits, qedge_orbits):
    """Edge orbits of G from the quotient's.

    Aut(G) contains the product of the symmetric groups on the twin classes, so:
      * a quotient edge {A,B} corresponds to ALL cross pairs a in A, b in B, and
        they are one orbit; quotient edges sharing a quotient edge orbit merge.
      * a TRUE-twin class is a clique whose internal edges vanish in the quotient.
        Sym(C) is transitive on its pairs, so each such class contributes one
        orbit, and classes sharing a quotient VERTEX orbit merge.
    """
    out = []
    for qo in qedge_orbits:
        grp = set()
        for (i, j) in qo:
            for a in cls[i]:
                for b in cls[j]:
                    grp.add((min(a, b), max(a, b)))
        if grp: out.append(sorted(grp))
    # internal edges of true-twin (clique) classes, flag supplied by twin_classes
    for vo in qvert_orbits:
        grp = set()
        for i in vo:
            if is_clique[i] and len(cls[i]) > 1:
                m = cls[i]
                for x in range(len(m)):
                    for y in range(x + 1, len(m)):
                        grp.add((min(m[x], m[y]), max(m[x], m[y])))
        if grp: out.append(sorted(grp))
    return out

def orb_cache_path(edges_path):
    """Where the structure result for this edge file lives. Keyed on the file's
    content, so editing the graph invalidates it."""
    edges_path = Path(edges_path)
    key = hashlib.sha1(edges_path.read_bytes()).hexdigest()[:16]
    return edges_path.with_suffix("").parent / f".orb_{key}.json"


def structure(n, adj, ue, binary=None, work=None, cache=None):
    """Contract, solve the coloured quotient, lift vertex AND edge orbits.

    n      number of vertices, indexed 0..n-1
    adj    dict v -> set of neighbours
    ue     canonical undirected edge list, (min, max) index pairs
    cache  optional path. If it exists the result is READ FROM IT and no solving
           happens, so the returned timings are not measurements. Off by default,
           which keeps --raw and any benchmark honest. Callers that cache must
           say so in their output.
    Returns (vertex_orbits, edge_orbits, how, stats), all in vertex indices.
    """
    if cache is not None and Path(cache).exists():
        d = json.loads(Path(cache).read_text())
        return ([list(o) for o in d["v"]],
                [[tuple(e) for e in g] for g in d["e"]],
                d["how"] + "  [READ FROM CACHE, timings are not measurements]",
                d["st"])
    with workdir(work) as wd:
        return _structure(n, adj, ue, find_binary(binary), wd, cache)


def _structure(n, adj, ue, binary, work, cache):
    t0 = time.time()
    cls, clq = twin_classes(n, adj)
    rep = {v: i for i, c in enumerate(cls) for v in c}
    m = len(cls)
    qe = sorted({(min(rep[x], rep[y]), max(rep[x], rep[y])) for x, y in ue
                 if rep[x] != rep[y]})
    # Colour by (size, clique). Size alone is NOT enough: a class of three false
    # twins is an independent set and a class of three true twins is a triangle.
    # Same size, different graphs, and mapping one onto the other would make the
    # lift unsound. See negative_case.py for a graph where it goes wrong.
    key = [(len(c), clq[i]) for i, c in enumerate(cls)]
    cmap = {k: i for i, k in enumerate(sorted(set(key)))}
    (work / "_quot.el").write_text(
        f"{m} {len(qe)}\n" + "\n".join(f"{x} {y}" for x, y in qe) + "\n")
    (work / "_quot.col").write_text(
        "\n".join(str(cmap[k]) for k in key) + "\n")
    tc = time.time() - t0

    qorb, qt = call(binary, work / "_quot.el", work / "_quot.col",
                    work / "_quot.out")

    t1 = time.time()
    qeorb = parse_edges((work / "_quot.out").read_text())
    vlift = [sorted(v for i in g for v in cls[i]) for g in qorb]
    elift = lift_edge_orbits(cls, clq, qorb, qeorb)
    tl = time.time() - t1          # parsing the edge classes and lifting both sides

    ttot = time.time() - t0        # the whole of structure(), nothing excluded
    how = (f"twin-contracted quotient ({m}/{n} vertices, {100*m/n:.1f}%, "
           f"{len(qe)} edges, {len(cmap)} colours, {sum(clq)} true-twin classes), "
           f"contract {tc:.2f}s + solver {qt:.2f}s + lift {tl:.2f}s = {ttot:.2f}s")
    st = dict(m=m, tc=tc, qt=qt, tl=tl, ttot=ttot, qe=len(qe),
              n_clique=sum(clq), qeorb=len(qeorb))
    if cache is not None:
        Path(cache).write_text(json.dumps(
            {"v": vlift, "e": [[list(e) for e in g] for g in elift],
             "how": how, "st": st}))
    return vlift, elift, how, st


def main():
    t_start = time.time()
    ap = argparse.ArgumentParser()
    ap.add_argument("--edges", required=True)
    ap.add_argument("--out", default="orbits_serial.txt",
                    help='output path, or "-" for stdout')
    ap.add_argument("--binary", default=None,
                    help="path to %s (default: $GRAPHCRUNCH_BIN, then alongside "
                         "this script, then PATH)" % BIN_NAME)
    ap.add_argument("--raw", action="store_true")
    a = ap.parse_args()

    edges = read_edges(a.edges)
    nodes = sorted({x for e in edges for x in e})
    idx = {x: i for i, x in enumerate(nodes)}
    n = len(nodes)
    adj = defaultdict(set)
    for u, v in edges:
        if u != v: adj[idx[u]].add(idx[v]); adj[idx[v]].add(idx[u])
    ue = sorted({(min(idx[u], idx[v]), max(idx[u], idx[v])) for u, v in edges
                 if u != v})
    print(f"graph: {n} vertices, {len(ue)} edges")

    binary = find_binary(a.binary)
    lifted, eo, how, st = structure(n, adj, ue, binary)
    print(f"contraction: {n} -> {st['m']} vertices ({100*st['m']/n:.1f}%), "
          f"{st['qe']} edges, {st['n_clique']} true-twin classes, {st['tc']:.2f}s")
    print(f"automorph_serial_col on quotient: {st['qt']:.2f}s")
    print(f"lift: {st.get('tl', float('nan')):.2f}s")
    nt = [o for o in lifted if len(o) > 1]
    cov = sum(len(o) for o in nt) / n
    print(f"lifted: {len(lifted)} orbits, {len(nt)} non-trivial, "
          f"coverage {100*cov:.2f}%   "
          f"contract+solve+lift {st.get('ttot', st['tc'] + st['qt']):.2f}s")
    qeorb = st['qeorb']
    covered = set()
    for g in eo: covered.update(g)
    ok = covered == set(ue)
    print(f"edge orbits: {qeorb} on the quotient -> {len(eo)} on G, "
          f"covering {len(covered)}/{len(ue)} edges   {'COMPLETE' if ok else 'INCOMPLETE'}")
    if not ok:
        miss = set(ue) - covered
        print(f"   {len(miss)} edges unaccounted, e.g. "
              f"{[(nodes[x], nodes[y]) for x, y in list(miss)[:3]]}")

    if a.raw:
      with workdir() as work:
        (work/"_raw.el").write_text(
            f"{n} {len(ue)}\n" + "\n".join(f"{x} {y}" for x, y in ue) + "\n")
        rorb, rt = call(binary, work/"_raw.el", None, work/"_raw.out")
        same = ({frozenset(o) for o in lifted} == {frozenset(o) for o in rorb})
        print(f"raw graph, no contraction: {rt:.2f}s, {len(rorb)} orbits")
        print(f"IDENTICAL PARTITION: {same}   "
              f"speedup {rt/max(st['tc']+st['qt'],1e-9):.1f}x")
        if not same:
            sys.exit("raw and lifted partitions disagree, refusing to continue")

    def emit(f):
        f.write("Vertex Classes\n%d\n" % len(lifted))
        for o in lifted:
            f.write("[" + ", ".join(nodes[v] for v in o) + "]\n")
        f.write("Edge Classes\n%d\n" % len(eo))
        for g in eo:
            f.write("[" + ", ".join(f"({nodes[x]}, {nodes[y]})" for x, y in g) + "]\n")

    if a.out == "-":
        emit(sys.stdout)
    else:
        with open(a.out, "w") as f:
            emit(f)
        print(f"wrote {a.out}")
    print(f"END TO END (read + contract + solve + lift + write): "
          f"{time.time() - t_start:.2f}s")

if __name__ == "__main__":
    main()
