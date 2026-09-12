#!/usr/bin/env python3
"""Audit and prepare upstream updates without changing the source checkout.

This tool deliberately uses only the Python standard library and Git.  It reads
the repository object database in offline mode.  With --fetch it creates a bare
cache below an explicitly supplied build directory and fetches into that cache.
It never updates the source repository's index, worktree, or refs.
"""

from __future__ import annotations

import argparse
import difflib
import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import re
import shutil
import subprocess
import sys
from typing import Any, Dict, Iterable, List, Mapping, Optional, Sequence, Tuple


SCHEMA_VERSION = 1
KINDS = {"direct", "modified", "template", "local", "removed"}
ID_RE = re.compile(r"^[a-z0-9][a-z0-9._-]*$")


class UpstreamError(RuntimeError):
    pass


def git_env() -> Dict[str, str]:
    env = os.environ.copy()
    env["GIT_OPTIONAL_LOCKS"] = "0"
    return env


def run_git(
    repository: Path,
    arguments: Sequence[str],
    *,
    check: bool = True,
) -> subprocess.CompletedProcess[bytes]:
    result = subprocess.run(
        ["git", "-C", str(repository), *arguments],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_env(),
        check=False,
    )
    if check and result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise UpstreamError(
            "git {} failed{}".format(
                " ".join(arguments), ": " + detail if detail else ""
            )
        )
    return result


def repository_root() -> Path:
    return Path(__file__).resolve().parents[1]


def manifest_path(root: Path) -> Path:
    return root / "upstream" / "files.json"


def load_manifest(root: Path) -> Dict[str, Any]:
    path = manifest_path(root)
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError as exc:
        raise UpstreamError("manifest does not exist: {}".format(path)) from exc
    except json.JSONDecodeError as exc:
        raise UpstreamError("invalid JSON in {}: {}".format(path, exc)) from exc
    if not isinstance(data, dict):
        raise UpstreamError("manifest root must be a JSON object")
    return data


def safe_relative_path(value: Any, field: str) -> Optional[str]:
    if value is None:
        return None
    if not isinstance(value, str) or not value:
        raise UpstreamError("{} must be a non-empty path string or null".format(field))
    if "\\" in value:
        raise UpstreamError("{} must use '/' separators: {}".format(field, value))
    raw_parts = value.split("/")
    if any(part in ("", ".", "..") for part in raw_parts):
        raise UpstreamError("{} has an unsafe path component: {}".format(field, value))
    windows_path = PureWindowsPath(value)
    if windows_path.drive or windows_path.root or ":" in value:
        raise UpstreamError("{} may not contain a Windows drive or stream: {}".format(field, value))
    path = PurePosixPath(value)
    if path.is_absolute():
        raise UpstreamError("{} is not a safe relative path: {}".format(field, value))
    return path.as_posix()


def local_source_path(root: Path, value: str, field: str = "local_path") -> Path:
    relative = safe_relative_path(value, field)
    assert relative is not None
    path = root / Path(*PurePosixPath(relative).parts)
    resolved = path.resolve(strict=False)
    if not resolved.is_relative_to(root.resolve()):
        raise UpstreamError("{} resolves outside the repository: {}".format(field, value))
    return path


def path_reported_by_git(root: Path, value: str) -> Path:
    """Resolve a Git-reported path from native Git or MSYS2 Git."""
    if re.match(r"^/[A-Za-z](?:/|$)", value):
        value = value[1].upper() + ":" + value[2:]
    path = Path(value)
    if not path.is_absolute():
        path = root / path
    return path.resolve(strict=False)


def git_metadata_directories(root: Path) -> List[Path]:
    directories: List[Path] = []
    for argument in ("--git-dir", "--git-common-dir"):
        value = run_git(root, ["rev-parse", argument]).stdout.decode(
            "utf-8", "surrogateescape"
        ).strip()
        path = path_reported_by_git(root, value)
        if path not in directories:
            directories.append(path)
    return directories


def resolve_commit(repository: Path, revision: str) -> str:
    result = run_git(
        repository,
        ["rev-parse", "--verify", revision + "^0"],
        check=False,
    )
    if result.returncode != 0:
        detail = result.stderr.decode("utf-8", "replace").strip()
        raise UpstreamError(
            "commit '{}' is unavailable offline{}; provide --fetch and a build/cache directory"
            .format(revision, ": " + detail if detail else "")
        )
    return result.stdout.decode("ascii").strip()


def tree(repository: Path, commit: str) -> Dict[str, Dict[str, str]]:
    output = run_git(repository, ["ls-tree", "-rz", "--full-tree", commit]).stdout
    result: Dict[str, Dict[str, str]] = {}
    for record in output.split(b"\0"):
        if not record:
            continue
        metadata, raw_path = record.split(b"\t", 1)
        mode, object_type, object_id = metadata.decode("ascii").split(" ")
        path = raw_path.decode("utf-8", "surrogateescape")
        result[path] = {"mode": mode, "type": object_type, "blob": object_id}
    return result


def blob(repository: Path, object_id: str) -> bytes:
    return run_git(repository, ["cat-file", "blob", object_id]).stdout


def normalize_crlf(content: bytes) -> bytes:
    """Ignore CRLF/LF differences while preserving all other bytes."""
    return content.replace(b"\r\n", b"\n")


def newline_style(content: bytes) -> bytes:
    without_crlf = content.replace(b"\r\n", b"")
    if b"\r\n" in content and b"\n" not in without_crlf:
        return b"\r\n"
    return b"\n"


def apply_newline_style(content: bytes, separator: bytes) -> bytes:
    normalized = normalize_crlf(content)
    if separator == b"\r\n":
        return normalized.replace(b"\n", b"\r\n")
    return normalized


def object_exists(repository: Path, object_id: str) -> bool:
    return run_git(
        repository, ["cat-file", "-e", object_id], check=False
    ).returncode == 0


def has_suffix(path: str, suffixes: Iterable[str]) -> bool:
    lower = path.lower()
    return any(lower.endswith(suffix.lower()) for suffix in suffixes)


def local_inventory(root: Path) -> List[str]:
    output = run_git(
        root, ["ls-files", "-z", "--cached", "--others", "--exclude-standard"]
    ).stdout
    paths = [
        item.decode("utf-8", "surrogateescape")
        for item in output.split(b"\0")
        if item
    ]
    return [
        path
        for path in paths
        if (root / Path(*PurePosixPath(path).parts)).is_file()
    ]


def validate_manifest(root: Path, data: Mapping[str, Any]) -> List[str]:
    errors: List[str] = []
    if data.get("schema_version") != SCHEMA_VERSION:
        errors.append("schema_version must be {}".format(SCHEMA_VERSION))
    upstream = data.get("upstream")
    if not isinstance(upstream, dict):
        errors.append("upstream must be an object")
        return errors
    baseline = upstream.get("baseline")
    url = upstream.get("url")
    if not isinstance(baseline, str) or not baseline:
        errors.append("upstream.baseline must be a commit")
        return errors
    if not isinstance(url, str) or not url:
        errors.append("upstream.url must be a fetch URL")

    try:
        baseline_commit = resolve_commit(root, baseline)
        baseline_tree = tree(root, baseline_commit)
    except UpstreamError as exc:
        errors.append(str(exc))
        return errors

    files = data.get("files")
    if not isinstance(files, list):
        errors.append("files must be an array")
        return errors

    seen_ids = set()
    seen_upstream = set()
    seen_local = set()
    for position, entry in enumerate(files):
        prefix = "files[{}]".format(position)
        if not isinstance(entry, dict):
            errors.append(prefix + " must be an object")
            continue
        missing = {
            "id", "upstream_path", "local_path", "baseline_blob", "kind", "notes"
        } - set(entry)
        if missing:
            errors.append(prefix + " is missing fields: " + ", ".join(sorted(missing)))
            continue
        stable_id = entry.get("id")
        if not isinstance(stable_id, str) or not ID_RE.fullmatch(stable_id):
            errors.append(prefix + ".id is not stable-id syntax")
        elif stable_id in seen_ids:
            errors.append("duplicate id: " + stable_id)
        else:
            seen_ids.add(stable_id)
        kind = entry.get("kind")
        if kind not in KINDS:
            errors.append(prefix + ".kind must be one of " + ", ".join(sorted(KINDS)))
            continue
        if not isinstance(entry.get("notes"), str):
            errors.append(prefix + ".notes must be a string")
        try:
            upstream_path = safe_relative_path(entry.get("upstream_path"), prefix + ".upstream_path")
            local_path = safe_relative_path(entry.get("local_path"), prefix + ".local_path")
        except UpstreamError as exc:
            errors.append(str(exc))
            continue
        baseline_blob = entry.get("baseline_blob")
        if kind == "local":
            if upstream_path is not None or baseline_blob is not None or local_path is None:
                errors.append(prefix + ": local entries require only local_path")
        else:
            if upstream_path is None or not isinstance(baseline_blob, str):
                errors.append(prefix + ": upstream entries require path and baseline_blob")
                continue
            if upstream_path in seen_upstream:
                errors.append("duplicate upstream_path: " + upstream_path)
            seen_upstream.add(upstream_path)
            item = baseline_tree.get(upstream_path)
            if item is None or item["type"] != "blob":
                errors.append("baseline path is not a blob: " + upstream_path)
            elif item["blob"] != baseline_blob:
                errors.append(
                    "baseline_blob mismatch for {}: manifest {}, Git {}"
                    .format(upstream_path, baseline_blob, item["blob"])
                )
        if local_path is not None:
            if local_path in seen_local:
                errors.append("duplicate local_path: " + local_path)
            seen_local.add(local_path)
            try:
                local_file = local_source_path(root, local_path, prefix + ".local_path")
            except UpstreamError as exc:
                errors.append(str(exc))
                continue
            if kind == "removed":
                if local_file.exists():
                    errors.append("removed entry still exists: " + local_path)
            elif not local_file.is_file():
                errors.append("local file is missing: " + local_path)
            elif kind in ("direct", "modified") and isinstance(baseline_blob, str):
                if not object_exists(root, baseline_blob):
                    errors.append("baseline blob is unavailable: " + baseline_blob)
                else:
                    equal = normalize_crlf(local_file.read_bytes()) == normalize_crlf(
                        blob(root, baseline_blob)
                    )
                    if kind == "direct" and not equal:
                        errors.append("direct entry has local changes: " + local_path)
                    if kind == "modified" and equal:
                        errors.append("modified entry is byte-equivalent to baseline: " + local_path)
        elif kind not in ("removed",):
            errors.append(prefix + ": local_path may be null only for removed entries")

    coverage = data.get("coverage")
    if not isinstance(coverage, dict):
        errors.append("coverage must be an object")
    else:
        extensions = coverage.get("extensions", [])
        required_paths = coverage.get("required_paths", [])
        if not isinstance(extensions, list) or not all(
            isinstance(value, str) and value.startswith(".") for value in extensions
        ):
            errors.append("coverage.extensions must contain suffix strings")
            extensions = []
        if not isinstance(required_paths, list) or not all(
            isinstance(value, str) for value in required_paths
        ):
            errors.append("coverage.required_paths must contain paths")
            required_paths = []
        wanted = {
            path
            for path, item in baseline_tree.items()
            if item["type"] == "blob" and has_suffix(path, extensions)
        }
        wanted.update(required_paths)
        missing_coverage = sorted(wanted - seen_upstream)
        extra_required = sorted(path for path in required_paths if path not in baseline_tree)
        if missing_coverage:
            errors.append("uncovered upstream paths: " + ", ".join(missing_coverage))
        if extra_required:
            errors.append("coverage.required_paths absent at baseline: " + ", ".join(extra_required))
        local_extensions = coverage.get("local_extensions", extensions)
        exclude_prefixes = coverage.get("local_exclude_prefixes", [])
        template_suffixes = coverage.get("template_suffixes", [])
        if not isinstance(local_extensions, list) or not all(
            isinstance(value, str) and value.startswith(".") for value in local_extensions
        ):
            errors.append("coverage.local_extensions must contain suffix strings")
            local_extensions = []
        if not isinstance(template_suffixes, list) or not all(
            isinstance(value, str) and value.startswith(".") for value in template_suffixes
        ):
            errors.append("coverage.template_suffixes must contain suffix strings")
            template_suffixes = []
        if not isinstance(exclude_prefixes, list) or not all(
            isinstance(value, str) and value.endswith("/") for value in exclude_prefixes
        ):
            errors.append("coverage.local_exclude_prefixes must contain '/'-terminated prefixes")
            exclude_prefixes = []
        if isinstance(local_extensions, list):
            wanted_local = {
                path
                for path in local_inventory(root)
                if has_suffix(path, local_extensions)
                and not any(path.startswith(prefix) for prefix in exclude_prefixes)
            }
            missing_local = sorted(wanted_local - seen_local)
            if missing_local:
                errors.append("uncovered local paths: " + ", ".join(missing_local))

    relationships = data.get("generated_headers")
    if not isinstance(relationships, list):
        errors.append("generated_headers must be an array")
    else:
        for position, relation in enumerate(relationships):
            prefix = "generated_headers[{}]".format(position)
            if not isinstance(relation, dict):
                errors.append(prefix + " must be an object")
                continue
            try:
                template = safe_relative_path(relation.get("template"), prefix + ".template")
                output = safe_relative_path(relation.get("output"), prefix + ".output")
            except UpstreamError as exc:
                errors.append(str(exc))
                continue
            if template is not None and template not in seen_local:
                errors.append(prefix + ".template is not a manifested local path")
            if output is None:
                errors.append(prefix + ".output is required")
            if not isinstance(relation.get("generator"), str) or not relation.get("generator"):
                errors.append(prefix + ".generator is required")
    return errors


def compare_trees(
    repository: Path, baseline: str, target: str
) -> Tuple[List[Dict[str, Any]], List[Dict[str, Any]]]:
    old_tree = tree(repository, baseline)
    new_tree = tree(repository, target)
    changes: List[Dict[str, Any]] = []
    deleted: Dict[str, str] = {}
    added: Dict[str, str] = {}
    for path in sorted(set(old_tree) | set(new_tree)):
        old = old_tree.get(path)
        new = new_tree.get(path)
        if old is None:
            change = "added"
            added[path] = new["blob"]
        elif new is None:
            change = "deleted"
            deleted[path] = old["blob"]
        elif old != new:
            change = "modified"
        else:
            continue
        changes.append(
            {
                "path": path,
                "change": change,
                "old_blob": old["blob"] if old else None,
                "new_blob": new["blob"] if new else None,
                "old_mode": old["mode"] if old else None,
                "new_mode": new["mode"] if new else None,
                "old_type": old["type"] if old else None,
                "new_type": new["type"] if new else None,
            }
        )

    by_deleted_blob: Dict[str, List[str]] = {}
    by_added_blob: Dict[str, List[str]] = {}
    for path, object_id in deleted.items():
        by_deleted_blob.setdefault(object_id, []).append(path)
    for path, object_id in added.items():
        by_added_blob.setdefault(object_id, []).append(path)
    renames: List[Dict[str, Any]] = []
    for object_id in sorted(set(by_deleted_blob) & set(by_added_blob)):
        sources = sorted(by_deleted_blob[object_id])
        destinations = sorted(by_added_blob[object_id])
        ambiguous = len(sources) != 1 or len(destinations) != 1
        for source in sources:
            for destination in destinations:
                renames.append(
                    {
                        "from": source,
                        "to": destination,
                        "similarity": 100,
                        "ambiguous": ambiguous,
                        "reason": "identical Git blob",
                    }
                )
    return changes, renames


def assert_fresh_output(root: Path, requested: Path, local_paths: Iterable[str]) -> Path:
    if not requested.is_absolute():
        requested = Path.cwd() / requested
    lexical = Path(os.path.abspath(str(requested)))
    if lexical.exists() or lexical.is_symlink():
        raise UpstreamError("output path must not already exist: {}".format(lexical))
    root_resolved = root.resolve()
    output_resolved = lexical.resolve(strict=False)
    if output_resolved == root_resolved or root_resolved.is_relative_to(output_resolved):
        raise UpstreamError("output path may not be the repository or its parent")
    for metadata in git_metadata_directories(root):
        if (
            output_resolved == metadata
            or output_resolved.is_relative_to(metadata)
            or metadata.is_relative_to(output_resolved)
        ):
            raise UpstreamError(
                "output path may not contain or enter Git metadata: {}".format(metadata)
            )
    ancestor = lexical.parent
    while ancestor != ancestor.parent:
        if ancestor.is_symlink():
            raise UpstreamError("output path crosses a symlink: {}".format(ancestor))
        if ancestor == root_resolved.parent:
            break
        ancestor = ancestor.parent
    for value in local_paths:
        local = local_source_path(root, value).resolve(strict=False)
        if local == output_resolved or local.is_relative_to(output_resolved):
            raise UpstreamError("output path would contain a source path: {}".format(local))
    lexical.mkdir(parents=True)
    return lexical


def cache_repository(build_directory: Path, url: str, baseline: str, target: str) -> Tuple[Path, str, str]:
    cache = build_directory / ".upstream-cache.git"
    run_git(build_directory, ["init", "--bare", str(cache)])
    local_remote = Path(url)
    if local_remote.is_absolute():
        resolved_remote = local_remote.resolve()
        if resolved_remote.drive and len(resolved_remote.drive) == 2:
            # This spelling is understood by Git for Windows and MSYS2 Git.
            fetch_url = "file:///{}{}".format(
                resolved_remote.drive[0].lower(),
                resolved_remote.as_posix()[2:],
            )
        else:
            fetch_url = resolved_remote.as_uri()
    else:
        fetch_url = url
    baseline_fetch = run_git(cache, ["fetch", "--no-tags", "--force", fetch_url, baseline])
    del baseline_fetch
    baseline_commit = resolve_commit(cache, "FETCH_HEAD")
    run_git(cache, ["fetch", "--no-tags", "--force", fetch_url, target])
    target_commit = resolve_commit(cache, "FETCH_HEAD")
    return cache, baseline_commit, target_commit


def comparison(
    root: Path,
    data: Mapping[str, Any],
    target: str,
    *,
    fetch: bool,
    build_directory: Optional[Path],
) -> Tuple[Path, str, str, Dict[str, Any]]:
    upstream = data["upstream"]
    if fetch:
        if build_directory is None:
            raise UpstreamError("--fetch requires --cache-dir for compare")
        repository, baseline_commit, target_commit = cache_repository(
            build_directory, upstream["url"], upstream["baseline"], target
        )
    else:
        repository = root
        baseline_commit = resolve_commit(repository, upstream["baseline"])
        target_commit = resolve_commit(repository, target)
    changes, renames = compare_trees(repository, baseline_commit, target_commit)
    result = {
        "schema_version": SCHEMA_VERSION,
        "repository": data.get("repository"),
        "baseline": baseline_commit,
        "target": target_commit,
        "offline": not fetch,
        "summary": {
            "added": sum(change["change"] == "added" for change in changes),
            "deleted": sum(change["change"] == "deleted" for change in changes),
            "modified": sum(change["change"] == "modified" for change in changes),
            "rename_candidates": len(renames),
            "ambiguous_rename_candidates": sum(item["ambiguous"] for item in renames),
        },
        "changes": changes,
        "rename_candidates": renames,
    }
    return repository, baseline_commit, target_commit, result


def candidate_id(entry: Optional[Mapping[str, Any]], upstream_path: str) -> str:
    if entry is not None:
        return str(entry["id"])
    digest = hashlib.sha256(upstream_path.encode("utf-8", "surrogateescape")).hexdigest()[:12]
    return "unmapped-" + digest


def allocate_candidate_ids(
    data: Mapping[str, Any], changes: Iterable[Mapping[str, Any]]
) -> Dict[str, str]:
    entries = {
        entry["upstream_path"]: entry
        for entry in data["files"]
        if entry.get("upstream_path") is not None
    }
    used = {entry["id"] for entry in data["files"]}
    allocated: Dict[str, str] = {}
    for change in changes:
        upstream_path = change["path"]
        entry = entries.get(upstream_path)
        if entry is not None:
            allocated[upstream_path] = str(entry["id"])
            continue
        base = candidate_id(None, upstream_path)
        stable_id = base
        suffix = 2
        while stable_id in used:
            stable_id = "{}-{}".format(base, suffix)
            suffix += 1
        allocated[upstream_path] = stable_id
        used.add(stable_id)
    return allocated


def prepared_manifest(
    root: Path,
    data: Mapping[str, Any],
    repository: Path,
    target_commit: str,
    proposed_sources: Mapping[str, bytes],
    candidate_ids_by_path: Mapping[str, str],
) -> Dict[str, Any]:
    """Build the full advisory manifest expected after reviewed candidates apply."""
    candidate = json.loads(json.dumps(data))
    candidate["upstream"]["baseline"] = target_commit
    target_tree = tree(repository, target_commit)
    retained: List[Dict[str, Any]] = []
    upstream_paths = set()
    stable_ids = set()

    for original in candidate["files"]:
        entry = dict(original)
        stable_ids.add(entry["id"])
        upstream_path = entry.get("upstream_path")
        if upstream_path is None:
            retained.append(entry)
            continue
        target_item = target_tree.get(upstream_path)
        if target_item is None or target_item["type"] != "blob":
            local_path = entry.get("local_path")
            if local_path and local_source_path(root, local_path).is_file():
                entry["upstream_path"] = None
                entry["baseline_blob"] = None
                entry["kind"] = "local"
                entry["notes"] = (
                    entry["notes"]
                    + " Upstream path is absent at the candidate baseline; retained locally pending review."
                )
                retained.append(entry)
            continue

        entry["baseline_blob"] = target_item["blob"]
        upstream_paths.add(upstream_path)
        if entry["kind"] in ("direct", "modified"):
            local_path = entry["local_path"]
            source = proposed_sources.get(upstream_path)
            if source is None:
                local_file = local_source_path(root, local_path)
                if local_file.is_file():
                    source = local_file.read_bytes()
            if source is not None:
                upstream_content = blob(repository, target_item["blob"])
                entry["kind"] = (
                    "direct"
                    if normalize_crlf(source) == normalize_crlf(upstream_content)
                    else "modified"
                )
        retained.append(entry)

    coverage = candidate.get("coverage", {})
    extensions = coverage.get("extensions", [])
    required_path_list = [
        path for path in coverage.get("required_paths", []) if path in target_tree
    ]
    coverage["required_paths"] = required_path_list
    required_paths = set(required_path_list)
    for upstream_path, target_item in sorted(target_tree.items()):
        if upstream_path in upstream_paths or target_item["type"] != "blob":
            continue
        if upstream_path not in required_paths and not has_suffix(upstream_path, extensions):
            continue
        stable_id = candidate_ids_by_path.get(
            upstream_path, candidate_id(None, upstream_path)
        )
        base = stable_id
        suffix = 2
        while stable_id in stable_ids:
            stable_id = "{}-{}".format(base, suffix)
            suffix += 1
        retained.append(
            {
                "id": stable_id,
                "upstream_path": upstream_path,
                "local_path": None,
                "baseline_blob": target_item["blob"],
                "kind": "removed",
                "notes": (
                    "Advisory placeholder for an upstream addition; choose a local mapping "
                    "or confirm an intentional tombstone during manual review."
                ),
            }
        )
        stable_ids.add(stable_id)
        upstream_paths.add(upstream_path)

    candidate["files"] = sorted(retained, key=lambda item: item["id"])
    return candidate


def write_new(path: Path, content: bytes) -> None:
    if path.exists() or path.is_symlink():
        raise UpstreamError("refusing to overwrite candidate artifact: {}".format(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(content)


def unified_patch(old: bytes, new: bytes, old_name: str, new_name: str) -> bytes:
    if old == new:
        return b""
    def lf_lines(content: bytes) -> List[str]:
        text = normalize_crlf(content).decode("utf-8", "surrogateescape")
        if not text:
            return []
        parts = text.split("\n")
        lines = [part + "\n" for part in parts[:-1]]
        if parts[-1]:
            lines.append(parts[-1])
        return lines

    lines = difflib.unified_diff(
        lf_lines(old),
        lf_lines(new),
        fromfile=old_name,
        tofile=new_name,
        lineterm="\n",
    )
    rendered: List[str] = []
    for line in lines:
        rendered.append(line if line.endswith("\n") else line + "\n")
        if line[:1] in (" ", "+", "-") and not line.endswith("\n"):
            rendered.append("\\ No newline at end of file\n")
    return "".join(rendered).encode("utf-8", "surrogateescape")


def merge_candidate(current: bytes, base: bytes, incoming: bytes, directory: Path) -> Tuple[bytes, bool]:
    normalized_current = normalize_crlf(current)
    write_new(directory / "current", normalized_current)
    write_new(directory / "baseline", normalize_crlf(base))
    write_new(directory / "upstream", normalize_crlf(incoming))
    result = subprocess.run(
        [
            "git", "merge-file", "--stdout", "--diff3",
            "-L", "current", "-L", "baseline", "-L", "upstream",
            str(directory / "current"), str(directory / "baseline"), str(directory / "upstream"),
        ],
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        env=git_env(),
        check=False,
    )
    # merge-file returns the conflict count (capped at 127), not just 1.
    if result.returncode < 0 or result.returncode > 127:
        raise UpstreamError(
            "git merge-file failed: " + result.stderr.decode("utf-8", "replace").strip()
        )
    proposed = apply_newline_style(result.stdout, newline_style(current))
    write_new(directory / "proposed", proposed)
    return proposed, result.returncode > 0


def render_report_markdown(report: Mapping[str, Any]) -> str:
    lines = [
        "# Upstream preparation report",
        "",
        "- Repository: `{}`".format(report.get("repository")),
        "- Baseline: `{}`".format(report["baseline"]),
        "- Target: `{}`".format(report["target"]),
        "- Source checkout modified: no",
        "- Manifest candidate: `{}`".format(report["manifest_candidate"]["path"]),
        "- Manifest candidate ready for acceptance: {}".format(
            "yes" if report["manifest_candidate"]["acceptance_ready"] else "no"
        ),
        "",
        "| Upstream path | Local path | Change | Disposition | Manual review |",
        "| --- | --- | --- | --- | --- |",
    ]
    for item in report["items"]:
        lines.append(
            "| `{}` | {} | {} | {} | {} |".format(
                item["upstream_path"].replace("|", "\\|"),
                "`{}`".format(item["local_path"].replace("|", "\\|"))
                if item["local_path"] else "—",
                item["change"], item["disposition"],
                "yes" if item["review_required"] else "no",
            )
        )
    lines.extend(
        [
            "",
            "Candidate files are advisory. Review them and apply changes manually; this tool never writes source files or changes the recorded baseline.",
            "",
        ]
    )
    return "\n".join(lines)


def prepare(root: Path, data: Mapping[str, Any], target: str, output: Path, fetch: bool) -> Dict[str, Any]:
    local_paths = [
        entry["local_path"] for entry in data["files"] if entry.get("local_path")
    ]
    build_directory = assert_fresh_output(root, output, local_paths)
    repository, baseline_commit, target_commit, compared = comparison(
        root, data, target, fetch=fetch, build_directory=build_directory
    )
    entries = {
        entry["upstream_path"]: entry
        for entry in data["files"]
        if entry.get("upstream_path") is not None
    }
    candidate_ids_by_path = allocate_candidate_ids(data, compared["changes"])
    rename_by_source: Dict[str, List[Mapping[str, Any]]] = {}
    rename_by_destination: Dict[str, List[Mapping[str, Any]]] = {}
    for rename in compared["rename_candidates"]:
        rename_by_source.setdefault(rename["from"], []).append(rename)
        rename_by_destination.setdefault(rename["to"], []).append(rename)

    items: List[Dict[str, Any]] = []
    mapping: List[Dict[str, Any]] = []
    patches: List[bytes] = []
    proposed_sources: Dict[str, bytes] = {}
    for number, change in enumerate(compared["changes"], 1):
        upstream_path = change["path"]
        entry = entries.get(upstream_path)
        stable_id = candidate_ids_by_path[upstream_path]
        directory_name = "{:03d}-{}".format(number, stable_id)
        directory = build_directory / "candidates" / directory_name
        local_path = entry.get("local_path") if entry else None
        kind = entry.get("kind") if entry else None
        disposition = "manual_review"
        reason = "upstream path is not mapped"
        patch = b""
        artifact_files: List[str] = []
        current_present = False

        if (
            change["change"] == "modified"
            and change["old_type"] == "blob"
            and change["new_type"] == "blob"
            and change["old_mode"] == change["new_mode"]
            and entry
            and kind in ("direct", "modified")
        ):
            local_file = local_source_path(root, local_path)
            if not local_file.is_file():
                reason = "mapped local file is missing"
            else:
                current = local_file.read_bytes()
                base = blob(repository, change["old_blob"])
                incoming = blob(repository, change["new_blob"])
                proposed, conflicted = merge_candidate(current, base, incoming, directory)
                artifact_files = ["current", "baseline", "upstream", "proposed"]
                patch = unified_patch(current, proposed, "a/" + local_path, "b/" + local_path)
                if conflicted:
                    reason = "three-way merge contains conflicts"
                else:
                    disposition = "clean_merge"
                    reason = "three-way merge completed without conflicts"
                    proposed_sources[upstream_path] = proposed
        else:
            directory.mkdir(parents=True, exist_ok=False)
            old_content = (
                blob(repository, change["old_blob"])
                if change["old_blob"] and change["old_type"] == "blob"
                else b""
            )
            new_content = (
                blob(repository, change["new_blob"])
                if change["new_blob"] and change["new_type"] == "blob"
                else b""
            )
            if change["old_blob"]:
                write_new(directory / "baseline", normalize_crlf(old_content))
                artifact_files.append("baseline")
            if local_path:
                local_file = local_source_path(root, local_path)
                if local_file.is_file():
                    current = local_file.read_bytes()
                    current_present = True
                    write_new(directory / "current", normalize_crlf(current))
                    artifact_files.append("current")
                else:
                    current = b""
            else:
                current = b""
            if change["new_blob"]:
                write_new(directory / "upstream", normalize_crlf(new_content))
                artifact_files.append("upstream")
            patch_path = local_path or upstream_path
            if change["change"] == "added":
                patch = unified_patch(b"", new_content, "/dev/null", "b/" + patch_path)
                candidates = rename_by_destination.get(upstream_path, [])
                reason = "possible rename requires review" if candidates else "upstream addition requires review"
            elif change["change"] == "deleted":
                patch = unified_patch(
                    current if current_present else old_content,
                    b"",
                    "a/" + patch_path,
                    "/dev/null",
                )
                candidates = rename_by_source.get(upstream_path, [])
                reason = "possible rename requires review" if candidates else "upstream deletion requires review"
            elif kind == "template":
                patch = unified_patch(old_content, new_content, "a/" + local_path, "b/" + local_path)
                reason = "template mappings require manual placeholder review"
            elif kind == "removed":
                patch = unified_patch(old_content, new_content, "a/" + upstream_path, "b/" + upstream_path)
                reason = "tombstoned paths require manual review"
            elif entry:
                patch = unified_patch(
                    current if current_present else old_content,
                    new_content,
                    "a/" + patch_path,
                    "b/" + patch_path,
                )
                reason = "mapped change cannot be merged automatically"
            else:
                patch = unified_patch(
                    old_content,
                    new_content,
                    "a/" + upstream_path,
                    "b/" + upstream_path,
                )
                reason = "unmapped upstream modification requires review"
            if change["old_type"] not in (None, "blob") or change["new_type"] not in (None, "blob"):
                patch = b""
                reason = "non-blob Git entries require manual review"
            elif (
                change["change"] == "modified"
                and change["old_mode"] != change["new_mode"]
            ):
                reason = "Git mode change requires manual review"
            elif b"\0" in old_content or b"\0" in new_content:
                patch = b""
                reason += "; binary content is provided as raw candidate artifacts"

        patch_name: Optional[str] = None
        if patch:
            patch_name = "candidate.patch"
            write_new(directory / patch_name, patch)
            artifact_files.append(patch_name)
            patches.append(patch)
            if not patch.endswith(b"\n"):
                patches.append(b"\n")
        item = {
            "stable_id": stable_id,
            "upstream_path": upstream_path,
            "local_path": local_path,
            "kind": kind,
            "change": change["change"],
            "old_mode": change["old_mode"],
            "new_mode": change["new_mode"],
            "disposition": disposition,
            "review_required": disposition != "clean_merge",
            "reason": reason,
            "candidate_directory": "candidates/" + directory_name,
            "patch": patch_name,
            "rename_candidates": rename_by_source.get(upstream_path, [])
            + rename_by_destination.get(upstream_path, []),
        }
        items.append(item)
        mapping.append(
            {
                "stable_id": stable_id,
                "upstream_path": upstream_path,
                "local_path": local_path,
                "candidate_directory": item["candidate_directory"],
                "artifacts": artifact_files,
            }
        )

    manual_review_stable_ids = sorted(
        {item["stable_id"] for item in items if item["review_required"]}
    )
    report: Dict[str, Any] = {
        "schema_version": SCHEMA_VERSION,
        "repository": data.get("repository"),
        "baseline": baseline_commit,
        "target": target_commit,
        "offline": not fetch,
        "source_checkout_modified": False,
        "summary": {
            **compared["summary"],
            "clean_merges": sum(item["disposition"] == "clean_merge" for item in items),
            "manual_reviews": sum(item["review_required"] for item in items),
        },
        "rename_candidates": compared["rename_candidates"],
        "manifest_candidate": {
            "path": "manifest-candidate.json",
            "target_baseline": target_commit,
            "acceptance_ready": not manual_review_stable_ids,
            "review_required": bool(manual_review_stable_ids),
            "manual_review_stable_ids": manual_review_stable_ids,
        },
        "items": items,
    }
    manifest_candidate = prepared_manifest(
        root,
        data,
        repository,
        target_commit,
        proposed_sources,
        candidate_ids_by_path,
    )
    write_new(
        build_directory / "manifest-candidate.json",
        (json.dumps(manifest_candidate, indent=2, sort_keys=True) + "\n").encode(),
    )
    write_new(
        build_directory / "candidate-map.json",
        (json.dumps({"schema_version": SCHEMA_VERSION, "candidates": mapping}, indent=2, sort_keys=True) + "\n").encode(),
    )
    write_new(build_directory / "candidates.patch", b"".join(patches))
    write_new(
        build_directory / "report.json",
        (json.dumps(report, indent=2, sort_keys=True) + "\n").encode(),
    )
    write_new(build_directory / "report.md", render_report_markdown(report).encode())
    return report


def refreshed_manifest(root: Path, data: Mapping[str, Any]) -> Dict[str, Any]:
    refreshed = json.loads(json.dumps(data))
    baseline = resolve_commit(root, refreshed["upstream"]["baseline"])
    baseline_tree = tree(root, baseline)
    for entry in refreshed["files"]:
        upstream_path = entry.get("upstream_path")
        if upstream_path:
            item = baseline_tree.get(upstream_path)
            if item is None or item["type"] != "blob":
                raise UpstreamError("cannot refresh missing baseline path: " + upstream_path)
            entry["baseline_blob"] = item["blob"]
        if entry.get("kind") in ("direct", "modified"):
            local_path = entry["local_path"]
            local_file = local_source_path(root, local_path)
            if not local_file.is_file():
                raise UpstreamError("cannot refresh missing local file: " + local_path)
            same = normalize_crlf(local_file.read_bytes()) == normalize_crlf(
                blob(root, entry["baseline_blob"])
            )
            entry["kind"] = "direct" if same else "modified"
    coverage = refreshed.get("coverage", {})
    local_extensions = coverage.get("local_extensions", coverage.get("extensions", []))
    exclude_prefixes = coverage.get("local_exclude_prefixes", [])
    existing_local = {
        entry["local_path"] for entry in refreshed["files"] if entry.get("local_path")
    }
    existing_ids = {entry["id"] for entry in refreshed["files"]}
    repository_slug = re.sub(
        r"[^a-z0-9]+", "-", str(refreshed.get("repository", "repository")).lower()
    ).strip("-")
    for local_path in sorted(local_inventory(root)):
        if local_path in existing_local or not has_suffix(local_path, local_extensions):
            continue
        if any(local_path.startswith(prefix) for prefix in exclude_prefixes):
            continue
        slug = re.sub(r"[^a-z0-9]+", "-", local_path.lower()).strip("-")
        stable_id = "{}-local-{}".format(repository_slug, slug)
        if stable_id in existing_ids:
            stable_id += "-" + hashlib.sha256(local_path.encode()).hexdigest()[:8]
        if local_path.lower().endswith(".h.in"):
            notes = "Local generated-header template with no baseline upstream counterpart."
        elif local_path.lower().endswith(".cmake.in"):
            notes = "Local CMake template with no baseline upstream counterpart."
        else:
            notes = "Local source with no baseline upstream counterpart."
        refreshed["files"].append(
            {
                "id": stable_id,
                "upstream_path": None,
                "local_path": local_path,
                "baseline_blob": None,
                "kind": "local",
                "notes": notes,
            }
        )
        existing_ids.add(stable_id)
        existing_local.add(local_path)
    refreshed["files"] = sorted(refreshed["files"], key=lambda item: item["id"])
    return refreshed


def parse_arguments(arguments: Optional[Sequence[str]] = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("check", help="validate the manifest and local mappings")
    compare_parser = commands.add_parser("compare", help="compare the baseline with an upstream commit")
    compare_parser.add_argument("--to", required=True, metavar="COMMIT")
    compare_parser.add_argument("--fetch", action="store_true")
    compare_parser.add_argument("--cache-dir", type=Path)
    prepare_parser = commands.add_parser("prepare", help="write advisory merge candidates to a fresh build directory")
    prepare_parser.add_argument("--to", required=True, metavar="COMMIT")
    prepare_parser.add_argument("--output", required=True, type=Path)
    prepare_parser.add_argument("--fetch", action="store_true")
    refresh_parser = commands.add_parser("refresh", help="write a refreshed manifest copy without changing the source manifest")
    refresh_parser.add_argument("--output", required=True, type=Path)
    return parser.parse_args(arguments)


def main(arguments: Optional[Sequence[str]] = None) -> int:
    args = parse_arguments(arguments)
    root = repository_root()
    try:
        data = load_manifest(root)
        if args.command == "check":
            errors = validate_manifest(root, data)
            print(json.dumps({"valid": not errors, "errors": errors}, indent=2, sort_keys=True))
            return 0 if not errors else 1
        if args.command == "compare":
            build_directory = None
            if args.fetch:
                if args.cache_dir is None:
                    raise UpstreamError("--fetch requires --cache-dir")
                build_directory = assert_fresh_output(root, args.cache_dir, [])
            elif args.cache_dir is not None:
                raise UpstreamError("--cache-dir is only used with --fetch")
            _, _, _, result = comparison(
                root, data, args.to, fetch=args.fetch, build_directory=build_directory
            )
            print(json.dumps(result, indent=2, sort_keys=True))
            return 0
        if args.command == "prepare":
            result = prepare(root, data, args.to, args.output, args.fetch)
            print(json.dumps(result["summary"], indent=2, sort_keys=True))
            return 0
        if args.command == "refresh":
            local_paths = [
                entry["local_path"] for entry in data["files"] if entry.get("local_path")
            ]
            build_directory = assert_fresh_output(root, args.output, local_paths)
            output = build_directory / "files.json"
            with output.open("w", encoding="utf-8", newline="\n") as stream:
                stream.write(
                    json.dumps(refreshed_manifest(root, data), indent=2, sort_keys=True)
                    + "\n"
                )
            print(str(output))
            return 0
        raise UpstreamError("unknown command")
    except (KeyError, OSError, TypeError, UpstreamError, ValueError) as exc:
        print("error: {}".format(exc), file=sys.stderr)
        return 2


if __name__ == "__main__":
    sys.exit(main())
