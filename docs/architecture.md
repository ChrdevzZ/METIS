# METIS architecture notes

Agent workflow and editing contracts are in [AGENTS.md](../AGENTS.md). This page
keeps the architectural orientation previously held in the Claude-only guide.

METIS uses the multilevel paradigm: coarsen, construct an initial partition,
then refine while uncoarsening. Private `src/metislib.h` includes GKlib, the
configured public METIS header, symbol renaming, template declarations, types,
macros and prototypes. Applications enter through `apps/metisbin.h`; consumers
must use the public header instead of installing those private interfaces.

| Entry point in src/ | Responsibility |
| --- | --- |
| `kmetis.c`, `pmetis.c` | Direct k-way and recursive graph partitioning |
| `ometis.c` | Nested dissection ordering |
| `coarsen.c`, `initpart.c` | Coarsening and initial partition construction |
| `fm.c`, `kwayfm.c`, `sfm.c` | Two-way, k-way and separator refinement |
| `contig.c`, `minconn.c` | Contiguity and subdomain connectivity |
| `mesh.c`, `meshpart.c` | Mesh conversion and partitioning |
| `frename.c`, `fortran.c` | Existing Fortran wrappers and naming variants |

`src/struct.h` defines `graph_t` (CSR graph and partition state), `ctrl_t`
(algorithm options, timers and workspaces) and `mesh_t` (element/node incidence).
Existing renaming headers isolate internal symbols; do not rename exports as a
side effect of refactoring. Internal allocation uses GKlib and workspace push/pop
patterns. Public callers release returned allocations through `METIS_Free`.

Follow the status contract of each API declaration rather than assuming every
internal helper returns a METIS status. Use `METIS_SetDefaultOptions` to initialize
option arrays. Widths come from the configured public header; see the
[build reference](building.md) and [fork comparison](../FORK_CHANGES.md).
