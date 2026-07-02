#!/usr/bin/env python3
"""Summarize commits between the last 2 pushed git tags into commits.md and pull-requests.md."""

import argparse
import re
import subprocess
import sys
from pathlib import Path

PR_REF_RE = re.compile(r"#(\d+)")


def get_repo(cli_repo: str | None) -> str:
    if cli_repo:
        return cli_repo
    try:
        result = subprocess.run(
            ["git", "remote", "get-url", "origin"],
            capture_output=True,
            text=True,
            check=True,
        )
        url = result.stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        sys.exit("Could not detect repo from git remote. Pass --repo owner/name.")

    if url.startswith("git@"):
        path = url.split(":", 1)[1]
    else:
        path = url.split("github.com/", 1)[-1]
    return path.removesuffix(".git")


def get_last_two_tags() -> tuple[str, str]:
    result = subprocess.run(
        ["git", "tag", "--sort=-creatordate"],
        capture_output=True,
        text=True,
        check=True,
    )
    tags = [t for t in result.stdout.splitlines() if t]
    if len(tags) < 2:
        sys.exit(f"Need at least 2 tags, found {len(tags)}.")
    return tags[1], tags[0]  # previous, latest


def get_commits(prev_tag: str, latest_tag: str) -> list[tuple[str, str]]:
    result = subprocess.run(
        [
            "git",
            "log",
            "--pretty=format:%H\x1f%s",
            f"{prev_tag}..{latest_tag}",
        ],
        capture_output=True,
        text=True,
        check=True,
    )
    commits = []
    for line in result.stdout.splitlines():
        if not line:
            continue
        sha, subject = line.split("\x1f", 1)
        commits.append((sha, subject))
    return commits


def write_commits_md(path: Path, repo: str, prev_tag: str, latest_tag: str, commits: list[tuple[str, str]]):
    lines = [f"# Commits: {prev_tag}..{latest_tag}", ""]
    for sha, subject in commits:
        link = f"https://github.com/{repo}/commit/{sha}"
        lines.append(f"- [{subject}]({link})")
    path.write_text("\n".join(lines) + "\n")


def write_pull_requests_md(path: Path, repo: str, commits: list[tuple[str, str]]):
    pr_numbers = []
    seen = set()
    for _, subject in commits:
        for match in PR_REF_RE.finditer(subject):
            number = match.group(1)
            if number not in seen:
                seen.add(number)
                pr_numbers.append(number)

    lines = ["# Pull Requests", ""]
    for number in pr_numbers:
        lines.append(f"- https://github.com/{repo}/pull/{number}")
    path.write_text("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--repo",
        help="owner/repo (default: detected from git remote 'origin')",
    )
    parser.add_argument(
        "--out",
        default=".",
        help="Output directory for commits.md and pull-requests.md (default: current directory)",
    )
    args = parser.parse_args()

    repo = get_repo(args.repo)
    prev_tag, latest_tag = get_last_two_tags()
    commits = get_commits(prev_tag, latest_tag)

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    commits_path = out_dir / "release-commits.md"
    prs_path = out_dir / "release-pull-requests.md"

    write_commits_md(commits_path, repo, prev_tag, latest_tag, commits)
    write_pull_requests_md(prs_path, repo, commits)

    print(f"Repo: {repo}")
    print(f"Tags: {prev_tag}..{latest_tag}")
    print(f"Commits: {len(commits)}")
    print(f"Wrote {commits_path}")
    print(f"Wrote {prs_path}")


if __name__ == "__main__":
    main()
