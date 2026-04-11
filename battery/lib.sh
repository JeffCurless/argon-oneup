#!/usr/bin/env bash

set -euo pipefail

ONEUP_MODULE_NAME="oneUpPower"
ONEUP_MODPROBE_CONF="/etc/modprobe.d/${ONEUP_MODULE_NAME}.conf"
ONEUP_MODULES_LOAD_CONF="/etc/modules-load.d/${ONEUP_MODULE_NAME}.conf"
ONEUP_ETC_MODULES="/etc/modules"
ONEUP_OVERLAY_DIR="/boot/firmware/overlays"
ONEUP_CONFIG_TXT="/boot/firmware/config.txt"

detect_distro() {
    local id_like=""

    if [[ -r /etc/os-release ]]; then
        # shellcheck disable=SC1091
        source /etc/os-release
        DISTRO_ID="${ID:-unknown}"
        DISTRO_PRETTY_NAME="${PRETTY_NAME:-$DISTRO_ID}"
        id_like="${ID_LIKE:-}"
    else
        DISTRO_ID="unknown"
        DISTRO_PRETTY_NAME="unknown"
    fi

    case "$DISTRO_ID $id_like" in
        *fedora*|*rhel*|*centos*|*rocky*|*almalinux*)
            DISTRO_FAMILY="fedora"
            ;;
        *raspbian*|*debian*|*ubuntu*)
            DISTRO_FAMILY="debian"
            ;;
        *alpine*)
            DISTRO_FAMILY="alpine"
            ;;
        *)
            DISTRO_FAMILY="unknown"
            ;;
    esac
}

module_install_path() {
    printf '/lib/modules/%s/kernel/drivers/power/supply/%s.ko\n' "$(uname -r)" "$ONEUP_MODULE_NAME"
}

module_autoload_path() {
    detect_distro
    case "$DISTRO_FAMILY" in
        fedora)
            printf '%s\n' "$ONEUP_MODULES_LOAD_CONF"
            ;;
        *)
            printf '%s\n' "$ONEUP_ETC_MODULES"
            ;;
    esac
}

install_module_autoload() {
    local autoload_path
    autoload_path="$(module_autoload_path)"

    case "$autoload_path" in
        "$ONEUP_ETC_MODULES")
            if [[ ! -f "$ONEUP_ETC_MODULES" ]] || ! grep -qxF "$ONEUP_MODULE_NAME" "$ONEUP_ETC_MODULES"; then
                echo "Configuring module autoload via $ONEUP_ETC_MODULES"
                printf '%s\n' "$ONEUP_MODULE_NAME" >> "$ONEUP_ETC_MODULES"
            fi
            ;;
        "$ONEUP_MODULES_LOAD_CONF")
            echo "Configuring module autoload via $ONEUP_MODULES_LOAD_CONF"
            printf '%s\n' "$ONEUP_MODULE_NAME" > "$ONEUP_MODULES_LOAD_CONF"
            ;;
    esac
}

remove_module_autoload() {
    local autoload_path
    autoload_path="$(module_autoload_path)"

    case "$autoload_path" in
        "$ONEUP_ETC_MODULES")
            if [[ -f "$ONEUP_ETC_MODULES" ]]; then
                echo "Removing module autoload entry from $ONEUP_ETC_MODULES"
                sed -i "\:^${ONEUP_MODULE_NAME}\$:d" "$ONEUP_ETC_MODULES"
            fi
            ;;
        "$ONEUP_MODULES_LOAD_CONF")
            echo "Removing module autoload file $ONEUP_MODULES_LOAD_CONF"
            rm -f "$ONEUP_MODULES_LOAD_CONF"
            ;;
    esac
}

write_modprobe_config() {
    if [[ -f "$ONEUP_MODPROBE_CONF" ]]; then
        echo "Module config already exists at $ONEUP_MODPROBE_CONF — not overwriting."
        echo "  Current setting: $(grep . "$ONEUP_MODPROBE_CONF")"
    else
        echo "Writing default module config to $ONEUP_MODPROBE_CONF"
        printf 'options %s soc_shutdown=5\n' "$ONEUP_MODULE_NAME" > "$ONEUP_MODPROBE_CONF"
    fi
}

install_overlay() {
    local dtbo_src="${1:-argon-oneup-battery.dtbo}"
    if [[ ! -f "$dtbo_src" ]]; then
        echo "Warning: $dtbo_src not found; skipping overlay install."
        echo "Run ./setup first to compile the overlay."
        return 0
    fi
    cp -vf "$dtbo_src" "$ONEUP_OVERLAY_DIR/"
    if ! grep -qF "dtoverlay=argon-oneup-battery" "$ONEUP_CONFIG_TXT"; then
        printf 'dtoverlay=argon-oneup-battery\n' >> "$ONEUP_CONFIG_TXT"
        echo "Added dtoverlay=argon-oneup-battery to $ONEUP_CONFIG_TXT"
        echo "Note: the overlay takes effect after the next reboot."
    else
        echo "dtoverlay=argon-oneup-battery already present in $ONEUP_CONFIG_TXT"
    fi
}
