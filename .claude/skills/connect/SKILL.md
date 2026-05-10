---
name: connect
description: Establish a working SSH connection to the H15 SBC over ethernet. Use when the user asks to connect to the board or set up board access. Default is root@10.0.0.1, password `root`.
tools: Bash, Read
---

# /connect — bring up board connectivity

Get the user to a state where `ssh root@10.0.0.1` works over ethernet. The board's default IP is `10.0.0.1` on the `10.0.0.0/24` subnet; the host should be `10.0.0.2`. Default credentials are `root` / `root`.

## Steps

1. **Probe the board.**
   ```bash
   ssh -o ConnectTimeout=3 -o StrictHostKeyChecking=no root@10.0.0.1 'uname -a; cat /etc/os-release'
   ```
   At the prompt, enter password `root`. `uname -a` returns the kernel string and `/etc/os-release` shows the Hailo SW version (e.g. `1.11.0`) — the H15L SBC quickstart §2.3 uses os-release as the canonical version check. Save the IP for the session.

   If it hangs, returns `No route to host`, or `Connection refused`, go to step 2.

2. **Check ethernet link and host IP.**
   - Confirm the cable is plugged into the SBC's RJ45 connector and the host's ethernet port.
   - `ip -br addr` — the host needs an interface with `10.0.0.2/24`. If not, go to step 3.
   - `ping -c 2 10.0.0.1` — board reachable?
   - If pingable but ssh refuses, the daemon may not be up yet — wait for boot to finish (green LED on, ~10s after power-on).

3. **Set the host's static IP** (only if `ip -br addr` shows no `10.0.0.x` interface). Either the GUI or `nmcli` works — both edit the same NetworkManager connection and persist across reboots.

   **CLI:**
   ```bash
   # find the wired connection name (usually "Wired connection 1")
   nmcli -t -f NAME,TYPE connection show | grep ethernet

   # apply manual IPv4 (replace <name> with the value from above)
   nmcli connection modify "<name>" ipv4.method manual ipv4.addresses 10.0.0.2/24 ipv4.gateway ""
   nmcli connection up "<name>"
   ```

   **GUI:** GNOME Settings → Network → Wired (gear icon) → IPv4 → Manual, Address `10.0.0.2`, Netmask `255.255.255.0`, Gateway empty → Apply.

   Then re-probe.

## Gotchas

- The H15L SBC reports as `Linux hailo15l … aarch64` from `uname -a`. The H15H reports `hailo15h`. Use this to confirm board type.

## What success looks like

`ssh root@10.0.0.1 'echo OK'` returns `OK` (after entering password `root`). Report the board IP and `uname -a` back to the user.
