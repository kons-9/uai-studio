#!/usr/bin/env bash

set -euo pipefail

echo "ネイティブLinuxのST-LINK接続を確認中..."

if ! command -v lsusb >/dev/null 2>&1; then
    echo "警告: lsusbが見つかりません。"
    exit 0
fi

if ! lsusb_output=$(lsusb); then
    echo "警告: USB一覧を取得できませんでした。"
    exit 0
fi

printf '%s\n' "$lsusb_output"
if ! printf '%s\n' "$lsusb_output" | grep -Eqi 'STMicroelectronics.*STLINK|0483:37(54|4[0-9])'; then
    echo "警告: ST-LINKが見つかりません。ST-LINKのUSBケーブルを接続してください。"
fi
