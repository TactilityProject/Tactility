#!/usr/bin/env python3
"""Download all artifacts from a GitHub Actions workflow run."""

import argparse
import os
import subprocess
import sys
import zipfile
from pathlib import Path

import requests

API_URL = "https://api.github.com"


def get_token(cli_token: str | None) -> str:
    if cli_token:
        return cli_token
    env_token = os.environ.get("GITHUB_TOKEN") or os.environ.get("GH_TOKEN")
    if env_token:
        return env_token
    try:
        result = subprocess.run(
            ["gh", "auth", "token"], capture_output=True, text=True, check=True
        )
        return result.stdout.strip()
    except (FileNotFoundError, subprocess.CalledProcessError):
        sys.exit(
            "No token found. Pass --token, set GITHUB_TOKEN/GH_TOKEN, or run `gh auth login`."
        )


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

    # Handle both SSH and HTTPS remote URLs.
    if url.startswith("git@"):
        path = url.split(":", 1)[1]
    else:
        path = url.split("github.com/", 1)[-1]
    return path.removesuffix(".git")


def download_artifacts(repo: str, run_id: str, token: str, out_dir: Path):
    headers = {
        "Authorization": f"Bearer {token}",
        "Accept": "application/vnd.github+json",
        "X-GitHub-Api-Version": "2022-11-28",
    }

    out_dir.mkdir(parents=True, exist_ok=True)

    url = f"{API_URL}/repos/{repo}/actions/runs/{run_id}/artifacts"
    artifacts = []
    while url:
        resp = requests.get(url, headers=headers, params={"per_page": 100})
        resp.raise_for_status()
        data = resp.json()
        artifacts.extend(data["artifacts"])
        url = resp.links.get("next", {}).get("url")

    if not artifacts:
        print(f"No artifacts found for run {run_id}.")
        return

    for artifact in artifacts:
        name = artifact["name"]
        if name == "all-artifacts":
            print(f"Skipping {name}")
            continue
        if artifact.get("expired"):
            print(f"Skipping expired artifact: {name}")
            continue

        print(f"Downloading {name} ({artifact['size_in_bytes']} bytes)...")
        download_url = artifact["archive_download_url"]
        resp = requests.get(download_url, headers=headers, stream=True)
        resp.raise_for_status()

        zip_path = out_dir / f"{name}.zip"
        with open(zip_path, "wb") as f:
            for chunk in resp.iter_content(chunk_size=8192):
                f.write(chunk)

        if name.endswith(".bin"):
            with zipfile.ZipFile(zip_path) as zf:
                zf.extractall(out_dir)
            zip_path.unlink()
            print(f"  Extracted to {out_dir}")
        else:
            print(f"  Saved to {zip_path}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("run_id", help="GitHub Actions run ID")
    parser.add_argument(
        "--repo",
        help="owner/repo (default: detected from git remote 'origin')",
    )
    parser.add_argument(
        "--token",
        help="GitHub token (default: $GITHUB_TOKEN, $GH_TOKEN, or `gh auth token`)",
    )
    parser.add_argument(
        "--out",
        default="artifacts",
        help="Output directory (default: ./artifacts)",
    )
    args = parser.parse_args()

    repo = get_repo(args.repo)
    token = get_token(args.token)
    out_dir = Path(args.out)

    print(f"Repo: {repo}")
    print(f"Run ID: {args.run_id}")
    print(f"Output: {out_dir.resolve()}")

    download_artifacts(repo, args.run_id, token, out_dir)


if __name__ == "__main__":
    main()
