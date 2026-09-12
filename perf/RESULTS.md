# Historical METIS Bit-Identical Optimization Campaign — Results

This is an archive of measurements from the `perf-bit-identical` campaign driven by
`PERF-REVIEW.md`. The measured changes were compared byte-for-byte with the campaign's
reference outputs for `seed=12345`; the tables and machine details below remain historical
evidence, not measurements of the current checkout. The selected optimizations are already
part of this checkout's fixed upstream baseline. For current commands, enabled cases,
reference manifests and output locations, see
[performance validation](../docs/development.md#performance-validation).

## Method
- Historical build: `make config cc=gcc-15 && make` (gcc-15, `-O3 -march=native`, single-threaded → deterministic). Current builds use CMake as described in the README.
- Historical gate: `perf/harness.sh verify perf/ref`; the pass count had to match that
  campaign reference manifest with `fail=0`.
- **Two measurement modes, deliberately:**
  - **Per-phase attribution** (`perf/harness.sh bench`): uses **`-dbglvl=2`** to read METIS's
    internal phase timers (coarsen/match/contract/initpart/refine). Its purpose is *attribution*,
    not headline timing: the cit-Patents total drifts ±5–8% run-to-run, which swamps a 0.05–0.1 s
    single-phase win, so the phase column is the only way to see (e.g.) matching go 0.88→0.78 s or
    refine move in isolation — and to confirm an opt touches only the phase it should.
    **Measured `-dbglvl=2` overhead (same binary, min-of-3):** gpmetis kway cit_100 **−2.1%**,
    mdual_100 **−2.4%** (both within noise → gpmetis phase tables are accurate, because its timers
    fire only ~once per coarsening/refinement *level*). **ndmetis +113%** (1.38 s → 2.94 s) — its
    timers fire per dissection node (~n/120 of them), so **the nd `-dbglvl=2` numbers are NOT valid
    timings and are used only to see which files/phase a change hits; the nd headline is dbglvl=0.**
  - **Headline speedup** (`perf/compare.sh`): runs with **NO `-dbglvl` flag, i.e. dbglvl=0**
    (the program default, now in apps/cmdline_*.c), so there is zero timer overhead.
    This is an **interleaved A/B**: baseline binary and current binary alternated per repeat so
    machine-state drift hits both equally; min-of-N of the program's own `(METIS time)` line.
    **All headline % figures below are dbglvl=0.**
- **Memory** (`perf/mem.sh`, dbglvl=0): METIS's gk-tracked heap high-water (`Max memory used`,
  printed even at dbglvl=0) plus OS max RSS via `/usr/bin/time -l` as a ground-truth cross-check.
- Machine: Apple Silicon (10 cores), Darwin 25.5.0. Runs serial, machine otherwise idle.
- Campaign raw rows accumulated in `perf/RESULTS.tsv`. The current harness writes raw rows
  to `<METIS_BUILD_DIR>/perf/RESULTS.tsv`.

## Baseline phase breakdown (serial, min of 3, `-dbglvl=2`) — for attribution only

The `metis` column here is the **dbglvl=2** total. For gpmetis that equals the dbglvl=0 total
(overhead ~0, measured above). For **nd_mdual the total is timer-inflated ~2×** — its true
dbglvl=0 time is ~1.4 s (see headline tables); the phase split below is shown only to locate
where nd time goes, not as absolute seconds.

| config | metis(dbg2) | coarsen | contract | initpart | refine |
|---|---|---|---|---|---|
| kway_cit_10 | 8.108 | 2.476 | 1.585 | 3.055 | 1.796 |
| kway_cit_50 | 11.202 | 2.508 | 1.617 | 2.672 | 5.008 |
| kway_cit_100 | 13.148 | 2.370 | 1.532 | 2.593 | 7.007 |
| kway_mdual_10 | 0.064 | 0.039 | 0.023 | 0.016 | 0.002 |
| kway_mdual_50 | 0.080 | 0.038 | 0.022 | 0.024 | 0.009 |
| kway_mdual_100 | 0.116 | 0.037 | 0.022 | 0.051 | 0.017 |
| rb_mdual_10 | 0.195 | 0.148 | 0.088 | 0.000 | 0.020 |
| rb_mdual_50 | 0.277 | 0.197 | 0.114 | 0.002 | 0.038 |
| rb_mdual_100 | 0.332 | 0.230 | 0.133 | 0.004 | 0.049 |
| nd_mdual ⚠inflated | 2.895 | 0.465 | 0.266 | 0.424 | 1.864 |

Reference edgecuts (seed=12345): cit k10/50/100 = 1673921 / 2741107 / 3213730;
mdual kway 11157/24100/32113; rb 10313/22831/30689; nd opcount 5.964e+10.

## Optimizations (each verified bit-identical before measuring)

| ID | Description | Status | Key effect |
|---|---|---|---|
| T2.2 | Delete dead cmap stores in matching | ✅ bit-identical | matching −5..6% (cit) |
| T2.6 | SHEM condition reorder | ✅ bit-identical | matching −4..15% more (cit) |
| T2.8 | MMDOrder dead restore pass | ✅ bit-identical | nd neutral (noise) on mdual |
| T2.1 | Contraction hash-probe (htkeys) | ❌ REVERTED | contract +8% regression on ARM (extra store/cache pressure; removed load was already hot) |
| T2.3 | Fuse rename into split extraction | ✅ bit-identical | neutral on matrix (split phase tiny here; payoff is large recursive bisection) |
| T2.5 | cnbrpool presizing | ❌ REVERTED | speed-neutral but +7–8% (cit) / +25% (mdual) PEAK MEMORY — bad trade |
| T2.7 | sqrt table (gain priority) | wip | kway refine |
| T2.9 | CompressGraph early abort | ✅ bit-identical | exercised on mdual (rejected); nd front-end saving sub-noise |
| T2.4 | Reuse 2-way FM pqueues across calls | ❌ REVERTED | bit-identical but neutral (alloc churn sub-noise, like T2.5); not worth persistent-state complexity |
| T2.10 | SoA cnbr_t pool | ⏸ DEFERRED | see rationale below |
| T3.1 | LTO (CMAKE_INTERPROCEDURAL_OPTIMIZATION) | ✅ bit-identical KEPT | broad small gain; standard zero-risk |

### Outcome summary
**Kept (7):** T2.2, T2.6 (matching −10..20% on cit), T2.7 (k-way refine −6..7% on cit
50/100), T3.1 LTO (broad small), and T2.3/T2.8/T2.9 (bit-identical, correct, remove
proven work, neutral-but-trivial on this matrix). Net **~3.5–4% on k-way cit-Patents**
and rb_mdual — all bit-identical, and **peak memory identical to baseline** (see memory
section). The only kept opt that allocates anything new is T2.7 (+~1 KB sqrt table).

**Reverted (3), benchmarking-guided:**
- **T2.1** (contraction htkeys): measured **+8% contraction regression** — the removed
  `cadjncy[htable[kk]]` load was already hot, and the parallel `htkeys[]` added a store +
  cache pressure. Apple-Silicon OoO hid the original dependent-load latency.
- **T2.4** (FM queue reuse): **neutral** — the `rpqCreate` O(nvtxs) locator init it removes
  is sub-noise vs the FM work; not worth the persistent ctrl-queue state.
- **T2.5** (cnbrpool presizing): speed-**neutral** but **+6.7–8.5% peak memory on cit-Patents
  (+101–130 MB) and +24–25% on mdual (+7 MB)** — see memory section. Front-loading the full
  `graph->nedges` pool (264 MB on cit) makes it resident alongside the entire coarsening
  hierarchy, instead of growing lazily as coarse graphs are freed. Neutral speed + real
  memory cost = bad trade. (Caught only because memory was measured — initially mis-kept as
  "neutral, removes churn.")

**Deferred (1): T2.10 SoA cnbr_t.** Splitting `cnbr_t{pid,ed}` into parallel `pid[]/ed[]`
arrays touches struct.h, wspace.c, kwayfm.c, kwayrefine.c, macros.h, contig.c, minconn.c —
a large refactor of the hottest refinement loop with high bit-identical risk. Its mechanism
(cache-line efficiency of pid-only scans) is the same class as T2.1, which **measurably
regressed** on this wide-OoO/large-cache target; and the ed[] value is consumed right after
the pid in the gain computation, so both arrays are touched per entry anyway. Expected
benefit on this hardware ≈ 0; risk high. Recommend implementing only behind a profiler on a
cache-bound target (e.g. older x86, or i64/r64 builds where cnbr_t is wider). Not done to
avoid destabilizing the verified working set for predicted-zero gain.

### Key lesson (Apple Silicon, gcc-15 -O3 -march=native -arch arm64)
Opts that remove **actual redundant work** (dead stores T2.2, the SHEM cache-miss skip T2.6)
or **expensive ops on the critical path** (sqrt T2.7) win. Opts that merely trade compute
for memory or rearrange allocation/layout (T2.1, T2.4, T2.5) are neutral-or-worse here — the
wide out-of-order core with large caches already hides the latencies they target.

## Cumulative SPEED result — interleaved A/B at dbglvl=0, baseline vs current (FINAL)

`perf/compare.sh` min-of-5, **dbglvl=0** (no timer overhead), baseline and current alternated
per repeat to cancel machine drift. Baseline = master (no opts, no LTO). Current = final kept
set (T2.2, T2.3, T2.6, T2.7, T2.8, T2.9, T3.1; after reverting T2.1/T2.4/T2.5).

| config | baseline s | current s | delta |
|---|---|---|---|
| kway_cit_10 | 8.022 | 7.699 | **-4.0%** |
| kway_cit_50 | 10.994 | 10.595 | **-3.6%** |
| kway_cit_100 | 13.529 | 13.060 | **-3.5%** |
| kway_mdual_10 | 0.062 | 0.062 | +0.0% (noise) |
| kway_mdual_50 | 0.080 | 0.080 | +0.0% (noise) |
| kway_mdual_100 | 0.118 | 0.120 | +1.7% (noise) |
| rb_mdual_10 | 0.174 | 0.170 | -2.3% |
| rb_mdual_50 | 0.288 | 0.280 | -2.8% |
| rb_mdual_100 | 0.347 | 0.335 | -3.5% |
| nd_mdual | 1.441 | 1.410 | -2.2% |
| nd_mdual_cc | 1.530 | 1.510 | -1.3% |

Net: **~3.5–4% faster on k-way cit-Patents** (the most stable read; cit_100 = -3.5% in two
independent sessions), **rb_mdual -2.3..-3.5%**, **nd -1.3..-2.2%**. mdual k-way is
0.06–0.12 s, so its matching win is sub-millisecond and shows as noise (±). Driven by matching
(T2.2/T2.6) and k-way refine (T2.7); LTO contributes a small, broad amount. All bit-identical.

> An earlier interleaved run (pre-T2.5-revert) read cit -5.4/-4.4/-3.5%; the difference vs the
> -4.0/-3.6/-3.5% above is inter-session baseline drift (that run's baseline was 7.825 vs 8.022
> here). cit_100 = -3.5% is stable across both. Removing T2.5 did **not** reduce speed.

## Cumulative MEMORY result — A/B at dbglvl=0, baseline vs current (FINAL)

`perf/mem.sh`, dbglvl=0. **heap** = METIS gk-tracked high-water (`Max memory used`,
deterministic); **RSS** = OS max resident set via `/usr/bin/time -l` (single-shot, noisy —
includes the 265 MB graph read + page cache). The deterministic heap number is authoritative.

| config | base heap MB | cur heap MB | heap delta | base RSS | cur RSS |
|---|---|---|---|---|---|
| kway_cit_10 | 1520.762 | 1520.763 | +0.0% | 1977.0 | 1975.5 |
| kway_cit_100 | 1527.885 | 1527.886 | +0.0% | 1884.8 | 1861.9 |
| kway_mdual_10 | 29.251 | 29.251 | +0.0% | 51.5 | 50.8 |
| kway_mdual_100 | 29.287 | 29.287 | +0.0% | 50.7 | 50.4 |
| rb_mdual_100 | 28.992 | 28.992 | +0.0% | 52.7 | 66.1* |
| nd_mdual | 29.972 | 29.972 | +0.0% | 66.7 | 59.8* |

**Peak heap is identical to baseline on every config** (the +0.001 MB on cit is T2.7's
`cnbrsqrt` table: `(nparts+1)·8` bytes ≈ 0.8 KB at 100 parts). The kept opts otherwise only
*remove* work or reorder code — no new allocations. (*RSS entries marked `*` are single-shot
variance: gk-heap is byte-identical there, so the swing is page-cache/measurement noise, not a
real regression.)

### Per-kept-opt memory impact
| opt | memory delta |
|---|---|
| T2.2, T2.6, T2.8 | 0 (delete stores/passes; no allocation) |
| T2.3 | 0 (removes a pass; allocations unchanged) |
| T2.9 | 0 (early loop exit; allocations unchanged) |
| T3.1 (LTO) | 0 at runtime (compiler flag) |
| T2.7 | +`(nparts+1)·sizeof(double)` ≈ **0.8 KB** at 100 parts (per refinement workspace) |

Conclusion: the final kept set buys ~3.5–4% on the main k-way/rb workloads **at zero peak-memory
cost**. The only memory-increasing candidate (T2.5) was reverted precisely because its cost was
not justified by any speedup.

### Measurement caveats discovered
- **`-dbglvl=2` doubles ndmetis time** (1.4 s true vs 2.9 s timed): per-phase
  `gk_startcputimer` calls fire thousands of times across the deep dissection recursion. The
  harness phase tables (dbglvl=2) are inflated for nd; the headline figures use dbglvl=0
  (`compare.sh`) and are clean.
- cit-Patents *totals* drift ~5–8% run-to-run with machine state; the reliable reads are
  per-phase isolation (judge an opt by the column it touches) and interleaved A/B at dbglvl=0.
- **Single-shot RSS is noisy**; the gk-tracked heap high-water is deterministic and is the
  authoritative peak-memory number.
