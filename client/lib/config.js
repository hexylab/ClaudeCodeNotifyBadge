// ~/.clawd-badge.json の読み書きを担当するモジュール
import fs from "node:fs";
import os from "node:os";
import path from "node:path";

export const CONFIG_PATH = path.join(os.homedir(), ".clawd-badge.json");
export const DEFAULT_HOSTNAME = "clawd-badge.local";

/**
 * 設定ファイルを読み込む。存在しない・壊れている場合は空オブジェクトを返す。
 */
export function loadConfig() {
  try {
    const raw = fs.readFileSync(CONFIG_PATH, "utf8");
    const data = JSON.parse(raw);
    if (data && typeof data === "object") return data;
    return {};
  } catch {
    return {};
  }
}

/**
 * 設定ファイルへ保存する(既存キーは維持しつつマージ)。
 */
export function saveConfig(partial) {
  const current = loadConfig();
  const next = {
    hostname: DEFAULT_HOSTNAME,
    ...current,
    ...partial,
  };
  fs.writeFileSync(CONFIG_PATH, JSON.stringify(next, null, 2) + "\n", "utf8");
  return next;
}
