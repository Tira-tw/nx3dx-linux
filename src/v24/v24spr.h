/* (v2.4) The v2.4 mode graphics in data/extra.bin (tools/v24_sprites.py; keep the two in step).
 * CARD: 256x256 like menu_p_mode_icon_* (title over the white panel, panel at 62..191 x 121..192)
 * BAR:  512x64, menu_p_title_bar with the rule icon at (14,12) and the name from x 90
 * ICON: 64x40, the rule's panel picture only (same for both languages) */
#ifndef V24SPR_H
#define V24SPR_H

#define V24SPR_BASE 128400

enum {
    V24MODE_NORMAL, V24MODE_TEAM2V2, V24MODE_EXTREME, V24MODE_HPBATTLE,
    V24MODE_EVENT, V24MODE_ONLINE, V24MODE_LAN, V24MODE_SETTINGS, V24MODE_COUNT
};

/* lang: 0 Japanese, 1 English */
#define V24SPR_CARD(mode, lang) (V24SPR_BASE + 2 * (mode) + (lang))
#define V24SPR_BAR(mode, lang)  (V24SPR_BASE + 20 + 2 * (mode) + (lang))
#define V24SPR_ICON(mode)       (V24SPR_BASE + 40 + (mode))

/* the same, spelt out (JA id, EN id) */
#define V24SPR_CARD_NORMAL_JA   128400
#define V24SPR_CARD_NORMAL_EN   128401
#define V24SPR_CARD_TEAM2V2_JA  128402
#define V24SPR_CARD_TEAM2V2_EN  128403
#define V24SPR_CARD_EXTREME_JA  128404
#define V24SPR_CARD_EXTREME_EN  128405
#define V24SPR_CARD_HPBATTLE_JA 128406
#define V24SPR_CARD_HPBATTLE_EN 128407
#define V24SPR_CARD_EVENT_JA    128408
#define V24SPR_CARD_EVENT_EN    128409
#define V24SPR_CARD_ONLINE_JA   128410
#define V24SPR_CARD_ONLINE_EN   128411
#define V24SPR_CARD_LAN_JA      128412
#define V24SPR_CARD_LAN_EN      128413
#define V24SPR_CARD_SETTINGS_JA 128414
#define V24SPR_CARD_SETTINGS_EN 128415

#define V24SPR_BAR_NORMAL_JA    128420
#define V24SPR_BAR_NORMAL_EN    128421
#define V24SPR_BAR_TEAM2V2_JA   128422
#define V24SPR_BAR_TEAM2V2_EN   128423
#define V24SPR_BAR_EXTREME_JA   128424
#define V24SPR_BAR_EXTREME_EN   128425
#define V24SPR_BAR_HPBATTLE_JA  128426
#define V24SPR_BAR_HPBATTLE_EN  128427
#define V24SPR_BAR_EVENT_JA     128428
#define V24SPR_BAR_EVENT_EN     128429
#define V24SPR_BAR_ONLINE_JA    128430
#define V24SPR_BAR_ONLINE_EN    128431
#define V24SPR_BAR_LAN_JA       128432
#define V24SPR_BAR_LAN_EN       128433
#define V24SPR_BAR_SETTINGS_JA  128434
#define V24SPR_BAR_SETTINGS_EN  128435

#define V24SPR_ICON_NORMAL      128440
#define V24SPR_ICON_TEAM2V2     128441
#define V24SPR_ICON_EXTREME     128442
#define V24SPR_ICON_HPBATTLE    128443
#define V24SPR_ICON_EVENT       128444
#define V24SPR_ICON_ONLINE      128445
#define V24SPR_ICON_LAN         128446
#define V24SPR_ICON_SETTINGS    128447

#endif
