// 対話プロンプト用の簡易ユーティリティ(追加依存なし)
import readline from "node:readline";

const CTRL_C = "";
const BACKSPACE = "";
const DELETE = "";

/**
 * 通常の対話入力(エコーあり)。
 */
export function ask(question) {
  const rl = readline.createInterface({ input: process.stdin, output: process.stdout });
  return new Promise((resolve) => {
    rl.question(question, (answer) => {
      rl.close();
      resolve(answer);
    });
  });
}

/**
 * パスワード等の伏せ字入力。TTYであれば入力文字を "*" に置き換えて表示する。
 * TTYでない場合(パイプ等)は通常入力にフォールバックする。
 */
export function askHidden(question) {
  if (!process.stdin.isTTY) return ask(question);

  return new Promise((resolve) => {
    process.stdout.write(question);
    const stdin = process.stdin;
    let value = "";

    const cleanup = () => {
      stdin.setRawMode?.(false);
      stdin.pause();
      stdin.off("data", onData);
    };

    const onData = (chunk) => {
      const str = chunk.toString("utf8");
      for (const ch of str) {
        if (ch === "\n" || ch === "\r") {
          cleanup();
          process.stdout.write("\n");
          resolve(value);
          return;
        } else if (ch === CTRL_C) {
          cleanup();
          process.stdout.write("\n");
          process.exit(130);
        } else if (ch === BACKSPACE || ch === DELETE) {
          if (value.length > 0) {
            value = value.slice(0, -1);
            process.stdout.write("\b \b");
          }
        } else {
          value += ch;
          process.stdout.write("*");
        }
      }
    };

    stdin.setRawMode?.(true);
    stdin.resume();
    stdin.on("data", onData);
  });
}
