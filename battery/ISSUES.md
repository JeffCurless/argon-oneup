# Known Issues

## Medium Severity

### 1. Hardcoded `VOLTAGE_NOW` and `TEMP` return fake values *(partially fixed)*

`POWER_SUPPLY_PROP_TEMP` and `POWER_SUPPLY_PROP_VOLTAGE_NOW` have been removed
from `power_battery_props[]` and their `case` branches dropped. Userspace now
receives `-EINVAL` (property not supported) rather than plausible-looking but
wrong data.

#### Path forward — real register reads (requires CW2217 datasheet)

The CW2217 exposes cell voltage at registers `0x02` (high) / `0x03` (low) and
die temperature at `0x06` (high) / `0x07` (low). Once the register map and
scaling factors are confirmed, the implementation path is:

1. **Add cached fields to `struct oneup_battery`:**
   ```c
   int voltage_uv;   /* cell voltage in µV */
   int temp_dc;      /* temperature in tenths of °C */
   ```

2. **Add a `check_voltage_temp()` helper** (alongside `check_battery_state`)
   that reads the two register pairs, applies the datasheet scaling, and
   writes the results under `bat->lock`.

3. **Call it from `oneup_battery_work()`** after `check_battery_state()`.

4. **Re-add the properties** to `power_battery_props[]` and their `case`
   branches in `oneup_bat_get_property()`, reading from the cached fields
   under the spinlock.

5. **Propagate changes** — include `voltage_uv` and `temp_dc` in the
   `power_supply_changed(bat->bat_psy)` condition check.

Use `tools/cw2217_probe.py` to dump raw register values from a live system
and cross-check against a known voltage (multimeter on the battery terminals)
and known temperature to reverse-engineer the scaling if the datasheet is
unavailable.
