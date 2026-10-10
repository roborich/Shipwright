#!/usr/bin/env python3
# SOH [Unbound] Compares two converted bases (oot-unbound.o2r) entry by entry, for the release check in
# unbound-docs/SPEC.md §10.1: does a base from the previous release still hold what this one would write?
#
#   scripts/unbound-base-diff.py <previous oot-unbound.o2r> <fresh oot-unbound.o2r> [--all]
#
# Entries are compared by CRC and size from the zip directory (nothing is extracted) and reported in three
# groups: the manifest (unbound.json, compared field by field), what the converter writes (its JSON documents
# and collision.bin), and the game files it copies from oot.o2r. Lists stop at 20 names unless --all.
# Exit status: 0 when only the manifest differs, 1 when anything else does (review it against §10.1),
# 2 on bad usage.
import json
import sys
import zipfile

MANIFEST = "unbound.json"
LIST_LIMIT = 20


def is_converted(name):
    return name.endswith(".json") or name.endswith("/collision.bin")


def index(path):
    with zipfile.ZipFile(path) as archive:
        entries = {info.filename: (info.CRC, info.file_size) for info in archive.infolist()}
        manifest = json.loads(archive.read(MANIFEST)) if MANIFEST in entries else {}
    entries.pop(MANIFEST, None)
    return entries, manifest


def compare(old, new):
    return {
        "only in previous": sorted(old.keys() - new.keys()),
        "only in fresh": sorted(new.keys() - old.keys()),
        "changed": sorted(name for name in old.keys() & new.keys() if old[name] != new[name]),
    }


def split(entries, converted):
    return {name: value for name, value in entries.items() if is_converted(name) == converted}


def print_manifest(old, new):
    print(f"== {MANIFEST}")
    keys = sorted(old.keys() | new.keys())
    differing = [key for key in keys if old.get(key) != new.get(key)]
    for key in differing:
        print(f"  {key}: {json.dumps(old.get(key))} -> {json.dumps(new.get(key))}")
    if not differing:
        print("  identical")
    if old.get("baseVersion", 1) != new.get("baseVersion", 1):
        print("  baseVersion differs: the fresh build will not use the previous base")


def print_group(title, diff, show_all):
    total = sum(len(names) for names in diff.values())
    print(f"== {title}: {total} difference(s)")
    for kind, names in diff.items():
        if not names:
            continue
        print(f"  {kind}: {len(names)}")
        for name in names if show_all else names[:LIST_LIMIT]:
            print(f"    {name}")
        if not show_all and len(names) > LIST_LIMIT:
            print(f"    ... {len(names) - LIST_LIMIT} more (--all)")
    return total


def main(argv):
    args = [arg for arg in argv if arg != "--all"]
    if len(args) != 2:
        print("usage: unbound-base-diff.py <previous oot-unbound.o2r> <fresh oot-unbound.o2r> [--all]", file=sys.stderr)
        return 2
    show_all = "--all" in argv
    (old, old_manifest), (new, new_manifest) = index(args[0]), index(args[1])

    print_manifest(old_manifest, new_manifest)
    differences = print_group("converter output", compare(split(old, True), split(new, True)), show_all)
    differences += print_group("copied game files", compare(split(old, False), split(new, False)), show_all)
    if differences:
        print("Review the differences above against unbound-docs/SPEC.md §10.1 before deciding on a baseVersion bump.")
    return 1 if differences else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
