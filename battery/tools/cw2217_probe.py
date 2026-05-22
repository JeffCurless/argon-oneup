#!/usr/bin/env python3
"""
cw2217_probe.py — dump raw CW2217 register values to help reverse-engineer
the voltage and temperature scaling without the datasheet.

Run on the target hardware while the kernel module is NOT loaded (or while
i2c-dev access is available). Compare the candidate interpretations against:
  - A multimeter reading on the battery terminals (for voltage)
  - An ambient/case thermometer (for temperature)

Usage:
    sudo python3 cw2217_probe.py [--bus 1] [--addr 0x64] [--interval 2] [--count 10]

Dependencies:
    pip install smbus2        (or: apt install python3-smbus)
"""

import argparse
import time
import sys

try:
    from smbus2 import SMBus
except ImportError:
    try:
        from smbus import SMBus
    except ImportError:
        sys.exit("smbus2 (or smbus) is required: pip install smbus2")

# ── Register map ────────────────────────────────────────────────────────────
REG_VCELL_H   = 0x02   # cell voltage, high byte
REG_VCELL_L   = 0x03   # cell voltage, low byte
REG_SOC_H     = 0x04   # state of charge, high byte (0–100)
REG_SOC_L     = 0x05   # state of charge, low byte (fractional)
REG_TEMP_H    = 0x06   # temperature, high byte
REG_TEMP_L    = 0x07   # temperature, low byte
REG_CURRENT_H = 0x0E   # current direction (bit 7: 1=discharge, 0=charge)
REG_CURRENT_L = 0x0F
REG_CONTROL   = 0x08
REG_ICSTATE   = 0xA7


def read_byte(bus, addr, reg):
    try:
        return bus.read_byte_data(addr, reg)
    except OSError as e:
        return f"ERR({e.errno})"


def candidate_voltage(hi, lo):
    """Return a dict of candidate voltage interpretations in mV."""
    if not isinstance(hi, int) or not isinstance(lo, int):
        return {}
    raw16 = (hi << 8) | lo
    raw12 = (hi << 4) | (lo >> 4)
    return {
        # CW221x family common formula: 12-bit, 5000 mV full-scale
        "12-bit  5000mV/4096":  round(raw12 * 5000 / 4096,  1),
        # Alternative: 305 µV per LSB (12-bit)
        "12-bit  305µV/LSB":    round(raw12 * 0.305,         1),
        # Some variants use the full 16-bit word
        "16-bit  5000mV/65536": round(raw16 * 5000 / 65536,  1),
        "16-bit  305µV/LSB":    round(raw16 * 0.305 / 1000,  1),
    }


def candidate_temp(hi, lo):
    """Return a dict of candidate temperature interpretations in °C."""
    if not isinstance(hi, int) or not isinstance(lo, int):
        return {}
    raw16 = (hi << 8) | lo
    raw12 = (hi << 4) | (lo >> 4)
    # Signed 12-bit
    raw12_s = raw12 if raw12 < 2048 else raw12 - 4096
    raw16_s = raw16 if raw16 < 32768 else raw16 - 65536
    return {
        # CW221x common: 12-bit unsigned, 0.1°C/LSB, offset -40°C
        "12-bit  0.1°C/LSB -40°C":  round(raw12 * 0.1 - 40,  1),
        # No offset variant
        "12-bit  0.1°C/LSB":        round(raw12 * 0.1,        1),
        # Signed, 0.1°C/LSB
        "12-bit  signed 0.1°C/LSB": round(raw12_s * 0.1,      1),
        # Full 16-bit, 0.1°C/LSB
        "16-bit  0.1°C/LSB -40°C":  round(raw16 * 0.1 - 40,  1),
    }


def print_row(label, hi, lo):
    raw16 = f"0x{(hi << 8) | lo:04X}" if isinstance(hi, int) and isinstance(lo, int) else "N/A"
    print(f"  {label:<12} hi=0x{hi:02X}  lo=0x{lo:02X}  word={raw16}"
          if isinstance(hi, int) and isinstance(lo, int)
          else f"  {label:<12} hi={hi}  lo={lo}")


def probe_once(bus, addr):
    vcell_h   = read_byte(bus, addr, REG_VCELL_H)
    vcell_l   = read_byte(bus, addr, REG_VCELL_L)
    soc_h     = read_byte(bus, addr, REG_SOC_H)
    soc_l     = read_byte(bus, addr, REG_SOC_L)
    temp_h    = read_byte(bus, addr, REG_TEMP_H)
    temp_l    = read_byte(bus, addr, REG_TEMP_L)
    current_h = read_byte(bus, addr, REG_CURRENT_H)
    current_l = read_byte(bus, addr, REG_CURRENT_L)
    icstate   = read_byte(bus, addr, REG_ICSTATE)
    control   = read_byte(bus, addr, REG_CONTROL)

    print(f"\n{'─'*60}")
    print(f"  Time:     {time.strftime('%H:%M:%S')}")
    print(f"  IC state: 0x{icstate:02X}  control: 0x{control:02X}"
          if isinstance(icstate, int) and isinstance(control, int)
          else f"  IC state: {icstate}  control: {control}")

    soc_pct = soc_h if isinstance(soc_h, int) else "?"
    soc_frac = f".{soc_l}" if isinstance(soc_l, int) else ""
    ac = "charging" if (isinstance(current_h, int) and (current_h & 0x80) == 0) else "discharging"
    print(f"  SOC:      {soc_pct}{soc_frac}%   AC: {ac}")

    print()
    print("  Raw registers:")
    print_row("VCELL",   vcell_h,   vcell_l)
    print_row("TEMP",    temp_h,    temp_l)
    print_row("CURRENT", current_h, current_l)

    print()
    print("  Voltage candidates (compare to multimeter reading in mV):")
    for desc, val in candidate_voltage(vcell_h, vcell_l).items():
        print(f"    {desc:<35} {val:7.1f} mV")

    print()
    print("  Temperature candidates (compare to thermometer reading in °C):")
    for desc, val in candidate_temp(temp_h, temp_l).items():
        print(f"    {desc:<35} {val:7.1f} °C")


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bus",      type=int, default=1,    help="I2C bus number (default 1)")
    ap.add_argument("--addr",     type=lambda x: int(x, 0), default=0x64,
                    help="CW2217 I2C address (default 0x64)")
    ap.add_argument("--interval", type=float, default=2.0, help="seconds between reads (default 2)")
    ap.add_argument("--count",    type=int,   default=0,   help="number of reads, 0=infinite")
    args = ap.parse_args()

    print(f"CW2217 register probe — bus {args.bus}, addr 0x{args.addr:02X}")
    print("Compare voltage candidates to a multimeter on the battery terminals.")
    print("Compare temperature candidates to an ambient thermometer.")
    print("Ctrl-C to stop.\n")

    iteration = 0
    try:
        with SMBus(args.bus) as bus:
            while True:
                probe_once(bus, args.addr)
                iteration += 1
                if args.count and iteration >= args.count:
                    break
                time.sleep(args.interval)
    except KeyboardInterrupt:
        print("\nStopped.")
    except PermissionError:
        sys.exit("Permission denied — try: sudo python3 cw2217_probe.py")
    except OSError as e:
        sys.exit(f"I2C error opening bus {args.bus}: {e}")


if __name__ == "__main__":
    main()
