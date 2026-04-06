# ksystemstats wrapper

A fix for KDE's system monitor hanging on Raspberry Pi (KDE bug [#493093](https://bugs.kde.org/show_bug.cgi?id=493093)). The CPU plugin in `ksystemstats` expects an x86-style `/proc/cpuinfo` and spins indefinitely on ARM. This wrapper patches the view of `/proc/cpuinfo` that `ksystemstats` sees, without affecting the rest of the system.

## Install

```bash
./install
```

Compiles and installs the wrapper, then restarts the `plasma-ksystemstats` service. KDE's system monitor (and the taskbar CPU widget) should work normally after this.

## Remove

```bash
./remove
```

Removes the wrapper and restores the default service behaviour.
