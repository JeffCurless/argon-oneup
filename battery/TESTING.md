# oneUpPower — Manual Testing Guide

Testing must be performed on the target Raspberry Pi 5. All commands below are
run from the `battery/` directory unless noted otherwise.

---

## Pre-flight

### 1. Check for the Argon40 service conflict

```bash
systemctl is-active argononeupd.service
```

If it reports `active`, stop it before proceeding:

```bash
sudo systemctl disable --now argononeupd.service
```

### 2. Confirm the battery IC is visible on I2C bus 1

```bash
sudo i2cdetect -y 1
```

Expected: a device at address `0x64`. If that cell is empty the IC is not
responding — check the hardware connection before continuing.

### 3. Confirm no prior instance of the driver is loaded

```bash
lsmod | grep oneUpPower
```

Must return nothing. If the module is already loaded, unload it first:

```bash
sudo rmmod oneUpPower
```

---

## Build

Build on the Pi against the running kernel:

```bash
./build
```

Expected last line:

```
LD [M]  oneUpPower.ko
```

No warnings should appear. A "Skipping BTF generation" notice is normal when
`vmlinux` is absent and is not an error.

Confirm the module targets the running kernel before proceeding:

```bash
modinfo oneUpPower.ko | grep vermagic
```

The `vermagic` string must match `uname -r` exactly. A mismatch means the
kernel headers do not match the running kernel — re-run `./firsttime` and
rebuild.

---

## Installation

Open a second terminal and start watching the kernel log before installing:

```bash
sudo dmesg -w | grep oneUpPower
```

Then in the first terminal:

```bash
sudo ./install
```

The `install` script:
1. Copies `oneUpPower.ko` to `/lib/modules/$(uname -r)/kernel/drivers/power/supply/`
2. Adds `oneUpPower` to `/etc/modules` so it loads automatically on boot
3. Writes the default configuration to `/etc/modprobe.d/oneUpPower.conf`
4. Runs `depmod -a` to update the module dependency database
5. Loads the module immediately with `insmod`

### Expected dmesg output

Battery IC profile initialisation takes up to ~30 seconds on first load if the
profile needs to be written to the IC:

```
oneUpPower: Checking battery profile...
oneUpPower: Battery profile is valid.
oneUpPower: Probe successful (v1.0.3)
```

If the profile was out of date or blank:

```
oneUpPower: Checking battery profile...
oneUpPower: Programming battery profile...
oneUpPower: Battery IC activated.
oneUpPower: Battery profile updated successfully.
oneUpPower: Probe successful (v1.0.3)
```

`Probe successful` must appear. There must be no `Adapter =` lines — those
indicate a stale binary from the old kthread-based code.

---

## Functional verification

### Power supply devices registered

```bash
ls /sys/class/power_supply/
```

Must list both `BAT0` and `AC0`.

### Battery attributes

```bash
cat /sys/class/power_supply/BAT0/{status,capacity,capacity_level,charge_full,charge_now,technology,manufacturer,model_name}
```

| Attribute | Expected value |
|-----------|----------------|
| `status` | `Charging`, `Discharging`, or `Full` |
| `capacity` | Integer 0–100 |
| `capacity_level` | `Full`, `High`, `Normal`, `Low`, or `Critical` |
| `charge_full` | `4800000` |
| `charge_now` | approximately `capacity × 48000` |
| `technology` | `Li-ion` |
| `manufacturer` | `Argon40` |
| `model_name` | `oneUp Battery` |

Cross-check that `capacity_level` is consistent with `capacity`:

```bash
cap=$(cat /sys/class/power_supply/BAT0/capacity)
lvl=$(cat /sys/class/power_supply/BAT0/capacity_level)
echo "capacity=$cap  level=$lvl"
```

| `capacity` | Expected `capacity_level` |
|------------|---------------------------|
| > 95 | `Full` |
| > 85 | `High` |
| > 75 | `Normal` |
| > 40 | `Low` |
| ≤ 40 | `Critical` |

### AC attribute

```bash
cat /sys/class/power_supply/AC0/online
```

`1` = charger connected, `0` = running on battery.

---

## Charger plug/unplug test

### Step 1 — baseline with charger connected

```bash
cat /sys/class/power_supply/AC0/online       # expect: 1
cat /sys/class/power_supply/BAT0/status      # expect: Charging or Full
```

### Step 2 — disconnect the charger

Physically unplug the power adapter. Within one polling cycle (≤2 seconds):

```bash
cat /sys/class/power_supply/AC0/online       # expect: 0
cat /sys/class/power_supply/BAT0/status      # expect: Discharging
```

Expected dmesg line:

```
oneUpPower: AC Power is disconnected.
```

### Step 3 — reconnect the charger

```bash
cat /sys/class/power_supply/AC0/online       # expect: 1
cat /sys/class/power_supply/BAT0/status      # expect: Charging or Full
```

Expected dmesg line:

```
oneUpPower: AC Power is connected.
```

---

## Module parameter — `soc_shutdown`

### Read the current value

```bash
cat /sys/module/oneUpPower/parameters/soc_shutdown
# expect: 5  (default from /etc/modprobe.d/oneUpPower.conf)
```

### Live changes — valid values

```bash
echo 10 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
# dmesg: oneUpPower: Changing automatic shutdown when battery is below 10%

echo 0 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
# dmesg: oneUpPower: Disabling automatic shutdown when battery is below threshold.
```

Restore to the default after testing:

```bash
echo 5 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
```

### Boundary and rejection tests

```bash
# Value too high — rejected, parameter unchanged
echo 21 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
# dmesg: oneUpPower: Invalid value (21%), please change to: 0 to disable, 1-20 to set...
cat /sys/module/oneUpPower/parameters/soc_shutdown   # must still read 5

# Negative value — write returns an error
echo -1 | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
# shell reports write error; parameter unchanged

# Non-numeric input
echo abc | sudo tee /sys/module/oneUpPower/parameters/soc_shutdown
# dmesg: oneUpPower: Could not convert to integer
```

---

## Suspend / resume test

Verifies the PM ops: `cancel_delayed_work_sync` on suspend and
`schedule_delayed_work` on resume.

In one terminal, watch the kernel log:

```bash
sudo dmesg -w | grep -E "oneUpPower|PM:"
```

Suspend and wake the system:

```bash
sudo systemctl suspend
# wake with keyboard or power button
```

Expected sequence in dmesg:

```
PM: suspend entry (s2idle)
...
PM: suspend exit
oneUpPower: Battery State of charge is XX%   # first poll after resume
```

After resume, confirm the sysfs attributes are still readable:

```bash
cat /sys/class/power_supply/BAT0/capacity
cat /sys/class/power_supply/AC0/online
```

---

## Clean reload cycle

Verifies that unloading and reloading the module leaves no leaked resources or
stale sysfs state.

```bash
sudo rmmod oneUpPower

lsmod | grep oneUpPower              # must return nothing
ls /sys/class/power_supply/          # BAT0 and AC0 must be absent

sudo modprobe oneUpPower
# dmesg: Probe successful (v1.0.3)
cat /sys/class/power_supply/BAT0/capacity   # must work immediately
```

Repeat 2–3 times. After each unload inspect dmesg for any `WARNING:` or
`BUG:` lines; there must be none.

---

## Removal

To fully uninstall the driver:

```bash
sudo ./remove
```

The `remove` script unloads the module, removes the `.ko` from
`/lib/modules/`, deletes the `/etc/modules` entry, and removes
`/etc/modprobe.d/oneUpPower.conf`.

### Verify clean removal

```bash
lsmod | grep oneUpPower
# nothing

ls /sys/class/power_supply/
# BAT0 and AC0 absent

grep oneUpPower /etc/modules
# nothing

ls /etc/modprobe.d/oneUpPower.conf
# no such file

ls /lib/modules/$(uname -r)/kernel/drivers/power/supply/oneUpPower.ko
# no such file
```

---

## Troubleshooting

| Symptom | Likely cause | Resolution |
|---------|-------------|------------|
| `Probe successful` never appears in dmesg | `0x64` not responding on I2C bus 1 | Confirm with `sudo i2cdetect -y 1`; check `argononeupd.service` is stopped |
| `Failed to register battery supply` in dmesg | `BAT0` already registered by another driver | `ls /sys/class/power_supply/` to identify the owner |
| `Profile init failed; SOC readings may be inaccurate` | I2C write error during profile programming | Transient — reload the module. If persistent, check the I2C bus with `i2cget -y 1 0x64 0x08` |
| `capacity` reads 0 or 255 | IC not yet active after profile write | Wait 10 s and re-read. If persistent, inspect IC state: `sudo i2cget -y 1 0x64 0xa7` (bits [3:2] must be non-zero) |
| sysfs attributes absent after `modprobe` | `vermagic` mismatch — stale `.ko` | `modinfo oneUpPower.ko \| grep vermagic`; rebuild on the Pi |
| Unexpected system shutdown | `soc_shutdown` threshold too high, or battery is genuinely low | Read `capacity` and `soc_shutdown`; set the parameter to `0` temporarily to disable automatic shutdown while investigating |
