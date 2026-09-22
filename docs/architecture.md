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
internal helper returns a METIS status. Constructors and workspace/refinement
helpers return status before an incomplete object is used. Affected graph and
mesh entry points restore temporary numbering and leave caller-owned result
pointers uncommitted on failure. Malformed input returns `METIS_ERROR_INPUT`,
while an unrepresentable or failed allocation returns `METIS_ERROR_MEMORY`.
Workspace callers check marker insertion and payload allocation before use;
markerless pop leaves the preceding frame intact. Mesh partitioning also checks
its node-element lists and row-induction arrays before assigning the induced
partition.

Graph and mesh validation checks representability where derived storage is
allocated. METIS edge weights are positive, vertex weights and sizes are
nonnegative, and CLI output is validated before a temporary output file is
created. Full duplicate-edge and symmetry validation remains an input/debug
tool responsibility rather than an added cost on every public partition call.

Use `METIS_SetDefaultOptions` to initialize option arrays. Widths come from the
configured public header; see the [build reference](building.md) and
[fork comparison](../FORK_CHANGES.md).
