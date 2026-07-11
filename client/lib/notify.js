// clawd-badge notify <state> の実処理
// Claude Code の hooks から呼ばれる想定のため、いかなる失敗でも例外を投げず
// 呼び出し元(bin側)で必ず exit 0・無出力にできるようにする。
import { resolveHostCandidates } from "./resolve.js";

const TIMEOUT_MS = 3000;
const VALID_STATES = ["working", "done", "approval", "notify", "idle", "error"];

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
 */
export async function notify(state) {
  const targetState = VALID_STATES.includes(state) ? state : "notify";

  let sid = "";
  let msg = "";
  try {
    const hook = await readStdinHook();
    if (hook && typeof hook === "object") {
      if (typeof hook.session_id === "string") sid = hook.session_id.slice(0, 8);
      const rawMsg = hook.message || hook.prompt || "";
      if (typeof rawMsg === "string") msg = rawMsg.slice(0, 64);
    }
  } catch {
    // sid/msg なしで送信を継続
  }

  const payload = { state: targetState, msg, sid, ts: Math.floor(Date.now() / 1000) };

  const candidates = resolveHostCandidates();
  for (const host of candidates) {
    try {
      const ok = await sendTo(host, payload);
      if (ok) return true;
    } catch {
      // 次の候補へフォールバック
    }
  }
  return false;
}
