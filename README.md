# Argon ONE UP

This repository contains all data related to the ONE UP laptop by Argon40

## Battery

Currently working on making a bettey driver for the system, so that we can monitor the battery using the standard toolbar plugin.  The basis of the driver is from the test_power.c driver that lives in the linux kernel tree.  The test code appears to be workng on non-trixie versions of the OS.  Once I have it working and cleaned up, I will be porting the python code from Argon40 to C for the driver to read the battery info in the same manner as they do.

### Build Instructions

In order to build the driver;
```
cd battery
./build
./install
```
To remove it:

```
./remove
```

Once the driver is loaded, go to the task bar and add a battery.  If there is already a battery plugin loaded, remove it, exit, and re-add.
## config

This directory contains changes I make to the /boot/firmware/config.txt file

## FIOScripts

A growing set of FIO jobs to help test performance on a number of different NVME drives.

## monitor1up.py

A hacky little program to monitor nvme,fan and CPU temperature.


## Work that needs to be completed

- [X] Get test_power.c code running and simualating a battery for the Raspberry PI.
- [X] Remove all unneeded code from the driver, called oneUpPower.c.
- [X] Fix the naming of all the internerals properly, and makes sure the battery tech, and manuacturer are correct.
- [X] Port python battery code to C.
- [X] Incorprate working C code into driver, and do all the plumbing.
- [ ] Add code to support clean shutdown of laptop if system is not charging and hits a minimum SOC (5%).
- [ ] Add code to allow user to set a different SOC for shutdown or disable feature.
- [ ] Review python code to see if there is anything else that needs to be moved over. 
- [ ] Create an installer
- [ ] Create an uninstaller

## Supported Operating Systems

> [!NOTE]
> This code is currently only supported on 64 bit Raspberry PI OS.  There is not current plan to make it operational on any other OS at this time.
> Driver does not build under Trixie yet, as the kernel headers are not up to date...


## Pictures

Discharging... at 88%
![Discharging](/pictures/PXL_20251007_022637735.jpg)

Charging
![Charging](/pictures/PXL_20251007_022658734.jpg)




