# oneUpPower — Battery Driver

A Linux kernel module for the Argon ONE UP laptop's battery controller. It
communicates with the CW2217 fuel gauge IC over I2C and integrates with the
kernel's power supply framework, exposing battery state (capacity, status,
charge, voltage, current, power, temperature, cycle count and health) and AC
power state through the standard sysfs interface at
`/sys/class/power_supply/`. A delayed work item polls the hardware once per
second and triggers a graceful system shutdown when the charge drops below a
configurable threshold while unplugged.

At probe time the driver verifies the 80-byte battery model (OCV) profile
stored in the CW2217 and reprograms it if it is missing or differs from the
known-good profile for the ONE UP pack; without it the IC's state-of-charge
readings are inaccurate near full and empty.

The driver uses the standard Linux I2C driver model. The kernel binds it to
the hardware automatically at boot through a Device Tree overlay that declares
the battery IC on `i2c1` at address `0x64` (compatible string
`argon40,oneup-battery`; the overlay targets `raspberrypi,5-compute-module`,
since the ONE UP is a CM5 carrier). Run `sudo ./setup` to compile everything
and deploy it; a reboot is required to activate the overlay.

> [!IMPORTANT]
> If you have an older version of the driver installed, run `sudo ./remove`
> before running `sudo ./setup`.

---

## Setup

Run the unified setup script once as root. It installs build dependencies,
compiles the module and Device Tree overlay, registers the driver with DKMS,
and loads the module immediately:

```bash
sudo ./setup
```

The script is safe to re-run — DKMS registration is torn down and rebuilt
cleanly each time, and idempotency guards prevent duplicate entries in
`/etc/modules` and `/boot/firmware/config.txt`.

### What setup does

| Step | Action |
|------|--------|
| 1 | Detects the OS and installs required packages |
| 2 | Compiles `oneUpPower.ko` against the running kernel's headers |
| 3 | Compiles `argon-oneup-battery.dtbo` from the DTS source |
| 4 | Registers, builds, and installs the module via DKMS |
| 5 | Copies the overlay to `/boot/firmware/overlays/` |
| 6 | Appends `dtoverlay=argon-oneup-battery` to `/boot/firmware/config.txt` |
| 7 | Configures module autoload (`/etc/modules` or `/etc/modules-load.d/`) |
| 8 | Writes the default modprobe config to `/etc/modprobe.d/oneUpPower.conf` |
| 9 | Loads the module immediately with `modprobe` |
| 10 | If the overlay is not active yet, instantiates the I2C device (`oneup-battery 0x64` on `i2c-1`) so `probe()` runs without waiting for a reboot |

With DKMS the module is rebuilt automatically on every kernel update — no
manual reinstall required (the DKMS package is `oneUpPower/1.0`, installed to
`/lib/modules/<kernel>/kernel/drivers/power/supply/`). The Device Tree
overlay is unaffected by kernel updates and does not need to be reinstalled.

**A reboot is required** for the firmware to merge the overlay into the Device
Tree. After rebooting the kernel calls `probe()` automatically and `BAT0` and
`AC0` appear in `/sys/class/power_supply/`.

### Packages installed per OS

| OS | Packages |
|----|----------|
| Raspberry Pi OS / Debian | `build-essential`, `dkms`, `device-tree-compiler`, plus the kernel headers metapackage for the running kernel (e.g. `linux-headers-rpi-2712` on Pi 5 / CM5) |
| Ubuntu | `build-essential`, `linux-headers-<uname -r>`, `dkms`, `device-tree-compiler` |
| Fedora / RHEL family | `gcc`, `make`, `dkms`, `dtc`, kernel devel matching the running kernel |
| Alpine | `build-base`, `linux-dev`, `dtc` |

> [!NOTE]
> On Raspberry Pi OS the kernel headers must track kernel updates or DKMS
> will skip the rebuild. `setup` installs the headers *metapackage* for the
> running kernel's flavor (e.g. `linux-headers-rpi-2712` for a
> `*+rpt-rpi-2712` kernel) rather than only the versioned headers, so future
> kernel updates pull in matching headers automatically.

On Fedora Raspberry Pi systems the platform-specific devel package is
selected automatically — e.g. `kernel-rpi5-devel-$(uname -r)` on Pi 5.

On Fedora add `dtparam=i2c_arm=on` to `/boot/config.txt` for battery state
to be reported correctly.

### Cleaning build artefacts

```bash
sudo ./setup clean
```

Removes `oneUpPower.ko`, intermediate object files, and
`argon-oneup-battery.dtbo` from the source directory.

---

## Expected output after probe

```
oneup-battery: Changing automatic shutdown when battery is below 5%
oneup-battery 1-0064: Checking battery profile...
oneup-battery 1-0064: Battery profile is valid.
oneup-battery 1-0064: AC Power is connected at boot.
oneup-battery 1-0064: Battery State of charge is 98%
oneup-battery 1-0064: Probe successful (v1.0.9)
```

If the stored profile is missing or wrong you will instead see
`Programming battery profile...` followed by `Battery IC activated.` and
`Battery profile updated successfully.`

The module file is `oneUpPower.ko`, but the I2C driver (and therefore its log
prefix) is named `oneup-battery`. Watch in real time with:

```bash
sudo dmesg -w | grep oneup-battery
```

---

## Remove

```bash
sudo ./remove
```

Fully uninstalls the driver. Specifically:

1. Removes the DKMS registration with `dkms remove`
2. Unloads the module with `rmmod`
3. Removes the `.ko` from `/lib/modules/`
4. Removes the `oneUpPower` autoload entry from `/etc/modules` or `/etc/modules-load.d/`
5. Removes `/etc/modprobe.d/oneUpPower.conf`
6. Removes `argon-oneup-battery.dtbo` from `/boot/firmware/overlays/`
7. Removes the `dtoverlay=argon-oneup-battery` line from `/boot/firmware/config.txt`

**A reboot is required** for the firmware to stop applying the overlay.

---

## Kconfig (in-tree build)

When building the driver as part of the kernel source tree, enable it with:

```
CONFIG_BATTERY_ONEUP=m
```

The `Kconfig` file contains the full entry. The `Makefile` supports both the
in-tree `obj-$(CONFIG_BATTERY_ONEUP)` path and the out-of-tree `obj-m` path
used by DKMS.

---

## Argon40 service conflict

The `argononeupd.service` daemon shipped by Argon40 also attempts to manage
battery reporting. Running both at the same time causes conflicts. Disable it
before using this driver:

```bash
sudo systemctl disable --now argononeupd.service
```

---

## Settings

Both settings are module parameters, readable and writable at runtime under
`/sys/module/oneUpPower/parameters/`.

### `soc_shutdown` — low-battery shutdown threshold

Controls the state-of-charge percentage at which the driver initiates an
automatic shutdown when the system is running on battery power alone. The
default is `5`.

The setting is stored in `/etc/modprobe.d/oneUpPower.conf`:

```
options oneUpPower soc_shutdown=5
```

Edit the file and reload the module to change it persistently, or write
directly to the sysfs parameter file to change it while the module is running:

```bash
echo 10 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
```

| Value | Meaning |
|-------|---------|
| `0` | Disable automatic shutdown entirely |
| `1`–`20` | Shut down when battery drops below this percentage and AC is disconnected |

Values outside this range are rejected. A shutdown is triggered only when AC
power is disconnected **and** the reported state of charge falls below the
threshold. The driver calls `orderly_poweroff()` once; it will not re-trigger
until the module is reloaded.

### `ac_debounce_polls` — AC state debounce

Number of consecutive one-second polls (default `3`, clamped to `1`–`10`)
that must agree before the driver reports an AC connect/disconnect. This
filters out brief glitches in the current-direction bit. AC state is read
directly at probe time, so the initial value is correct without waiting for
the debounce.

```bash
echo 2 | sudo tee /sys/module/oneUpPower/parameters/ac_debounce_polls
```

To make it persistent, add it to `/etc/modprobe.d/oneUpPower.conf`:

```
options oneUpPower soc_shutdown=5 ac_debounce_polls=2
```

---

## System interfaces

The driver registers two power supply devices with the kernel:

### `BAT0` — battery

Located at `/sys/class/power_supply/BAT0/`. Values are cached by the 1-second
poll loop; reading sysfs never touches the I2C bus. Units follow the kernel
power-supply ABI (µV, µA, µW, µAh, tenths of °C, seconds).

| Attribute | Source | Description |
|-----------|--------|-------------|
| `status` | derived | `Charging`, `Discharging`, or `Full` (AC connected and capacity > 95%) |
| `capacity` | `0x04` | State of charge, 0–100% |
| `capacity_level` | derived | `Full`, `High`, `Normal`, `Low`, or `Critical` (see below) |
| `charge_type` | derived | `Fast` while charging, otherwise `N/A` |
| `health` | derived | `Good`, `Overheat` (> 55 °C), `Cold` (< 0 °C), or `Dead` (SOH < 40%) |
| `present` | fixed | Always `1` |
| `technology` | fixed | `Li-ion` |
| `cycle_count` | `0xA4`–`0xA5` | Full charge cycles counted by the IC |
| `voltage_now` | `0x02`–`0x03` | Pack voltage: cell voltage (312.5 µV/LSB) × 3 cells in series |
| `voltage_min_design` | fixed | 9,900,000 µV (3 × 3.3 V) |
| `voltage_max_design` | fixed | 12,600,000 µV (3 × 4.2 V) |
| `current_now` | `0x0E`–`0x0F` | Battery current in µA; positive = charging, negative = discharging (10 mΩ sense resistor) |
| `power_now` | derived | `voltage_now` × \|`current_now`\|, in µW |
| `temp` | `0x06` | Temperature in tenths of °C (−40 °C + 0.5 °C/LSB) |
| `charge_full_design` | fixed | 4,800,000 µAh (4,800 mAh) |
| `charge_full` | `0xA6` | Design capacity scaled by the IC's state of health (SOH). Until the IC reports a SOH estimate, 100% is assumed |
| `charge_now` | `0x04`–`0x05` | `charge_full` × fractional SOC (1/256% resolution) |
| `charge_empty` | fixed | `0` |
| `time_to_empty_avg` | derived | Seconds remaining while discharging: remaining charge ÷ \|current\|; falls back to a 6 h full-pack estimate if current is below 50 mA. `0` when not discharging |
| `time_to_full_now` | derived | Seconds to full while charging: missing charge ÷ current; falls back to a 2.5 h full-charge estimate if current is below 50 mA. `0` when not charging |
| `current_standby` | `0xA8`–`0xA9` | Driver-specific attribute: the IC's standby/quiescent current reading in µA |
| `manufacturer` | fixed | `Argon40` |
| `model_name` | fixed | `oneUp Battery` |

Example output on a discharging system:

```
$ grep . /sys/class/power_supply/BAT0/{status,capacity,voltage_now,current_now,power_now,cycle_count,time_to_empty_avg}
/sys/class/power_supply/BAT0/status:Discharging
/sys/class/power_supply/BAT0/capacity:94
/sys/class/power_supply/BAT0/voltage_now:12602811
/sys/class/power_supply/BAT0/current_now:-814592
/sys/class/power_supply/BAT0/power_now:10266149
/sys/class/power_supply/BAT0/cycle_count:38
/sys/class/power_supply/BAT0/time_to_empty_avg:19940
```

Capacity level thresholds:

| `capacity` | `capacity_level` |
|------------|------------------|
| > 95% | `Full` |
| > 75% | `High` |
| > 25% | `Normal` |
| > 10% | `Low` |
| ≤ 10% | `Critical` |

### `AC0` — AC power

Located at `/sys/class/power_supply/AC0/`. The single attribute `online` is
`1` when the charger is connected and `0` when running on battery. The CW2217
has no dedicated AC-detect line, so this is derived from the current-direction
bit (bit 7 of register `0x0E`) and debounced as described under
`ac_debounce_polls`. `AC0` declares `BAT0` as its supplicant.

### Suspend / resume

The poll loop is cancelled on system suspend and restarted immediately on
resume, so the first reading after wake-up is fresh.

---

## Diagnostics

Watch driver log messages in real time:

```bash
sudo dmesg -w | grep oneup-battery
```

The driver logs AC connect/disconnect events, capacity changes, and the
shutdown trigger. At load time it prints the result of the battery profile
check, the AC state at boot, the version string, and confirms `probe()`
succeeded.

`tools/cw2217_probe.py` dumps raw CW2217 registers over `i2c-dev` for
low-level debugging (run it with the module unloaded):

```bash
sudo python3 tools/cw2217_probe.py --bus 1 --addr 0x64
```
