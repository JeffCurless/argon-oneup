# Known Issues

## Medium Severity

### 1. No locking on shared state

**Location:** `struct oneup_battery` fields (lines 69–74)

`bat->soc`, `ac_online`, `status`, and `capacity_level` are written by
`oneup_battery_work` (workqueue context) and read by `oneup_bat_get_property` /
`oneup_ac_get_property` (callable from arbitrary context). No spinlock or other
synchronization protects these fields — this is a data race.

---

## Low Severity

### 2. `sprintf` used in sysfs getter

**Location:** `param_get_soc_shutdown` (line 654)

```c
return sprintf(buffer, "%d", soc_shutdown);
```

The kernel convention for sysfs callbacks is `sysfs_emit(buffer, ...)` (kernel 5.10+) or
at minimum `scnprintf`. Plain `sprintf` has no bounds checking on the provided buffer.

---

### 3. `module_param` declared after `module_i2c_driver`

**Location:** lines 627 vs. 663

`module_i2c_driver` expands to `module_init`/`module_exit`. Placing `module_param` and its
supporting ops struct after this macro is unconventional. Parameter declarations should
appear before the driver registration macro.

---

### 4. `REG_SOCALERT` write clobbers alert threshold bits

**Location:** `init_battery_profile` (line 374)

```c
i2c_smbus_write_byte_data(client, REG_SOCALERT, 0x80);
```

The entire register is written as `0x80` without a read-modify-write. If the CW2217 uses
lower bits of `REG_SOCALERT` for SOC alert thresholds, they are silently zeroed.
