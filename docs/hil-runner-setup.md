# HIL Runner Setup — fm\_board Bench

Manual checklist to activate the self-hosted HIL runner for FW-RemoteStation.
These steps require a GitHub PAT or admin access; they cannot be automated.

> **Bench background**: The bench has already been provisioned by the Ansible
> playbook in [OE5XRX/FW-HIL](https://github.com/OE5XRX/FW-HIL) (`ansible/site.yml`).
> The steps below wire the running box into GitHub.

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

---

## 5 — Bench prerequisites (already provisioned)

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

## 6 — First run

After completing steps 1–4, trigger the gate manually:

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
  `dialout` and `plugdev` groups; `hil` is in both. If the runner runs as a
  different user, adjust group membership accordingly.
- **Audio loopback (Baustein 8.4)** is wired into the workflow
  (`scripts/hil_audio_loopback.py`). It is a **purely digital** firmware
  loopback: `CONFIG_FM_TEST_LOOPBACK=y` routes the UAC2 OUT stream
  (host→device) straight back into the UAC2 IN stream (device→host), bypassing
  the SA818 entirely. **No ALSA loopback cable and no 12 V / RF are required** —
  the reference tone is played and captured over the board's own UAC2
  playback/capture ALSA device (`card … [FM Transceiver Board]`). The gate scores
  the capture with `fw_hil.audio_analysis.analyze_loopback` (defaults:
  correlation ≥ 0.9, SNR ≥ 20 dB, dropout-fraction ≤ 0.01).
- **The `fw_hil` bench-gate CLI entrypoint** does not yet exist;
  `scripts/hil_bench_gate.py` and `scripts/hil_audio_loopback.py` call the
  fw\_hil library directly. When FW-HIL exposes a proper CLI, update the
  invocations in `hil.yml` (marked with `# TODO`).
