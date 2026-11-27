#!/usr/bin/env python3

"""Find successful jobs of earlier runs that tested the same tree."""

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
# make checkstyle also checks the message of the head commit (commitcheck).
SAME_MESSAGE = {"checkstyle"}
# The CI type selects the OS list and test suite of a QEMU PR run.  Setup
# records it in the name of a step, which the jobs API returns.
CI_TYPE_STEP = "CI type: "
MAX_AGE = datetime.timedelta(days=7)
# Push runs of one commit to consider, e.g. after pushing it to a PR branch,
# and PR runs of one branch.
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


def usable_run(run, repo, event, head, filename, now):
    created = datetime.datetime.fromisoformat(
        run["created_at"].replace("Z", "+00:00"))
    return (run["head_commit"] is not None and
            run["head_commit"]["tree_id"] == head["tree"] and
            (head["message"] is None or
             head["message"] == run["head_commit"]["message"]) and
            run["event"] == event and
            run["path"] == f".github/workflows/{filename}" and
            run["repository"]["full_name"].lower() == repo.lower() and
            run["head_repository"]["full_name"].lower() ==
            head["repo"].lower() and
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


def tested_nothing(job, required_steps):
    """Whether a job skipped all its work, as one reusing a job does."""
    steps = {step["name"]: step["conclusion"] for step in job["steps"]}
    return all(steps.get(name) == "skipped" for name in required_steps)


def find_jobs(api, queries, head, workflow, names, now, ci_type,
              this_run):
    """Return reusable jobs, and the targets of running and queued jobs."""
    filename, runner, job_name, required_steps = WORKFLOWS[workflow]
    # QEMU push runs use full tests, even with ZTS_OS_OVERRIDE. Dispatch
    # runs can use different inputs.
    now = now or datetime.datetime.now(datetime.timezone.utc)
    runs = {}
    for repo, event, params in queries:
        query = urllib.parse.urlencode({"event": event, **params})
        for run in api(f"repos/{repo}/actions/workflows/{filename}/runs"
                       f"?{query}")["workflow_runs"]:
            if (run["id"] != this_run and
                    usable_run(run, repo, event, head, filename, now)):
                runs[run["id"]] = run
    result = {}
    decided = set()
    running = set()
    queued = set()
    # Runs are newest first.  The newest finished result for each job
    # decides it: a success is reused, a failure is not, and an older
    # success never replaces it.  A job that was cancelled, skipped or has
    # not finished yet (for example in the run started by pushing an
    # already tested commit to the PR branch) has no result, so an older
    # run of the same tree can still supply one.  So has a job that only
    # reused another: the search goes on to the job it reused.
    for run in sorted(runs.values(), reverse=True,
                      key=lambda run: (run["created_at"], run["id"])):
        if len(decided) == len(names):
            break
        jobs = api(f"repos/{run['repository']['full_name']}/actions/runs/"
                   f"{run['id']}/jobs?filter=latest&per_page=100")
        # A truncated matrix cannot establish which results are current.
        if jobs["total_count"] > len(jobs["jobs"]):
            break
        # A PR run of QEMU jobs counts only with the same CI type.  The new
        # run of a PR branch cancels its earlier runs, so jobs of those that
        # have not finished are not waited for.
        pr_run = run["event"] == "pull_request"
        if pr_run and job_name is None and not any(
                step["name"] == f"{CI_TYPE_STEP}{ci_type}"
                for job in jobs["jobs"] for step in job["steps"]):
            continue
        matrix = [(names[job["name"]], job) for job in jobs["jobs"]
                  if job["name"] in names and
                  (job["status"] == "completed" or not pr_run)]
        # A run lists its QEMU jobs only once its Setup job is done.
        if not matrix and run["status"] != "completed" and not pr_run:
            queued.update(names.values())
        for target, job in matrix:
            if target in decided:
                continue
            if job["status"] == "in_progress":
                running.add(target)
            elif job["status"] != "completed":
                queued.add(target)
            elif (job["conclusion"] not in ("cancelled", "skipped") and
                  not tested_nothing(job, required_steps)):
                decided.add(target)
                if successful_job(job, run, runner, required_steps):
                    result[target] = f"{run['html_url']}/job/{job['id']}"
    return result, running - decided, queued - decided


def push_queries(head):
    """Push runs of the head commit, and the latest push runs."""
    return [(head["repo"], "push", {"head_sha": head["sha"],
                                    "per_page": MAX_RUNS}),
            (head["repo"], "push", {"per_page": RECENT_RUNS})]


def reusable_jobs(event_name, event, targets, api=github_api, now=None,
                  workflow="zfs-qemu", wait=0, clock=time.monotonic,
                  sleep=time.sleep, ci_type=None, this_run=None):
    job_name = WORKFLOWS[workflow][2]
    if job_name is None and not targets:
        return {}
    # A PR run looks at the earlier runs of its branch and at the push runs
    # of its fork, a push run at the push runs of its repository.
    if event_name == "pull_request":
        pr = event["pull_request"]
        repo = pr["base"]["repo"]["full_name"]
        head = {"repo": pr["head"]["repo"]["full_name"],
                "sha": pr["head"]["sha"]}
        queries = [(repo, "pull_request",
                    {"branch": pr["head"]["ref"], "per_page": MAX_RUNS})]
        if head["repo"].lower() != repo.lower():
            queries += push_queries(head)
    else:
        head = {"repo": event["repository"]["full_name"],
                "sha": event["after"]}
        queries = push_queries(head)

    # A run tests the tree of its commit, so its results also apply to a
    # commit that differs only in its message or history.
    commit = api(f"repos/{head['repo']}/git/commits/{head['sha']}")
    head["tree"] = commit["tree"]["sha"]
    head["message"] = (commit["message"] if workflow in SAME_MESSAGE
                       else None)
    names = ({f"qemu-x86 ({target})": target for target in targets}
             if job_name is None else {job_name: workflow})
    # Wait for jobs that have no result yet, then reuse those that
    # succeeded.  A fork out of runners may not start its jobs for hours,
    # so jobs that have not started soon are not waited for.
    start = clock()
    late = set()
    while True:
        result, running, queued = find_jobs(api, queries, head, workflow,
                                            names, now, ci_type, this_run)
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
    event_name = os.environ.get("GITHUB_EVENT_NAME")
    try:
        if (event_name in ("pull_request", "push") and
                os.environ.get("GITHUB_RUN_ATTEMPT", "1") == "1"):
            with open(os.environ["GITHUB_EVENT_PATH"]) as stream:
                event = json.load(stream)
            # Only a PR run waits, for the push runs of its fork: two push
            # runs of one tree would wait for each other.
            wait = 0
            if event_name == "pull_request":
                wait = 60 * int(os.environ.get("REUSE_WAIT_MINUTES") or 0)
            result = reusable_jobs(
                event_name, event,
                json.loads(os.environ.get("ZTS_TARGETS", "[]")),
                workflow=sys.argv[1] if len(sys.argv) > 1 else "zfs-qemu",
                wait=min(wait, MAX_WAIT), ci_type=os.environ.get("CI_TYPE"),
                this_run=int(os.environ["GITHUB_RUN_ID"]))
    # Whatever goes wrong, reuse nothing and run every job.
    except Exception as error:
        print(f"Cannot reuse CI jobs: {error}", file=sys.stderr)
    print(json.dumps(result))


if __name__ == "__main__":
    main()
