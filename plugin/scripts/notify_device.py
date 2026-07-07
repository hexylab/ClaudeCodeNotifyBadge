# -*- coding: utf-8 -*-
"""Claude Code の状態を ESP32-C6-LCD バッジへシリアル送信するブリッジ(プラグイン版)。

使い方:
  python notify_device.py <state> [message]
  state: working | done | approval | notify | idle | error

Claude Code の hooks から呼ばれる想定。stdin に hook の JSON が来ても無視する。
ポートが開けない場合(未接続・書き込み中など)は静かに終了する。

ポート選択:
  1. 環境変数 CLAUDE_BADGE_PORT があればそれを使う(例: COM5, /dev/ttyACM0)
  2. なければ接続中のポートから Espressif USB-Serial/JTAG (VID 0x303A) を自動検出
"""
import json
import os
import sys
import time

BAUD = 115200
ESPRESSIF_VID = 0x303A


def find_port():
    port = os.environ.get("CLAUDE_BADGE_PORT")
    if port:
        return port
    try:
        from serial.tools import list_ports
        for p in list_ports.comports():
            if p.vid == ESPRESSIF_VID:
                return p.device
    except Exception:
        pass
    return None


def main() -> int:
    state = sys.argv[1] if len(sys.argv) > 1 else "notify"
    message = sys.argv[2] if len(sys.argv) > 2 else ""

    # hook の stdin JSON からセッション情報を拾えれば付加する(失敗しても無視)
    # メッセージが引数で渡された場合は stdin を読まない(対話シェルからの起動でブロックするため)
    try:
        if len(sys.argv) <= 2 and not sys.stdin.isatty():
            # Windows では sys.stdin がロケールエンコーディング(CP932等)で解釈され、
            # UTF-8 の日本語がサロゲート文字化して送信時に例外になるため、
            # バイナリで読んで明示的に UTF-8 デコードする
            raw = sys.stdin.buffer.read().decode("utf-8", "replace")
            if raw.strip():
                hook = json.loads(raw)
                if not message:
                    message = hook.get("message", "") or hook.get("prompt", "")[:48]
    except Exception:
        pass

    payload = json.dumps(
        {"state": state, "msg": message[:64], "ts": int(time.time())},
        ensure_ascii=False,
    )

    port = find_port()
    if not port:
        return 0

    try:
        import serial  # pyserial
        # ESP32-C6 の USB-Serial/JTAG は DTR/RTS の組合せでリセットが掛かるため無効化する
        ser = serial.Serial()
        ser.port = port
        ser.baudrate = BAUD
        ser.timeout = 1
        ser.write_timeout = 1
        ser.dtr = False
        ser.rts = False
        ser.open()
        # 想定外の不正文字が混じっても送信自体は成立させる
        ser.write((payload + "\n").encode("utf-8", "replace"))
        ser.flush()
        ser.close()
    except Exception:
        # デバイス未接続・ポート使用中でも Claude Code 側の動作を妨げない
        return 0
    return 0


if __name__ == "__main__":
    sys.exit(main())
