# Argon ONE UP

This repository contains all data related to the ONE UP laptop by Argon40

## Battery

Currently working on making a bettey driver for the system, so that we can monitor the battery using the standard toolbar plugin.  The basis of the driver is from the test_power.c driver that lives in the linux kernel tree.  The test code appears to be workng on non-trixie versions of the OS.  Once I have it working and cleaned up, I will be porting the python code from Argon40 to C for the driver to read the battery info in the same manner as they do.

## config

This directory contains changes I make to the /boot/firmware/config.txt file

## FIOScripts

A growing set of FIO jobs to help test performance on a number of different NVME drives.

## monitor1up.py

A hacky little program to monitor nvme,fan and CPU temperature.


