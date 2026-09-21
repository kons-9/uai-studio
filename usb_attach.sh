#!/usr/bin/env bash

set -euo pipefail

# Windows側のゲートウェイIPアドレスを自動取得
WIN_IP=$(ip route show default 2>/dev/null | awk 'NR == 1 {print $3}' || true)
# WSLのネットワーク設定によってはip routeを読めないため、DNSに使われる
# Windows側アドレスをフォールバックとして使う。
if [ -z "$WIN_IP" ]; then
    WIN_IP=$(awk '/^nameserver[[:space:]]/ {print $2; exit}' /etc/resolv.conf 2>/dev/null)
fi

BUSID="${USBIP_BUSID:-4-8}"
# bind済みのusbipd-winへLinuxクライアントで直接接続するのが標準。
# Windows側の --wsl 経路を試す場合だけ USBIP_ATTACH_MODE=windows を指定する。
ATTACH_MODE="${USBIP_ATTACH_MODE:-legacy}"
USBIPD_WIN="${USBIPD_WIN:-/mnt/c/Program Files/usbipd-win/usbipd.exe}"
USBIP_BIN="${USBIP_BIN:-$(command -v usbip 2>/dev/null || true)}"

if [ -z "$WIN_IP" ]; then
    echo "エラー: WindowsのIPアドレスを取得できませんでした。"
    exit 1
fi

echo "USBデバイス (${BUSID}) をWSLへアタッチ中..."

# 必要なモジュールを手動ロード
if ! sudo modprobe vhci-hcd 2>/dev/null; then
    echo "警告: vhci-hcdをロードできませんでした。既にロード済みなら続行します。"
fi

case "$ATTACH_MODE" in
    legacy)
        if [ -z "$USBIP_BIN" ] || [ ! -x "$USBIP_BIN" ]; then
            echo "エラー: Linuxのusbipが見つかりません。"
            exit 1
        fi

        echo "Windows (${WIN_IP}):3240 の共有デバイスを確認中..."
        if ! REMOTE_DEVICES=$("$USBIP_BIN" list --remote="$WIN_IP"); then
            echo "エラー: Windows側のusbipd-winへ接続できませんでした。"
            echo "WindowsファイアウォールでTCP 3240が許可されているか確認してください。"
            exit 1
        fi
        printf '%s\n' "$REMOTE_DEVICES"
        if ! printf '%s\n' "$REMOTE_DEVICES" | grep -Eq "(^|[[:space:]])${BUSID}:"; then
            echo "エラー: BUSID ${BUSID} が共有デバイス一覧にありません。"
            exit 1
        fi

        if ! sudo "$USBIP_BIN" attach --remote="$WIN_IP" --busid="$BUSID"; then
            echo "エラー: Linuxのusbip attachに失敗しました。"
            exit 1
        fi
        ;;
    windows)
        # usbipd-win 4.x以降のWindows側attach経路。
        if [ ! -x "$USBIPD_WIN" ]; then
            echo "エラー: usbipd.exeが見つかりません: $USBIPD_WIN"
            exit 1
        fi
        if ! "$USBIPD_WIN" attach --wsl --busid "$BUSID"; then
            echo "エラー: usbipd.exeでアタッチできませんでした。"
            exit 1
        fi
        ;;
    *)
        echo "エラー: USBIP_ATTACH_MODEはlegacyまたはwindowsを指定してください。"
        exit 1
        ;;
esac

# 結果確認
echo "現在のUSB接続状態:"
if command -v lsusb >/dev/null 2>&1; then
    lsusb
else
    echo "警告: lsusbが見つかりません。"
fi
