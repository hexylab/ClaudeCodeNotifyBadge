# Claude Code Notify Badge (Clawd Badge)

Waveshare ESP32-C6-Touch-LCD-1.47 を Claude Code のステータスバッジにするプロジェクト。
PC 上の Claude Code の状態（作業中 / 完了 / 承認待ち など）を USB シリアル経由で受信し、
Anthropic デザインの画面でマスコット「Clawd」がアニメーションで知らせてくれます。

## 構成

```
firmware/ClawdBadge/     ESP32-C6 用 Arduino ファームウェア (JD9853 172x320)
plugin/                  Claude Code プラグイン (hooks + 通知スクリプト、USB シリアル版)
client/                  npm クライアント clawd-badge (hooks + CLI、LAN/HTTP 版)
bridge/notify_device.py  旧・手動設定用スクリプト(プラグイン版は plugin/scripts/ 参照)
tools/gen_sprites.py     Clawd スプライト(clawd_sprites.h)を参照 GIF から再生成するツール
```

バッジへの通知経路は次の 2 通りがあります。用途に応じて選んでください。

1. **USB シリアル接続 + Claude Code プラグイン**(従来どおり): バッジを USB 接続した PC でのみ動作。
2. **LAN 経由(WiFi)+ npm クライアント**(新規): バッジが WiFi に一度接続されれば、同じ LAN 上の任意の PC から HTTP で通知できます。

## 使い方(1) USB シリアル接続 + Claude Code プラグイン

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

## 使い方(2) LAN 経由(WiFi)+ npm クライアント

ファームウェアが WiFi と HTTP サーバ(`POST /notify`)に対応したことで、バッジを USB 接続していない PC からも LAN 経由で状態を通知できるようになりました。npm パッケージ名は `clawd-badge`(Node.js 18 以上)です。

現時点では npm 未公開のため、リポジトリからインストールしてください:

```bash
git clone https://github.com/hexylab/ClaudeCodeNotifyBadge.git
npm i -g ./ClaudeCodeNotifyBadge/client
```

セットアップ(バッジを USB 接続した PC で一度だけ):

```bash
clawd-badge setup
```

SSID・パスワードを対話入力すると、シリアル経由でバッジに書き込み → WiFi 接続 → IP アドレス取得 → `~/.clawd-badge.json` に保存 → Claude Code の hooks(`~/.claude/settings.json`)への自動登録、まで行われます。

バッジの IP がすでに分かっている別の PC では、シリアル接続なしでセットアップできます:

```bash
clawd-badge setup --host <バッジのIP>
```

主なコマンド:

- `clawd-badge notify <state>` — バッジへ状態を通知(通常は hooks から自動実行)
- `clawd-badge status` — バッジの現在状態を確認
- `clawd-badge unhook` — 登録した hooks を削除

送信先の解決順は 環境変数 `CLAUDE_BADGE_HOST` > `~/.clawd-badge.json` > mDNS 名 `clawd-badge.local` です。

詳細なコマンドリファレンスやトラブルシューティングは [`client/README.md`](client/README.md) を参照してください。

## ハードウェア仕様（実機確認済み）

- LCD: JD9853 コントローラ 172×320（Arduino_GFX の ST7789 クラス＋公式デモの JD9853 初期化シーケンスを使用）
- SPI: MOSI=GPIO2, SCLK=GPIO1, CS=GPIO14, DC=GPIO15, RST=GPIO22, BL=GPIO23（アクティブHigh）
- タッチ: AXS5106L (I2C SDA=GPIO18, SCL=GPIO19) ※現状未使用
- RGB LED: 非搭載（ファームウェア内では `ENABLE_RGB_LED 0` で無効化）

## 仕組み

1. Claude Code の hooks（`~/.claude/settings.json`）がイベント発生時に通知スクリプト(プラグインの `notify_device.py` または npm クライアントの `clawd-badge notify`)を起動
   - `UserPromptSubmit` → `working`（作業中）
   - `Stop` → `done`（作業完了）
   - `Notification` → `approval`（承認依頼・注意）
   - `SessionEnd` → `idle`
2. USB シリアル経由の場合は `{"state":"working","sid":"...","msg":"...","ts":...}` の JSON 1 行を COM ポート (115200bps) へ、LAN 経由の場合は同様の内容を `POST /notify` へ送信
3. ファームウェアが状態に応じた Clawd のアニメーションと LED 表示に切り替え

### 複数セッション対応

`sid`(セッション識別子、任意の文字列)を含めることで、複数の Claude Code セッション(複数マシン/複数ターミナル)の状態を個別に管理できます。

- `sid` を省略した場合は固定 `sid` `"_nosid"` として扱われ、旧クライアントは単一セッションとして動きます。
- 画面(キャラ・ヘッダー)には全セッションを優先度 `error > approval > notify > working > done` で集約した状態を表示します。セッションが1つも無ければ `idle` です。
- `state:"idle"`(`SessionEnd` 相当)を受信すると、そのセッションはテーブルから削除されます(集約状態には影響しません)。
- 画面下部にはセッション数ぶんのドットが状態色で表示され(`approval` は約500ms周期で点滅)、右側にセッション数が数字で表示されます。セッションが0件のときはドット・数字とも非表示です。
- セッションテーブルは最大8件。満杯の場合は最終更新が最も古いセッションを新しいセッションで上書きします。
- タイムアウトはセッションごとに判定されます: `working` は30分、それ以外(`done`/`approval`/`notify`/`error`)は5分で自動的に該当セッションが削除されます。

## WiFi / LAN 通知(ファームウェア側)

`firmware/ClawdBadge/ClawdBadge.ino` は WiFi 接続と HTTP サーバに対応しています。

- WiFi 認証情報は NVS(Preferences namespace `clawd`)に永続化され、起動時に自動接続します。切断時は 15 秒間隔で再接続を試みます。
- USB シリアル(115200bps、1行 JSON)経由のコマンド:
  - `{"cmd":"wifi","ssid":"...","pass":"..."}` — WiFi 認証情報を保存して接続。`{"event":"wifi","status":"connecting"}` → `{"event":"wifi","status":"connected","ip":"...","hostname":"clawd-badge.local"}` (失敗時は `{"event":"wifi","status":"failed"}`) が返されます。
  - `{"cmd":"wifi_status"}` — 現在の WiFi 接続状態を問い合わせ
  - `{"cmd":"wifi_clear"}` — 保存済みの WiFi 認証情報を消去
- HTTP サーバ(ポート 80): `POST /notify`(ボディ例 `{"state":"working","sid":"a1b2c3d4","msg":"..."}`)、`GET /status`
  - `GET /status` のレスポンスにはセッション数 `"sessions"`、承認待ち件数 `"waiting"`、各セッションの `"sid"`/`"state"` を格納した `"list"` 配列が含まれます。
- mDNS 名 `clawd-badge.local` で名前解決できます。
- 従来の USB シリアル通知(プラグイン版)はそのまま動作します。WiFi を設定しなくても今までどおり使えます。

npm クライアント `clawd-badge setup` を使うと、これらのシリアルコマンドを内部的に呼び出して WiFi 設定を自動化できます(詳細は上記の「使い方(2)」を参照)。

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

WiFi / WebServer / mDNS を追加したことでスケッチがデフォルトパーティション(APP 1.2MB)に収まらなくなったため、
`PartitionScheme=min_spiffs` を指定してビルドしてください(フラッシュ使用率 74%・RAM 使用率 14% で検証済み)。

```powershell
& "$env:USERPROFILE\arduino-cli\arduino-cli.exe" compile --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs firmware\ClawdBadge
& "$env:USERPROFILE\arduino-cli\arduino-cli.exe" upload -p COM5 --fqbn esp32:esp32:esp32c6:CDCOnBoot=cdc,PartitionScheme=min_spiffs firmware\ClawdBadge
```

## 手動テスト

```powershell
python bridge\notify_device.py done "テスト"
```
