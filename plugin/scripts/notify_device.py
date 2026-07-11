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

# Claude Code の Notification hook は権限承認以外(アイドル入力待ち等)でも発火する。
# notification_type がこの集合に含まれる場合のみ、実際にユーザーの承認操作が必要なため
# approval として送信する(それ以外は idle_prompt 等の情報通知であり送信しない)。
APPROVAL_NOTIFICATION_TYPES = {
    "permission_prompt",  # ツール実行の許可承認待ち
    "elicitation_dialog",  # MCPサーバーがユーザー入力を要求
    "agent_needs_input",  # バックグラウンドセッションが入力待ち
}


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
    # sid(セッション識別子)取得のため、message が引数で渡されていても stdin は読む。
    # ただし対話シェルからの起動(isatty)ではブロックを避けるため読まない。
    sid = ""
    notification_type = ""
    try:
        if not sys.stdin.isatty():
            # Windows では sys.stdin がロケールエンコーディング(CP932等)で解釈され、
            # UTF-8 の日本語がサロゲート文字化して送信時に例外になるため、
            # バイナリで読んで明示的に UTF-8 デコードする
            # (utf-8-sig: PowerShellのパイプ等が付けるBOMも透過的に除去)
            raw = sys.stdin.buffer.read().decode("utf-8-sig", "replace")
            if raw.strip():
                hook = json.loads(raw)
                if not message:
                    message = hook.get("message", "") or hook.get("prompt", "")[:48]
                sid = hook.get("session_id", "")[:8]
                notification_type = hook.get("notification_type", "") or ""
    except Exception:
        pass

    # approval かつ notification_type が判明していて、かつ承認系種別でない場合は送信しない
    # (idle_prompt 等の情報通知のため。notification_type が無い場合は後方互換として送信を継続する)
    if state == "approval" and notification_type and notification_type not in APPROVAL_NOTIFICATION_TYPES:
        return 0

    payload = json.dumps(
        {"state": state, "msg": message[:64], "sid": sid, "ts": int(time.time())},
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
