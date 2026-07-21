#!/usr/bin/env python3
"""Generate a release summary markdown by combining gh-release-summary.py output with Claude."""

import argparse
import subprocess
import sys
from pathlib import Path

SCRIPT_DIR = Path(__file__).resolve().parent

SECTIONS = [
    "New Devices",
    "New Features",
    "New Apps",
    "Improvements & Fixes",
    "Other Changes",
    "TactilitySDK",
    "Known Issues",
]


def run_gh_release_summary(out_dir: Path, repo: str | None):
    cmd = [sys.executable, str(SCRIPT_DIR / "gh-release-summary.py"), "--out", str(out_dir)]
    if repo:
        cmd += ["--repo", repo]
    subprocess.run(cmd, check=True)


def build_prompt() -> str:
    section_list = "\n".join(f"- {s}" for s in SECTIONS)
    return (
        "You are given a list of commits (with GitHub commit links) and a list of pull "
        "request links from a software release. Write a release summary in Markdown, "
        "organizing changes into these sections (omit a section if it has no relevant "
        "changes):\n\n"
        f"{section_list}\n\n"
        "Use concise bullet points. Base every bullet strictly on the given commits and "
        "pull requests, referencing the pull request link where applicable. Output only "
        "the Markdown, no commentary before or after it."
        "If a pull request or commit has multiple changes add them as separate entries."
        "Ignore irrelevant changes: documentation, minor fixes, non-descript fixes, scripts, github workflows."
        "For 'New Devices': only mention the device name, don't like pull request."
        "For 'Improvements & Fixes': Avoid non-descript entries, write specifics."
        "For 'TactilitySDK': Avoid generic changes, be specific. Mention API changes."
    )


def run_claude(commits_md: str, prs_md: str) -> str:
    prompt = build_prompt()
    stdin_content = f"## Commits\n\n{commits_md}\n\n## Pull Requests\n\n{prs_md}"
    result = subprocess.run(
        ["claude", "-p", prompt],
        input=stdin_content,
        capture_output=True,
        text=True,
        check=True,
    )
    return result.stdout


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", help="owner/repo (default: detected from git remote 'origin')")
    parser.add_argument("--out", default=".", help="Output directory (default: current directory)")
    args = parser.parse_args()

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)

    run_gh_release_summary(out_dir, args.repo)

    commits_md = (out_dir / "release-commits.md").read_text()
    prs_md = (out_dir / "release-pull-requests.md").read_text()

    print("Generating release summary via claude...")
    summary = run_claude(commits_md, prs_md)

    summary_path = out_dir / "release-summary.md"
    summary_path.write_text(summary)
    print(f"Wrote {summary_path}")


if __name__ == "__main__":
    main()
