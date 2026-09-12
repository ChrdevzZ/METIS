# Tracking METIS upstream changes

For current build instructions see [building](building.md). For the fixed
upstream source, compatibility changes and Unreleased history see
[fork changes](../FORK_CHANGES.md).


`tools/upstream.py` is a developer-only audit and preparation tool. It compares this migration with the official KarypisLab repository while keeping the source checkout, Git index, and refs unchanged. The ordinary CMake configure and build do not require Python.

The manifest records the official baseline commit `272d4a91c5f66c92327493339a476c553ebf1f5d` from `https://github.com/KarypisLab/METIS.git`. Its important path mappings are:

| Upstream path | Local path | Treatment |
| --- | --- | --- |
| `libmetis/*` | `src/*` | Direct or locally modified source mapping |
| `programs/*` | `apps/*` | Direct or locally modified source mapping |
| `include/metis.h` | `include/metis.h.in` | CMake template; always reviewed manually |
| Removed legacy build/test files | none | Tombstones; always reviewed manually |

The generated-header relationships record that CMake configures `include/metis.h.in` as `build/include/metis.h` and creates `build/include/metis_export.h` with `GenerateExportHeader`. Generated build-tree files are not upstream baselines and are never written by this tool.

## Commands

Run commands from the repository root with Python 3.9 or newer:

```sh
python tools/upstream.py check
python tools/upstream.py compare --to <commit>
python tools/upstream.py prepare --to <commit> --output <fresh-build-directory>
```

`check` validates the JSON schema, baseline blob IDs, coverage of upstream and local C, C++, header, Fortran, and template files, named legacy build files, mapping uniqueness, local file state, and generated-header relationships. Extension checks are case-insensitive. The nested `ext/` repository is excluded from METIS local inventory and is checked by GKlib's own manifest. This is a developer check. If the baseline object is missing, it fails with a clear diagnostic; that does not affect a normal CMake build.

`compare` is offline by default. The target commit must already exist in the source repository's object database. It compares complete tracked trees, including additions, deletions, mode changes, and modifications. Exact-content delete/add pairs are reported as rename candidates. When one blob maps to several source or destination paths, every candidate is marked ambiguous.

Fetching is explicit and needs a fresh cache directory:

```sh
python tools/upstream.py compare --to <commit-or-ref> --fetch --cache-dir build/upstream-compare
python tools/upstream.py prepare --to <commit-or-ref> --fetch --output build/upstream-prepare
```

The fetch URL comes from the manifest. A bare repository is created only at `<build-directory>/.upstream-cache.git`; the source repository's remotes, refs, index, and worktree are not changed.

`prepare` creates advisory files in a fresh output directory. It never applies a patch, overwrites a source file, changes the manifest baseline, or stages changes. For direct and modified mappings, it runs:

```text
git merge-file --stdout --diff3 current baseline upstream
```

Only CRLF/LF differences are normalized for comparisons and merging. Other whitespace and bytes remain significant, and a clean candidate uses the current local file's line-ending style.

The output contains:

- `report.json`: machine-readable comparison and disposition data;
- `report.md`: a review-oriented summary;
- `candidate-map.json`: stable IDs and source/upstream paths for every candidate directory;
- `manifest-candidate.json`: a complete advisory `files.json` candidate for the resolved target baseline;
- `candidates.patch`: combined advisory patches;
- `candidates/<number>-<stable-id>/`: baseline, current, upstream, proposed, and per-candidate patch files when applicable.

The manifest candidate retains stable IDs, records the target's full commit and blob IDs, and recalculates `direct` or `modified` from each clean proposed source. Once reviewed source candidates and the manifest are accepted together, that manifest can be checked and used as the baseline for the next update.

Upstream additions, deletions, possible renames, template changes, tombstones, unmapped files, missing local files, and three-way conflicts always carry `review_required: true`. `report.json` lists their stable IDs under `manifest_candidate.manual_review_stable_ids`, and sets `manifest_candidate.acceptance_ready` to false while any remain. To keep the candidate complete without deciding for the reviewer, covered additions appear as advisory `removed` placeholders, while locally present files deleted upstream become `local` entries. Required coverage paths absent from the target are omitted from the candidate coverage list. Exact-content rename candidates remain separate deletion and addition decisions; the tool does not connect the new path to the old local file. Reviewers must resolve these placeholders and entries before accepting the manifest. A clean merge is still only a candidate; applying it is a separate, manual decision.

## Manifest

`upstream/files.json` is the source of truth. Every entry contains all six fields:

```json
{
  "id": "metis-libmetis-auxapi-c",
  "upstream_path": "libmetis/auxapi.c",
  "local_path": "src/auxapi.c",
  "baseline_blob": "<Git blob id>",
  "kind": "direct",
  "notes": "Relocated from libmetis/ to src/."
}
```

Stable IDs do not encode a current commit and must remain unchanged when a path's contents change. Kinds have these meanings:

- `direct`: local content equals the baseline after CRLF normalization;
- `modified`: the mapped local file has intentional changes;
- `template`: upstream content maps to a configured template and requires manual adaptation;
- `local`: no upstream counterpart exists;
- `removed`: the upstream path is intentionally absent locally.

After concurrent source changes, generate a refreshed advisory copy:

```sh
python tools/upstream.py refresh --output build/upstream-manifest-refresh
```

This writes `build/upstream-manifest-refresh/files.json`, recalculating baseline blob IDs and `direct`/`modified` classifications for existing mappings and adding entries for newly inventoried local source or template files. It never overwrites `upstream/files.json` or changes `upstream.baseline`. Review the copy and update the real manifest through the normal source-review process.

The output directory for `prepare`, fetched `compare`, and `refresh` must not already exist. The tool rejects repository roots, parent directories that could contain the checkout, paths that could contain mapped source files, paths crossing symlinks, and paths in either the Git directory or common Git directory (including linked worktrees). Manifest paths reject absolute paths, Windows drives and streams, empty or dot components, and symlink escapes. Modified-content renames are deliberately shown as separate deletion/addition changes because only exact Git-blob rename candidates are inferred automatically. Mode-only changes remain manual reviews because content patches cannot carry their permission change.

## Keeping the fork comparison current

The fork repository is the modification destination; the KarypisLab URL in the
mapping is the official source. The documented source branch is `master`;
comparison uses the full accepted SHA, not its moving tip. When accepting an
update, revise the baseline and current differences in `FORK_CHANGES.md`
together with the reviewed mapping. Do not assign a release or fork commit to
unpublished work, and do not update a parent's dependency revision implicitly.
