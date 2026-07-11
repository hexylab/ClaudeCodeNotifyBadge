// clawd-badge setup の実処理
import { saveConfig } from "./config.js";
import { addHooks } from "./hooks.js";
import { ask, askHidden } from "./prompt.js";
import { findEspressifPort, provisionWifi } from "./serial.js";

/**
 * setup コマンド本体。
 * options: { ssid, pass, host, skipHooks, port }
 */
export async function runSetup(options) {
  if (options.host) {
    // シリアルプロビジョニングをスキップし、IPの保存のみ行う
    saveConfig({ host: options.host });
    console.log(`設定を保存しました: host=${options.host}`);
    maybeRegisterHooks(options.skipHooks);
    return;
  }

  let ssid = options.ssid;
  let pass = options.pass;

  if (!ssid) {
    ssid = (await ask("WiFi SSID: ")).trim();
  }
  if (!ssid) {
    throw new Error("SSIDが指定されていません");
  }
  if (!pass) {
    pass = await askHidden("WiFi パスワード: ");
  }

  console.log("バッジデバイスのシリアルポートを検索しています...");
  const portPath = options.port || (await findEspressifPort());
  if (!portPath) {
    throw new Error(
      "バッジデバイスのシリアルポートが見つかりませんでした。" +
        "USB接続を確認するか、--port オプションでポート名(例: COM5)を指定してください。"
    );
  }
  console.log(`ポート ${portPath} でWiFi設定を送信します...`);

  let result;
  try {
    result = await provisionWifi({ portPath, ssid, pass, timeoutMs: 40000 });
  } catch (err) {
    throw new Error(`WiFiプロビジョニングに失敗しました: ${err.message}`);
  }

  console.log(`WiFi接続に成功しました: ip=${result.ip} hostname=${result.hostname ?? "-"}`);
  saveConfig({ host: result.ip });
  console.log("設定を保存しました。");

  maybeRegisterHooks(options.skipHooks);
}

function maybeRegisterHooks(skipHooks) {
  if (skipHooks) {
    console.log("--skip-hooks が指定されたため、Claude Code hooks の登録はスキップしました。");
    return;
  }
  const added = addHooks();
  if (added.length > 0) {
    console.log(`Claude Code hooks を登録しました: ${added.join(", ")}`);
  } else {
    console.log("Claude Code hooks は既に登録済みです(変更なし)。");
  }
}
