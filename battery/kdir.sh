#!/usr/bin/env bash

set -euo pipefail

kernel_release="${1:-$(uname -r)}"
default_kdir="/lib/modules/${kernel_release}/build"

if [[ -d "$default_kdir" ]]; then
    printf '%s\n' "$default_kdir"
    exit 0
fi

# Fedora on Raspberry Pi exposes platform-specific devel packages. Prefer the
# exact tree for the running kernel, but keep the older rpi5 -> rpi fallback as
# a last-resort workaround for systems with incomplete packaging.
exact_kdir="/usr/src/kernels/${kernel_release}"
if [[ -d "$exact_kdir" ]]; then
    printf '%s\n' "$exact_kdir"
    exit 0
fi

if [[ "$kernel_release" == *".rpi5."* ]]; then
    alt_release="${kernel_release/.rpi5./.rpi.}"
    alt_kdir="/usr/src/kernels/${alt_release}"
    if [[ -d "$alt_kdir" ]]; then
        printf '%s\n' "$alt_kdir"
        exit 0
    fi
fi

echo "Unable to find kernel build directory for ${kernel_release}." >&2
echo "Checked: ${default_kdir}" >&2
echo "Checked exact tree: ${exact_kdir}" >&2
if [[ "${alt_kdir:-}" != "" ]]; then
    echo "Checked fallback: ${alt_kdir}" >&2
fi
echo "Install the matching kernel development package for the running kernel and try again." >&2
if [[ "$kernel_release" == *".rpi5."* ]]; then
    echo "Fedora Raspberry Pi 5 hint: sudo dnf install kernel-rpi5-devel-${kernel_release}" >&2
elif [[ "$kernel_release" == *".rpi4."* ]]; then
    echo "Fedora Raspberry Pi 4 hint: sudo dnf install kernel-rpi4-devel-${kernel_release}" >&2
elif [[ "$kernel_release" == *".rpi."* ]]; then
    echo "Fedora Raspberry Pi hint: sudo dnf install kernel-devel-${kernel_release}" >&2
fi
exit 1
