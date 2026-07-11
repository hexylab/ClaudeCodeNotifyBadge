// clawd-badge notify <state> の実処理
// Claude Code の hooks から呼ばれる想定のため、いかなる失敗でも例外を投げず
// 呼び出し元(bin側)で必ず exit 0・無出力にできるようにする。
import { resolveHostCandidates } from "./resolve.js";

const TIMEOUT_MS = 3000;
const VALID_STATES = ["working", "done", "approval", "notify", "idle", "error"];

/**
 * 指定ホストへ POST /notify を送る。成功すれば true。
 */
async function sendTo(host, state) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const res = await fetch(`http://${host}/notify`, {
      method: "POST",
      headers: { "content-type": "application/json" },
      body: JSON.stringify({ state, ts: Math.floor(Date.now() / 1000) }),
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
 */
export async function notify(state) {
  const targetState = VALID_STATES.includes(state) ? state : "notify";
  const candidates = resolveHostCandidates();
  for (const host of candidates) {
    try {
      const ok = await sendTo(host, targetState);
      if (ok) return true;
    } catch {
      // 次の候補へフォールバック
    }
  }
  return false;
}
