// clawd-badge notify <state> の実処理
// Claude Code の hooks から呼ばれる想定のため、いかなる失敗でも例外を投げず
// 呼び出し元(bin側)で必ず exit 0・無出力にできるようにする。
import fs from "node:fs";
import os from "node:os";
import path from "node:path";
import { resolveHostCandidates } from "./resolve.js";

const TIMEOUT_MS = 3000;
const VALID_STATES = ["working", "done", "approval", "notify", "idle", "error"];

// Claude Code の Notification hook は権限承認以外(アイドル入力待ち等)でも発火する。
// stdin JSON の notification_type のうち、実際にユーザーの承認操作が必要な種別のみを
// approval 表示の対象とする(それ以外は idle_prompt 等の情報通知であり、
// 入力待ち相当は Stop フックの done で既にカバーされるため送信しない)。
const APPROVAL_NOTIFICATION_TYPES = new Set([
  "permission_prompt", // ツール実行の許可承認待ち
  "elicitation_dialog", // MCPサーバーがユーザー入力を要求
  "agent_needs_input", // バックグラウンドセッションが入力待ち
]);

// デバッグログ(~/.clawd-badge.log)。設定ファイル(CONFIG_PATH)と同じ場所に置く。
const LOG_PATH = path.join(os.homedir(), ".clawd-badge.log");
const LOG_ROTATE_SIZE = 256 * 1024;

/**
 * hooks の stdin から渡される JSON(session_id, message, prompt 等)を読み取る。
 * TTY(対話起動)の場合は読みに行かない(ブロック回避)。
 * hooks からは stdin が即座に close されるため、ここでは特別なタイムアウト処理は行わない。
 * 読み取り・パースに失敗した場合は空オブジェクトを返す(呼び出し元は sid/msg なしで送信を継続する)。
 */
async function readStdinHook() {
  if (process.stdin.isTTY) return {};
  try {
    const chunks = [];
    for await (const chunk of process.stdin) {
      chunks.push(chunk);
    }
    // PowerShellのパイプ等が先頭に付けるUTF-8 BOMを除去してからパースする
    const raw = Buffer.concat(chunks).toString("utf-8").replace(/^\uFEFF/, "").trim();
    if (!raw) return {};
    return JSON.parse(raw);
  } catch {
    return {};
  }
}

/**
 * ローカルタイムゾーンでのISO 8601形式の日時文字列を返す(例: 2026-07-11T21:00:00.000+09:00)。
 */
function toLocalISOString(date) {
  const offsetMin = -date.getTimezoneOffset();
  const sign = offsetMin >= 0 ? "+" : "-";
  const pad = (n) => String(Math.trunc(Math.abs(n))).padStart(2, "0");
  const offset = `${sign}${pad(offsetMin / 60)}:${pad(offsetMin % 60)}`;
  const local = new Date(date.getTime() - date.getTimezoneOffset() * 60000);
  return local.toISOString().replace("Z", offset);
}

/**
 * デバッグログ(~/.clawd-badge.log)へJSON 1行を追記する。
 * ログ書き込みの失敗は notify() 全体の「失敗しても無出力・exit 0」設計を壊さないよう、
 * ここで完全に握りつぶす(呼び出し元へは絶対に例外を伝播させない)。
 */
function appendLog(entry) {
  try {
    try {
      const stat = fs.statSync(LOG_PATH);
      if (stat.size > LOG_ROTATE_SIZE) {
        fs.renameSync(LOG_PATH, `${LOG_PATH}.1`);
      }
    } catch {
      // ファイルが無い、またはリネームに失敗しても追記は継続する
    }
    // notification_type/sid/msg は値がある場合のみ出力する(古いClaude Codeやstdinなしの場合は省略)
    const record = { ts: toLocalISOString(new Date()), state: entry.state, sent: entry.sent };
    if (entry.notification_type) record.notification_type = entry.notification_type;
    if (entry.sid) record.sid = entry.sid;
    if (entry.msg) record.msg = entry.msg;
    record.result = entry.result;
    fs.appendFileSync(LOG_PATH, JSON.stringify(record) + "\n", "utf8");
  } catch {
    // ログ書き込みの失敗は無視する
  }
}

/**
 * 指定ホストへ POST /notify を送る。成功すれば true。
 */
async function sendTo(host, payload) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const res = await fetch(`http://${host}/notify`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(payload),
      signal: controller.signal,
    });
    return res.ok;
  } catch {
    return false;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * state をバッジへ通知する。候補ホストへ順にフォールバックしながら送信する。
 * 全滅しても例外は投げない(呼び出し元は常に成功扱いでよい)。
 * hooks の stdin から session_id(先頭8文字を sid として付加)・message/prompt(64文字に切り詰めて msg として付加)を拾う。
 *
 * targetState が approval かつ stdin JSON に notification_type がある場合は、
 * APPROVAL_NOTIFICATION_TYPES に含まれる種別のときのみ実際に送信する(それ以外は
 * idle_prompt 等の情報通知のため送信をスキップし、バッジの状態は変えない)。
 * notification_type が無い場合(古い Claude Code)は後方互換として従来どおり送信する。
 */
export async function notify(state) {
  const targetState = VALID_STATES.includes(state) ? state : "notify";

  let sid = "";
  let msg = "";
  let notificationType = "";
  try {
    const hook = await readStdinHook();
    if (hook && typeof hook === "object") {
      if (typeof hook.session_id === "string") sid = hook.session_id.slice(0, 8);
      const rawMsg = hook.message || hook.prompt || "";
      if (typeof rawMsg === "string") msg = rawMsg.slice(0, 64);
      if (typeof hook.notification_type === "string") notificationType = hook.notification_type;
    }
  } catch {
    // sid/msg/notification_type なしで送信を継続
  }

  // approval かつ notification_type が判明していて、かつ承認系種別でない場合のみスキップする
  if (targetState === "approval" && notificationType && !APPROVAL_NOTIFICATION_TYPES.has(notificationType)) {
    appendLog({
      state,
      sent: null,
      notification_type: notificationType,
      sid,
      msg,
      result: "skipped",
    });
    return true;
  }

  const payload = { state: targetState, msg, sid, ts: Math.floor(Date.now() / 1000) };

  const candidates = resolveHostCandidates();
  for (const host of candidates) {
    try {
      const ok = await sendTo(host, payload);
      if (ok) {
        appendLog({
          state,
          sent: targetState,
          notification_type: notificationType,
          sid,
          msg,
          result: host,
        });
        return true;
      }
    } catch {
      // 次の候補へフォールバック
    }
  }
  appendLog({
    state,
    sent: targetState,
    notification_type: notificationType,
    sid,
    msg,
    result: "failed",
  });
  return false;
}
