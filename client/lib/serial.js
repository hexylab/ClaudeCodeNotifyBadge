// USBシリアル経由のWiFiプロビジョニングを担当するモジュール。
// setup コマンドでのみ使用するため、serialport は本モジュール内で遅延importする。
const BAUD = 115200;
const ESPRESSIF_VID = 0x303a;

/**
 * serialport パッケージを遅延importする。未インストールなら分かりやすいエラーを投げる。
 */
async function loadSerialPort() {
  try {
    const mod = await import("serialport");
    return mod.SerialPort;
  } catch (err) {
    throw new Error(
      "serialport パッケージが見つかりません。`npm install -g serialport` " +
        "または `npm install serialport` を実行してから再度お試しください。\n" +
        `(詳細: ${err.message})`
    );
  }
}

/**
 * Espressif (VID 0x303A) のUSBシリアルポートを自動検出する。
 * 見つからなければ null を返す。
 */
export async function findEspressifPort() {
  const SerialPort = await loadSerialPort();
  const ports = await SerialPort.list();
  const found = ports.find((p) => {
    if (!p.vendorId) return false;
    return parseInt(p.vendorId, 16) === ESPRESSIF_VID;
  });
  return found ? found.path : null;
}

/**
 * ポートを開く。ESP32-C6 の USB-Serial/JTAG は DTR/RTS の変化でリセットされるため、
 * hupcl を無効化した上で、開いた直後に DTR=false, RTS=false を明示的に設定する。
 */
async function openPort(portPath) {
  const SerialPort = await loadSerialPort();
  const port = new SerialPort({
    path: portPath,
    baudRate: BAUD,
    autoOpen: false,
    hupcl: false,
  });

  await new Promise((resolve, reject) => {
    port.open((err) => (err ? reject(err) : resolve()));
  });

  await new Promise((resolve, reject) => {
    port.set({ dtr: false, rts: false }, (err) => (err ? reject(err) : resolve()));
  });

  return port;
}

/**
 * ポートへ1行分のJSONコマンドを書き込む。
 */
function writeLine(port, obj) {
  return new Promise((resolve, reject) => {
    port.write(JSON.stringify(obj) + "\n", "utf8", (err) => {
      if (err) return reject(err);
      port.drain((drainErr) => (drainErr ? reject(drainErr) : resolve()));
    });
  });
}

/**
 * ポートから受信するテキストを行単位のJSONイベントに変換して逐次コールバックする。
 * 戻り値は購読解除用の関数。
 */
function onJsonLine(port, callback) {
  let buffer = "";
  const onData = (chunk) => {
    buffer += chunk.toString("utf8");
    let idx;
    while ((idx = buffer.indexOf("\n")) >= 0) {
      const line = buffer.slice(0, idx).trim();
      buffer = buffer.slice(idx + 1);
      if (!line) continue;
      try {
        callback(JSON.parse(line));
      } catch {
        // JSONとして解釈できない行は無視する
      }
    }
  };
  port.on("data", onData);
  return () => port.off("data", onData);
}

function closePort(port) {
  return new Promise((resolve) => {
    if (!port || port.closing || !port.isOpen) return resolve();
    port.close(() => resolve());
  });
}

/**
 * WiFi認証情報をデバイスへ送信し、接続完了(またはタイムアウト/失敗)まで待つ。
 * 成功時: { ip, hostname } を返す
 * 失敗時: Error を投げる
 */
export async function provisionWifi({ portPath, ssid, pass, timeoutMs = 40000 }) {
  const port = await openPort(portPath);

  try {
    const result = await new Promise((resolve, reject) => {
      let unsubscribe;
      const timer = setTimeout(() => {
        unsubscribe?.();
        reject(new Error(`WiFi接続がタイムアウトしました(${Math.floor(timeoutMs / 1000)}秒)`));
      }, timeoutMs);

      unsubscribe = onJsonLine(port, (event) => {
        if (event?.event !== "wifi") return;
        if (event.status === "connected") {
          clearTimeout(timer);
          unsubscribe();
          resolve({ ip: event.ip, hostname: event.hostname });
        } else if (event.status === "failed") {
          clearTimeout(timer);
          unsubscribe();
          reject(new Error("デバイスがWiFi接続に失敗しました(SSID/パスワードを確認してください)"));
        }
        // "connecting" はそのまま待機継続
      });

      writeLine(port, { cmd: "wifi", ssid, pass }).catch((err) => {
        clearTimeout(timer);
        unsubscribe();
        reject(err);
      });
    });
    return result;
  } finally {
    await closePort(port);
  }
}

/**
 * デバイスの現在のWiFi状態を問い合わせる。
 */
export async function queryWifiStatus(portPath, timeoutMs = 5000) {
  const port = await openPort(portPath);
  try {
    const result = await new Promise((resolve, reject) => {
      let unsubscribe;
      const timer = setTimeout(() => {
        unsubscribe?.();
        reject(new Error("デバイスからの応答がタイムアウトしました"));
      }, timeoutMs);

      unsubscribe = onJsonLine(port, (event) => {
        if (event?.event !== "wifi") return;
        clearTimeout(timer);
        unsubscribe();
        resolve(event);
      });

      writeLine(port, { cmd: "wifi_status" }).catch((err) => {
        clearTimeout(timer);
        unsubscribe();
        reject(err);
      });
    });
    return result;
  } finally {
    await closePort(port);
  }
}
