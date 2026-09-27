#!/usr/bin/env bash
# runner-cleanup-hook.sh — GitHub Actions "job completed" hook for the HIL bench.
#
# Wired host-side via ACTIONS_RUNNER_HOOK_JOB_COMPLETED in the self-hosted
# runner's .env (see docs/hil-runner-setup.md), NOT from the workflow YAML — so a
# PR cannot alter or skip it. The runner invokes it AFTER every job, regardless
# of outcome. It does two things:
#
#   1. Kill stray bench processes the job may have left running — pyocd holding
#      the SWD probe, aplay/arecord holding the ALSA device, dfu-util mid
#      transfer — so the next job finds the hardware free.
#   2. Wipe the runner's _work workspace so PR-controlled files from this run do
#      not persist into the next job.
#
# Defensive + idempotent: safe when there is nothing to clean, and it refuses to
# delete anything outside the runner's _work tree. It never touches the
# persistent west workspace at /home/hil/zephyrproject.
#
# Note: -e is deliberately NOT set — cleanup must run to completion even when an
# individual step has nothing to do (e.g. pkill finds no match and exits 1).
set -uo pipefail

log() { echo "[runner-cleanup-hook] $*"; }

# ── 1. Kill stray bench processes owned by this runner user ──────────────────
# Scope every kill to the current user so we never touch another user's tools.
# pkill exits non-zero when nothing matches; that is expected, not an error.
me=$(id -un)

for proc in pyocd aplay arecord dfu-util; do
  if pkill -x -u "$me" "$proc" 2>/dev/null; then
    log "killed stray $proc"
  fi
done

# pyocd is frequently launched as `python .../pyocd ...`, which -x by exact name
# misses. Match the pyocd token in the full command line, still scoped to this
# user. The bracket in '[p]yocd' keeps pkill from matching its own arg list.
if pkill -u "$me" -f '[p]yocd' 2>/dev/null; then
  log "killed stray pyocd (python launcher form)"
fi

# ── 2. Wipe the runner _work workspace ───────────────────────────────────────
# RUNNER_WORKSPACE points at .../_work/<repo>; GITHUB_WORKSPACE at
# .../_work/<repo>/<repo>. Clean the enclosing per-repo workspace directory.
ws="${RUNNER_WORKSPACE:-${GITHUB_WORKSPACE:-}}"
if [ -z "$ws" ]; then
  log "no RUNNER_WORKSPACE/GITHUB_WORKSPACE set — nothing to wipe"
  exit 0
fi

# Canonicalize before deleting. A textual match on "$ws" is not enough: a job
# runs as this same user, so it could point a component of the workspace path at
# the persistent west workspace via a symlink. realpath resolves every symlink;
# we then require the RESOLVED path to still sit inside a runner _work tree. If
# resolution fails or the resolved path escaped _work, we refuse to delete.
ws_real=$(realpath -e "$ws" 2>/dev/null) || {
  log "cannot resolve '$ws' — refusing to wipe"
  exit 0
}
case "$ws_real" in
  */_work/*) : ;;
  *)
    log "resolved path '$ws_real' is not inside a runner _work tree — refusing to wipe"
    exit 0
    ;;
esac
# Never operate on the _work root itself or an absurdly short path.
if [ "$ws_real" = "${ws_real%%/_work/*}/_work" ] || [ "${#ws_real}" -lt 12 ]; then
  log "refusing to wipe '$ws_real' — too close to the _work root"
  exit 0
fi

if [ -d "$ws_real" ]; then
  # Delete the contents but keep the directory itself; the runner expects it to
  # exist for the next job. Children that are symlinks are removed as links
  # (rm does not follow them), so only the _work tree's own files are deleted.
  find "$ws_real" -mindepth 1 -maxdepth 1 -exec rm -rf {} + 2>/dev/null || true
  log "wiped workspace contents under $ws_real"
else
  log "workspace '$ws_real' does not exist — nothing to wipe"
fi

exit 0
