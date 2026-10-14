#!/usr/bin/env python3

"""Find successful push jobs that can replace QEMU jobs in a fork PR."""

import datetime
import json
import os
import sys
import urllib.parse
import urllib.request


WORKFLOW = ".github/workflows/zfs-qemu.yml"
REQUIRED_STEPS = {
    "Setup QEMU", "Start build machine", "Install dependencies",
    "Build modules", "Setup testing machines", "Run tests", "Test Summary",
}
MAX_AGE = datetime.timedelta(days=7)


def github_api(path):
    request = urllib.request.Request(
        f"https://api.github.com/{path}", headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
            "X-GitHub-Api-Version": "2022-11-28",
        })
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.load(response)


def reusable_jobs(event, targets, api=github_api, now=None):
    pr = event["pull_request"]
    repo = pr["head"]["repo"]["full_name"]
    if repo.lower() == pr["base"]["repo"]["full_name"].lower():
        return {}
    head = pr["head"]["sha"]
    if not targets:
        return {}

    # Push uses ci_type=default (full tests), even with ZTS_OS_OVERRIDE.
    # Dispatch and PR runs can use different inputs or a reduced test suite.
    query = urllib.parse.urlencode({
        "event": "push", "head_sha": head, "per_page": 1,
    })
    runs = api(f"repos/{repo}/actions/workflows/zfs-qemu.yml/runs?{query}")
    if not runs["workflow_runs"]:
        return {}
    run = runs["workflow_runs"][0]
    now = now or datetime.datetime.now(datetime.timezone.utc)
    created = datetime.datetime.fromisoformat(
        run["created_at"].replace("Z", "+00:00"))
    if (run["head_sha"] != head or run["event"] != "push" or
            run["status"] != "completed" or run["path"] != WORKFLOW or
            run["repository"]["full_name"].lower() != repo.lower() or
            run["head_repository"]["full_name"].lower() != repo.lower() or
            not datetime.timedelta(0) <= now - created <= MAX_AGE):
        return {}

    jobs = api(f"repos/{repo}/actions/runs/{run['id']}/jobs"
               "?filter=latest&per_page=100")
    # A truncated matrix cannot establish which results are current.
    if jobs["total_count"] > len(jobs["jobs"]):
        return {}
    names = {f"qemu-x86 ({target})": target for target in targets}
    result = {}
    for job in jobs["jobs"]:
        target = names.get(job["name"])
        if (target is None or job["head_sha"] != head or
                job["run_id"] != run["id"] or
                job["status"] != "completed" or
                job["conclusion"] != "success" or
                job["labels"] != ["ubuntu-24.04"]):
            continue
        steps = {step["name"]: step for step in job["steps"]}
        if not all(name in steps and
                   steps[name]["status"] == "completed" and
                   steps[name]["conclusion"] == "success"
                   for name in REQUIRED_STEPS):
            continue
        result[target] = (
            f"https://github.com/{repo}/actions/runs/{run['id']}"
            f"/job/{job['id']}")
    return result


def main():
    result = {}
    try:
        if os.environ.get("GITHUB_EVENT_NAME") == "pull_request":
            with open(os.environ["GITHUB_EVENT_PATH"]) as stream:
                event = json.load(stream)
            result = reusable_jobs(
                event, json.loads(os.environ["ZTS_TARGETS"]))
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f"Cannot reuse fork QEMU jobs: {error}", file=sys.stderr)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
