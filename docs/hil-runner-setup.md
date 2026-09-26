# HIL Runner Setup — fm\_board Bench

Manual checklist to activate the self-hosted HIL runner for FW-RemoteStation.
These steps require a GitHub PAT or admin access; they cannot be automated.

> **Bench background**: The bench has already been provisioned by the Ansible
> playbook in [OE5XRX/FW-HIL](https://github.com/OE5XRX/FW-HIL) (`ansible/site.yml`).
> The steps below wire the running box into GitHub.

---

## 1 — Register the self-hosted runner

**Host:** `192.168.88.67`, user `hil`

1. Go to **FW-RemoteStation → Settings → Actions → Runners → New self-hosted runner**.
2. Choose **Linux / x64**.
3. SSH to the bench as `pbuchegger` (or `hil`):
   ```bash
   ssh pbuchegger@192.168.88.67
   sudo -u hil -i
   cd /home/hil
   ```
4. Follow the GitHub-generated download + configure instructions.  
   When prompted for labels, enter exactly:
   ```
   self-hosted,fm-board-bench
   ```
5. Start the runner as a systemd service (the Ansible playbook already placed
   the unit at `/etc/systemd/system/gh-actions-runner.service`):
   ```bash
   sudo systemctl enable --now gh-actions-runner.service
   ```
   Alternatively, re-run the Ansible playbook with the token:
   ```bash
   cd /home/pbuchegger/FW-HIL   # local control-node
   ansible-playbook ansible/site.yml -e gh_runner_token=<TOKEN>
   ```

---

## 2 — Require approval for fork-PR workflows

Prevents untrusted fork code from running on the physical board without human
review.

1. Go to **FW-RemoteStation → Settings → Actions → General**.
2. Under *Fork pull request workflows*, select:
   **"Require approval for all outside collaborators"**.
3. Save.

---

## 3 — Create the `hil-ok` label

The `hil.yml` workflow fires only when this label is present on a PR, giving
maintainers an explicit gate before code touches the board.

1. Go to **FW-RemoteStation → Issues → Labels → New label**.
2. Name: `hil-ok`
3. Description: `Activates the HIL bench CI gate for this PR`
4. Color: choose something visible (e.g. `#0075ca`)
5. Save.

---

## 4 — Bench prerequisites (already provisioned)

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

## 5 — First run

After completing steps 1–3, trigger the gate manually:

1. Go to **FW-RemoteStation → Actions → HIL Bench Gate**.
2. Click **Run workflow** → **Run**.

The first run should complete all three bench steps:
- SWD flash baseline (via west + pyocd)
- DFU update cycle (firmware self-reboot + MCUboot swap)
- DFU revert cycle (unhealthy image → MCUboot reverts)
- USB composite assert (VID `2fe3:0012`, UAC2 + CDC + DFU)

---

## Known limitations (to be resolved with future work)

- **`dfu-util` and `pyusb` require USB access** — the udev rules grant the
  `dialout` and `plugdev` groups; `hil` is in both. If the runner runs as a
  different user, adjust group membership accordingly.
- **Audio loopback (Baustein 8.4)** is disabled in the workflow (`if: false`).
  It will be enabled once the ALSA loopback cable is wired on the bench.
- **The `fw_hil` bench-gate CLI entrypoint** does not yet exist; `scripts/hil_bench_gate.py`
  calls the fw\_hil library directly. When FW-HIL exposes a proper CLI, update
  the invocation in `hil.yml` (marked with `# TODO`).
