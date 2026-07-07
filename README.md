# Claude Code Notify Badge (Clawd Badge)

Waveshare ESP32-C6-Touch-LCD-1.47 を Claude Code のステータスバッジにするプロジェクト。
PC 上の Claude Code の状態（作業中 / 完了 / 承認待ち など）を USB シリアル経由で受信し、
Anthropic デザインの画面でマスコット「Clawd」がアニメーションで知らせてくれます。

## 構成

```
firmware/ClawdBadge/     ESP32-C6 用 Arduino ファームウェア (JD9853 172x320)
plugin/                  Claude Code プラグイン (hooks + 通知スクリプト)
bridge/notify_device.py  旧・手動設定用スクリプト(プラグイン版は plugin/scripts/ 参照)
tools/gen_sprites.py     Clawd スプライト(clawd_sprites.h)を参照 GIF から再生成するツール
```

## PC 側セットアップ(Claude Code プラグイン)

pyserial を入れて、プラグインをインストールするだけです:

```
pip install pyserial
```

Claude Code 内で:

```
/plugin marketplace add hexylab/ClaudeCodeNotifyBadge
/plugin install clawd-badge@clawd-badge
```

シリアルポートは自動検出(Espressif VID 0x303A)。明示したい場合は
環境変数 `CLAUDE_BADGE_PORT` を設定してください。詳細は `plugin/README.md` 参照。

## ハードウェア仕様（実機確認済み）

- LCD: JD9853 コントローラ 172×320（Arduino_GFX の ST7789 クラス＋公式デモの JD9853 初期化シーケンスを使用）
- SPI: MOSI=GPIO2, SCLK=GPIO1, CS=GPIO14, DC=GPIO15, RST=GPIO22, BL=GPIO23（アクティブHigh）
- タッチ: AXS5106L (I2C SDA=GPIO18, SCL=GPIO19) ※現状未使用
- RGB LED: 非搭載（ファームウェア内では `ENABLE_RGB_LED 0` で無効化）

## 仕組み

1. Claude Code の hooks（`~/.claude/settings.json`）がイベント発生時に `bridge/notify_device.py` を起動
   - `UserPromptSubmit` → `working`（作業中）
   - `Stop` → `done`（作業完了）
   - `Notification` → `approval`（承認依頼・注意）
   - `SessionEnd` → `idle`
2. スクリプトが `{"state":"working","msg":"...","ts":...}` の JSON 1 行を COM5 (115200bps) へ送信
3. ファームウェアが状態に応じた Clawd のアニメーションと LED 表示に切り替え

## 状態と表示

| state    | 画面                     |
|----------|--------------------------|
| working  | ハサミをカチカチ、タイピング風 |
| done     | ぴょんと跳ねて ✓          |
| approval | 手を振って「?」吹き出し    |
| notify   | ベル＋首かしげ            |
| idle     | ゆっくり呼吸＋まばたき     |
| error    | 目が「><」               |

## ビルドと書き込み

```powershell
& "$env:USERPROFILE\arduino-cli\arduino-cli.exe" compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc firmware\ClawdBadge
& "$env:USERPROFILE\arduino-cli\arduino-cli.exe" upload -p COM5 --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc firmware\ClawdBadge
```

## 手動テスト

```powershell
python bridge\notify_device.py done "テスト"
```
