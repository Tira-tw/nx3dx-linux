/* (v2.4) The port's own texts, English and Japanese (lang.h). Included by hud.c. */
#include "lang.h"

static const int *lang_en_src;

void lang_bind(const int *english) { lang_en_src = english; }
int lang_is_en(void) { return lang_en_src && *lang_en_src == 1; }

/* { English, Japanese } by id */
static const char *const LANG_TAB[LS_COUNT][2] = {
  [LS_MORE_OPTIONS] = { "MORE OPTIONS", "詳細設定" },
  [LS_DONE] = { "DONE", "決定" },
  [LS_RESTART_L2] = { "WHEN THE GAME RESTARTS. RESTART NOW?", "ゲームの再起動後に変わります。今すぐ再起動しますか？" },
  [LS_RESTART_YES] = { "OK", "ＯＫ" },
  [LS_RESTART_NO] = { "LATER", "あとで" },
  [LS_RESTART_LANG] = { "THE LANGUAGE CHANGES", "言語を変更しました" },
  [LS_RESTART_RES] = { "THE RESOLUTION CHANGES", "解像度を変更しました" },
  [LS_RESTART_REN] = { "THE RENDERER CHANGES", "レンダラーを変更しました" },
  [LS_RESTART_SOME] = { "THE ASPECT CHANGES", "画面比率を変更しました" },
  [LS_HUD_LAYOUT] = { "HUD LAYOUT", "ＨＵＤ配置" },
  [LS_HUD_LAYOUT_TIP] = { "TAP A PART - DRAG IT TO MOVE IT", "部品をタップ　ドラッグで移動" },
  [LS_CRACK] = { "CRACK", "追加機能" },
  [LS_KEY_MAPPING] = { "KEY MAPPING", "キー設定" },
  [LS_BIND_TIP] = { "TAP A BOX, THEN PRESS A KEY OR PUSH A STICK. TAP IT AGAIN TO CLEAR IT.",
                    "枠をタップしてキーを押すかスティックを倒してください。もう一度タップで解除。" },
  [LS_BIND_KEYS_TIP] = { "ARROWS: MOVE   ENTER: SET   V: KEYS / SENSITIVITY / DEVICES   BACKSPACE: DONE",
                         "矢印：移動　ENTER：設定　V：キー／感度／デバイス　BACKSPACE：決定" },
};

const char *L(int id) {
  if (id <= LS_NONE || id >= LS_COUNT) return "";
  const char *s = LANG_TAB[id][lang_is_en() ? 0 : 1];
  return s ? s : LANG_TAB[id][0] ? LANG_TAB[id][0] : "";
}
