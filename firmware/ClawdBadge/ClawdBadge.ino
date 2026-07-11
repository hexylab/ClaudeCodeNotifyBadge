/*
 * ClawdBadge - Claude Code ステータス通知バッジ
 * 対象基板: Waveshare ESP32-C6-Touch-LCD-1.47 (JD9853, 172x320, タッチ AXS5106L)
 *
 * PCで動作する Claude Code の状態を USB CDC シリアル (115200bps) または
 * WiFi経由のHTTP(POST /notify)から1メッセージのJSONで受信し、
 * Anthropicデザインのマスコット「Clawd」(オレンジ色のかわいいカニ) を
 * 状態に応じてアニメーション表示する。
 *
 * 受信JSON例: {"state":"working","sid":"a1b2c3d4","msg":"...","ts":1234567890}
 * state: working | done | approval | notify | idle | error (未知は notify 扱い。
 *        ただしHTTP /notify では未知stateは400エラーを返す)
 * sid: セッション識別子(任意)。複数のClaude Codeセッションを個別に管理し、
 *      画面には優先度(error>approval>notify>working>done)で集約した状態を表示する。
 *      sid省略時は固定sid "_nosid" 扱い(旧クライアント互換)。
 *
 * WiFi: シリアル経由で {"cmd":"wifi","ssid":"...","pass":"..."} を送ると
 * NVS(Preferences)に認証情報を保存して接続する。接続後はHTTPサーバ(ポート80)と
 * mDNS(clawd-badge.local)が有効になり、USBを介さずLAN経由で状態通知を受け付ける。
 * WiFi接続はloop内で非ブロッキングに処理され、未接続でもUSBシリアル動作に影響しない。
 *
 * LCDコントローラは JD9853 (ST7789互換コマンドセット)。Arduino_GFXに専用クラスが
 * 無いため Arduino_ST7789 を流用し、begin() 直後にWaveshare公式デモ由来の
 * JD9853専用レジスタ初期化列 (lcd_reg_init) を送ることで正しく初期化する。
 * 本機はRGB LED非搭載のため updateLed() の rgbLedWrite 呼び出しは無効化する。
 */

#include <Arduino_GFX_Library.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include "ClawdTypes.h"
#include "clawd_sprites.h"

// ============================================================
// ピン定義 (Waveshare ESP32-C6-Touch-LCD-1.47 公式デモ確定値)
// ============================================================
#define PIN_LCD_MOSI 2
#define PIN_LCD_SCLK 1
#define PIN_LCD_CS 14
#define PIN_LCD_DC 15
#define PIN_LCD_RST 22
#define PIN_LCD_BL 23 // アクティブHigh
#define PIN_WS2812 8  // 本機には非搭載 (ENABLE_RGB_LED=0 で無効化)

// RGB LED (本機は非搭載)
#define ENABLE_RGB_LED 0

// パネル仕様
#define LCD_W 172
#define LCD_H 320
#define LCD_ROTATION 2 // 縦向き 172x320 (旧ランドスケープの右辺が上)。実機確認済み: 0だと上下逆
#define LCD_COL_OFFSET1 34
#define LCD_ROW_OFFSET1 0
#define LCD_COL_OFFSET2 34
#define LCD_ROW_OFFSET2 0

// 回転後の実座標系サイズ (rotation=0/2 でポートレート)
#define SCR_W 172
#define SCR_H 320

// ============================================================
// GFXバス / パネル / キャンバス
// ============================================================
Arduino_DataBus *bus = new Arduino_HWSPI(
    PIN_LCD_DC /* DC */, PIN_LCD_CS /* CS */, PIN_LCD_SCLK /* SCK */,
    PIN_LCD_MOSI /* MOSI */);

Arduino_GFX *gfxPanel = new Arduino_ST7789(
    bus, PIN_LCD_RST, LCD_ROTATION, false /* IPS */, LCD_W, LCD_H,
    LCD_COL_OFFSET1, LCD_ROW_OFFSET1, LCD_COL_OFFSET2, LCD_ROW_OFFSET2);

// ちらつき防止のためフルフレームバッファを挟む
Arduino_Canvas *canvas = new Arduino_Canvas(SCR_W, SCR_H, gfxPanel);

// ============================================================
// JD9853 専用レジスタ初期化列
// (Waveshare公式デモ Arduino/examples/01_gfx_helloworld/01_gfx_helloworld.ino
//  の lcd_reg_init() を完全な形でそのまま移植)
// ============================================================
void lcd_reg_init(void) {
  static const uint8_t init_operations[] = {
    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x11,  // 2: Out of sleep mode, no args, w/delay
    END_WRITE,
    DELAY, 120,

    BEGIN_WRITE,
    WRITE_C8_D16, 0xDF, 0x98, 0x53,
    WRITE_C8_D8, 0xB2, 0x23,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 4,
    0x00, 0x47, 0x00, 0x6F,

    WRITE_COMMAND_8, 0xBB,
    WRITE_BYTES, 6,
    0x1C, 0x1A, 0x55, 0x73, 0x63, 0xF0,

    WRITE_C8_D16, 0xC0, 0x44, 0xA4,
    WRITE_C8_D8, 0xC1, 0x16,

    WRITE_COMMAND_8, 0xC3,
    WRITE_BYTES, 8,
    0x7D, 0x07, 0x14, 0x06, 0xCF, 0x71, 0x72, 0x77,

    WRITE_COMMAND_8, 0xC4,
    WRITE_BYTES, 12,
    0x00, 0x00, 0xA0, 0x79, 0x0B, 0x0A, 0x16, 0x79, 0x0B, 0x0A, 0x16, 0x82,

    WRITE_COMMAND_8, 0xC8,
    WRITE_BYTES, 32,
    0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00, 0x3F, 0x32, 0x29, 0x29, 0x27, 0x2B, 0x27, 0x28, 0x28, 0x26, 0x25, 0x17, 0x12, 0x0D, 0x04, 0x00,

    WRITE_COMMAND_8, 0xD0,
    WRITE_BYTES, 5,
    0x04, 0x06, 0x6B, 0x0F, 0x00,

    WRITE_C8_D16, 0xD7, 0x00, 0x30,
    WRITE_C8_D8, 0xE6, 0x14,
    WRITE_C8_D8, 0xDE, 0x01,

    WRITE_COMMAND_8, 0xB7,
    WRITE_BYTES, 5,
    0x03, 0x13, 0xEF, 0x35, 0x35,

    WRITE_COMMAND_8, 0xC1,
    WRITE_BYTES, 3,
    0x14, 0x15, 0xC0,

    WRITE_C8_D16, 0xC2, 0x06, 0x3A,
    WRITE_C8_D16, 0xC4, 0x72, 0x12,
    WRITE_C8_D8, 0xBE, 0x00,
    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x01, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x35, 0x00,
    WRITE_C8_D8, 0x3A, 0x05,

    WRITE_COMMAND_8, 0x2A,
    WRITE_BYTES, 4,
    0x00, 0x22, 0x00, 0xCD,

    WRITE_COMMAND_8, 0x2B,
    WRITE_BYTES, 4,
    0x00, 0x00, 0x01, 0x3F,

    WRITE_C8_D8, 0xDE, 0x02,

    WRITE_COMMAND_8, 0xE5,
    WRITE_BYTES, 3,
    0x00, 0x02, 0x00,

    WRITE_C8_D8, 0xDE, 0x00,
    WRITE_C8_D8, 0x36, 0x00,
    WRITE_COMMAND_8, 0x21,
    END_WRITE,

    DELAY, 10,

    BEGIN_WRITE,
    WRITE_COMMAND_8, 0x29,  // 5: Main screen turn on, no args, w/delay
    END_WRITE
  };
  bus->batchOperation(init_operations, sizeof(init_operations));
}

// ============================================================
// Anthropicデザインカラーパレット (RGB565)
// ============================================================
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((uint16_t)(r & 0xF8) << 8) | ((uint16_t)(g & 0xFC) << 3) | (b >> 3);
}

const uint16_t COLOR_BG = rgb565(0x00, 0x00, 0x00);    // 背景 (黒)
const uint16_t COLOR_IVORY = rgb565(0xF0, 0xEE, 0xE6); // テキスト/UI (黒背景での視認用)
const uint16_t COLOR_CORAL = rgb565(0xD9, 0x77, 0x57); // ヘッダー/フッターUIの状態色 (working)
const uint16_t COLOR_DONE = rgb565(0x7A, 0x9B, 0x76);     // セージグリーン
const uint16_t COLOR_APPROVAL = rgb565(0xD4, 0xA2, 0x7F); // アンバー
const uint16_t COLOR_ERROR = rgb565(0xBF, 0x4D, 0x43);    // レッド
const uint16_t COLOR_IDLE = rgb565(0x91, 0x91, 0x8D);     // グレー
const uint16_t COLOR_CLOUD = rgb565(0xE8, 0xE6, 0xDC);    // フッタードット(非アクティブ)

// ============================================================
// 状態定義 (enumは ClawdTypes.h に分離)
// ============================================================
AppState currentState = ST_IDLE; // 全セッションから集約した表示用状態
unsigned long stateEnteredMs = 0;
bool bootAnimDone = false;

// タイムアウト定数
const unsigned long TIMEOUT_SHORT_MS = 5UL * 60UL * 1000UL;  // 5分
const unsigned long TIMEOUT_WORKING_MS = 30UL * 60UL * 1000UL; // 30分

// ============================================================
// セッションテーブル (複数のClaude Codeセッションを個別に管理)
// ============================================================
#define MAX_SESSIONS 8
struct Session {
  char sid[17];          // セッション識別子 (切り詰め保存、末尾\0)
  AppState state;
  unsigned long lastMsgMs;
  bool used;
};
Session sessions[MAX_SESSIONS]; // グローバル配列なのでゼロ初期化済み(used=false)

// 状態の優先度 (集約状態の決定に使用): error > approval > notify > working > done
int statePriority(AppState s) {
  switch (s) {
    case ST_ERROR: return 5;
    case ST_APPROVAL: return 4;
    case ST_NOTIFY: return 3;
    case ST_WORKING: return 2;
    case ST_DONE: return 1;
    default: return 0;
  }
}

// セッションテーブルから集約状態を再計算し、変化があれば enterState する
// (enterState は stateEnteredMs をリセットするのでアニメが再スタートする)
void updateAggregateState() {
  AppState agg = ST_IDLE;
  int best = -1;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used) continue;
    int p = statePriority(sessions[i].state);
    if (p > best) { best = p; agg = sessions[i].state; }
  }
  if (agg != currentState) enterState(agg);
}

// セッション単位で状態を適用する (シリアル/HTTP共通)。
// state==ST_IDLE は SessionEnd の意味なので、該当セッションを削除する。
void applySessionState(const String &sidIn, AppState state) {
  String s = (sidIn.length() > 0) ? sidIn : String("_nosid");
  char buf[sizeof(sessions[0].sid)];
  s.toCharArray(buf, sizeof(buf)); // 16文字に切り詰め

  int foundIdx = -1, emptyIdx = -1, oldestIdx = 0;
  unsigned long oldestMs = 0xFFFFFFFFUL;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].used && strcmp(sessions[i].sid, buf) == 0) { foundIdx = i; break; }
    if (!sessions[i].used && emptyIdx < 0) emptyIdx = i;
    if (sessions[i].used && sessions[i].lastMsgMs < oldestMs) {
      oldestMs = sessions[i].lastMsgMs;
      oldestIdx = i;
    }
  }

  if (state == ST_IDLE) {
    if (foundIdx >= 0) sessions[foundIdx].used = false;
    updateAggregateState();
    return;
  }

  int idx = (foundIdx >= 0) ? foundIdx : ((emptyIdx >= 0) ? emptyIdx : oldestIdx);
  strncpy(sessions[idx].sid, buf, sizeof(sessions[idx].sid));
  sessions[idx].sid[sizeof(sessions[idx].sid) - 1] = '\0';
  sessions[idx].state = state;
  sessions[idx].lastMsgMs = millis();
  sessions[idx].used = true;

  updateAggregateState();
}

// ============================================================
// シリアル受信バッファ
// ============================================================
String serialLineBuf;

// "state":"xxx" を単純な文字列検索で抽出
String extractStateToken(const String &line) {
  int idx = line.indexOf("\"state\"");
  if (idx < 0) return String();
  int colon = line.indexOf(':', idx);
  if (colon < 0) return String();
  int q1 = line.indexOf('"', colon + 1);
  if (q1 < 0) return String();
  int q2 = line.indexOf('"', q1 + 1);
  if (q2 < 0) return String();
  return line.substring(q1 + 1, q2);
}

// "key":"value" を単純な文字列検索で抽出する汎用版。
// \" \\ \n \t \/ の最低限のエスケープを解除する(日本語等のマルチバイトは
// UTF-8のまま無加工で通す)。
String extractStringField(const String &line, const String &key) {
  String pat = "\"" + key + "\"";
  int idx = line.indexOf(pat);
  if (idx < 0) return String();
  int colon = line.indexOf(':', idx + pat.length());
  if (colon < 0) return String();
  int q1 = line.indexOf('"', colon + 1);
  if (q1 < 0) return String();
  String out;
  int i = q1 + 1;
  int len = line.length();
  while (i < len) {
    char c = line[i];
    if (c == '\\' && i + 1 < len) {
      char n = line[i + 1];
      if (n == '"') { out += '"'; i += 2; continue; }
      if (n == '\\') { out += '\\'; i += 2; continue; }
      if (n == 'n') { out += '\n'; i += 2; continue; }
      if (n == 't') { out += '\t'; i += 2; continue; }
      if (n == '/') { out += '/'; i += 2; continue; }
      // 未対応のエスケープ(\uXXXX等)はそのまま残す
      out += c;
      i += 1;
      continue;
    }
    if (c == '"') break; // 終端(エスケープされていない引用符)
    out += c;
    i += 1;
  }
  return out;
}

AppState tokenToState(const String &tok) {
  if (tok == "working") return ST_WORKING;
  if (tok == "done") return ST_DONE;
  if (tok == "approval") return ST_APPROVAL;
  if (tok == "notify") return ST_NOTIFY;
  if (tok == "idle") return ST_IDLE;
  if (tok == "error") return ST_ERROR;
  return ST_NOTIFY; // 未知のstateはnotify扱い
}

// 既知のstate文字列かどうか (HTTP側で不明stateを400にするために使用)
bool isKnownStateToken(const String &tok) {
  return tok == "working" || tok == "done" || tok == "approval" ||
         tok == "notify" || tok == "idle" || tok == "error";
}

const char *stateToken(AppState s) {
  switch (s) {
    case ST_WORKING: return "working";
    case ST_DONE: return "done";
    case ST_APPROVAL: return "approval";
    case ST_NOTIFY: return "notify";
    case ST_IDLE: return "idle";
    case ST_ERROR: return "error";
  }
  return "idle";
}

void enterState(AppState s) {
  currentState = s;
  stateEnteredMs = millis();
}

void handleSerialLine(const String &line) {
  if (line.length() == 0) return;
  if (line.indexOf("\"cmd\"") >= 0) {
    handleSerialCommand(line);
    return;
  }
  String tok = extractStateToken(line);
  if (tok.length() == 0) return; // stateキーが無い行は無視
  String sid = extractStringField(line, "sid");
  applySessionState(sid, tokenToState(tok));
}

void pollSerial() {
  while (Serial.available() > 0) {
    char c = (char)Serial.read();
    if (c == '\n') {
      handleSerialLine(serialLineBuf);
      serialLineBuf = "";
    } else if (c != '\r') {
      if (serialLineBuf.length() < 512) serialLineBuf += c;
    }
  }
}

// セッションごとにタイムアウトを判定し、超過したエントリを削除する
void checkSessionTimeouts() {
  unsigned long now = millis();
  bool changed = false;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used) continue;
    unsigned long limit =
        (sessions[i].state == ST_WORKING) ? TIMEOUT_WORKING_MS : TIMEOUT_SHORT_MS;
    if (now - sessions[i].lastMsgMs > limit) {
      sessions[i].used = false;
      changed = true;
    }
  }
  if (changed) updateAggregateState();
}

// ============================================================
// WiFi接続 + NVS永続化 + シリアルプロビジョニング + HTTPサーバ
// ============================================================
Preferences prefs;
WebServer httpServer(80);

bool wifiConfigured = false;      // NVSに認証情報が保存されているか
bool wifiConnecting = false;      // 接続試行中か
bool wifiAnnounceThisAttempt = false; // 今回の試行結果をシリアルに出すか
bool wifiServicesStarted = false; // HTTPサーバ/mDNSを一度でも起動したか
String wifiSavedSsid;
String wifiSavedPass;
unsigned long wifiConnectStartMs = 0;
unsigned long lastWifiRetryMs = 0;
const unsigned long WIFI_CONNECT_TIMEOUT_MS = 20000; // 約20秒
const unsigned long WIFI_RETRY_INTERVAL_MS = 15000;  // 再接続間隔

void wifiSaveCreds(const String &ssid, const String &pass) {
  prefs.begin("clawd", false);
  prefs.putString("ssid", ssid);
  prefs.putString("pass", pass);
  prefs.end();
}

void wifiLoadCreds(String &ssid, String &pass) {
  prefs.begin("clawd", true);
  ssid = prefs.getString("ssid", "");
  pass = prefs.getString("pass", "");
  prefs.end();
}

void wifiClearCreds() {
  prefs.begin("clawd", false);
  prefs.remove("ssid");
  prefs.remove("pass");
  prefs.end();
}

void printWifiEvent(const char *status) {
  Serial.print("{\"event\":\"wifi\",\"status\":\"");
  Serial.print(status);
  Serial.println("\"}");
}

void printWifiConnected() {
  Serial.print("{\"event\":\"wifi\",\"status\":\"connected\",\"ip\":\"");
  Serial.print(WiFi.localIP().toString());
  Serial.println("\",\"hostname\":\"clawd-badge.local\"}");
}

// HTTPサーバ + mDNSを(再)起動する。接続/再接続のたびに呼んでよい。
void startWifiServices() {
  httpServer.begin();
  wifiServicesStarted = true;
  if (MDNS.begin("clawd-badge")) {
    MDNS.addService("http", "tcp", 80);
  }
}

// 非ブロッキングでWiFi接続を開始する。announce=trueなら結果をシリアルへ出力する。
void startWifiConnect(const String &ssid, const String &pass, bool announce) {
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid.c_str(), pass.c_str());
  wifiConnecting = true;
  wifiAnnounceThisAttempt = announce;
  wifiConnectStartMs = millis();
  if (announce) printWifiEvent("connecting");
}

// POST /notify: 既存シリアルと同形式のJSONを受け取り状態遷移する
void handleHttpNotify() {
  String body = httpServer.hasArg("plain") ? httpServer.arg("plain") : String();
  String tok = extractStateToken(body);
  if (tok.length() == 0 || !isKnownStateToken(tok)) {
    httpServer.send(400, "application/json", "{\"ok\":false,\"error\":\"unknown state\"}");
    return;
  }
  String sid = extractStringField(body, "sid");
  applySessionState(sid, tokenToState(tok));
  httpServer.send(200, "application/json", "{\"ok\":true}");
}

// GET /status: 現在状態・ネットワーク情報・セッション一覧を返す
void handleHttpStatus() {
  bool connected = (WiFi.status() == WL_CONNECTED);
  String ip = connected ? WiFi.localIP().toString() : String("0.0.0.0");
  long rssi = connected ? WiFi.RSSI() : 0;

  int sessionCount = 0, waitingCount = 0;
  String list = "[";
  bool first = true;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (!sessions[i].used) continue;
    sessionCount++;
    if (sessions[i].state == ST_APPROVAL) waitingCount++;
    if (!first) list += ",";
    first = false;
    list += "{\"sid\":\"" + String(sessions[i].sid) + "\",\"state\":\"" +
            stateToken(sessions[i].state) + "\"}";
  }
  list += "]";

  String json = String("{\"state\":\"") + stateToken(currentState) +
                "\",\"ip\":\"" + ip + "\",\"rssi\":" + String(rssi) +
                ",\"uptime\":" + String(millis() / 1000UL) +
                ",\"sessions\":" + String(sessionCount) +
                ",\"waiting\":" + String(waitingCount) +
                ",\"list\":" + list + "}";
  httpServer.send(200, "application/json", json);
}

// {"cmd":"..."} で始まるシリアル行(WiFiプロビジョニング用コマンド)を処理する
void handleSerialCommand(const String &line) {
  String cmd = extractStringField(line, "cmd");
  if (cmd == "wifi") {
    String ssid = extractStringField(line, "ssid");
    String pass = extractStringField(line, "pass");
    if (ssid.length() == 0) return; // SSID未指定は無視
    wifiSaveCreds(ssid, pass);
    wifiSavedSsid = ssid;
    wifiSavedPass = pass;
    wifiConfigured = true;
    startWifiConnect(ssid, pass, true);
  } else if (cmd == "wifi_status") {
    if (!wifiConfigured) {
      printWifiEvent("unconfigured");
    } else if (WiFi.status() == WL_CONNECTED) {
      printWifiConnected();
    } else if (wifiConnecting) {
      printWifiEvent("connecting");
    } else {
      printWifiEvent("failed");
    }
  } else if (cmd == "wifi_clear") {
    wifiClearCreds();
    wifiConfigured = false;
    wifiConnecting = false;
    WiFi.disconnect(true, true);
    printWifiEvent("cleared");
  }
}

// loop内でWiFi接続状態を監視する。接続待ちはブロッキングしない。
void pollWifi() {
  static bool wasConnected = false;
  bool nowConnected = (WiFi.status() == WL_CONNECTED);

  if (wifiConnecting) {
    if (nowConnected) {
      wifiConnecting = false;
      if (wifiAnnounceThisAttempt) printWifiConnected();
    } else if (millis() - wifiConnectStartMs > WIFI_CONNECT_TIMEOUT_MS) {
      wifiConnecting = false;
      lastWifiRetryMs = millis();
      if (wifiAnnounceThisAttempt) printWifiEvent("failed");
    }
  } else if (wifiConfigured && !nowConnected) {
    // 切断中は一定間隔で静かに再接続を試みる
    // (arduino-esp32のWiFiスタック自身が先に自動再接続することもあるが、
    //  その場合は下のwasConnectedによるエッジ検出でサービス再起動される)
    if (millis() - lastWifiRetryMs >= WIFI_RETRY_INTERVAL_MS) {
      lastWifiRetryMs = millis();
      startWifiConnect(wifiSavedSsid, wifiSavedPass, false);
    }
  }

  // 切断->接続への変化を検出したら毎回サービスを(再)起動する。
  // WiFiスタックの自動再接続でwifiConnectingを経由せず復帰するケースも
  // これで確実に拾う (mDNSは特にインターフェース再確立後に再起動が必要)。
  if (nowConnected && !wasConnected) {
    startWifiServices();
  }
  wasConnected = nowConnected;

  if (wifiServicesStarted) httpServer.handleClient();
}

// ============================================================
// LED (WS2812 / GPIO8) -- ESP32-C6-Touch-LCD-1.47 は非搭載のため無効化
// ============================================================
void updateLed() {
#if ENABLE_RGB_LED
  unsigned long t = millis() - stateEnteredMs;
  switch (currentState) {
    case ST_IDLE: {
      rgbLedWrite(PIN_WS2812, 8, 8, 8); // 薄暗い白
      break;
    }
    case ST_WORKING: {
      // Coralでゆっくり明滅 (周期2000ms)
      float phase = (float)(t % 2000) / 2000.0f;
      float b = (sinf(phase * 2.0f * PI) * 0.5f + 0.5f);
      rgbLedWrite(PIN_WS2812, (uint8_t)(0xD9 * b), (uint8_t)(0x77 * b),
                  (uint8_t)(0x57 * b));
      break;
    }
    case ST_DONE: {
      rgbLedWrite(PIN_WS2812, 0x20, 0x50, 0x20); // グリーン点灯
      break;
    }
    case ST_APPROVAL: {
      // アンバー点滅
      bool on = ((t / 400) % 2) == 0;
      if (on) rgbLedWrite(PIN_WS2812, 0x40, 0x28, 0x10);
      else rgbLedWrite(PIN_WS2812, 0, 0, 0);
      break;
    }
    case ST_NOTIFY: {
      // 白点滅数回 -> 点灯
      if (t < 1800) {
        bool on = ((t / 150) % 2) == 0;
        if (on) rgbLedWrite(PIN_WS2812, 0x30, 0x30, 0x30);
        else rgbLedWrite(PIN_WS2812, 0, 0, 0);
      } else {
        rgbLedWrite(PIN_WS2812, 0x30, 0x30, 0x30);
      }
      break;
    }
    case ST_ERROR: {
      rgbLedWrite(PIN_WS2812, 0x50, 0x08, 0x08); // 赤
      break;
    }
  }
#endif // ENABLE_RGB_LED
}

// ============================================================
// Clawd 描画 (参照GIFから1:1抽出したドット絵スプライトを再生)
// clawd_sprites.h の RLE (run長, パレット番号) フレームをデコードして
// fillRect で1行ずつ描く。手続き的な近似描画は行わない。
// ============================================================

// アニメ内の経過時間tから再生すべき seq 上のフレーム番号を求める
uint8_t animFrameIndexForTime(const ClawdAnim &anim, unsigned long t) {
  uint32_t total = 0;
  for (uint8_t i = 0; i < anim.seqLen; i++) total += anim.durMs[i];
  if (total == 0) return 0;
  uint32_t tm = (uint32_t)(t % total);
  uint32_t acc = 0;
  for (uint8_t i = 0; i < anim.seqLen; i++) {
    acc += anim.durMs[i];
    if (tm < acc) return anim.seq[i];
  }
  return anim.seq[anim.seqLen - 1];
}

// 縦レイアウトのキャラ配置領域 (ヘッダー下〜フッタードット上)
#define CLAWD_AREA_TOP 34
#define CLAWD_AREA_BOTTOM (SCR_H - 24)

// 1フレーム分のRLEをデコードして描画する。
// 縦画面用配置: 水平センタリング + 配置領域内で垂直センタリング。
// スプライトが画面幅より広い場合 (ANIM_HAPPY=193px) は左右を均等にクリップ。
void drawClawdFrame(Arduino_GFX *g, const ClawdAnim &anim, uint8_t frameIdx) {
  const ClawdFrame &f = anim.frames[frameIdx];
  const int16_t baseX = (SCR_W - (int16_t)anim.w) / 2;
  const int16_t baseY =
      CLAWD_AREA_TOP + (CLAWD_AREA_BOTTOM - CLAWD_AREA_TOP - (int16_t)anim.h) / 2;
  const uint16_t w = anim.w;
  uint32_t pos = 0; // クロップ内の線形位置 (0..w*h-1)

  for (uint32_t i = 0; i + 1 < f.rleLen; i += 2) {
    uint8_t run = f.rle[i];
    uint8_t idx = f.rle[i + 1];
    if (idx != 0) { // idx==0 は透過 (背景の黒をそのまま残す)
      uint32_t remaining = run;
      uint32_t p = pos;
      while (remaining > 0) {
        uint32_t y = p / w;
        uint32_t x = p % w;
        uint32_t seg = w - x;
        if (seg > remaining) seg = remaining;
        int16_t sy = baseY + (int16_t)y;
        if (sy >= 0 && sy < SCR_H) {
          // 横方向クリップ (画面外にはみ出す分を切り詰める)
          int16_t sx = baseX + (int16_t)x;
          int16_t sw = (int16_t)seg;
          if (sx < 0) { sw += sx; sx = 0; }
          if (sx + sw > SCR_W) sw = SCR_W - sx;
          if (sw > 0) {
            g->fillRect(sx, sy, sw, 1, CLAWD_PALETTE[idx]);
          }
        }
        p += seg;
        remaining -= seg;
      }
    }
    pos += run;
  }
}

// アニメを経過時間tに応じて描画する
void drawClawdAnim(Arduino_GFX *g, const ClawdAnim &anim, unsigned long t) {
  uint8_t frameIdx = animFrameIndexForTime(anim, t);
  drawClawdFrame(g, anim, frameIdx);
}

// ============================================================
// 状態別ヘッダーラベル
// ============================================================
const char *stateLabel(AppState s) {
  switch (s) {
    case ST_IDLE: return "IDLE";
    case ST_WORKING: return "WORKING";
    case ST_DONE: return "DONE";
    case ST_APPROVAL: return "APPROVAL";
    case ST_NOTIFY: return "NOTIFY";
    case ST_ERROR: return "ERROR";
  }
  return "IDLE";
}

uint16_t stateColor(AppState s) {
  switch (s) {
    case ST_IDLE: return COLOR_IDLE;
    case ST_WORKING: return COLOR_CORAL;
    case ST_DONE: return COLOR_DONE;
    case ST_APPROVAL: return COLOR_APPROVAL;
    case ST_NOTIFY: return COLOR_CORAL;
    case ST_ERROR: return COLOR_ERROR;
  }
  return COLOR_IDLE;
}

// ============================================================
// 上部ラベル & 下部インジケータ
// ============================================================
void drawHeader(Arduino_GFX *g, AppState s) {
  uint16_t c = stateColor(s);
  g->setTextColor(c, COLOR_BG);
  g->setTextSize(2);
  const char *label = stateLabel(s);
  int16_t textW = strlen(label) * 12; // size2フォントは概ね幅12px/文字
  g->setCursor((SCR_W - textW) / 2, 8);
  g->print(label);
  // ラベル下の細いアンダーライン
  g->fillRoundRect((SCR_W - textW) / 2, 26, textW, 2, 1, c);
}

// セッション数ぶんのドット(状態色、approvalは約500ms周期で点滅)を水平センタリングして描画し、
// 右側にセッション数を数字で併記する。セッションが1つも無ければ何も描画しない。
void drawFooterDots(Arduino_GFX *g, unsigned long t) {
  int idxs[MAX_SESSIONS];
  int n = 0;
  for (int i = 0; i < MAX_SESSIONS; i++) {
    if (sessions[i].used) idxs[n++] = i;
  }
  if (n == 0) return; // idle(セッション0件)は何も描画しない

  const int16_t cy = SCR_H - 12;
  const int16_t dotSpacing = 14;
  const int16_t numAreaW = 12; // size1の数字1〜2桁分の幅の目安
  int16_t dotsW = (n - 1) * dotSpacing;
  int16_t startX = (SCR_W - (dotsW + numAreaW)) / 2;

  for (int i = 0; i < n; i++) {
    Session &sess = sessions[idxs[i]];
    bool blinkOff = (sess.state == ST_APPROVAL) && (((t / 500) % 2) != 0);
    if (blinkOff) continue;
    g->fillCircle(startX + i * dotSpacing, cy, 3, stateColor(sess.state));
  }

  g->setTextColor(COLOR_IVORY, COLOR_BG);
  g->setTextSize(1);
  g->setCursor(startX + dotsW + 8, cy - 3);
  g->print(n);
}

// ============================================================
// 各状態の描画ロジック (状態 -> ClawdAnim の対応のみ)
// 演出 (ターミナル/キーボード/チェックマーク/吹き出し/ベル/ERROR文字等) は
// すべて参照GIFから抽出したアニメーションデータ自体に含まれている。
// ============================================================
void renderState(Arduino_GFX *g, AppState s, unsigned long t) {
  switch (s) {
    case ST_IDLE: drawClawdAnim(g, ANIM_IDLE, t); break;
    case ST_WORKING: drawClawdAnim(g, ANIM_TYPING, t); break;
    case ST_DONE: drawClawdAnim(g, ANIM_HAPPY, t); break;
    case ST_APPROVAL: drawClawdAnim(g, ANIM_THINKING, t); break;
    case ST_NOTIFY: drawClawdAnim(g, ANIM_NOTIFICATION, t); break;
    case ST_ERROR: drawClawdAnim(g, ANIM_ERROR, t); break;
  }
}

// ============================================================
// 起動アニメーション
// ============================================================
void playBootAnimation() {
  // 黒背景に ANIM_IDLE の先頭フレーム + "CLAWD BADGE" タイトルを表示するだけの
  // 簡素な起動演出 (キャラの演出そのものはアニメデータ側に既に含まれている)
  unsigned long titleStart = millis();
  const unsigned long titleDur = 1000;
  while (millis() - titleStart < titleDur) {
    canvas->fillScreen(COLOR_BG);
    drawClawdFrame(canvas, ANIM_IDLE, ANIM_IDLE.seq[0]);
    canvas->setTextColor(COLOR_IVORY, COLOR_BG);
    canvas->setTextSize(2);
    const char *title = "CLAWD BADGE";
    int16_t textW = strlen(title) * 12;
    canvas->setCursor((SCR_W - textW) / 2, SCR_H - 20);
    canvas->print(title);
    canvas->flush();
    delay(16);
  }
  bootAnimDone = true;
}

// ============================================================
// setup / loop
// ============================================================
unsigned long lastFrameMs = 0;
const unsigned long FRAME_INTERVAL_MS = 33; // 約30fps

void setup() {
  Serial.begin(115200);

  pinMode(PIN_LCD_BL, OUTPUT);
  digitalWrite(PIN_LCD_BL, HIGH); // バックライト点灯 (アクティブHigh)

#if ENABLE_RGB_LED
  rgbLedWrite(PIN_WS2812, 0, 0, 0);
#endif

  // Canvas経由でも実体はgfxPanel(Arduino_ST7789)なので、begin()でパネルの
  // 基本初期化(リセット/SPI設定)が行われた直後にJD9853専用レジスタ列を
  // busへ直接送ることで、確実にパネルへ初期化コマンドを届ける。
  canvas->begin();
  lcd_reg_init();
  gfxPanel->setRotation(LCD_ROTATION);
  canvas->fillScreen(COLOR_BG);
  canvas->flush();

  playBootAnimation();

  stateEnteredMs = millis();

  // HTTPルートはWiFi未接続でも先に登録しておく(接続後にstartWifiServices()でbegin()する)
  httpServer.on("/notify", HTTP_POST, handleHttpNotify);
  httpServer.on("/status", HTTP_GET, handleHttpStatus);

  // NVSに保存済みのWiFi認証情報があれば非ブロッキングで接続開始
  String savedSsid, savedPass;
  wifiLoadCreds(savedSsid, savedPass);
  if (savedSsid.length() > 0) {
    wifiSavedSsid = savedSsid;
    wifiSavedPass = savedPass;
    wifiConfigured = true;
    startWifiConnect(savedSsid, savedPass, false);
  }
}

void loop() {
  pollSerial();
  pollWifi();
  checkSessionTimeouts();

  unsigned long now = millis();
  if (now - lastFrameMs < FRAME_INTERVAL_MS) return;
  lastFrameMs = now;

  unsigned long t = now - stateEnteredMs;

  canvas->fillScreen(COLOR_BG);
  renderState(canvas, currentState, t);
  drawHeader(canvas, currentState);
  drawFooterDots(canvas, now);
  canvas->flush();

  updateLed();
}
