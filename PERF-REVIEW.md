# Historical METIS Runtime Performance Review — Findings & Implementation Handoff

**Date:** 2026-06-09
**Scope:** Full review of `libmetis/` for runtime-performance opportunities, conducted by five
parallel subsystem reviews (coarsening, k-way refinement, bisection/2-way FM, nested
dissection/separators, infrastructure/build) plus measured phase timings and a CPU-sampler
profile on the live gcc-15 build.
**Archive status:** This records the review state on 2026-06-09. The statement that no code
changes had been made applied on that date; selected optimizations were evaluated later and
are present in the fixed upstream baseline used by this checkout. This is not an active
implementation checklist. See [development checks](docs/development.md#performance-validation)
for the current build, reference, verification, case-set and output-path contract, and
[historical results](perf/RESULTS.md) for the subsequent campaign measurements.

---

## 1. Baseline measurements

### 1.1 Build configuration measured

- gcc-15 (Homebrew GCC 15.2.0), flags confirmed from `build/libmetis/CMakeFiles/metis.dir/flags.make`:
  `-O3 -march=native -fno-strict-aliasing -std=c99 -DNDEBUG -DNDEBUG2`
- Asserts compiled out; timer/debug code gated behind `IFSET(ctrl->dbglvl, ...)` — negligible when off.
- **No LTO anywhere** (neither METIS nor the GKlib static archive).
- idx_t/real_t = 32-bit (default build).
- Build: `make config cc=gcc-15 && make` from repo root. GKlib at `~/local`.

### 1.2 Phase timings (`-dbglvl=2`)

> Caveat: the two benchmark batches below ran concurrently (possible memory-bandwidth
> contention; cit-Patents touches ~1.5 GB). Relative phase proportions are trustworthy;
> absolute times are approximate. **Re-run serially before using as an A/B baseline.**

| Workload | Total | Coarsen (Match/Contract) | InitPart | Refine | Project |
|---|---|---|---|---|---|
| cit-Patents kway k=10 | 8.39s | 2.65s (0.96/1.67) | **3.17s** | 1.78s | 0.69s |
| cit-Patents kway k=100 | 13.24s | 2.58s (0.90/1.67) | 2.64s | **6.89s** | 0.98s |
| mdual kway k=10 | 0.079s | **0.052s** (0.021/0.031) | 0.016s | 0.003s | 0.007s |
| mdual kway k=100 | 0.129s | 0.048s | 0.053s | 0.015s | 0.013s |
| mdual rb k=10 | 0.181s | **0.136s** | ~0 | 0.020s | 0.016s |
| mdual rb k=100 | 0.362s | **0.252s** | 0.004s | 0.053s | 0.033s |
| mdual ndmetis | 3.091s | 0.50s | 0.44s | **2.00s (sep-FM)** | 0.047s |

Reference quality numbers (for regression checks):
- cit-Patents k=10: edgecut 1,685,082; k=100: edgecut 3,193,062
- mdual kway k=10: edgecut 10,989; k=100: 32,242; rb k=10: 10,282; rb k=100: 30,912
- ndmetis mdual: nonzeros 4.167e+07, opcount 5.136e+10
- Peak memory cit-Patents: ~1,520 MB; mdual: ~29 MB

### 1.3 CPU sample (5s window, cit-Patents k=100, refinement phase)

- `Greedy_KWayCutOptimize` inline body: **66%**
- `rpqGetTop`: **17%**; `rpqInsert`+`rpqUpdate`+`rpqDelete`: ~6%
- `ProjectKWayPartition`: 10%

Per-pass move trace (`-dbglvl=8`): moves collapse geometrically per level, e.g.
`10643, 1945, 466, 140, 60, 13, 0` over 6–10 passes, while every pass pays the same fixed
cost. Subdomain connectivity at k=100 on cit-Patents is avg 99/99 (fully connected), so
per-vertex `cnbr` lists are nparts-long.

### 1.4 The hotspot map (graph-class dependent)

- **Power-law graphs (cit-Patents):** k-way refinement (52% at k=100) and initial
  partitioning (38% at k=10). Coarsening stalls early → large coarsest graph.
- **Mesh-like graphs (mdual):** coarsening ~60% (contraction ≈ 2× matching). rb is
  2.3–2.8× slower than kway purely from re-coarsening subgraphs.
- **Ordering (ndmetis):** separator refinement (`sfm.c`) is 65% of total.

---

## 2. Tier 1 — High impact, changes results (needs quality A/B)

These attack the measured multipliers. They alter move/trial sequences, so outputs will not
be bit-identical; quality (edgecut/fill) is expected to be neutral on average but MUST be
validated per the protocol in §7.

### T1.1 Active-set k-way refinement

- **Where:** `libmetis/kwayfm.c:184-193` (queue fill), `:196-349` (drain), termination
  `:372`; same pattern in vol variant `:513-519`/`:680` and mc variants `:844-853`/`:1188-1194`.
- **Problem:** Every pass inserts ALL nbnd boundary vertices into the rpq (O(nbnd·log nbnd))
  and extracts every one, re-attempting moves on vertices whose neighborhoods haven't changed
  since the previous pass rejected them. After pass 1–2, <1% of extractions produce moves;
  the final pass (nmoved==0) is 100% wasted verification. Queue ops ≈23% of refinement time;
  much of the 66% body time is rejected-move selection (`:226-299`).
- **Fix options:**
  (a) Active set: seed pass N's queue only from vertices adjacent to pass N-1's moves —
      the `updind` list already tracks exactly these — plus vertices whose pwgts feasibility
      changed.
  (b) Cheaper/lower-risk: early-exit when `nmoved < eps*nbnd` or pass gain below threshold.
- **Impact:** plausibly 30–50% of refinement time on convergent levels → up to ~25% total
  on cit-Patents k=100. **Risk:** medium — rejection is not purely local (pwgts are global),
  so move sequences shift; deterministic but not bit-identical. Start with (b), measure, then (a).

### T1.2 Stop re-coarsening from scratch per trial (three sites, same disease)

1. **kmetis init partitioning:** `kmetis.c:199` sets `options[METIS_OPTION_NCUTS] =
   ctrl->nIparts` (4–5 per `kmetis.c:57`) and calls `METIS_PartGraphRecursive`. Each ncuts
   iteration at `pmetis.c:237-262` calls `CoarsenGraph()` again because
   `Refine2Way`/`Project2WayPartition` (`refine.c:213`) frees coarser levels as it projects.
   So every bisection node of the k-way init runs 4–5 full coarsen+initpart+refine cycles.
   **This is the direct cause of the 3.2s InitPart on cit-Patents k=10.**
2. **ND nseps bump:** `ometis.c:110-111` bumps nseps 1→2 when compression factor >1.5;
   `MlevelNodeBisectionMultiple` (`ometis.c:316`) then redoes the full coarsening hierarchy
   per trial at every tree node with nvtxs ≥ 1000. Near-2× on bisection for compressible inputs.
3. **ND L2 best-of-5:** `ometis.c:347,366-380` — every dissection node ≥5000 vertices
   coarsens 4 levels then runs `nruns=5` FULL multilevel L1 bisections of the coarse graph,
   always all 5 unless a zero-cut appears (`:375`).
- **Fix:** keep one coarsening hierarchy alive across trials; re-diversify only at the
  initial-partition level. Precedent exists in-code: L2 itself shares one 4-level coarsening
  across its 5 runs. For (3), additionally break after 2 consecutive non-improving runs.
  Implementation note: the obstacle is that projection frees levels — either defer frees
  until the trial loop ends, or snapshot the level list. Watch `ctrl->mcore` (WCOREPUSH/POP
  discipline) and `graph_WriteToDisk` ownership.
- **Impact:** high (init phase ~2.6–3.2s on cit-Patents; ND bisection up to 2×).
  **Risk:** moderate — per-trial matching diversity is intentional; reusing one hierarchy
  reduces diversity. A/B fill/cut required. Lower-risk variant: restrict nseps=2 to top
  tree levels only.

### T1.3 Cheaper init-partition trials on coarsening-resistant graphs

- **Where:** trial loop `initpart.c:216-310` (`GrowBisection`); trial count
  `pmetis.c:240` (`niparts = nvtxs<=CoarsenTo ? SMALLNIPARTS(5) : LARGENIPARTS(7)`,
  `defs.h:45-46`); coarsening stop rule `coarsen.c:71-73` (stop when reduction factor
  > COARSEN_FRACTION=0.85 or nedges <= nvtxs/2).
- **Problem:** When coarsening stalls (power-law graphs), the coarsest graph is LARGE —
  and exactly then the code runs MORE trials (7), each paying two O(nvtxs) isets
  (`initpart.c:217-219`), `Compute2WayPartitionParams` O(n+m) (`:286`), `Balance2Way`, and
  FM with the full `ctrl->niter=10` passes (`:298`). (Inconsistency: `RandomBisection`
  hardcodes 4 passes at `initpart.c:165`.) Only early exit is bestcut==0 (`:307`).
- **Fix:** run trials with ~4 FM passes, full-niter refine only the winning `bestwhere`;
  prune trials whose post-BFS cut is ≥2× current bestcut; consider capping trial count by
  coarsest-graph size rather than increasing it.
- **Impact:** high on power-law inputs, medium elsewhere. **Risk:** small quality change.

---

## 3. Tier 2 — Medium-high impact, bit-identical output (zero quality risk)

Validation for everything in this tier: outputs must be byte-identical to the pre-change
build (same `.part.N` / `.iperm` files for fixed seed). If any of these changes output,
the implementation is wrong.

### T2.1 Contraction hash-probe fixes (hottest coarsening loop)

- **Where:** `coarsen.c:923, 939, 955` (probe), `:953-957` (reset); `HTLENGTH=8191`
  (`defs.h:23`).
- **Problem:** probe is `for (kk=k&mask; htable[kk]!=-1 && cadjncy[htable[kk]]!=k; kk=((kk+1)&mask))`
  — two DEPENDENT random loads per probe (`htable[kk]`, then `cadjncy[htable[kk]]`). Runs
  once per fine edge per level (~2× m total). The reset loop then RE-PROBES every inserted
  key a second time.
- **Fix:** (a) store keys alongside slots (parallel `htkeys[]` or `ikv_t` table) so the
  probe compares `htkeys[kk]!=k` — one load; (b) record occupied slot indices in a scratch
  array at insert time and clear those slots directly in the reset (preserve current LIFO
  order). Coarse graph and edge order identical → bit-identical results.

### T2.2 Delete dead `cmap` stores in matching

- **Where:** `coarsen.c:251` (Match_RM), `:397` (Match_SHEM), `:510` (Match_2HopAny),
  `:601` (Match_2HopAll); also `:773` in dead Match_JC.
- **Problem:** `cmap[i]=cmap[maxidx]=cnvtxs++` is written per matched pair, but cmap is
  FULLY recomputed by the renumbering pass (`coarsen.c:267-276` / `:413-422`) before any
  read; 2-hop routines never read cmap. Two random writes per pair, pure dead work.
- **Fix:** delete the cmap assignments; KEEP `match[]` updates and the `cnvtxs` increments
  (2-hop routines return cnvtxs). Provably bit-identical.

### T2.3 Fuse renaming into split-graph extraction (two identical sites)

- **Where:** `pmetis.c:365-370` (SplitGraphPart; rename[] computed at `:307-311`, copy
  loops `:329-363`) and `ometis.c:507-514` (SplitGraphOrder; rename at `:450-454`, copy
  `:485-499`; same in SplitGraphOrderCC `:633-635`).
- **Problem:** adjacency is copied with ORIGINAL ids, then a second full pass re-reads and
  rewrites every split edge to apply `rename` — one extra O(snedges) read+write sweep per
  recursion node, on by-then cache-cold arrays. Cumulative O(|E|·log k) / O(m·log n).
- **Fix:** apply `rename[...]` inline during the copy (`sadjncy[l]=rename[adjncy[j]]` in the
  interior fast path; `rename[k]` in the boundary path); delete the second loop. Safe:
  interior vertices have ed==0 so all neighbors are in mypart; rename is defined for every
  vertex. Bit-identical.

### T2.4 Reuse priority queues + refinement arrays across levels (alloc churn)

- **Where:** `rpqCreate(nvtxs)` per call per level: `fm.c:62-63`, `kwayfm.c:156,486,821,1167`,
  `balance.c:79,206,320`, `sfm.c:47-48,288,510`, `fm.c:253` (one queue PER PART for mc!).
  GKlib `gk_mkpqueue.h:33-40`: Create does O(maxnodes) `-1` locator fill + mallocs;
  `Reset` is only O(#queued) (`:46-55`). Also: `iset(nvtxs,-1,moved)` per pass
  (`fm.c:69`, `sfm.c:62`, `balance.c:81`); per-level `AllocateKWayPartitionMemory` → 5
  gk_mallocs (`kwayrefine.c:121-129`, called at `:350`);
  `Allocate2WayNodePartitionMemory` → 5 mallocs per projection level (`srefine.c:68-79`);
  `SetupSplitGraph` → 7 mallocs per child (`graph.c:145-155`).
- **Problem:** the O(nvtxs) locator fills alone sum to ~6.7×nvtxs writes over the hierarchy
  (geometric sum at COARSEN_FRACTION=0.85), while actual FM work is O(boundary). Multiplied
  enormously in ndmetis by the dissection tree (per-node fixed cost dominates at deep levels).
- **Fix:** hoist queues into `ctrl_t` (created once at finest nvtxs in
  `AllocateRefinementWorkSpace`, `wspace.c:42`), use Reset between uses; replace `moved`
  isets with generation counters; allocate refinement arrays once at finest size and reuse
  (coarser levels strictly smaller). Care: Reset invariants after early exits;
  FreeRData/ondisk interplay. Bit-identical if done right.

### T2.5 cnbrpool presizing (avoid huge realloc copies)

- **Where:** `wspace.c:178-185` (cnbrpoolGetNext 1.5× growth), initial size
  `2*cgraph->nedges` at `kmetis.c:128`; `kwayrefine.c:350,372-373`.
- **Problem:** pool reached 33M entries (264 MB) on cit-Patents; geometric reallocs copy
  the whole live pool (hundreds of MB per copy; worse under i64).
- **Fix:** presize after `cnbrpoolReset` in `ProjectKWayPartition` to
  `min(graph->nedges, Σ min(deg_i,nparts))` (computable O(nvtxs), or just nedges).
  Bit-identical.

### T2.6 SHEM condition reorder (one line)

- **Where:** `coarsen.c:357-364`.
- **Problem:** `if (match[k]==UNMATCHED && maxwgt<adjwgt[j] && ...)` — `match[k]` is a
  scattered (cache-missing) load; `adjwgt[j]` is sequential/in-cache, and for most edges
  the candidate loses on weight anyway.
- **Fix:** reorder to `maxwgt<adjwgt[j] && match[k]==UNMATCHED && ...`. Operands side-effect
  free → semantically identical, bit-identical matching. Note SHEM runs on every level
  after the first (eqewgts cleared at `coarsen.c:66`).

### T2.7 sqrt elimination (two distinct sites)

1. Gain priority `1.0*ed/sqrt(nnbrs)-id`: `kwayfm.c:187-189` (per boundary vertex per pass)
   and `macros.h:179-180` in UpdateQueueInfo (per adjacent vertex per move). Fix: precompute
   `sqrttab[k]=sqrt((double)k)` for k≤nparts once per call; keep the same divide → results
   bit-identical.
2. Degree bucketing `bnum=sqrt(1+degree)`: `coarsen.c:186, 326` (also `:657`), capped at
   avgdegree. Fix: integer table of squares + stepwise compare, exact floor semantics by
   construction. O(n) per level saved ~15–20 cycles/vertex.

### T2.8 MMDOrder dead restore pass

- **Where:** `ometis.c:688-693`.
- **Problem:** after `genmmd`, code restores xadj/adjncy to 0-based — but genmmd DESTROYS
  adjncy contents (documented `mmd.c:32`, overwritten in place by mmdelm), and all three
  call sites (`ometis.c:216-217, 222-223, 286-287`) FreeGraph immediately after. Full
  O(n+m) pass over every leaf graph (~n/120 leaves) for nothing.
- **Fix:** delete lines 688-693. Grep confirms ometis.c is the only caller. Zero risk.

### T2.9 CompressGraph early abort + better key

- **Where:** `compress.c:38-86`; threshold COMPRESSION_FRACTION.
- **Problem:** compression always runs on METIS_NodeND (default compress=1): O(m) key calc
  (weak key: sum of neighbor ids + i), O(n log n) ikvsorti, grouping with O(deg)
  verification per key collision — all discarded at `:86` when the graph is incompressible
  (typical FE meshes).
- **Fix:** (a) `cnvtxs` is non-decreasing → abort inside the loop at `:49` as soon as
  `cnvtxs >= COMPRESSION_FRACTION*nvtxs` (identical decision, just earlier);
  (b) better-mixed hash key to slash false collisions (changes nothing downstream —
  verification still exact). Front-end latency on every NodeND call.

### T2.10 SoA split of cnbr_t (helps the 66% refinement body)

- **Where:** pool `cnbr_t {pid, ed}` in `struct.h`; scans in `macros.h:142-167`
  (UpdateAdjacentVertexInfoAndBND — up to two O(nnbrs) linear scans per adjacent vertex per
  move), move selection `kwayfm.c:227-257`, also `contig.c:562-570`, `minconn.c:508-517`.
- **Problem:** pid-only searches touch 2× the cache lines (AoS). At k=100 on cit-Patents
  nnbrs ≈ 99 on hubs.
- **Fix (zero-risk variant only):** split pool into parallel `pids[]`/`eds[]` arrays; same
  iteration order → identical semantics, dense vectorizable pid scan. DO NOT sort/reorder
  entries (entry order feeds tie-breaking via the reverse scans at `kwayfm.c:227,244` —
  reordering changes results).

### T2.11 Misc exact micro-fixes

- `ComputeKWayPartitionParams` dup-detection: use the htable approach from
  `ProjectKWayPartition` (`kwayrefine.c:393-409`) in `:215-230`/`:277-293`. First-encounter
  order identical. Low impact (coarsest graph only).
- `eqewgts` full O(m) scan (`coarsen.c:29-34, 94-99`): SetupGraph already knows it created
  adjwgt as all-1s via ismalloc (`graph.c:80`) — carry a unit-weights flag.
- `ConstructSeparator` where[] copy-out/copy-in (`separator.c:33-45`): free everything
  except `where` instead. Coarsest graphs only.
- `MlevelNodeBisectionMultiple` re-runs Compute2WayNodePartitionParams when best≠last
  (`ometis.c:332-335`): snapshot pwgts/nbnd/bndind with bestwhere. Only matters nseps>1.
- Init-trial param recompute: `Refine2Way` recomputes params unconditionally (`refine.c:23`)
  even when the last trial won in GrowBisection/RandomBisection (`initpart.c:176-177,
  312-313`) — skip when best==last, mirroring `pmetis.c:264-267`.

---

## 4. Tier 3 — Build system (no code changes)

### T3.1 Enable LTO ⭐ best effort-to-payoff in the whole list

- **Facts:** no `CMAKE_INTERPROCEDURAL_OPTIMIZATION` anywhere; the heap ops (`rpqGetTop`
  etc. = ~23% of refinement samples), `irandInRange` (2 opaque calls per random number),
  and `iset/icopy/isum` are instantiated in `libmetis/gklib.c:18,33-34,40` — cross-TU calls
  with no inlining of comparators, no hoisting of `queue->heap`/`locator` loads.
- **Fix:** `set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)` guarded by `check_ipo_supported()`
  in CMakeLists.txt; build GKlib with `-flto` as well (static archive needs
  `gcc-ar`/`gcc-ranlib`). Alternative with zero build-system risk: move GK_MKPQUEUE
  instantiations into a header as `static inline`.
- **Verify:** symbol renaming (`rename.h`/`gklib_rename.h`, `libmetis__` prefixes) must
  survive — ParMETIS depends on it. Outputs must stay bit-identical (pure compiler change;
  FP is all double, no -ffast-math).

### T3.2 Fix the Clang flag gap (also a latent correctness issue)

- **Where:** `conf/gkbuild.cmake:29-47`.
- **Problem:** `-O3 -march=native -fno-strict-aliasing -std=c99` are gated on
  `CMAKE_COMPILER_IS_GNUCC` (false for Clang/AppleClang — the macOS default). Clang builds
  get bare `-O3`: no arch tuning AND no `-fno-strict-aliasing`, which the codebase
  apparently needs for correctness.
- **Fix:** `if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")`. Note: there is also a known
  pre-existing Apple-clang link failure with GKlib on this machine (separate issue, not
  yet diagnosed).

### T3.3 RNG replacement (deliberate decision, not a quick fix)

- **Facts:** default GKRAND=OFF (`conf/gkbuild.cmake:15`) → `gk_randint32()` =
  `(uint32_t)rand()` (GKlib `random.c:127-134`). glibc `rand()` takes a global lock; the
  `%max` in `randInRange` (`gk_mkrandom.h:59-64`) is an integer div per number. Hot sites:
  `coarsen.c:182,322,653` (irandArrayPermute nvtxs/8 × 4 swaps × 2 rand calls per level —
  see `gk_mkrandom.h:72-103`), `kwayfm.c:184,...`, `fm.c:83,286`, `initpart.c:225-238`.
- **Fix:** inline PCG32/xoshiro128++ in a header (combines with LTO), or default GKRAND=ON.
- **Risk:** changes ALL random sequences → different (statistically equivalent) partitions;
  reproducibility flag-day; ParMETIS expects consistent RNG semantics. Treat as opt-in.

---

## 5. Tier 4 — Mode-specific (zero impact unless the option is used)

### `-minconn` (three findings, ranked)

1. `SelectSafeTargetSubdomains` (`macros.h:223-255`, called per EXTRACTED vertex at
   `kwayfm.c:222-223,546-547,880-881,1221-1222`): per neighbor subdomain, uncompress
   nads entries into vtmp, scan, recompress — O(nnbrs·(nads+nnbrs)) ≈ 10^4 ops per
   extraction at k=100, including vertices that never move. Fix: run the gain screen
   (`kwayfm.c:226-241`) first, compute safetos lazily only for surviving candidates;
   and/or per-subdomain adjacency bitsets. Preserve the `safetos[to]==2` zero-gain rule
   (`:267,588`) semantics exactly.
2. `iarray2csr` full O(nvtxs) rebuild after EVERY group move (`minconn.c:460` inside the
   nested while loops at `:262`). Fix: incremental update (only nind vertices moved).
3. `UpdateEdgeSubDomainGraph` linear scans O(deg·nads) per move (`minconn.c:145-150`,
   `iargmax` over nparts at `:179`; called from `kwayfm.c:318-324` etc.). Fix: dense
   nparts×nparts matrix for nparts ≤ ~512 (40–80 KB at k=100, L2-resident), sparse fallback.

### `-contig`

- Check-then-recompute: `kwayrefine.c:35,66,97` each call `FindPartitionInducedComponents`
  just to test the count, then `EliminateComponents` recomputes the identical decomposition
  (`contig.c:366`). Also 4 heap allocs of nvtxs per call (`contig.c:46-60`) instead of
  wspace. Fix: fold the check into EliminateComponents (early return if ncmps<=nparts);
  use iwspacemalloc.

### `-objtype=vol`

- Marker reset loops in `KWayVolUpdate`/`ComputeKWayVolGains` (`kwayfm.c:1780-1813,
  1543-1545, 1754-1756`; `kwayrefine.c:614-616, 648-650`): explicit O(nnbrs) reset per
  edge. Fix: generation-stamped markers (`pmarker[p]==stamp`) — halves marker traffic,
  bit-identical.

### Mesh paths (`mpmetis`, `m2gmetis`)

- `METIS_MeshToDual`/`MeshToNodal` run the dominant hash-join TWICE: once for sizes
  (`mesh.c:204-207` / `:315-318`), once to fill (`:220-225` / `:331-336`); worker
  `FindCommonElements` at `mesh.c:237-271`. Fix: single pass into a geometrically-grown
  buffer, then compact to CSR. ≈40% off dual-graph construction.

### `-dropedges`

- Full `isortd` per coarse vertex to extract ONE median (`coarsen.c:1017-1018`). Fix:
  quickselect — O(d) and exact (only the median value is consumed).

### 2-hop matching (power-law graphs)

- `Match_2HopAny` rebuilds its inverted index from scratch up to 3× (maxdegree=2, 3, nvtxs;
  `coarsen.c:442-447, 481-498`) — 3× redundant O(n+m_unmatched). Fix: build once at largest
  threshold, filter by degree in the pairing loop — but MUST preserve per-threshold visit
  order for bit-identical matching (medium care).
- `Match_2HopAll` runtime `%mask` per edge (`coarsen.c:556, 568-571`): maxdegree is
  constant 64 at the only call site (`:443`). Zero-risk variant: hoist a
  reciprocal/magic-number division. Changing the hash changes match order → not bit-identical.

---

## 6. Structural opportunities (bigger lifts, design changes)

1. **Don't materialize unit adjwgt** (`graph.c:80` `ismalloc(nedges,1)`): unweighted graphs
   (the common case) pay a permanent extra `adjwgt[j]` load in every gain/cut inner loop and
   doubled contraction traffic. Largest pure-bandwidth win available; requires every kernel
   to become NULL-adjwgt-aware (the `droppedewgt` machinery is related precedent). High
   effort/risk.
2. **Carry `adjwgtsum` per vertex** (METIS 4.x had it): `Project2WayPartition`
   (`refine.c:183-206`) scans the FULL adjacency of interior vertices only to compute
   `id[i]`; with adjwgtsum (`sum(u')=sum(v1)+sum(v2)-2w(v1,v2)` maintained in contraction),
   interior vertices get `id=adjwgtsum, ed=0` with no edge scan → projection drops to
   O(boundary edges + n) per level. Identity `id = adjwgtsum - ed` keeps results exact.
3. **Dense table vs hash table in contraction** (`coarsen.c:912` branch; dense path
   `:963-1006`): dense `dtable[k]` is strictly cheaper per edge than the probe loop, and its
   historical disadvantage (O(cnvtxs) clearing) is gone — the reset is incremental. BUT the
   two paths emit different `cadjncy` edge ORDER → perturbs downstream tie-breaking →
   different (not worse) partitions. Benchmark A/B.
4. **Coarse-graph allocation strategy** (`coarsen.c:1095-1096, 1113-1119`): per level,
   cadjncy/cadjwgt malloc'd at FINE nedges+1, then ReAdjustMemory shrink-reallocs (almost
   always fires, may memcpy). Cheapest: skip/raise-threshold the realloc. Better: exact or
   bounded cnedges precount. Ambitious: double-buffered persistent arenas (watch
   graph_WriteToDisk ownership).
5. **MMDSWITCH=120 crossover** (`defs.h:52`; `ometis.c:213,219,282`): subgraphs of 121–5000
   vertices still pay the full multilevel machinery + SplitGraphOrder mallocs; per-node fixed
   overhead dwarfs genmmd at a few hundred vertices, and tree nodes in 120–1000 range number
   ~n/500. Experiment: raise to 200–500. RISK: MMD produces more fill at larger block sizes
   — the 120 was presumably tuned; validate with cmpfillin. Genuinely uncertain where the
   modern crossover sits.
6. **Approximate priority structure for k-way refinement** (`kwayfm.c:197`; rpqGetTop = 17%
   of samples): the key `ed/sqrt(nnbrs)-id` is itself an approximation, so exact heap order
   is over-engineering — quantized gain buckets would make extraction ~O(1). Changes visit
   order → quality-sensitive; needs careful A/B.
7. **GrowBisection BFS restart** (`initpart.c:238-246`, same in GrowBisectionNode
   `:488-496`): O(nvtxs) linear scan per component restart → O(C·nvtxs) per trial.
   cit-Patents has 3,627 components, and ND subgraphs after separator removal are routinely
   disconnected. Fix: compacted untouched-list with swap-removal, O(1) pick. Changes
   RNG-to-vertex mapping → not bit-identical (quality-neutral in expectation).

---

## 7. Historical validation and benchmarking protocol

The commands and acceptance language below preserve the original campaign protocol. They
refer to the former handwritten build entry points, source-tree reference directory and
program layout, and must not be used as current mandatory instructions. Use the active
[performance validation](docs/development.md#performance-validation) workflow instead.

### 7.1 Build

```bash
cd <repo-root>
make config cc=gcc-15 && make    # build/ tree; binaries in build/programs/
```

### 7.2 Benchmarks (per CLAUDE.md, plus what this review measured)

Run SERIALLY (one process at a time — the baseline in §1.2 had concurrent runs; redo it
first), ≥3 repeats, report median:

```bash
# k-way and rb at 10/50/100 parts on both graphs
./build/programs/gpmetis -dbglvl=2 graphs/cit-Patents.metis {10|50|100}
./build/programs/gpmetis -dbglvl=2 graphs/mdual.graph {10|50|100}
./build/programs/gpmetis -ptype=rb -dbglvl=2 graphs/mdual.graph {10|50|100}
# ordering + fill quality
./build/programs/ndmetis -dbglvl=2 graphs/mdual.graph
./build/programs/cmpfillin graphs/mdual.graph mdual.graph.iperm
# mode-specific tiers: add -minconn, -contig, -objtype=vol runs when touching those paths
```

### 7.3 Campaign acceptance criteria

- **Tier 2 / T3.1 (bit-identical class):** output partition/iperm files MUST be byte-identical
  to the baseline build for the same seed. `cmp` the files. Any diff = implementation bug.
- **Tier 1 / structural (results-change class):** edgecut within ~1% (averaged over seeds
  `-seed=1..5`) and ndmetis opcount/fill within ~2% of baseline, with the timing win
  documented per phase. Run both graphs — a change that helps cit-Patents must not regress
  mdual.
- Always confirm balance constraint still met (gpmetis report prints it) and, where
  applicable, contiguity.

### 7.4 Suggested attack order

1. **T3.1 LTO + T3.2 Clang flags** — pure build changes, verify bit-identical, measure.
2. **Tier 2 batch** (T2.1–T2.11) — land in small commits, each verified bit-identical,
   then one timing run for the batch. Expected combined: meaningful constant-factor wins in
   coarsening + ndmetis fixed costs.
3. **T1.1 early-exit variant** (nmoved threshold) → measure → escalate to active-set if
   warranted. Biggest single lever for large-k power-law.
4. **T1.2 hierarchy reuse** for kmetis ncuts (biggest init-part lever), then ND variants.
5. **T1.3 trial slimming**, then structural items behind benchmarks.

### 7.5 Gotchas for the implementer

- Determinism is a feature: many users diff partitions across versions. Separate
  bit-identical commits from results-changing commits; never mix in one commit.
- `rename.h`/`gklib_rename.h` prefix all internal symbols `libmetis__` — anything moved
  into headers/static-inline must not break ParMETIS's expectations.
- `ctrl->mcore` WCOREPUSH/WCOREPOP is strict LIFO — new ctrl-owned caches must NOT come
  from wspace if their lifetime crosses a WCOREPOP (e.g., cmap is read by
  ProjectKWayPartition during uncoarsening → cannot be wspace'd; checked, not actionable).
- `graph_WriteToDisk` (ondisk mode) takes ownership of graph arrays — arena/buffer-reuse
  schemes must respect the free_* flags on graph_t.
- The benchmark graphs live in `graphs/`; cit-Patents.metis is untracked (265 MB) — do not
  commit it.
- Known incidental issues found (not perf): `kmetis.c:344` aliases `vwgt = graph->xadj` in
  GrowMultisection (dbglvl&512-only path; real bug); `kwayfm.c:80` unconditionally zeroes
  `ffactor`, making the fudge-factor from `kwayrefine.c:60` inert in the cut path (relevant
  if benchmarking against it); dead code: `Match_JC` (`coarsen.c:625`),
  `ConstructMinCoverSeparator` (`separator.c:69`), `mincover.c` in current call graph.

---

## 8. Things checked and ruled out (don't re-investigate)

- **BNDInsert/BNDDelete** (`macros.h:46-69`): O(1) swap-with-last — fine.
- **GKlib binary heap** (`gk_mkpqueue.h`): all ops O(log n) with locator; Update early-outs
  on equal keys. No O(n) traps. (Locator is ssize_t = 8B even in i32 builds — minor cache
  note only.)
- **BucketSortKeysInc** (`bucketsort.c:23-43`): clean O(n+max) counting sort; max is small.
- **No O(nparts) scans in the default-cut per-move hot path** — the design confines
  O(nparts) work to per-pass/per-level setup. minconn/contig are where it leaks (Tier 4).
- **Workspace core sizing** (`wspace.c:21-31`): verified empirically —
  `num_hallocs: 2, size_hallocs: 599000` on mdual k=50; only the 2-hop rowind overflows
  core. Not a bottleneck. (Caveat: tested one graph/one k; minconn/vol untested.)
- **Timers/debug/asserts in release:** fully compiled out or dbglvl-gated — no cost.
- **Match_2HopAny backward pairing loop** (`coarsen.c:506-518`): looks quadratic, is
  amortized O(column length) — fine.
- **mmd.c kernel**: leaves are ≤120 vertices; the leverage is the MMDSWITCH threshold
  (§6.5), not the kernel.
- **sfm.c 1-sided hot loop** (`sfm.c:370-401`): recompute-on-pull single-queue design is
  already the cheap variant; no queue-churn pathology beyond rpqCreate sizing (T2.4). The
  ndmetis 65%-in-refinement cost is mostly per-node fixed overhead (T2.4) and trial
  multipliers (T1.2), not the FM inner loop itself.

---

*Generated from a five-subsystem parallel review + sampled profile, 2026-06-09. Baseline
machine: Apple Silicon (Darwin 25.5.0), Homebrew gcc-15, METIS master @ c3ebd27.*
