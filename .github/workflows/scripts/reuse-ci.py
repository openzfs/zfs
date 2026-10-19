#!/usr/bin/env python3

"""Find successful fork push jobs for PR checks."""

import datetime
import json
import os
import sys
import urllib.parse
import urllib.request


WORKFLOWS = {
    "zfs-qemu": ("zfs-qemu.yml", "ubuntu-24.04", None, {
        "Setup QEMU", "Start build machine", "Install dependencies",
        "Build modules", "Setup testing machines", "Run tests", "Test Summary",
    }),
    "zloop": ("zloop.yml", "ubuntu-24.04", "zloop", {
        "Install dependencies", "Autogen.sh", "Configure", "Make",
        "Install", "Tests",
    }),
    "unit-tests": ("unit-tests.yml", "ubuntu-24.04", "unit-tests", {
        "Install dependencies", "Autogen", "Configure", "Unit tests",
    }),
    "zfs-arm": ("zfs-arm.yml", "ubuntu-24.04-arm", "ZFS ARM build", {
        "Install dependencies", "Build modules",
    }),
    "checkstyle": ("checkstyle.yaml", "ubuntu-22.04", "checkstyle", {
        "Install dependencies", "Prepare", "Configure", "Make",
        "Checkstyle", "Lint", "CheckABI",
    }),
}
MAX_AGE = datetime.timedelta(days=7)
# Push runs of one commit to consider, e.g. after pushing it to a PR branch.
MAX_RUNS = 10


def github_api(path):
    request = urllib.request.Request(
        f"https://api.github.com/{path}", headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
            "X-GitHub-Api-Version": "2022-11-28",
        })
    with urllib.request.urlopen(request, timeout=10) as response:
        return json.load(response)


def usable_run(run, repo, head, filename, now):
    created = datetime.datetime.fromisoformat(
        run["created_at"].replace("Z", "+00:00"))
    return (run["head_sha"] == head and run["event"] == "push" and
            run["path"] == f".github/workflows/{filename}" and
            run["repository"]["full_name"].lower() == repo.lower() and
            run["head_repository"]["full_name"].lower() == repo.lower() and
            datetime.timedelta(0) <= now - created <= MAX_AGE)


def successful_job(job, run, head, runner, required_steps):
    if (job["head_sha"] != head or job["run_id"] != run["id"] or
            job["status"] != "completed" or
            job["conclusion"] != "success" or
            job["labels"] != [runner]):
        return False
    steps = {step["name"]: step for step in job["steps"]}
    return all(name in steps and
               steps[name]["status"] == "completed" and
               steps[name]["conclusion"] == "success"
               for name in required_steps)


def reusable_jobs(event, targets, api=github_api, now=None,
                  workflow="zfs-qemu"):
    filename, runner, job_name, required_steps = WORKFLOWS[workflow]
    pr = event["pull_request"]
    repo = pr["head"]["repo"]["full_name"]
    if repo.lower() == pr["base"]["repo"]["full_name"].lower():
        return {}
    head = pr["head"]["sha"]
    if job_name is None and not targets:
        return {}

    # QEMU push runs use full tests, even with ZTS_OS_OVERRIDE. Dispatch
    # and PR runs can use different inputs or a reduced test suite.
    query = urllib.parse.urlencode({
        "event": "push", "head_sha": head, "per_page": MAX_RUNS,
    })
    runs = api(f"repos/{repo}/actions/workflows/{filename}/runs?{query}")
    now = now or datetime.datetime.now(datetime.timezone.utc)
    names = ({f"qemu-x86 ({target})": target for target in targets}
             if job_name is None else {job_name: workflow})
    result = {}
    decided = set()
    # Runs are newest first.  The newest finished result for each job
    # decides it: a success is reused, a failure is not, and an older
    # success never replaces it.  A job that was cancelled, skipped or has
    # not finished yet (for example in the run started by pushing an
    # already tested commit to the PR branch) has no result, so an older
    # run of the same commit can still supply one.  Only jobs in completed
    # runs are reused.
    for run in runs["workflow_runs"]:
        if len(decided) == len(names):
            break
        if not usable_run(run, repo, head, filename, now):
            continue
        jobs = api(f"repos/{repo}/actions/runs/{run['id']}/jobs"
                   "?filter=latest&per_page=100")
        # A truncated matrix cannot establish which results are current.
        if jobs["total_count"] > len(jobs["jobs"]):
            break
        completed = run["status"] == "completed"
        for job in jobs["jobs"]:
            target = names.get(job["name"])
            if (target is None or target in decided or
                    job["status"] != "completed" or
                    job["conclusion"] in ("cancelled", "skipped")):
                continue
            if not completed and job["conclusion"] == "success":
                continue
            decided.add(target)
            if completed and successful_job(job, run, head, runner,
                                            required_steps):
                result[target] = (
                    f"https://github.com/{repo}/actions/runs/{run['id']}"
                    f"/job/{job['id']}")
    return result


def main():
    result = {}
    try:
        if (os.environ.get("GITHUB_EVENT_NAME") == "pull_request" and
                os.environ.get("GITHUB_RUN_ATTEMPT", "1") == "1"):
            with open(os.environ["GITHUB_EVENT_PATH"]) as stream:
                event = json.load(stream)
            result = reusable_jobs(
                event, json.loads(os.environ.get("ZTS_TARGETS", "[]")),
                workflow=sys.argv[1] if len(sys.argv) > 1 else "zfs-qemu")
    except (OSError, ValueError, KeyError, TypeError, RuntimeError) as error:
        print(f"Cannot reuse fork CI jobs: {error}", file=sys.stderr)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
