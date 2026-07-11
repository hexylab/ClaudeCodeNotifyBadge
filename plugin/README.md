# Clawd Badge Notifier プラグイン

Claude Code の状態(作業中 / 完了 / 承認待ち / アイドル)を、USB 接続した
ESP32-C6 LCD バッジ「Clawd Badge」へシリアル送信する hooks プラグインです。

## 必要なもの

- Clawd Badge ファームウェアを書き込んだ Waveshare ESP32-C6-Touch-LCD-1.47(リポジトリの `firmware/` 参照)
- Python 3.x(`python` コマンドが PATH に通っていること)
- pyserial: `pip install pyserial`

## インストール

```
/plugin marketplace add hexylab/ClaudeCodeNotifyBadge
/plugin install clawd-badge@clawd-badge
```

## 動作

| Claude Code イベント | 送信 state | バッジ表示 |
|---|---|---|
| UserPromptSubmit | working | ハサミをカチカチ(作業中) |
| Stop | done | ぴょんと跳ねて ✓ |
| Notification | approval | 手を振って「?」吹き出し |
| SessionEnd | idle | ゆっくり呼吸+まばたき |

## ポート設定

既定では接続中のシリアルポートから Espressif USB-Serial/JTAG(VID 0x303A)を
自動検出します。複数の ESP32 デバイスを接続している場合などは、環境変数
`CLAUDE_BADGE_PORT` でポートを明示できます(例: `COM5`, `/dev/ttyACM0`)。

デバイス未接続やポート使用中でも hooks はエラーにならず静かにスキップします。

hooks の stdin から渡される JSON の `session_id`(先頭8文字)を `sid` として送信payloadに含めます。複数セッション対応のバッジ側でのセッション識別に使われます。

## 手動テスト

リポジトリの `plugin/` ディレクトリから:

```powershell
python scripts/notify_device.py done "テスト"
```

プラグインとしてインストール済みの場合は、インストール先
(例: `~/.claude/plugins/cache/clawd-badge/clawd-badge/<version>/scripts/notify_device.py`)
を指定して実行できます。
