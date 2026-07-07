// 状態・列挙型の定義
// Arduinoスケッチの自動プロトタイプ生成は #include 直後に関数プロトタイプを
// 挿入するため、独自enumはヘッダに分離して #include より前に確定させる必要がある。
#ifndef CLAWD_TYPES_H
#define CLAWD_TYPES_H

enum AppState {
  ST_IDLE = 0,
  ST_WORKING,
  ST_DONE,
  ST_APPROVAL,
  ST_NOTIFY,
  ST_ERROR
};

enum EyeMode { EYE_OPEN, EYE_BLINK, EYE_HAPPY, EYE_ERROR };

#endif // CLAWD_TYPES_H
