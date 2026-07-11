// ~/.claude/settings.json への hooks 登録・解除を担当するモジュール
// 既存の設定を壊さないよう、読み込み→必要な差分のみ追記・削除する。
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { fileURLToPath } from "node:url";

export const SETTINGS_PATH = path.join(os.homedir(), ".claude", "settings.json");

// plugin/hooks/hooks.json と同じイベント対応
export const EVENT_STATE_MAP = {
  UserPromptSubmit: "working",
  Stop: "done",
  Notification: "approval",
  SessionEnd: "idle",
};

// clawd-badge は npm レジストリ未公開のため `npx -y clawd-badge` は使えない
// (レジストリ取得に失敗する、または同名の第三者パッケージが実行される危険がある)。
// 代わりに、このモジュール自身から見た bin/clawd-badge.js の絶対パスを
// `node <絶対パス> notify <state>` の形で登録する。
export function resolveCliPath() {
  return fileURLToPath(new URL("../bin/clawd-badge.js", import.meta.url));
}

function buildArgs(state, cliPath) {
  return [cliPath, "notify", state];
}

function argsEqual(a, b) {
  if (!Array.isArray(a) || !Array.isArray(b) || a.length !== b.length) return false;
  return a.every((v, i) => v === b[i]);
}

/**
 * clawd-badge が登録した(またはしうる)hookエントリかどうかをゆるく判定する。
 * - 新形式: command "node", args [<clawd-badgeのbinパス>, "notify", <state>]
 *   パスはマシン・インストール方法ごとに異なるため、args[0]に "clawd-badge" を
 *   含むかどうかで判定する(厳密なパス一致は行わない)。
 * - 旧形式(過去バージョンで登録された可能性がある未公開npxコマンド)も
 *   削除対象に含めるため、あわせて判定する: command "npx",
 *   args ["-y", "clawd-badge", "notify", <state>]
 */
function isClawdBadgeHook(hookEntry, state) {
  if (!hookEntry || hookEntry.type !== "command") return false;
  const args = Array.isArray(hookEntry.args) ? hookEntry.args : [];

  if (hookEntry.command === "node") {
    if (args.length < 3) return false;
    const [cliPath, cmd, cmdState] = args.slice(-3);
    return (
      typeof cliPath === "string" &&
      cliPath.includes("clawd-badge") &&
      cmd === "notify" &&
      cmdState === state
    );
  }

  if (hookEntry.command === "npx") {
    // 旧形式(未公開レジストリ経由で失敗しうるため現在は登録しないが、
    // 過去に登録されたものは unhook で確実に消せるようにする)
    return argsEqual(args, ["-y", "clawd-badge", "notify", state]);
  }

  return false;
}

/**
 * 設定ファイルを読み込む。存在しなければ空オブジェクト。
 */
function loadSettings() {
  try {
    const raw = fs.readFileSync(SETTINGS_PATH, "utf8");
    const data = JSON.parse(raw);
    if (data && typeof data === "object") return data;
    return {};
  } catch {
    return {};
  }
}

function saveSettings(settings) {
  fs.mkdirSync(path.dirname(SETTINGS_PATH), { recursive: true });
  fs.writeFileSync(SETTINGS_PATH, JSON.stringify(settings, null, 2) + "\n", "utf8");
}

/**
 * clawd-badge 用の hooks を ~/.claude/settings.json にマージ登録する。
 * 同一コマンドが既に登録済みならそのイベントには追加しない(重複防止)。
 * 戻り値: 実際に追加したイベント名の配列
 */
export function addHooks() {
  const cliPath = resolveCliPath();
  const settings = loadSettings();
  if (!settings.hooks || typeof settings.hooks !== "object") settings.hooks = {};

  const added = [];

  for (const [event, state] of Object.entries(EVENT_STATE_MAP)) {
    if (!Array.isArray(settings.hooks[event])) settings.hooks[event] = [];

    const alreadyRegistered = settings.hooks[event].some(
      (matcher) =>
        Array.isArray(matcher?.hooks) &&
        matcher.hooks.some((h) => isClawdBadgeHook(h, state))
    );

    if (alreadyRegistered) continue;

    settings.hooks[event].push({
      hooks: [
        {
          type: "command",
          command: "node",
          args: buildArgs(state, cliPath),
          timeout: 10,
          async: true,
        },
      ],
    });
    added.push(event);
  }

  saveSettings(settings);
  return added;
}

/**
 * clawd-badge 用に登録した hooks を ~/.claude/settings.json から削除する。
 * 戻り値: 実際に削除したイベント名の配列
 */
export function removeHooks() {
  const settings = loadSettings();
  if (!settings.hooks || typeof settings.hooks !== "object") return [];

  const removed = [];

  for (const [event, state] of Object.entries(EVENT_STATE_MAP)) {
    const matchers = settings.hooks[event];
    if (!Array.isArray(matchers)) continue;

    let changed = false;
    const nextMatchers = [];

    for (const matcher of matchers) {
      if (!Array.isArray(matcher?.hooks)) {
        nextMatchers.push(matcher);
        continue;
      }
      const filteredHooks = matcher.hooks.filter((h) => !isClawdBadgeHook(h, state));
      if (filteredHooks.length !== matcher.hooks.length) changed = true;
      if (filteredHooks.length > 0) {
        nextMatchers.push({ ...matcher, hooks: filteredHooks });
      }
    }

    if (changed) {
      settings.hooks[event] = nextMatchers;
      removed.push(event);
    }
  }

  if (removed.length > 0) saveSettings(settings);
  return removed;
}
