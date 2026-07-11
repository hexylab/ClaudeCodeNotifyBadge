// バッジデバイスの送信先(ホスト)候補を解決するモジュール
import { loadConfig, DEFAULT_HOSTNAME } from "./config.js";

/**
 * 送信先候補を優先順で配列で返す。
 * 優先順: 環境変数 CLAUDE_BADGE_HOST > 設定ファイルの host > mDNS名(clawd-badge.local)
 * 重複は除去する。
 */
export function resolveHostCandidates() {
  const candidates = [];
  const envHost = process.env.CLAUDE_BADGE_HOST;
  if (envHost) candidates.push(envHost);

  const config = loadConfig();
  if (config.host) candidates.push(config.host);

  candidates.push(config.hostname || DEFAULT_HOSTNAME);

  return [...new Set(candidates.filter(Boolean))];
}
