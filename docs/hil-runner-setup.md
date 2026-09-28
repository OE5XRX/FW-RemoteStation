# HIL Runner Setup — fm\_board Bench

Manual checklist to activate the self-hosted HIL runner for FW-RemoteStation.
These steps require a GitHub PAT or admin access; they cannot be automated.

> **Bench background**: The bench has already been provisioned by the Ansible
> playbook in [OE5XRX/FW-HIL](https://github.com/OE5XRX/FW-HIL) (`ansible/site.yml`).
> The steps below wire the running box into GitHub.

> ⚠️ **CRITICAL — the runner service MUST run as the `hil` user, NEVER as
> `pbuchegger`.**
>
> This gate executes **PR-controlled code** on the physical bench. The admin
> account `pbuchegger` has passwordless sudo (`NOPASSWD:ALL`) — if the runner
> ran as `pbuchegger`, any PR (including from an approved fork) would get
> **instant root** on the box. The `hil` service account is deliberately
> **sudo-less** — it holds only the hardware-access groups it needs
> (`dialout` + `plugdev` for the CDC/ST-Link udev rules, and `audio` for the
> loopback gate's ALSA access; see the group note under *Known limitations*) —
> and can read no credentials, so untrusted firmware code is confined to the
> hardware it needs and nothing more.
>
> Every `sudo -u hil -i` and every `systemctl` unit below is written for `hil` on
> purpose. Do not "fix" a permission error by switching the runner to
> `pbuchegger`.

---

## 1 — Register the self-hosted runner

**Host:** `192.168.88.67`, user `hil`

The Ansible playbook pre-creates the systemd unit and expects the runner at
`/opt/actions-runner`. Use the Ansible path to keep them consistent.

**Option A — Ansible (recommended):**
```bash
cd /home/pbuchegger/FW-HIL   # local control-node
ansible-playbook ansible/site.yml -e gh_runner_token=<TOKEN>
```
This installs the runner to `/opt/actions-runner`, configures it with the
correct labels, and enables `gh-actions-runner.service`.

**Option B — manual (if Ansible is unavailable):**

1. Go to **FW-RemoteStation → Settings → Actions → Runners → New self-hosted runner**.
2. Choose **Linux / x64**.
3. SSH to the bench and install to `/opt/actions-runner` (must match the
   pre-created service unit):
   ```bash
   ssh pbuchegger@192.168.88.67
   sudo mkdir -p /opt/actions-runner && sudo chown hil:hil /opt/actions-runner
   sudo -u hil -i
   cd /opt/actions-runner
   ```
4. Follow the GitHub-generated download + configure instructions.  
   When prompted for labels, enter exactly:
   ```
   self-hosted,hil,fm_board
   ```
5. Start the runner via the pre-created systemd unit:
   ```bash
   sudo systemctl enable --now gh-actions-runner.service
   ```

---

## 2 — Bootstrap the west workspace (one-time, as `hil`)

The Ansible playbook provisions the SDK, venv, and `fw_hil`, but **does not**
create the west workspace. This must be done once on a fresh bench before the
runner can build firmware.

```bash
sudo -u hil -i
west init -m https://github.com/OE5XRX/FW-RemoteStation /home/hil/zephyrproject
cd /home/hil/zephyrproject
west update
```

After `west update`, apply the downstream patches once to verify they apply
cleanly:
```bash
west patch apply
```

The workspace persists between CI runs; subsequent runs only run `git fetch` +
`git checkout` to sync the manifest repo to the PR commit (no `west update`
needed unless the manifest's `west.yml` changes).

---

## 3 — Require approval for fork-PR workflows

Prevents untrusted fork code from running on the physical board without human
review.

1. Go to **FW-RemoteStation → Settings → Actions → General**.
2. Under *Fork pull request workflows*, select:
   **"Require approval for all outside collaborators"**.
3. Save.

---

## 4 — Create the `hil-ok` label

The `hil.yml` workflow fires only when this label is present on a PR, giving
maintainers an explicit gate before code touches the board.

1. Go to **FW-RemoteStation → Issues → Labels → New label**.
2. Name: `hil-ok`
3. Description: `Activates the HIL bench CI gate for this PR`
4. Color: choose something visible (e.g. `#0075ca`)
5. Save.

> **One-shot approval:** the gate automatically **removes** `hil-ok` at the end of
> every run (pass or fail) — see the `remove-hil-ok-label` job in `hil.yml`. So a
> present label always means "approved to run right now". To run the bench again,
> a maintainer must consciously re-add the label **after re-reviewing the diff**
> (including any change to `.github/workflows/` and `scripts/`).

---

## 5 — Wire the job-cleanup hook (one-time, host-side)

After each job the runner should wipe its `_work` workspace and kill any stray
bench processes (a `pyocd` still holding the SWD probe, `aplay`/`arecord` holding
the ALSA device, a `dfu-util` mid-transfer) so the next job starts from a clean
slate and the hardware is free. This is wired **host-side** via a root-owned
systemd drop-in that sets `ACTIONS_RUNNER_HOOK_JOB_COMPLETED` — **not** from the
workflow YAML — so a PR cannot alter or skip it.

> **Scope of this hook:** it is **defense-in-depth hygiene**, not the privilege
> boundary. The real containment is that `hil` has no sudo, no readable
> credentials, and (planned) network isolation — see the top-of-file warning and
> the security notes below. Even if a determined job managed to neutralize the
> hook, the blast radius is limited to *leftover files / stray processes* for the
> next run, which the SHA-pin, one-shot `hil-ok` re-review, and clean rebuild
> (`west build -p always`) already defend against. Harden it as far as the runner
> tree's ownership allows, but do not treat it as the thing standing between a PR
> and the host.

The hook script lives in this repo at
[`scripts/runner-cleanup-hook.sh`](../scripts/runner-cleanup-hook.sh). It is
defensive and idempotent: it only ever deletes inside a **resolved** runner
`_work` tree (it `realpath`-canonicalizes the workspace and refuses if it escaped
`_work`, defeating symlink redirection) and never touches the persistent west
workspace at `/home/hil/zephyrproject`.

> ⚠️ **Install the hook root-owned and NOT writable by `hil`.** The hook runs as
> the runner user (`hil`), which also executes PR-controlled build code. If the
> hook lived in a `hil`-writable path, a PR could overwrite it during the job and
> make the runner execute attacker-controlled cleanup (or none) afterwards —
> defeating the whole point. Install it (and its parent directory) `root:root`,
> mode `0755`, so `hil` can execute but not modify it.

1. As an admin, **review** `scripts/runner-cleanup-hook.sh` in the reviewed
   commit, then install it to a root-owned path (outside `/opt/actions-runner` so
   it survives runner auto-updates):
   ```bash
   sudo install -d -o root -g root -m 0755 /opt/runner-hooks
   sudo install -o root -g root -m 0755 \
     /home/hil/zephyrproject/FW-RemoteStation/scripts/runner-cleanup-hook.sh \
     /opt/runner-hooks/runner-cleanup-hook.sh
   ```
   (`hil` can read/execute `/opt/runner-hooks/runner-cleanup-hook.sh` but, since
   both the file and its parent are root-owned, cannot overwrite or replace it.)
2. Wire the hook via a **root-owned systemd drop-in**, not the runner's `.env`.
   The `.env` file lives under `/opt/actions-runner`, which the FW-HIL playbook
   owns as `hil` — the same account that runs PR code — so a job could rewrite or
   delete an `ACTIONS_RUNNER_HOOK_JOB_COMPLETED` line there and bypass the hook.
   A drop-in under `/etc/systemd` is root-owned and outside the runner tree, so
   neither PR code nor a runner auto-update can touch it:
   ```bash
   sudo install -d -m 0755 /etc/systemd/system/gh-actions-runner.service.d
   sudo tee /etc/systemd/system/gh-actions-runner.service.d/10-cleanup-hook.conf >/dev/null <<'UNIT'
   [Service]
   Environment=ACTIONS_RUNNER_HOOK_JOB_COMPLETED=/opt/runner-hooks/runner-cleanup-hook.sh
   UNIT
   sudo systemctl daemon-reload
   ```
3. Close the `.env` override path. The runner sources `.env` at start, so a
   `.env` that sets `ACTIONS_RUNNER_HOOK_JOB_COMPLETED` could win over the
   drop-in. **Root-owning the `.env` file is not sufficient by itself:** on Unix,
   the right to unlink and re-create a file comes from write permission on its
   *directory*, so while `/opt/actions-runner` is owned/writable by `hil`, a job
   can delete the root-owned `.env` and drop in its own. Closing this properly
   means the runner root directory must **not** be `hil`-writable:
   ```bash
   # Make the runner root root-owned; grant hil only the subdirs it must write.
   sudo chown root:root /opt/actions-runner
   sudo chmod 0755 /opt/actions-runner
   sudo chown -R hil:hil /opt/actions-runner/_work /opt/actions-runner/_diag
   # Provide an empty, root-owned .env so nothing overrides the drop-in.
   sudo install -o root -g root -m 0644 /dev/null /opt/actions-runner/.env
   ```
   Confirm `hil` cannot recreate it: `sudo -u hil touch /opt/actions-runner/.env`
   must fail with *Permission denied*.
   > **FW-HIL playbook follow-up (required for this to survive re-provisioning):**
   > the playbook currently `chown -R hil`s all of `/opt/actions-runner`, which
   > re-opens this path on the next run. Change it to keep the runner root
   > `root`-owned and grant `hil` only `_work` (and `_diag`). Verify the runner
   > service still starts and self-updates under that ownership; if a specific
   > runner state file needs to be `hil`-writable, narrow the grant to that file
   > rather than the whole tree. Until the playbook is fixed, re-apply the
   > ownership above after every provisioning run.
4. Restart the runner so it picks up the drop-in:
   ```bash
   sudo systemctl restart gh-actions-runner.service
   ```

Verify: after the next job, the runner log shows `[runner-cleanup-hook]` lines
and `_work/FW-RemoteStation` is empty. Confirm the wiring is root-protected with
`systemctl show gh-actions-runner.service -p Environment | grep HOOK`.

---

## 6 — Bench prerequisites (already provisioned)

The following are already in place on `192.168.88.67` after the Ansible run.
Listed here for reference / re-provisioning:

| Item | Path / value |
|------|--------------|
| fw\_hil editable install | `/opt/fw-hil` (src from `FW-HIL` main) |
| Python venv | `/opt/fw-hil-venv` (west, pyocd, pyserial, pyusb) |
| udev rule — CDC ACM | `/dev/fm-board-cdc` → VID `2fe3`, PID `0012` |
| udev rule — ST-Link | `/dev/stlink-hil` → VID `0483`, PID `3748` |
| Board iSerial | `2031394D3646500E004B004F` |
| ST-Link probe serial | `35FF70064D4D323837380843` |
| Zephyr west workspace | `/home/hil/zephyrproject` (FW-RemoteStation + Zephyr kernel) |
| Zephyr SDK | `/opt/zephyr-sdk-1.0.1` (ARM cross-toolchain) |
| pyocd U5 pack | `Keil.STM32U5xx_DFP` (installed in hil home cache) |
| hardware-map | `/etc/fw-hil/hardware-map.yaml` (hil-owned) |
| Board USB | `2fe3:0012` (fm\_board, USB composite: UAC2 + CDC + DFU) |

Refer to `ansible/site.yml` and `ansible/README.md` in FW-HIL for
re-provisioning steps.

---

## 7 — First run

After completing steps 1–6, trigger the gate manually:

1. Go to **FW-RemoteStation → Actions → HIL Bench Gate**.
2. Click **Run workflow** → **Run**.

The first run should complete all bench steps:
- SWD flash baseline (via west + pyocd)
- DFU update cycle (firmware self-reboot + MCUboot swap)
- DFU revert cycle (unhealthy image → MCUboot reverts)
- USB composite assert (VID `2fe3:0012`, UAC2 + CDC + DFU)
- Audio loopback gate — builds the `CONFIG_FM_TEST_LOOPBACK=y` variant,
  flashes it over SWD, and scores an internal UAC2 OUT→IN loopback
  (correlation / SNR / dropout thresholds)

---

## Known limitations (to be resolved with future work)

- **`dfu-util` and `pyusb` require USB access** — the udev rules grant the
  `dialout` and `plugdev` groups; `hil` must be in both. If the runner runs as a
  different user, adjust group membership accordingly.
- **The audio-loopback gate needs the `audio` group** — `aplay`/`arecord` open
  the board's ALSA device, which requires `hil` to be in the `audio` group. The
  FW-HIL Ansible playbook currently provisions only `dialout` + `plugdev`, so on
  a freshly re-provisioned box add `audio` explicitly
  (`sudo usermod -aG audio hil` and re-login the service), or the loopback step
  fails with an ALSA permission error. Folding `audio` into the playbook's runner
  groups is a FW-HIL follow-up.
- **Audio loopback (Baustein 8.4)** is wired into the workflow
  (`scripts/hil_audio_loopback.py`). It is a **purely digital** firmware
  loopback: `CONFIG_FM_TEST_LOOPBACK=y` routes the UAC2 OUT stream
  (host→device) straight back into the UAC2 IN stream (device→host), bypassing
  the SA818 entirely. **No ALSA loopback cable and no 12 V / RF are required** —
  the reference tone is played and captured over the board's own UAC2
  playback/capture ALSA device (`card … [FM Transceiver Board]`). The gate scores
  the capture with `fw_hil.audio_analysis.analyze_loopback` (defaults:
  correlation ≥ 0.9, SNR ≥ 20 dB, dropout-fraction ≤ 0.01).
- **Persistent west workspace is not reset per job** — for rebuild speed the
  bench reuses `/home/hil/zephyrproject`; only the manifest repo is re-checked-out
  to the PR commit (`west update` runs only when `west.yml` changes, which the
  gate detects and fails on). The app is always rebuilt clean (`west build -p
  always`), but PR-controlled build steps *could* mutate a Zephyr/module tree and
  that contamination would carry into the next run. This is an accepted tradeoff
  of the persistent-workspace model; the one-shot `hil-ok` gate (a maintainer
  re-reviews the diff before every run) is the compensating control. A fully
  disposable per-job workspace would remove the risk at the cost of a full
  `west update` each run — a possible future hardening.
- **The `fw_hil` bench-gate CLI entrypoint** does not yet exist;
  `scripts/hil_bench_gate.py` and `scripts/hil_audio_loopback.py` call the
  fw\_hil library directly. When FW-HIL exposes a proper CLI, update the
  invocations in `hil.yml` (marked with `# TODO`).
