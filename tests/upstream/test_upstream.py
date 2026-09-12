from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest


TOOL = Path(__file__).resolve().parents[2] / "tools" / "upstream.py"


def run(command, cwd, check=True):
    result = subprocess.run(
        [str(part) for part in command],
        cwd=str(cwd),
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
        check=False,
    )
    if check and result.returncode != 0:
        raise AssertionError(
            "command failed ({}): {}\nstdout:\n{}\nstderr:\n{}".format(
                result.returncode, command, result.stdout, result.stderr
            )
        )
    return result


def git(repository, *arguments, check=True):
    return run(["git", "-C", repository, *arguments], repository, check=check)


def write_tree(root, files):
    existing = [path for path in root.rglob("*") if path.is_file() and ".git" not in path.parts]
    for path in existing:
        path.unlink()
    for relative, content in files.items():
        path = root / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(content)


def ref_snapshot(repository):
    git_dir_text = git(repository, "rev-parse", "--git-dir").stdout.strip()
    git_dir = Path(git_dir_text)
    if not git_dir.is_absolute():
        git_dir = repository / git_dir
    refs = {}
    refs_root = git_dir / "refs"
    if refs_root.exists():
        for path in refs_root.rglob("*"):
            if path.is_file():
                refs[path.relative_to(git_dir).as_posix()] = path.read_bytes()
    packed = git_dir / "packed-refs"
    if packed.exists():
        refs["packed-refs"] = packed.read_bytes()
    return refs


class Fixture:
    def __init__(
        self,
        owner,
        base_files,
        target_files,
        clone_before_target=False,
        target_executable=None,
    ):
        self.owner = owner
        self.temp = Path(owner.temp.name)
        self.remote = self.temp / "remote"
        self.source = self.temp / "source"
        self.build_root = self.temp / "build"
        self.remote.mkdir()
        git(self.remote, "init")
        git(self.remote, "config", "user.email", "upstream@example.invalid")
        git(self.remote, "config", "user.name", "Upstream Test")
        write_tree(self.remote, base_files)
        git(self.remote, "add", "-A")
        git(self.remote, "commit", "-m", "baseline")
        self.baseline = git(self.remote, "rev-parse", "HEAD").stdout.strip()
        self.base_blobs = self._tree_blobs(self.remote, self.baseline)
        if clone_before_target:
            run(["git", "clone", "--no-hardlinks", self.remote, self.source], self.temp)
            git(self.source, "checkout", "--detach", self.baseline)
        write_tree(self.remote, target_files)
        git(self.remote, "add", "-A")
        for path in target_executable or []:
            git(self.remote, "update-index", "--chmod=+x", path)
        git(self.remote, "commit", "-m", "target")
        self.target = git(self.remote, "rev-parse", "HEAD").stdout.strip()
        if not clone_before_target:
            run(["git", "clone", "--no-hardlinks", self.remote, self.source], self.temp)
            git(self.source, "checkout", "--detach", self.baseline)
        (self.source / "tools").mkdir()
        shutil.copyfile(TOOL, self.source / "tools" / "upstream.py")
        (self.source / "upstream").mkdir()

    @staticmethod
    def _tree_blobs(repository, commit):
        result = {}
        for line in git(repository, "ls-tree", "-r", commit).stdout.splitlines():
            metadata, path = line.split("\t", 1)
            result[path] = metadata.split()[2]
        return result

    def manifest(
        self,
        entries,
        extensions=None,
        required_paths=None,
        generated=None,
        coverage_updates=None,
    ):
        data = {
            "schema_version": 1,
            "repository": "fixture",
            "upstream": {"url": str(self.remote), "baseline": self.baseline},
            "coverage": {
                "extensions": extensions if extensions is not None else [".c", ".h", ".in"],
                "required_paths": required_paths or [],
            },
            "generated_headers": generated or [],
            "files": [],
        }
        if coverage_updates:
            data["coverage"].update(coverage_updates)
        for entry in entries:
            item = dict(entry)
            item.setdefault("baseline_blob", self.base_blobs.get(item.get("upstream_path")))
            item.setdefault("notes", "fixture mapping")
            data["files"].append(item)
        (self.source / "upstream" / "files.json").write_text(
            json.dumps(data, indent=2) + "\n", encoding="utf-8"
        )

    def invoke(self, *arguments, check=True):
        return run(
            [sys.executable, "-B", self.source / "tools" / "upstream.py", *arguments],
            self.source,
            check=check,
        )


class UpstreamToolTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="upstream-tool-")

    def tearDown(self):
        self.temp.cleanup()

    def test_check_compare_and_prepare_leave_index_and_refs_unchanged(self):
        fixture = Fixture(self, {"tracked.c": b"one\ntwo\n"}, {"tracked.c": b"one\nTWO\n"})
        fixture.manifest(
            [{"id": "tracked-c", "upstream_path": "tracked.c", "local_path": "tracked.c", "kind": "direct"}]
        )
        index = fixture.source / ".git" / "index"
        before_index = hashlib.sha256(index.read_bytes()).hexdigest()
        before_refs = ref_snapshot(fixture.source)
        before_status = git(fixture.source, "status", "--porcelain=v1", "--untracked-files=all").stdout

        self.assertEqual(fixture.invoke("check").returncode, 0)
        comparison = json.loads(fixture.invoke("compare", "--to", fixture.target).stdout)
        self.assertEqual(comparison["summary"]["modified"], 1)
        output = fixture.build_root / "prepared"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)

        self.assertEqual(hashlib.sha256(index.read_bytes()).hexdigest(), before_index)
        self.assertEqual(ref_snapshot(fixture.source), before_refs)
        self.assertEqual(
            git(fixture.source, "status", "--porcelain=v1", "--untracked-files=all").stdout,
            before_status,
        )
        self.assertTrue((output / "report.json").is_file())

    def test_prepare_writes_a_clean_three_way_merge(self):
        fixture = Fixture(
            self,
            {"merge.c": b"one\nmiddle\nthree\n"},
            {"merge.c": b"one\nmiddle\nTHREE\n"},
        )
        (fixture.source / "merge.c").write_bytes(b"ONE\nmiddle\nthree\n")
        fixture.manifest(
            [{"id": "merge-c", "upstream_path": "merge.c", "local_path": "merge.c", "kind": "modified"}]
        )
        output = fixture.build_root / "clean"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(report["items"][0]["disposition"], "clean_merge")
        proposed = next((output / "candidates").glob("*/proposed")).read_bytes()
        self.assertEqual(proposed, b"ONE\nmiddle\nTHREE\n")
        self.assertIn(b"THREE", (output / "candidates.patch").read_bytes())

    def test_clean_manifest_candidate_can_be_accepted_then_updated_again(self):
        fixture = Fixture(
            self,
            {"merge.c": b"one\nkeep a\nkeep b\nmiddle\nkeep c\nkeep d\nthree\n"},
            {"merge.c": b"one\nkeep a\nkeep b\nmiddle\nkeep c\nkeep d\nTHREE\n"},
        )
        (fixture.source / "merge.c").write_bytes(
            b"ONE\nkeep a\nkeep b\nmiddle\nkeep c\nkeep d\nthree\n"
        )
        fixture.manifest(
            [{"id": "merge-c", "upstream_path": "merge.c", "local_path": "merge.c", "kind": "modified"}]
        )

        first_output = fixture.build_root / "first-update"
        fixture.invoke("prepare", "--to", fixture.target, "--output", first_output)
        first_report = json.loads((first_output / "report.json").read_text(encoding="utf-8"))
        first_manifest_path = first_output / "manifest-candidate.json"
        first_manifest = json.loads(first_manifest_path.read_text(encoding="utf-8"))
        first_entry = first_manifest["files"][0]
        self.assertTrue(first_report["manifest_candidate"]["acceptance_ready"])
        self.assertEqual(first_report["manifest_candidate"]["path"], "manifest-candidate.json")
        self.assertEqual(first_manifest["upstream"]["baseline"], fixture.target)
        self.assertEqual(first_entry["id"], "merge-c")
        self.assertEqual(first_entry["baseline_blob"], fixture._tree_blobs(fixture.remote, fixture.target)["merge.c"])
        self.assertEqual(first_entry["kind"], "modified")

        proposed = next((first_output / "candidates").glob("*/proposed")).read_bytes()
        (fixture.source / "merge.c").write_bytes(proposed)
        shutil.copyfile(first_manifest_path, fixture.source / "upstream" / "files.json")
        self.assertEqual(fixture.invoke("check").returncode, 0)

        write_tree(
            fixture.remote,
            {"merge.c": b"one\nkeep a\nkeep b\nMIDDLE\nkeep c\nkeep d\nTHREE\n"},
        )
        git(fixture.remote, "add", "-A")
        git(fixture.remote, "commit", "-m", "second target")
        second_target = git(fixture.remote, "rev-parse", "HEAD").stdout.strip()
        git(fixture.source, "fetch", "--no-tags", str(fixture.remote), second_target)
        second_output = fixture.build_root / "second-update"
        fixture.invoke("prepare", "--to", second_target, "--output", second_output)
        second_manifest = json.loads(
            (second_output / "manifest-candidate.json").read_text(encoding="utf-8")
        )
        self.assertEqual(second_manifest["upstream"]["baseline"], second_target)
        self.assertEqual(second_manifest["files"][0]["id"], "merge-c")
        self.assertEqual(second_manifest["files"][0]["kind"], "modified")
        second_proposed = next((second_output / "candidates").glob("*/proposed")).read_bytes()
        self.assertEqual(
            second_proposed,
            b"ONE\nkeep a\nkeep b\nMIDDLE\nkeep c\nkeep d\nTHREE\n",
        )

    def test_mode_only_changes_always_require_manual_review(self):
        fixture = Fixture(
            self,
            {"mode.c": b"same\n"},
            {"mode.c": b"same\n"},
            target_executable=["mode.c"],
        )
        fixture.manifest(
            [{"id": "mode-c", "upstream_path": "mode.c", "local_path": "mode.c", "kind": "direct"}]
        )
        comparison = json.loads(fixture.invoke("compare", "--to", fixture.target).stdout)
        self.assertEqual(comparison["changes"][0]["old_mode"], "100644")
        self.assertEqual(comparison["changes"][0]["new_mode"], "100755")
        output = fixture.build_root / "mode-only"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        item = report["items"][0]
        self.assertTrue(item["review_required"])
        self.assertEqual(item["disposition"], "manual_review")
        self.assertEqual(item["old_mode"], "100644")
        self.assertEqual(item["new_mode"], "100755")
        self.assertIn("mode change", item["reason"])
        self.assertIsNone(item["patch"])

    def test_prepare_records_a_diff3_conflict_for_manual_review(self):
        fixture = Fixture(self, {"conflict.c": b"value = 1;\n"}, {"conflict.c": b"value = 3;\n"})
        (fixture.source / "conflict.c").write_bytes(b"value = 2;\n")
        fixture.manifest(
            [{"id": "conflict-c", "upstream_path": "conflict.c", "local_path": "conflict.c", "kind": "modified"}]
        )
        output = fixture.build_root / "conflict"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertTrue(report["items"][0]["review_required"])
        proposed = next((output / "candidates").glob("*/proposed")).read_text(encoding="utf-8")
        self.assertIn("<<<<<<< current", proposed)
        self.assertIn("||||||| baseline", proposed)
        self.assertIn(">>>>>>> upstream", proposed)

    def test_prepare_accepts_git_conflict_counts_greater_than_one(self):
        baseline = b"top = 1;\nkeep a\nkeep b\nkeep c\nkeep d\nbottom = 1;\n"
        current = b"top = 2;\nkeep a\nkeep b\nkeep c\nkeep d\nbottom = 2;\n"
        incoming = b"top = 3;\nkeep a\nkeep b\nkeep c\nkeep d\nbottom = 3;\n"
        fixture = Fixture(self, {"multiple.c": baseline}, {"multiple.c": incoming})
        (fixture.source / "multiple.c").write_bytes(current)
        fixture.manifest(
            [{"id": "multiple-c", "upstream_path": "multiple.c", "local_path": "multiple.c", "kind": "modified"}]
        )
        output = fixture.build_root / "multiple-conflicts"
        result = fixture.invoke(
            "prepare", "--to", fixture.target, "--output", output, check=False
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        self.assertTrue(report["items"][0]["review_required"])

    def test_compare_reports_add_delete_and_ambiguous_exact_renames(self):
        fixture = Fixture(
            self,
            {"old.c": b"same\n", "deleted.c": b"gone\n"},
            {"new-one.c": b"same\n", "new-two.c": b"same\n", "added.c": b"new\n"},
        )
        fixture.manifest(
            [
                {"id": "old-c", "upstream_path": "old.c", "local_path": "old.c", "kind": "direct"},
                {"id": "deleted-c", "upstream_path": "deleted.c", "local_path": "deleted.c", "kind": "direct"},
            ]
        )
        result = json.loads(fixture.invoke("compare", "--to", fixture.target).stdout)
        self.assertEqual(result["summary"]["added"], 3)
        self.assertEqual(result["summary"]["deleted"], 2)
        candidates = [item for item in result["rename_candidates"] if item["from"] == "old.c"]
        self.assertEqual({item["to"] for item in candidates}, {"new-one.c", "new-two.c"})
        self.assertTrue(all(item["ambiguous"] for item in candidates))
        output = fixture.build_root / "tree-changes"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        changed = [item for item in report["items"] if item["change"] in ("added", "deleted")]
        self.assertTrue(changed)
        self.assertTrue(all(item["review_required"] for item in changed))
        renamed = [item for item in changed if item["rename_candidates"]]
        self.assertTrue(renamed)
        self.assertTrue(all("rename" in item["reason"] for item in renamed))

    def test_templates_and_tombstones_always_require_manual_review(self):
        fixture = Fixture(
            self,
            {"template.h": b"#define VALUE 1\n", "tombstone.h": b"old\n"},
            {"template.h": b"#define VALUE 2\n", "tombstone.h": b"new\n"},
        )
        (fixture.source / "template.h").rename(fixture.source / "template.h.in")
        (fixture.source / "tombstone.h").unlink()
        fixture.manifest(
            [
                {"id": "template-h", "upstream_path": "template.h", "local_path": "template.h.in", "kind": "template"},
                {"id": "tombstone-h", "upstream_path": "tombstone.h", "local_path": None, "kind": "removed"},
            ],
            generated=[
                {"template": "template.h.in", "output": "build/template.h", "generator": "fixture", "notes": "fixture"}
            ],
        )
        self.assertEqual(fixture.invoke("check").returncode, 0)
        output = fixture.build_root / "manual"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        by_kind = {item["kind"]: item for item in report["items"]}
        self.assertTrue(by_kind["template"]["review_required"])
        self.assertIn("template", by_kind["template"]["reason"])
        self.assertTrue(by_kind["removed"]["review_required"])
        self.assertIn("tombstoned", by_kind["removed"]["reason"])

    def test_manifest_candidate_keeps_structural_decisions_manual_and_conservative(self):
        fixture = Fixture(
            self,
            {
                "old.c": b"same\n",
                "deleted.c": b"deleted\n",
                "template.h": b"#define VALUE 1\n",
            },
            {
                "new.c": b"same\n",
                "added.c": b"added\n",
                "template.h": b"#define VALUE 2\n",
            },
        )
        (fixture.source / "template.h").rename(fixture.source / "template.h.in")
        fixture.manifest(
            [
                {"id": "old-c", "upstream_path": "old.c", "local_path": "old.c", "kind": "direct"},
                {"id": "deleted-c", "upstream_path": "deleted.c", "local_path": "deleted.c", "kind": "direct"},
                {"id": "template-h", "upstream_path": "template.h", "local_path": "template.h.in", "kind": "template"},
            ],
            generated=[
                {"template": "template.h.in", "output": "build/template.h", "generator": "fixture", "notes": "fixture"}
            ],
            required_paths=["deleted.c"],
        )
        output = fixture.build_root / "manual-manifest"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        candidate = json.loads(
            (output / "manifest-candidate.json").read_text(encoding="utf-8")
        )
        self.assertFalse(report["manifest_candidate"]["acceptance_ready"])
        self.assertTrue(report["manifest_candidate"]["review_required"])
        self.assertTrue(report["manifest_candidate"]["manual_review_stable_ids"])
        by_upstream = {entry["upstream_path"]: entry for entry in candidate["files"]}
        by_local = {entry["local_path"]: entry for entry in candidate["files"]}
        self.assertEqual(by_local["old.c"]["id"], "old-c")
        self.assertEqual(by_local["old.c"]["kind"], "local")
        self.assertEqual(by_local["deleted.c"]["id"], "deleted-c")
        self.assertEqual(by_local["deleted.c"]["kind"], "local")
        self.assertEqual(by_upstream["new.c"]["kind"], "removed")
        self.assertIsNone(by_upstream["new.c"]["local_path"])
        self.assertEqual(by_upstream["added.c"]["kind"], "removed")
        self.assertEqual(by_upstream["template.h"]["kind"], "template")
        self.assertNotEqual(by_upstream["new.c"].get("local_path"), "old.c")
        self.assertEqual(
            set(report["manifest_candidate"]["manual_review_stable_ids"]),
            {item["stable_id"] for item in report["items"] if item["review_required"]},
        )
        shutil.copyfile(
            output / "manifest-candidate.json",
            fixture.source / "upstream" / "files.json",
        )
        self.assertEqual(fixture.invoke("check").returncode, 0)

    def test_new_upstream_path_uses_one_collision_safe_id_in_every_artifact(self):
        fixture = Fixture(
            self,
            {"mapped.c": b"same\n"},
            {"mapped.c": b"same\n", "added.c": b"added\n"},
        )
        (fixture.source / "local.c").write_bytes(b"local\n")
        colliding_id = "unmapped-" + hashlib.sha256(b"added.c").hexdigest()[:12]
        fixture.manifest(
            [
                {"id": "mapped-c", "upstream_path": "mapped.c", "local_path": "mapped.c", "kind": "direct"},
                {"id": colliding_id, "upstream_path": None, "local_path": "local.c", "kind": "local"},
            ]
        )
        output = fixture.build_root / "id-collision"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        candidate_map = json.loads(
            (output / "candidate-map.json").read_text(encoding="utf-8")
        )
        candidate = json.loads(
            (output / "manifest-candidate.json").read_text(encoding="utf-8")
        )
        report_id = next(
            item["stable_id"] for item in report["items"] if item["upstream_path"] == "added.c"
        )
        map_id = next(
            item["stable_id"]
            for item in candidate_map["candidates"]
            if item["upstream_path"] == "added.c"
        )
        manifest_id = next(
            item["id"] for item in candidate["files"] if item["upstream_path"] == "added.c"
        )
        self.assertEqual(report_id, colliding_id + "-2")
        self.assertEqual(report_id, map_id)
        self.assertEqual(report_id, manifest_id)
        self.assertIn(report_id, report["manifest_candidate"]["manual_review_stable_ids"])

    def test_deleted_generated_template_keeps_manifest_candidate_manual(self):
        fixture = Fixture(self, {"template.h": b"#define VALUE 1\n"}, {})
        (fixture.source / "template.h").rename(fixture.source / "template.h.in")
        fixture.manifest(
            [
                {"id": "template-h", "upstream_path": "template.h", "local_path": "template.h.in", "kind": "template"}
            ],
            generated=[
                {"template": "template.h.in", "output": "build/template.h", "generator": "fixture", "notes": "fixture"}
            ],
        )
        output = fixture.build_root / "deleted-template"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        candidate = json.loads(
            (output / "manifest-candidate.json").read_text(encoding="utf-8")
        )
        item = next(item for item in report["items"] if item["stable_id"] == "template-h")
        self.assertTrue(item["review_required"])
        self.assertFalse(report["manifest_candidate"]["acceptance_ready"])
        self.assertIn("template-h", report["manifest_candidate"]["manual_review_stable_ids"])
        candidate_entry = next(item for item in candidate["files"] if item["id"] == "template-h")
        self.assertEqual(candidate_entry["kind"], "local")
        self.assertEqual(candidate_entry["local_path"], "template.h.in")

    def test_missing_target_object_has_a_clear_offline_error(self):
        fixture = Fixture(self, {"a.c": b"a\n"}, {"a.c": b"b\n"})
        fixture.manifest(
            [{"id": "a-c", "upstream_path": "a.c", "local_path": "a.c", "kind": "direct"}]
        )
        missing = "0" * 40
        result = fixture.invoke("compare", "--to", missing, check=False)
        self.assertEqual(result.returncode, 2)
        self.assertIn("unavailable offline", result.stderr)
        self.assertIn("--fetch", result.stderr)

    def test_crlf_is_the_only_content_normalization(self):
        fixture = Fixture(self, {"lines.c": b"one\ntwo\n"}, {"lines.c": b"one\nTWO\n"})
        (fixture.source / "lines.c").write_bytes(b"one\r\ntwo\r\n")
        fixture.manifest(
            [{"id": "lines-c", "upstream_path": "lines.c", "local_path": "lines.c", "kind": "direct"}]
        )
        self.assertEqual(fixture.invoke("check").returncode, 0)
        output = fixture.build_root / "line-endings"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        proposed = next((output / "candidates").glob("*/proposed")).read_bytes()
        self.assertEqual(proposed, b"one\r\nTWO\r\n")
        self.assertNotIn(b"one \r\n", proposed)

    def test_fetch_uses_only_a_bare_cache_below_the_build_directory(self):
        fixture = Fixture(
            self,
            {"remote.c": b"old\n"},
            {"remote.c": b"new\n"},
            clone_before_target=True,
        )
        fixture.manifest(
            [{"id": "remote-c", "upstream_path": "remote.c", "local_path": "remote.c", "kind": "direct"}]
        )
        before_refs = ref_snapshot(fixture.source)
        before_index = (fixture.source / ".git" / "index").read_bytes()
        offline = fixture.invoke("compare", "--to", fixture.target, check=False)
        self.assertEqual(offline.returncode, 2)
        cache_root = fixture.build_root / "fetch-compare"
        fetched = fixture.invoke(
            "compare", "--to", fixture.target, "--fetch", "--cache-dir", cache_root
        )
        self.assertEqual(json.loads(fetched.stdout)["target"], fixture.target)
        cache = cache_root / ".upstream-cache.git"
        self.assertEqual(git(cache, "rev-parse", "--is-bare-repository").stdout.strip(), "true")
        self.assertEqual(ref_snapshot(fixture.source), before_refs)
        self.assertEqual((fixture.source / ".git" / "index").read_bytes(), before_index)

    def test_output_must_be_fresh_and_cannot_contain_source_paths(self):
        fixture = Fixture(self, {"safe.c": b"a\n"}, {"safe.c": b"b\n"})
        fixture.manifest(
            [{"id": "safe-c", "upstream_path": "safe.c", "local_path": "safe.c", "kind": "direct"}]
        )
        existing = fixture.build_root / "existing"
        existing.mkdir(parents=True)
        result = fixture.invoke(
            "prepare", "--to", fixture.target, "--output", existing, check=False
        )
        self.assertEqual(result.returncode, 2)
        self.assertIn("must not already exist", result.stderr)
        result = fixture.invoke(
            "prepare", "--to", fixture.target, "--output", fixture.source, check=False
        )
        self.assertEqual(result.returncode, 2)

    def test_output_cannot_enter_git_or_linked_worktree_metadata(self):
        fixture = Fixture(self, {"safe.c": b"a\n"}, {"safe.c": b"b\n"})
        fixture.manifest(
            [{"id": "safe-c", "upstream_path": "safe.c", "local_path": "safe.c", "kind": "direct"}]
        )
        linked = Path(self.temp.name) / "linked"
        git(fixture.source, "worktree", "add", "--detach", linked, fixture.baseline)
        (linked / "tools").mkdir()
        shutil.copyfile(TOOL, linked / "tools" / "upstream.py")
        (linked / "upstream").mkdir()
        shutil.copyfile(
            fixture.source / "upstream" / "files.json",
            linked / "upstream" / "files.json",
        )

        def git_path(argument):
            value = git(linked, "rev-parse", argument).stdout.strip()
            if len(value) >= 3 and value[0] == "/" and value[1].isalpha() and value[2] == "/":
                value = value[1].upper() + ":" + value[2:]
            path = Path(value)
            return (path if path.is_absolute() else linked / path).resolve()

        git_directory = git_path("--git-dir")
        common_directory = git_path("--git-common-dir")
        self.assertNotEqual(git_directory, common_directory)
        for name, output in (
            ("git-dir", git_directory / "upstream-output"),
            ("common-dir", common_directory / "upstream-output"),
        ):
            with self.subTest(name=name):
                result = run(
                    [
                        sys.executable,
                        "-B",
                        linked / "tools" / "upstream.py",
                        "prepare",
                        "--to",
                        fixture.target,
                        "--output",
                        output,
                    ],
                    linked,
                    check=False,
                )
                self.assertEqual(result.returncode, 2)
                self.assertIn("Git metadata", result.stderr)
                self.assertFalse(output.exists())

    def test_manifest_paths_reject_windows_drives_streams_and_normalized_components(self):
        fixture = Fixture(self, {"safe.c": b"a\n"}, {"safe.c": b"b\n"})
        fixture.manifest(
            [{"id": "safe-c", "upstream_path": "safe.c", "local_path": "safe.c", "kind": "direct"}]
        )
        manifest_path = fixture.source / "upstream" / "files.json"
        original = json.loads(manifest_path.read_text(encoding="utf-8"))
        for unsafe in ("C:/outside.c", "C:outside.c", "safe.c:stream", "dir//safe.c", "./safe.c", "dir/../safe.c"):
            with self.subTest(path=unsafe):
                data = json.loads(json.dumps(original))
                data["files"][0]["local_path"] = unsafe
                manifest_path.write_text(json.dumps(data), encoding="utf-8")
                result = fixture.invoke("check", check=False)
                self.assertEqual(result.returncode, 1)
                self.assertIn('"valid": false', result.stdout)
                self.assertNotIn("Traceback", result.stderr)

    def test_mapped_symlink_cannot_escape_the_repository(self):
        fixture = Fixture(self, {"safe.c": b"a\n"}, {"safe.c": b"b\n"})
        outside = Path(self.temp.name) / "outside.c"
        outside.write_bytes(b"a\n")
        link = fixture.source / "link.c"
        try:
            link.symlink_to(outside)
        except OSError as exc:
            self.skipTest("file symlinks unavailable: {}".format(exc))
        fixture.manifest(
            [{"id": "safe-c", "upstream_path": "safe.c", "local_path": "link.c", "kind": "direct"}]
        )
        checked = fixture.invoke("check", check=False)
        self.assertEqual(checked.returncode, 1)
        self.assertIn("resolves outside the repository", checked.stdout)
        output = fixture.build_root / "symlink-escape"
        prepared = fixture.invoke(
            "prepare", "--to", fixture.target, "--output", output, check=False
        )
        self.assertEqual(prepared.returncode, 2)
        self.assertIn("resolves outside the repository", prepared.stderr)
        self.assertEqual(outside.read_bytes(), b"a\n")

    def test_refresh_writes_an_advisory_copy_inside_a_fresh_build_directory(self):
        fixture = Fixture(self, {"refresh.c": b"old\n"}, {"refresh.c": b"new\n"})
        fixture.manifest(
            [{"id": "refresh-c", "upstream_path": "refresh.c", "local_path": "refresh.c", "kind": "direct"}]
        )
        original = (fixture.source / "upstream" / "files.json").read_bytes()
        (fixture.source / "refresh.c").write_bytes(b"local change\n")
        output = fixture.build_root / "refresh"
        result = fixture.invoke("refresh", "--output", output)
        refreshed_path = Path(result.stdout.strip())
        self.assertEqual(refreshed_path, output / "files.json")
        refreshed = json.loads(refreshed_path.read_text(encoding="utf-8"))
        self.assertEqual(refreshed["files"][0]["kind"], "modified")
        self.assertEqual((fixture.source / "upstream" / "files.json").read_bytes(), original)

    def test_local_inventory_is_case_insensitive_and_honors_excluded_prefixes(self):
        fixture = Fixture(self, {"base.c": b"old\n"}, {"base.c": b"new\n"})
        local_fortran = fixture.source / "tests" / "MAIN.F90"
        local_fortran.parent.mkdir()
        local_fortran.write_bytes(b"program main\nend program main\n")
        local_template = fixture.source / "cmake" / "Local.CMAKE.IN"
        local_template.parent.mkdir()
        local_template.write_bytes(b"@VALUE@\n")
        excluded = fixture.source / "ext" / "vendor.cpp"
        excluded.parent.mkdir()
        excluded.write_bytes(b"int vendor;\n")
        fixture.manifest(
            [{"id": "base-c", "upstream_path": "base.c", "local_path": "base.c", "kind": "direct"}],
            coverage_updates={
                "local_extensions": [".c", ".cpp", ".f90", ".in"],
                "local_exclude_prefixes": ["ext/"],
                "template_suffixes": [".h.in", ".cmake.in"],
            },
        )
        checked = fixture.invoke("check", check=False)
        self.assertEqual(checked.returncode, 1)
        self.assertIn("tests/MAIN.F90", checked.stdout)
        self.assertIn("cmake/Local.CMAKE.IN", checked.stdout)
        self.assertNotIn("ext/vendor.cpp", checked.stdout)
        output = fixture.build_root / "inventory-refresh"
        fixture.invoke("refresh", "--output", output)
        refreshed = json.loads((output / "files.json").read_text(encoding="utf-8"))
        local_paths = {entry["local_path"] for entry in refreshed["files"]}
        self.assertIn("tests/MAIN.F90", local_paths)
        self.assertIn("cmake/Local.CMAKE.IN", local_paths)
        self.assertNotIn("ext/vendor.cpp", local_paths)

    def test_check_reports_malformed_local_extensions_without_a_traceback(self):
        fixture = Fixture(self, {"base.c": b"old\n"}, {"base.c": b"new\n"})
        fixture.manifest(
            [{"id": "base-c", "upstream_path": "base.c", "local_path": "base.c", "kind": "direct"}],
            coverage_updates={"local_extensions": [".c", 7]},
        )
        result = fixture.invoke("check", check=False)
        self.assertEqual(result.returncode, 1)
        self.assertIn("coverage.local_extensions", result.stdout)
        self.assertNotIn("Traceback", result.stderr)

    def test_unmapped_text_modifications_receive_advisory_patches(self):
        fixture = Fixture(
            self,
            {"mapped.c": b"same\n", "NOTES.txt": b"old notes"},
            {"mapped.c": b"same\n", "NOTES.txt": b"new notes"},
        )
        fixture.manifest(
            [{"id": "mapped-c", "upstream_path": "mapped.c", "local_path": "mapped.c", "kind": "direct"}]
        )
        output = fixture.build_root / "unmapped-modification"
        fixture.invoke("prepare", "--to", fixture.target, "--output", output)
        report = json.loads((output / "report.json").read_text(encoding="utf-8"))
        notes = next(item for item in report["items"] if item["upstream_path"] == "NOTES.txt")
        self.assertTrue(notes["review_required"])
        self.assertEqual(notes["patch"], "candidate.patch")
        patch = (output / notes["candidate_directory"] / notes["patch"]).read_text(encoding="utf-8")
        self.assertIn("-old notes", patch)
        self.assertIn("+new notes", patch)
        self.assertEqual(patch.count("\\ No newline at end of file"), 2)
        patch_path = output / notes["candidate_directory"] / notes["patch"]
        git(fixture.source, "apply", "--check", str(patch_path))


if __name__ == "__main__":
    unittest.main()
