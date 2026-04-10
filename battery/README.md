# oneUpPower — Battery Driver

A Linux kernel module for the Argon ONE UP laptop's battery controller. It integrates with the kernel's power supply framework, exposing battery state (capacity, status, charge) and AC power state through the standard sysfs interface at `/sys/class/power_supply/`. A background kernel thread monitors the battery and triggers a graceful system shutdown when the charge drops below a configurable threshold while unplugged.

## First-time setup

Run the `firsttime` script once to install the required build tools:

```bash
./firsttime
```

It detects the running OS and installs the appropriate packages:

| OS | Packages installed |
|----|--------------------|
| Raspberry Pi OS / Debian | `build-essential`, `linux-headers-rpi-v8`, `dkms` |
| Ubuntu | `build-essential`, `linux-headers-generic`, `dkms` |
| Fedora / RHEL family | `gcc`, `make`, Fedora-matching kernel devel package, `dkms` |
| Alpine | `build-base`, `linux-dev` |

On Fedora Raspberry Pi systems, install the platform-specific devel package that matches the running kernel, for example `kernel-rpi5-devel-$(uname -r)` on Raspberry Pi 5. The helper scripts prefer the exact kernel build tree and only fall back to alternate naming when the system packaging is incomplete.

On Fedora you have to add 'dtparam=i2c_arm=on' to /boot/config.txt for battery charge state and charge level to be displayed properly.

## Build

```bash
./build
```

Compiles `oneUpPower.ko` against the headers for the currently running kernel.

## Install and load

```bash
./install
```

Installs and starts the driver. Specifically it:

1. Copies `oneUpPower.ko` to `/lib/modules/$(uname -r)/kernel/drivers/power/supply/`
2. Configures `oneUpPower` to load automatically on boot
3. On Debian-family systems this uses `/etc/modules`; on Fedora/RHEL-family systems it uses `/etc/modules-load.d/oneUpPower.conf`
4. Writes the default configuration to `/etc/modprobe.d/oneUpPower.conf`
5. Runs `depmod -a` to update the module dependency database
6. Loads the module immediately with `modprobe`

## Remove

```bash
./remove
```

Unloads the driver and removes all installed files: the `.ko`, the distro-specific auto-load configuration, and `/etc/modprobe.d/oneUpPower.conf`.

## DKMS (optional)

```bash
./setupdkms
```

Installs the driver into DKMS at `/usr/src/oneUpPower-1.0/`. With DKMS, the module is automatically rebuilt whenever the kernel is updated, so it does not need to be manually rebuilt and reinstalled after a kernel upgrade. Use this if you plan to keep the driver installed long-term on a system that receives kernel updates.

## Argon40 service conflict

The `argononeupd.service` daemon shipped by Argon40 also attempts to manage battery reporting. Running both at the same time causes conflicts. Disable the Argon40 service before using this driver:

```bash
sudo systemctl disable --now argononeupd.service
```

## Settings

### `soc_shutdown` — low-battery shutdown threshold

Controls the state-of-charge percentage at which the driver initiates an automatic shutdown when the system is running on battery power alone. The default is `5`.

The setting is stored in `/etc/modprobe.d/oneUpPower.conf`:

```
options oneUpPower soc_shutdown=5
```

Edit the file and reload the module to change the threshold persistently, or write directly to the sysfs parameter file to change it while the module is running:

```bash
echo 10 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
```

| Value | Meaning |
|-------|---------|
| `0` | Disable automatic shutdown entirely |
| `2`–`19` | Shut down when battery drops below this percentage and AC is disconnected |

Values outside this range are rejected. A shutdown is triggered only when AC power is disconnected **and** the reported state of charge falls below the threshold.

## System interfaces

The driver registers two power supply devices with the kernel:

### `BAT0` — battery

Located at `/sys/class/power_supply/BAT0/`. Key attributes:

| Attribute | Description |
|-----------|-------------|
| `capacity` | State of charge, 0–100% |
| `status` | `Charging`, `Discharging`, or `Full` |
| `capacity_level` | Coarse level (see table below) |
| `charge_full` | Design capacity: 4,800,000 µAh (4,800 mAh) |
| `charge_now` | Estimated current charge in µAh |
| `time_to_empty_avg` | Estimated seconds of runtime remaining |
| `time_to_full_now` | Estimated seconds to full charge |
| `technology` | `Li-ion` |
| `manufacturer` | `Argon40` |
| `model_name` | `oneUp Battery` |

Capacity levels reported in `capacity_level`:

| `capacity` | `capacity_level` |
|------------|------------------|
| > 95% | `Full` |
| > 85% | `High` |
| > 75% | `Normal` |
| > 40% | `Low` |
| ≤ 40% | `Critical` |

### `AC0` — AC power

Located at `/sys/class/power_supply/AC0/`. The single attribute `online` is `1` when the charger is connected and `0` when running on battery.

## Diagnostics

Watch driver log messages in real time:

```bash
sudo dmesg -w | grep oneUpPower
```

The driver logs AC connect/disconnect events, capacity changes, and the shutdown trigger. At load time it prints its version and confirms the monitor thread has started.
