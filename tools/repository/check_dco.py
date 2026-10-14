#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0
# Copyright 2026 Junior Deogracias
"""Check author DCO trailers on non-merge commits in a PR or push range."""
import argparse
import re
import subprocess
import sys


def git(*args, input=None):
    return subprocess.check_output(["git", *args], input=input, text=True).strip()


def has_author_signoff(author, email, message):
    expected = f"{author} <{email}>".casefold()
    # Parse the trailer block, rather than accepting quoted text in the body.
    trailers = git("interpret-trailers", "--parse", input=message)
    for line in trailers.splitlines():
        key, separator, value = line.partition(":")
        if separator and key.casefold() == "signed-off-by" and value.strip().casefold() == expected:
            return True
    return False


def resolve(ref):
    if ref != "HEAD" and not re.fullmatch(r"[0-9a-fA-F]{40}", ref):
        raise ValueError("Commit must be HEAD or a full 40-character GitHub SHA")
    return git("rev-parse", "--verify", ref + "^{commit}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--base", help="PR base/push before SHA; omit for a single-commit check")
    parser.add_argument("--head", default="HEAD", help="PR head/push after SHA (default: HEAD)")
    args = parser.parse_args()
    head = resolve(args.head)
    if args.base and args.base != "0" * 40:
        selection = [resolve(args.base) + ".." + head]
    else:
        selection = ["--max-count=1", head]
    commits = git("rev-list", "--no-merges", "--reverse", *selection).splitlines()
    failures = []
    for sha in commits:
        author, email, message = git("show", "-s", "--format=%an%x00%ae%x00%B", sha).split("\0", 2)
        if not has_author_signoff(author, email, message):
            failures.append(sha[:12])
    if failures:
        print("Missing matching author Signed-off-by trailer: " + ", ".join(failures), file=sys.stderr)
        return 1
    print(f"DCO: {len(commits)} authored commits checked; merge commits excluded")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (ValueError, subprocess.CalledProcessError) as error:
        print(f"DCO check failed: {error}", file=sys.stderr)
        sys.exit(1)
