# clawd-badge

LAN内のESP32-C6バッジデバイスへ、Claude Codeの状態(作業中/完了/承認待ちなど)をHTTP経由で通知するCLIです。
Claude CodeのhooksからバッジのHTTPサーバ(`POST /notify`)へ状態を送信し、バッジ側の表示を更新します。

## インストール

グローバルインストール:

```bash
npm i -g clawd-badge
```

または `npx` で都度実行(hooksからの呼び出しはこちらを利用します):

```bash
npx -y clawd-badge notify working
```

## セットアップ手順

1. バッジデバイスをUSBでPCに接続します。
2. 以下を実行してWiFi接続とClaude Code hooksを設定します。

```bash
npx -y clawd-badge setup
```

- SSID・パスワードの指定が省略された場合は対話プロンプトで入力を求められます(パスワードは伏せ字表示)。
- Espressif製USBシリアル(VID `0x303A`)を自動検出します。複数接続されている、または自動検出に失敗する場合は `--port` で明示してください。
- WiFi接続が完了すると、デバイスのIPアドレスを `~/.clawd-badge.json` に保存します。
- 最後に、Claude Codeのhooks(`~/.claude/settings.json`)へ通知コマンドを自動登録します(`--skip-hooks` でスキップ可能)。
  - 登録されるコマンドは `node <clawd-badgeのCLIスクリプトの絶対パス> notify <state>` の形式です。`clawd-badge` は現時点でnpmレジストリに未公開のため、`npx -y clawd-badge ...` は使用しません(レジストリ取得に失敗する、または同名の別パッケージが実行される恐れがあるため)。絶対パスは `setup` 実行時に実行中のCLI自身から解決するため、`npm i -g` でのインストール・gitクローンからの実行のいずれでも正しく動作します。

### すでにバッジのIPが分かっている場合

シリアル接続を省略し、IPアドレスの登録のみ行うこともできます。

```bash
npx -y clawd-badge setup --host 192.168.1.50
```

## コマンドリファレンス

### `clawd-badge setup [options]`

バッジのWiFi接続をセットアップし、Claude Code hooksを登録します。

| オプション | 説明 |
| --- | --- |
| `--ssid <ssid>` | 接続先WiFiのSSID(省略時は対話入力) |
| `--pass <pass>` | 接続先WiFiのパスワード(省略時は対話入力) |
| `--host <ip>` | シリアルプロビジョニングを行わず、指定IPを直接保存 |
| `--port <COMx>` | シリアルポートを明示指定(自動検出に失敗する場合) |
| `--skip-hooks` | Claude Code hooksの自動登録をスキップ |

### `clawd-badge notify <state>`

バッジへ現在の状態を通知します。通常はClaude Codeのhooksから自動的に呼び出されます。

- `state` は `working` `done` `approval` `notify` `idle` `error` のいずれか。
- 送信先の解決順序: 環境変数 `CLAUDE_BADGE_HOST` → 設定ファイル(`~/.clawd-badge.json`)の `host` → mDNS名 `clawd-badge.local`。最初の候補で失敗した場合、次の候補に自動フォールバックします。
- タイムアウトは3秒。**バッジに到達できない場合でも常に終了コード0・原則無出力**です(Claude Codeの動作をブロックしないための仕様です)。
- 対話起動でない場合(Claude Code hooks経由など)は stdin から hook の JSON(`session_id` / `message` / `prompt`)を読み取り、`session_id` の先頭8文字を `sid`、`message`(または `prompt`)を64文字に切り詰めて `msg` として通知payloadに含めます。複数セッション対応のバッジ側でセッションを識別するために使われます。stdinの読み取り・解析に失敗した場合は `sid`/`msg` を付けずに送信します。

### `clawd-badge status`

現在の送信先に `GET /status` を問い合わせ、状態・IP・電波強度(RSSI)・稼働時間を表示します。到達できない場合は日本語のエラーメッセージを表示します。

### `clawd-badge unhook`

`setup` で登録したClaude Code hooksを `~/.claude/settings.json` から削除します。

## 環境変数

| 変数名 | 説明 |
| --- | --- |
| `CLAUDE_BADGE_HOST` | 通知先ホストを明示的に指定します(設定ファイルより優先されます)。IPアドレスまたはホスト名を指定できます。 |

## 設定ファイル

`~/.clawd-badge.json` に以下の内容が保存されます。

```json
{
  "host": "192.168.1.50",
  "hostname": "clawd-badge.local"
}
```

`host` はsetup時に取得したIPアドレス、`hostname` はmDNS名のフォールバック先です。手動で編集しても構いません。

## トラブルシューティング

- **`serialport` が見つからないというエラーが出る**
  `setup` コマンドのシリアルプロビジョニングにのみ `serialport` パッケージが必要です。`npm install -g serialport`(またはローカルインストール時は `npm install serialport`)を実行してから再度お試しください。`--host` オプションを使えばシリアル接続なしでセットアップできます。

- **シリアルポートが自動検出されない**
  他のプロセス(シリアルモニタ等)がポートを占有していないか確認してください。`--port COM5` のように明示的に指定することもできます。

- **WiFi接続がタイムアウトする**
  SSID・パスワードが正しいか、バッジデバイスが2.4GHz帯のWiFiに対応した範囲内にあるか確認してください。

- **`notify` を実行しても何も起きない**
  仕様上、送信に失敗しても無出力・終了コード0となります。動作確認には `clawd-badge status` を使い、バッジへの到達性を確認してください。`CLAUDE_BADGE_HOST` 環境変数やIPアドレス直指定(`clawd-badge setup --host <ip>`)で解決先を明示するのも有効です。

- **hooksを一時的に無効化したい**
  `clawd-badge unhook` を実行するか、`~/.claude/settings.json` の該当エントリを手動で削除してください。
