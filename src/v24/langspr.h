/* (v2.4) Language sprites: tools/lang_sprites.py appends them to data/extra.bin.
 *
 * LANG_WORD(k) is the v2.4 word k in the game's language (font 3, the OPTION screen's titles'):
 *   anm_draw_sprite(anm_sprite(LANG_WORD(m->lang, LW_NORMAL)), x, y, a);
 * LANGSPR_ALIAS: Japanese sprites (the port's own and, v2.4, the ghost battle's arcade ones) and
 * their English ones; app.c lang_english applies them once English is in use (anm_lang_alias),
 * so every draw of the Japanese id (animations' records included) shows the English one. */
#ifndef V24_LANGSPR_H
#define V24_LANGSPR_H

#define LANG_EN_BASE 127400
#define LANG_EN_STREAK (LANG_EN_BASE + 0)   /* " WIN STREAK", font 1: menu.c draws N then it */
#define LANG_EN_MIL_PLUS (LANG_EN_BASE + 1) /* "THIS RUN", font 4 (126800 今回の走行距離) */
#define LANG_EN_MIL_TOTAL (LANG_EN_BASE + 2) /* "TOTAL DISTANCE", font 4 (126801 総走行距離) */

#define LANG_WORD_JA 127420
#define LANG_WORD_EN 127440
enum { LW_NORMAL, LW_2V2, LW_EXTREME, LW_HP_BATTLE, LW_EVENT, LW_CONNECTION, LW_GAMEPLAY, LW_N };
/* ノーマル / NORMAL, 2対2 / 2V2, エクストリーム / EXTREME, HPバトル / HP BATTLE, イベント / EVENT,
 * 通信設定 / CONNECTION, ゲーム設定 / GAMEPLAY */
#define LANG_WORD(lang, k) (((lang) == 1 ? LANG_WORD_EN : LANG_WORD_JA) + (k))

/* (v2.4) The offline ghost battle's (分身対戦) English sprites: tools/tg_lang_sprites.py appends
 * them to data/extra.bin, TG_EN_BASE + k (127600..127689: top bars, search plates and notes,
 * the level / area / rival selects' discs, crowns, 最強's bars and badges, the crown taken, the
 * attract's titles, the MAXI coin button, the dress-up's buttons and part names, the tutorial's
 * windows) and TG_EN_WEAR + k (the wear banners, 101800 + k). Some arcade ones go to the
 * English pack's own picture instead (5893 -> 5891 Please choose a level, 6839 -> 6834 Choose
 * a rival, 7014 / 7015 -> 7009 Can not select, 107030 -> 2827 Ghost Versus Battle). Japanese
 * id, English id; the tool prints the same list. */
#define TG_EN_BASE 127600
#define TG_EN_WEAR 127700
#define LANGSPR_TG \
  { 7147, 127600 }, { 5893, 5891 }, { 6640, 127601 }, { 7144, 127601 }, { 6839, 6834 }, { 6838, 127602 }, \
  { 7373, 127603 }, { 7145, 127604 }, { 453, 127605 }, { 1896, 127606 }, { 1898, 127607 }, { 925, 127608 }, \
  { 1572, 127609 }, { 4173, 127610 }, { 4174, 127611 }, { 2516, 127612 }, { 1895, 127613 }, { 2513, 127614 }, \
  { 2951, 127615 }, { 674, 127616 }, { 7014, 7009 }, { 7015, 7009 }, { 1738, 127617 }, { 1739, 127618 }, \
  { 1740, 127619 }, { 1741, 127620 }, { 1742, 127621 }, { 1743, 127622 }, { 1744, 127623 }, { 4163, 127624 }, \
  { 2522, 127625 }, { 6340, 127626 }, { 6341, 127627 }, { 6342, 127628 }, { 6343, 127629 }, { 6344, 127630 }, \
  { 6345, 127631 }, { 6346, 127632 }, { 2251, 127633 }, { 2779, 127634 }, { 2780, 127635 }, { 2781, 127636 }, \
  { 2782, 127637 }, { 2783, 127638 }, { 2784, 127639 }, { 2785, 127640 }, { 2786, 127641 }, { 1462, 127642 }, \
  { 3173, 127643 }, { 7831, 127644 }, { 7915, 127645 }, { 1917, 127646 }, { 4955, 127647 }, { 4959, 127648 }, \
  { 6313, 127649 }, { 7585, 127650 }, { 8011, 127651 }, { 4168, 127652 }, { 4169, 127653 }, { 4935, 127654 }, \
  { 5862, 127655 }, { 5886, 127656 }, { 6324, 127657 }, { 5887, 127658 }, { 7541, 127659 }, { 7336, 127660 }, \
  { 7337, 127661 }, { 7338, 127662 }, { 7339, 127663 }, { 7340, 127664 }, { 7341, 127665 }, { 7552, 127666 }, \
  { 7553, 127667 }, { 7554, 127668 }, { 7549, 127669 }, { 7550, 127670 }, { 7729, 127671 }, { 6849, 127672 }, \
  { 6850, 127673 }, { 6356, 127674 }, { 6357, 127675 }, { 6358, 127676 }, { 6359, 127677 }, { 6370, 127678 }, \
  { 6360, 127679 }, { 6362, 127680 }, { 6371, 127681 }, { 6364, 127682 }, { 7376, 127683 }, { 6365, 127684 }, \
  { 6366, 127685 }, { 6369, 127686 }, { 4991, 127687 }, { 6355, 127688 }, { 107030, 2827 }, { 108003, 127689 }, \
  { 101800, 127700 }, { 101801, 127701 }, { 101802, 127702 }, { 101803, 127703 }, { 101804, 127704 }, \
  { 101805, 127705 }, { 101806, 127706 }, { 101807, 127707 }, { 101808, 127708 }, { 101809, 127709 }, \
  { 101810, 127710 }, { 101811, 127711 }, { 101812, 127712 }, { 101813, 127713 }, { 101814, 127714 }, \
  { 101815, 127715 }, { 101816, 127716 }, { 101817, 127717 }, { 101818, 127718 }, { 101819, 127719 }, \
  { 101820, 127720 }, { 101821, 127721 }, { 101822, 127722 }, { 101823, 127723 }, { 101824, 127724 }, \
  { 101825, 127725 }, { 101826, 127726 }, { 101827, 127727 }, { 101828, 127728 }, { 101829, 127729 }, \
  { 101830, 127730 }, { 101831, 127731 }, { 101832, 127732 }, { 101833, 127733 }, { 101834, 127734 }, \
  { 101835, 127735 }, { 101836, 127736 }, { 101837, 127737 }, { 101838, 127738 }, { 101839, 127739 }, \
  { 101840, 127740 }

/* (applied in app.c lang_english after the English pack: at the start, or the moment the MORE
 * page chooses English; the arcade's own ids too, any sprite anm_sprite finds) */
#define LANGSPR_ALIAS { { 126800, LANG_EN_MIL_PLUS }, { 126801, LANG_EN_MIL_TOTAL }, LANGSPR_TG }

#endif
