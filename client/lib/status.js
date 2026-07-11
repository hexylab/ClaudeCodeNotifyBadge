// clawd-badge status の実処理
import { resolveHostCandidates } from "./resolve.js";

const TIMEOUT_MS = 3000;

async function fetchStatus(host) {
  const controller = new AbortController();
  const timer = setTimeout(() => controller.abort(), TIMEOUT_MS);
  try {
    const res = await fetch(`http://${host}/status`, { signal: controller.signal });
    if (!res.ok) return null;
    return await res.json();
  } catch {
    return null;
  } finally {
    clearTimeout(timer);
  }
}

/**
 * 送信先候補へ順に /status を問い合わせ、成功した結果と使用ホストを返す。
 * すべて失敗した場合は null を返す。
 */
export async function getStatus() {
  const candidates = resolveHostCandidates();
  for (const host of candidates) {
    const data = await fetchStatus(host);
    if (data) return { host, data };
  }
  return null;
}
