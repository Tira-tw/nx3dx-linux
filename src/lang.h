/* (v2.4) The port's own texts in the language chosen (MORE OPTIONS > LANGUAGE: menu.h lang,
 * 0 Japanese, 1 English), UTF-8. hud_ui_text draws a string with any non-ASCII character in
 * the arcade's font 1 (clSpriteFont: its glyphs in src/langfont.h, tools/lang_font.py), an
 * ASCII one in the 5x7 font as before; so a Japanese text goes through hud_ui_text as it is.
 *
 * Adding a text: an id in the enum below, its { English, Japanese } row in lang.c (or, for a
 * one-off, LJ("ENGLISH", "日本語") in place), then run `python tools/lang_font.py` so its
 * characters get glyphs (a character without one is skipped).
 *
 * lang.c is not a unit of its own: hud.c includes it (no build list changes). */
#ifndef LANG_H
#define LANG_H

enum {
  LS_NONE,
  /* MORE OPTIONS */
  LS_MORE_OPTIONS, LS_DONE, LS_RESTART_L2, LS_RESTART_YES, LS_RESTART_NO, LS_RESTART_LANG, LS_RESTART_RES,
  LS_RESTART_REN, LS_RESTART_SOME,
  /* HUD LAYOUT */
  LS_HUD_LAYOUT, LS_HUD_LAYOUT_TIP,
  /* CRACK */
  LS_CRACK,
  /* KEY MAPPING */
  LS_KEY_MAPPING, LS_BIND_TIP, LS_BIND_KEYS_TIP,
  LS_COUNT
};

/* the text by the language in use (never NULL; "" for an id without one) */
const char *L(int id);
/* 1 when the port's texts are English */
int lang_is_en(void);
/* where the language setting is (menu.h's lang: 1 English); before it, Japanese */
void lang_bind(const int *english);
/* in place: the English or the Japanese (both string literals) */
#define LJ(en, ja) (lang_is_en() ? (en) : (ja))

#endif
