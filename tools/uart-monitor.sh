#!/bin/sh

set -eu

uart_device=${UART_DEVICE:-auto}
uart_baud=${UART_BAUD:-115200}
stlink_serial=${STM32_PROGRAM_SERIAL:-}

die() {
    echo "error: $*" >&2
    exit 2
}

resolve_stlink_vcp() {
    found_device=
    stlink_count=0

    for device_node in /dev/ttyACM*; do
        [ -e "$device_node" ] || continue

        tty_name=$(basename "$device_node")
        sysfs_node="/sys/class/tty/$tty_name/device"
        [ -e "$sysfs_node" ] || continue
        sysfs_node=$(readlink -f "$sysfs_node") || continue

        parent_node=$sysfs_node
        while [ "$parent_node" != "/" ]; do
            if [ -r "$parent_node/idVendor" ] &&
               [ -r "$parent_node/idProduct" ] &&
               [ -r "$parent_node/serial" ]; then
                vendor=$(tr -d '[:space:]' < "$parent_node/idVendor")
                product=$(tr -d '[:space:]' < "$parent_node/idProduct")
                serial=$(tr -d '[:space:]' < "$parent_node/serial")

                if [ "$vendor" = "0483" ] && [ "$product" = "3754" ] &&
                   { [ -z "$stlink_serial" ] || [ "$serial" = "$stlink_serial" ]; }; then
                    found_device=$device_node
                    stlink_count=$((stlink_count + 1))
                fi
                break
            fi
            parent_node=$(dirname "$parent_node")
        done
    done

    if [ "$stlink_count" -eq 0 ]; then
        if [ -n "$stlink_serial" ]; then
            die "ST-LINK VCP not found for serial $stlink_serial"
        fi
        die "no ST-LINK VCP found; set STM32_PROGRAM_SERIAL or UART_DEVICE explicitly"
    fi
    if [ "$stlink_count" -ne 1 ]; then
        die "multiple ST-LINK VCP devices matched; set STM32_PROGRAM_SERIAL explicitly"
    fi

    printf '%s\n' "$found_device"
}

if [ "$uart_device" = "auto" ] || [ -z "$uart_device" ]; then
    uart_device=$(resolve_stlink_vcp)
fi

[ -e "$uart_device" ] || die "UART device not found: $uart_device"
command -v minicom >/dev/null 2>&1 || die "minicom is required for make monitor"

echo "Opening UART monitor on $uart_device at ${uart_baud} baud" >&2
exec minicom -D "$uart_device" -b "$uart_baud" -8 -o
