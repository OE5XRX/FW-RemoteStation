#!/usr/bin/env bash
# runner-cleanup-hook.sh — GitHub Actions "job completed" hook for the HIL bench.
#
# Wired host-side via ACTIONS_RUNNER_HOOK_JOB_COMPLETED in a root-owned systemd
# drop-in on the runner service (see docs/hil-runner-setup.md), NOT from the
# workflow YAML and NOT from the hil-writable .env — so a PR cannot alter or skip
# it. The runner invokes it AFTER every job, regardless of outcome. It does two
# things:
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

# Job-completed hooks inherit the (PR-influenced) job environment. A PR can
# prepend a workspace directory to PATH via GITHUB_PATH, which would make an
# unqualified `id`/`pkill`/`realpath`/`find`/`rm` resolve to an attacker-planted
# lookalike. Pin PATH to trusted system directories before calling any helper.
export PATH=/usr/local/sbin:/usr/local/bin:/usr/sbin:/usr/bin:/sbin:/bin

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

# Canonicalize and require containment under the ACTUAL runner work root, not
# merely "some path with a _work component" (which would match e.g.
# /tmp/attacker/_work/x). Two protections at once:
#   * realpath resolves every symlink, so a symlinked path component cannot
#     redirect the delete at the persistent west workspace (the job runs as this
#     same user and could plant such a symlink).
#   * we then require the resolved workspace to be a strict descendant of the
#     resolved runner work root.
# The runner work root defaults to /opt/actions-runner/_work (the documented
# install path); override with RUNNER_CLEANUP_WORK_ROOT if the runner lives
# elsewhere.
work_root_real=$(realpath -e "${RUNNER_CLEANUP_WORK_ROOT:-/opt/actions-runner/_work}" 2>/dev/null) || {
  log "cannot resolve runner work root — refusing to wipe"
  exit 0
}
ws_real=$(realpath -e "$ws" 2>/dev/null) || {
  log "cannot resolve '$ws' — refusing to wipe"
  exit 0
}
if [ "$ws_real" = "$work_root_real" ]; then
  log "refusing to wipe the runner work root '$ws_real' itself"
  exit 0
fi
case "$ws_real/" in
  "$work_root_real"/*) : ;;
  *)
    log "resolved path '$ws_real' is not under runner work root '$work_root_real' — refusing to wipe"
    exit 0
    ;;
esac

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
