/*
 * ClawdBadge - Claude Code ステータス通知バッジ
 * 対象基板: Waveshare ESP32-C6-Touch-LCD-1.47 (JD9853, 172x320, タッチ AXS5106L)
 *
 * PCで動作する Claude Code の状態を USB CDC シリアル (115200bps) から
 * 1行1メッセージのJSONで受信し、Anthropicデザインのマスコット「Clawd」
 * (オレンジ色のかわいいカニ) を状態に応じてアニメーション表示する。
 *
 * 受信JSON例: {"state":"working","msg":"...","ts":1234567890}
 * state: working | done | approval | notify | idle | error (未知は notify 扱い)
 *
 * LCDコントローラは JD9853 (ST7789互換コマンドセット)。Arduino_GFXに専用クラスが
 * 無いため Arduino_ST7789 を流用し、begin() 直後にWaveshare公式デモ由来の
 * JD9853専用レジスタ初期化列 (lcd_reg_init) を送ることで正しく初期化する。
 * 本機はRGB LED非搭載のため updateLed() の rgbLedWrite 呼び出しは無効化する。
 */

#include <Arduino_GFX_Library.h>
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
#define LCD_ROTATION 1 // 横向き 320x172 で使用
#define LCD_COL_OFFSET1 34
#define LCD_ROW_OFFSET1 0
#define LCD_COL_OFFSET2 34
#define LCD_ROW_OFFSET2 0

// 回転後の実座標系サイズ (rotation=1/3 でランドスケープ)
#define SCR_W 320
#define SCR_H 172

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
AppState currentState = ST_IDLE;
unsigned long stateEnteredMs = 0;
unsigned long lastMsgMs = 0;
bool bootAnimDone = false;

// タイムアウト定数
const unsigned long TIMEOUT_SHORT_MS = 5UL * 60UL * 1000UL;  // 5分
const unsigned long TIMEOUT_WORKING_MS = 30UL * 60UL * 1000UL; // 30分

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

AppState tokenToState(const String &tok) {
  if (tok == "working") return ST_WORKING;
  if (tok == "done") return ST_DONE;
  if (tok == "approval") return ST_APPROVAL;
  if (tok == "notify") return ST_NOTIFY;
  if (tok == "idle") return ST_IDLE;
  if (tok == "error") return ST_ERROR;
  return ST_NOTIFY; // 未知のstateはnotify扱い
}

void enterState(AppState s) {
  currentState = s;
  stateEnteredMs = millis();
}

void handleSerialLine(const String &line) {
  if (line.length() == 0) return;
  String tok = extractStateToken(line);
  if (tok.length() == 0) return; // stateキーが無い行は無視
  AppState s = tokenToState(tok);
  lastMsgMs = millis();
  if (s != currentState) enterState(s);
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

void checkTimeouts() {
  unsigned long now = millis();
  if (currentState == ST_WORKING) {
    if (now - lastMsgMs > TIMEOUT_WORKING_MS) enterState(ST_IDLE);
  } else if (currentState == ST_DONE || currentState == ST_APPROVAL ||
             currentState == ST_NOTIFY) {
    if (now - lastMsgMs > TIMEOUT_SHORT_MS) enterState(ST_IDLE);
  }
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

// 1フレーム分のRLEをデコードして描画する。
// 全状態共通の絶対座標変換: screen_x = offX + 9 + x, screen_y = offY - 104 + y
void drawClawdFrame(Arduino_GFX *g, const ClawdAnim &anim, uint8_t frameIdx) {
  const ClawdFrame &f = anim.frames[frameIdx];
  const int16_t baseX = anim.offX + 9;
  const int16_t baseY = anim.offY - 104;
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
          g->fillRect(baseX + (int16_t)x, sy, (int16_t)seg, 1,
                      CLAWD_PALETTE[idx]);
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

void drawFooterDots(Arduino_GFX *g, AppState s, unsigned long t) {
  // スプライト(中央)と重ならないよう左下隅に置く
  int16_t cy = SCR_H - 12;
  if (s == ST_WORKING) {
    // "..." が流れるアニメーション
    int active = (t / 400) % 4; // 0..3
    for (int i = 0; i < 3; i++) {
      int16_t dx = 14 + i * 14;
      uint16_t c = (i < active) ? COLOR_CORAL : COLOR_CLOUD;
      g->fillCircle(dx, cy, 3, c);
    }
  } else {
    // 控えめな状態ドット (3つ、状態色で点灯)
    uint16_t c = stateColor(s);
    for (int i = 0; i < 3; i++) {
      int16_t dx = 14 + i * 14;
      g->fillCircle(dx, cy, 3, c);
    }
  }
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
  lastMsgMs = millis();
}

void loop() {
  pollSerial();
  checkTimeouts();

  unsigned long now = millis();
  if (now - lastFrameMs < FRAME_INTERVAL_MS) return;
  lastFrameMs = now;

  unsigned long t = now - stateEnteredMs;

  canvas->fillScreen(COLOR_BG);
  renderState(canvas, currentState, t);
  drawHeader(canvas, currentState);
  drawFooterDots(canvas, currentState, now);
  canvas->flush();

  updateLed();
}
