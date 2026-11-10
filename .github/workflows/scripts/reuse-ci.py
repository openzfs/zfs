#!/usr/bin/env python3

"""Find successful fork push jobs for PR checks."""

import datetime
import json
import os
import sys
import time
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
# Latest push runs in which to look for other commits of the same tree.
RECENT_RUNS = 100
# Waiting for unfinished fork jobs: the longest wait allowed, how long
# jobs that have not started are waited for, the time between checks
# (seconds), and the API requests to leave to other jobs.
MAX_WAIT = 300 * 60
START_WAIT = 30 * 60
POLL = 10 * 60
MIN_RATE = 200


def github_api(path):
    request = urllib.request.Request(
        f"https://api.github.com/{path}", headers={
            "Accept": "application/vnd.github+json",
            "Authorization": f"Bearer {os.environ['GH_TOKEN']}",
            "X-GitHub-Api-Version": "2022-11-28",
        })
    with urllib.request.urlopen(request, timeout=10) as response:
        remaining = response.headers.get("X-RateLimit-Remaining")
        if remaining is not None and int(remaining) < MIN_RATE:
            raise RuntimeError(f"only {remaining} API requests left")
        return json.load(response)


def usable_run(run, repo, tree, filename, now):
    created = datetime.datetime.fromisoformat(
        run["created_at"].replace("Z", "+00:00"))
    return (run["head_commit"] is not None and
            run["head_commit"]["tree_id"] == tree and
            run["event"] == "push" and
            run["path"] == f".github/workflows/{filename}" and
            run["repository"]["full_name"].lower() == repo.lower() and
            run["head_repository"]["full_name"].lower() == repo.lower() and
            datetime.timedelta(0) <= now - created <= MAX_AGE)


def successful_job(job, run, runner, required_steps):
    if (job["head_sha"] != run["head_sha"] or job["run_id"] != run["id"] or
            job["status"] != "completed" or
            job["conclusion"] != "success" or
            job["labels"] != [runner]):
        return False
    steps = {step["name"]: step for step in job["steps"]}
    return all(name in steps and
               steps[name]["status"] == "completed" and
               steps[name]["conclusion"] == "success"
               for name in required_steps)


def find_jobs(api, repo, head, tree, workflow, names, now):
    """Return reusable jobs, and the targets of running and queued jobs."""
    filename, runner, _, required_steps = WORKFLOWS[workflow]
    # QEMU push runs use full tests, even with ZTS_OS_OVERRIDE. Dispatch
    # and PR runs can use different inputs or a reduced test suite.
    runs = {}
    for params in ({"head_sha": head, "per_page": MAX_RUNS},
                   {"per_page": RECENT_RUNS}):
        query = urllib.parse.urlencode({"event": "push", **params})
        for run in api(f"repos/{repo}/actions/workflows/{filename}/runs"
                       f"?{query}")["workflow_runs"]:
            runs[run["id"]] = run
    now = now or datetime.datetime.now(datetime.timezone.utc)
    result = {}
    decided = set()
    running = set()
    queued = set()
    # Runs are newest first.  The newest finished result for each job
    # decides it: a success is reused, a failure is not, and an older
    # success never replaces it.  A job that was cancelled, skipped or has
    # not finished yet (for example in the run started by pushing an
    # already tested commit to the PR branch) has no result, so an older
    # run of the same tree can still supply one.
    for run in sorted(runs.values(), reverse=True,
                      key=lambda run: (run["created_at"], run["id"])):
        if len(decided) == len(names):
            break
        if not usable_run(run, repo, tree, filename, now):
            continue
        jobs = api(f"repos/{repo}/actions/runs/{run['id']}/jobs"
                   "?filter=latest&per_page=100")
        # A truncated matrix cannot establish which results are current.
        if jobs["total_count"] > len(jobs["jobs"]):
            break
        matrix = [(names[job["name"]], job) for job in jobs["jobs"]
                  if job["name"] in names]
        # A run lists its QEMU jobs only once its Setup job is done.
        if not matrix and run["status"] != "completed":
            queued.update(names.values())
        for target, job in matrix:
            if target in decided:
                continue
            if job["status"] == "in_progress":
                running.add(target)
            elif job["status"] != "completed":
                queued.add(target)
            elif job["conclusion"] not in ("cancelled", "skipped"):
                decided.add(target)
                if successful_job(job, run, runner, required_steps):
                    result[target] = (
                        f"https://github.com/{repo}/actions/runs/{run['id']}"
                        f"/job/{job['id']}")
    return result, running - decided, queued - decided


def reusable_jobs(event, targets, api=github_api, now=None,
                  workflow="zfs-qemu", wait=0, clock=time.monotonic,
                  sleep=time.sleep):
    job_name = WORKFLOWS[workflow][2]
    pr = event["pull_request"]
    repo = pr["head"]["repo"]["full_name"]
    if repo.lower() == pr["base"]["repo"]["full_name"].lower():
        return {}
    head = pr["head"]["sha"]
    if job_name is None and not targets:
        return {}

    # A push run tests the tree of its commit, so its results also apply
    # to a commit that differs only in its message or history.
    tree = api(f"repos/{repo}/git/commits/{head}")["tree"]["sha"]
    names = ({f"qemu-x86 ({target})": target for target in targets}
             if job_name is None else {job_name: workflow})
    # Wait for jobs that have no result yet, then reuse those that
    # succeeded.  A fork out of runners may not start its jobs for hours,
    # so jobs that have not started soon are not waited for.
    start = clock()
    late = set()
    while True:
        result, running, queued = find_jobs(api, repo, head, tree, workflow,
                                            names, now)
        waited = clock() - start
        if waited >= START_WAIT:
            late |= queued
        pending = " ".join(sorted((running | queued) - late))
        if not pending or waited >= wait:
            if pending and wait > 0:
                print(f"Stopped waiting for fork jobs: {pending}",
                      file=sys.stderr)
            return result
        print(f"Waiting for fork jobs: {pending}", file=sys.stderr)
        sleep(min(POLL, wait - waited))


def main():
    result = {}
    try:
        if (os.environ.get("GITHUB_EVENT_NAME") == "pull_request" and
                os.environ.get("GITHUB_RUN_ATTEMPT", "1") == "1"):
            with open(os.environ["GITHUB_EVENT_PATH"]) as stream:
                event = json.load(stream)
            wait = 60 * int(os.environ.get("REUSE_WAIT_MINUTES") or 0)
            result = reusable_jobs(
                event, json.loads(os.environ.get("ZTS_TARGETS", "[]")),
                workflow=sys.argv[1] if len(sys.argv) > 1 else "zfs-qemu",
                wait=min(wait, MAX_WAIT))
    # Whatever goes wrong, reuse nothing and run every job.
    except Exception as error:
        print(f"Cannot reuse fork CI jobs: {error}", file=sys.stderr)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
