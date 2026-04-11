#!/usr/bin/env bash

set -euo pipefail

ONEUP_MODULE_NAME="oneUpPower"
ONEUP_MODPROBE_CONF="/etc/modprobe.d/${ONEUP_MODULE_NAME}.conf"
ONEUP_MODULES_LOAD_CONF="/etc/modules-load.d/${ONEUP_MODULE_NAME}.conf"
ONEUP_ETC_MODULES="/etc/modules"

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
                printf '%s\n' "$ONEUP_MODULE_NAME" | sudo tee -a "$ONEUP_ETC_MODULES" >/dev/null
            fi
            ;;
        "$ONEUP_MODULES_LOAD_CONF")
            echo "Configuring module autoload via $ONEUP_MODULES_LOAD_CONF"
            printf '%s\n' "$ONEUP_MODULE_NAME" | sudo tee "$ONEUP_MODULES_LOAD_CONF" >/dev/null
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
                sudo sed -i "\:^${ONEUP_MODULE_NAME}\$:d" "$ONEUP_ETC_MODULES"
            fi
            ;;
        "$ONEUP_MODULES_LOAD_CONF")
            echo "Removing module autoload file $ONEUP_MODULES_LOAD_CONF"
            sudo rm -f "$ONEUP_MODULES_LOAD_CONF"
            ;;
    esac
}

write_modprobe_config() {
    echo "Writing module config to $ONEUP_MODPROBE_CONF"
    printf 'options %s soc_shutdown=5\n' "$ONEUP_MODULE_NAME" | sudo tee "$ONEUP_MODPROBE_CONF" >/dev/null
}
