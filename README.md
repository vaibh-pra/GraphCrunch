# GraphCrunch

**Fast automorphism vertex and edge orbits via twin contraction.**

A theoretically grounded preprocessor for [nauty](https://pallini.di.uniroma1.it/)
that removes the single most common source of blow-up in orbit computations on
real graphs, and returns **edge orbits** as well as vertex orbits.

---

## Why

Real graphs are full of **twins**, vertices with the same neighbours. Ontologies,
knowledge graphs, and schema graphs are especially bad for this, because hundreds
of leaf terms hang off the same parent and are structurally interchangeable.

A twin class of size `m` contributes a factor of `m!` to the automorphism group.
Backtrack searchers like nauty must discover that symmetry by search. The work is
spent proving something you already knew from the neighbourhoods alone.

GraphCrunch does not search for it. It reads the twin classes off the adjacency
structure in linear time, contracts each one to a single vertex, colours that
vertex so the contraction cannot be undone incorrectly, hands the much smaller
quotient to nauty, and lifts the answer back exactly.

The lift is not a heuristic and not an approximation. Contracting twin classes and
colouring each quotient vertex by class size and internal type makes the
automorphism group of the original graph a semidirect product `K ⋊ A`, where `K`
is the direct product of the symmetric groups on the twin classes and `A` is the
colour-preserving automorphism group of the quotient. Vertex orbits and edge
orbits of the original graph are recovered from the orbits of `A` with no loss.
See [Correctness](#correctness).

## Benchmarks

Disease Ontology, on a 12th Gen Intel Core i5-1235U, single run, same binary on
both sides.

| | vertices | edges | wall time |
|---|---|---|---|
| nauty on the raw graph | 12,282 | 17,376 | 265.24 s |
| GraphCrunch, contract + solve + lift | 5,884 (47.9%) | 8,491 | **0.10 s** |

Both produce **5,675 orbits** and the partitions are **identical**, which
`--raw` asserts rather than assumes. That is a **2,700x** speedup on this graph.

The gain tracks how much the graph contracts, not how large it is. A graph with no
twins contracts to itself and GraphCrunch costs one extra linear pass. Measure
your own graph with `--raw` before quoting a number.

## Installation

GraphCrunch needs nauty. It is **not bundled**, because nauty is separately
licensed and versioned. One command fetches and builds it.

```sh
git clone https://github.com/vaibh-pra/GraphCrunch.git
cd GraphCrunch
./scripts/get_nauty.sh     # downloads and builds nauty, links it as ./nauty
make
```

Already have nauty built somewhere?

```sh
make NAUTYDIR=/path/to/nauty2_8_8
```

Python needs only the standard library. No pip install, no dependencies.

Check it works:

```sh
make test       # brute-force validation, see Correctness
make example
```

> **Note on linking.** nauty's build produces an archive named `nauty.a`, not
> `libnauty.a`, so the Makefile links it by path. A plain `-lnauty` will not find it.

## Usage

Input is an edge list. One edge per line, tab or whitespace separated, arbitrary
vertex names. Blank lines and `#` comments are ignored, and extra columns are
ignored so a weighted or typed edge list works as is.

```
a1	a2
a2	a3
a1	b1
```

Run it:

```sh
python3 src/orbits_serial.py --edges examples/mygraph.txt --out orbits.txt
```

Useful flags:

| flag | meaning |
|---|---|
| `--edges FILE` | input edge list, required |
| `--out FILE` | output path, or `-` for stdout |
| `--raw` | also solve the uncontracted graph and assert the partitions match |
| `--binary PATH` | path to the solver |

The solver is located in this order: `--binary`, then `$GRAPHCRUNCH_BIN`, then
`automorph_serial_col` beside the script or one directory up, then `PATH`.

### Output

Two sections, in original vertex names.

```
Vertex Classes
3
[p1, p2, p3, q1, q2, q3]
[a2, a3, b2, b3]
[a1, b1]
Edge Classes
4
[(a1, p1), (a1, p2), (a1, p3), (b1, q1), (b1, q2), (b1, q3)]
[(a1, a2), (a1, a3), (b1, b2), (b1, b3)]
[(a1, b1)]
[(a2, a3), (b2, b3)]
```

Each bracketed line is one orbit. Every vertex appears in exactly one vertex
class and every edge in exactly one edge class. The run prints a coverage check
and reports `COMPLETE` when the edge classes account for every edge.

**Edge orbits are the part most tools do not give you.** `pynauty`, for instance,
returns vertex orbits only. They come out of the lift in two families, those from
edges between distinct twin classes and those internal to a twin class, the
second having no counterpart in the quotient at all.

## Correctness

`make test` checks the lifted orbits against the true automorphism orbits, which
are computed by enumerating the entire automorphism group by brute force. It
covers every labelled graph on up to 5 vertices, random graphs on 6 and 7
vertices, and lexicographic blow-ups built to have large twin classes, which is
the case the contraction actually targets.

```
1258 graphs checked, 0 failures
```

The colouring is load-bearing and is not an optimisation. Three false twins form
an independent set and three true twins form a triangle. Both are classes of size
three, so colouring by size alone would let nauty map one onto the other and the
lift would be wrong. Colouring by size **and** internal adjacency type is what
makes the lift exact.

## Layout

```
GraphCrunch/
├── Makefile
├── README.md  LICENSE  NOTICE
├── src/
│   ├── automorph_serial_col.c   nauty wrapper, emits vertex and edge classes
│   └── orbits_serial.py         contraction, colouring, lifting, CLI
├── scripts/get_nauty.sh         fetches and builds nauty
├── examples/mygraph.txt
└── tests/test_orbits.py         brute-force validation
```

## Citation

If you use GraphCrunch in published work, please cite both the method and nauty.

```bibtex
@misc{prakash_graphcrunch,
  author = {Prakash, Vaibhav N.},
  title  = {Twin Contraction and Exact Lifting of Vertex and Edge Orbits},
  year   = {2026},
  note   = {Software: GraphCrunch}
}

@article{mckay2014practical,
  author  = {McKay, Brendan D. and Piperno, Adolfo},
  title   = {Practical graph isomorphism, {II}},
  journal = {Journal of Symbolic Computation},
  volume  = {60},
  pages   = {94--112},
  year    = {2014}
}
```

## Licence

GraphCrunch is MIT licensed. See [LICENSE](LICENSE).

nauty is by Brendan McKay and Adolfo Piperno under the Apache License 2.0 and is
downloaded, not redistributed here. See [NOTICE](NOTICE).
