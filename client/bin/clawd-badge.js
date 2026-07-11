#!/usr/bin/env node
// clawd-badge CLI エントリポイント
import { notify } from "../lib/notify.js";
import { getStatus } from "../lib/status.js";
import { runSetup } from "../lib/setup.js";
import { removeHooks } from "../lib/hooks.js";

const HELP = `clawd-badge - LAN内のESP32バッジデバイスへClaude Codeの状態を送信するCLI

使い方:
  clawd-badge setup [--ssid <ssid>] [--pass <pass>] [--host <ip>] [--skip-hooks] [--port <COMx>]
    バッジデバイスのWiFi接続をセットアップし、Claude Code hooksを登録します。
    --host を指定した場合はシリアルプロビジョニングを行わず、IPの保存のみ行います。

  clawd-badge notify <state>
    バッジへ状態を通知します。state: working|done|approval|notify|idle|error
    (Claude Code hooksから呼ばれる想定。失敗しても常に exit 0)

  clawd-badge status
    バッジの現在の状態を取得して表示します。

  clawd-badge unhook
    setupで登録したClaude Code hooksを削除します。

  clawd-badge --help
    このヘルプを表示します。

環境変数:
  CLAUDE_BADGE_HOST   送信先ホストを明示的に指定(設定ファイルより優先)
`;

function parseArgs(argv) {
  const args = { _: [] };
  for (let i = 0; i < argv.length; i++) {
    const arg = argv[i];
    if (arg.startsWith("--")) {
      const key = arg.slice(2);
      const next = argv[i + 1];
      if (next !== undefined && !next.startsWith("--")) {
        args[key] = next;
        i++;
      } else {
        args[key] = true;
      }
    } else {
      args._.push(arg);
    }
  }
  return args;
}

async function main() {
  const argv = process.argv.slice(2);
  const args = parseArgs(argv);
  const command = args._[0];

  if (!command || args.help || args.h) {
    console.log(HELP);
    return 0;
  }

  switch (command) {
    case "notify": {
      const state = args._[1];
      // 失敗してもClaude Codeの動作を妨げないため、常に exit 0・原則無出力とする
      try {
        await notify(state);
      } catch {
        // 無視
      }
      return 0;
    }

    case "status": {
      try {
        const result = await getStatus();
        if (!result) {
          console.error("バッジデバイスに到達できませんでした。接続状態を確認してください。");
          return 1;
        }
        const { host, data } = result;
        console.log(`ホスト: ${host}`);
        console.log(`状態: ${data.state ?? "-"}`);
        console.log(`IP: ${data.ip ?? "-"}`);
        console.log(`RSSI: ${data.rssi ?? "-"}`);
        console.log(`稼働時間: ${data.uptime ?? "-"}`);
        return 0;
      } catch (err) {
        console.error(`エラー: ${err.message}`);
        return 1;
      }
    }

    case "setup": {
      try {
        await runSetup({
          ssid: typeof args.ssid === "string" ? args.ssid : undefined,
          pass: typeof args.pass === "string" ? args.pass : undefined,
          host: typeof args.host === "string" ? args.host : undefined,
          port: typeof args.port === "string" ? args.port : undefined,
          skipHooks: Boolean(args["skip-hooks"]),
        });
        return 0;
      } catch (err) {
        console.error(`エラー: ${err.message}`);
        return 1;
      }
    }

    case "unhook": {
      try {
        const removed = removeHooks();
        if (removed.length > 0) {
          console.log(`Claude Code hooks を削除しました: ${removed.join(", ")}`);
        } else {
          console.log("登録済みのclawd-badge hooksは見つかりませんでした。");
        }
        return 0;
      } catch (err) {
        console.error(`エラー: ${err.message}`);
        return 1;
      }
    }

    default:
      console.error(`不明なコマンドです: ${command}\n`);
      console.log(HELP);
      return 1;
  }
}

main()
  .then((code) => process.exit(code ?? 0))
  .catch((err) => {
    // notify以外の予期しないエラー。notifyは内部でcatch済みのためここには来ない想定。
    console.error(`予期しないエラー: ${err?.message ?? err}`);
    process.exit(1);
  });
