/* (v2.3) The ghost battle's first-time tutorial FLOW (mseqTarget), replacing tutorial.inc's
 * one-shot TUT_TG overlay. #included into src/menu.c right AFTER v23/tutorial.inc (it drives
 * tutorial.inc's player: tut_open / tut_step / tut_draw). Everything static.
 *
 * The arcade (main, _Z10mseqTargetb 0x88ce758), verified from the disassembly:
 *  0x88ce830: getBeforeModeCard()+0x1a4 == 0 -> 0x88cf2e2: isTargetFirstPlay ->
 *    setTargetFirstTutorial(1); playStream(0x29 = maxi3/sys_03), setStreamLoop(1);
 *    seqTgTutorial (clSeqTgTutorialThread::run: scene table 0x940a2d8 = 2 rows {ptr,count 2},
 *      both rows the same: {0x2b7 game_1}, {0x2b8 game_2}; no movie, no 3D car, black curtain);
 *    seqTgTutorialStart (clTgTutorialStart: ANM 0x300 bunsin00, 0..0x77 once, 0x77+0x3c loop);
 *    then jmp 0x88ce843 = THE NORMAL GHOST BATTLE MENUS (setTargetSeqTime 99, seqTgSearchTypeSelect
 *    ...), BGM 0x29 still on. The menus behave differently while isTargetFirstTutorial:
 *      seqTgSearchTypeSelect (0x884a7ab): three flags 1, the menu time 30 s (0x41f00000);
 *      seqTgLevelSelect (0x885132b): no screen (jumps to its auto pick);
 *      seqTgCourseSelect (0x885a7e6): no screen: setTargetAreaSelect(4), setRandomTarget(rank, 4),
 *        returns 0 = the area is FORCED to clEffAreaType 4 and the ghost is a random one of the
 *        player's rank there;
 *      seqStdDayTimeSelect (0x877958b): skipped in target mode + tutorial;
 *      seqTgRivalDataDisp / TgLoad / clRaceThread::initTarget / getLevelTargetList: tutorial-aware.
 *    The tutorial hint cards on those menus: ANM 0x304 menu_p_tutorial_bunsin04 (search type,
 *    course, rival select threads), 0x305 bunsin07 (rival data disp), 0x306 bunsin08 (the race's
 *    matching information, setAnime(0x306, 4, 0, 0x18, 1)), and on every one of them the
 *    clTgTutorialExplain banner ANM 0x309 menu_p_tutorial_bunsin_txt (setAnime(0x309,6,1,0,1):
 *    looped) -- that part is src/v23/tgtut_menus.inc's (the other helper's).
 *  The race (seqRaceTarget): SConfigPlay +0xd1 = 1 -> 2.8 km (car.c P.tg.tutorial). In the race:
 *    clInfoMessage (EGameMode 8, isTargetFirstTutorial): ANM 0x56 cons_p_tutorial, setAnime(.., 6
 *      = looped, 0, 0, 1) with image replace UV 0x1790/0x8a: the "分身対戦体験中" console message;
 *    clTargetConsole -> clMeterConsole::setTgTutorial: the meter's rival name font set to
 *      L"チュートリアル" (0x93d2b8c) and the distance text cleared;
 *    clRaceResultConsole::drawTgTutorial: the result under a clMenuFade with its tutorial anims.
 *  After the race (0x88cef5b, isTargetFirstTutorial): no retire branch, no seqTgMaxiCoin/LvUp/
 *    Title/Mileage (0x88cf024 -> 0x88d0056):
 *    seqTgTutorialDressupInfo (bunsin09 / bunsin09_r35, + the explain banner);
 *    seqTgTutorialEnd (bunsin00a);
 *    setTargetFirstPlay(0), setTargetFirstTutorial(0), area select = backup = 7 (none),
 *    playStream(0x5f = sys_02_sel) looped, jmp 0x88ce843: BACK TO THE GHOST BATTLE MENUS for
 *    the real game (the tutorial race was free).
 *
 * The port: tgtut_begin() at the ghost disc (instead of tut_open(TUT_TG ...)), then
 * tgtut_step() from step(); the menu code asks tgtut_on() (= isTargetFirstTutorial) to skip the
 * level / course / day time screens and force area 4; app.c sets car_target.tutorial from
 * tgtut_racing(); after the race the app calls tgtut_after_race(r35) instead of the result
 * chain; tgtut_done() tells the menu to go back to the ghost battle's first page. */

#define TGTUT_AREA 4 /* seqTgCourseSelect's setTargetAreaSelect(4) in the tutorial */

enum { TGTUT_OFF, TGTUT_INTRO, TGTUT_START, TGTUT_MENUS, TGTUT_RACE, TGTUT_INFO, TGTUT_END, TGTUT_BACK };

static struct {
  int st;
  int r35;
  int race_anim, banner_anim; /* cons_p_tutorial, menu_p_tutorial_bunsin_txt */
  int t;
} TGT;

/* isTargetFirstTutorial: from the intro up to the closing card */
static int tgtut_on(void) { return TGT.st != TGTUT_OFF && TGT.st != TGTUT_BACK; }
/* the race to run is the 2.8 km tutorial one (SConfigPlay +0xd1) */
static int tgtut_racing(void) { return TGT.st == TGTUT_MENUS || TGT.st == TGTUT_RACE; }
/* the overlays of tutorial.inc are up (the menu waits) */
static int tgtut_cards(void) { return TGT.st == TGTUT_INTRO || TGT.st == TGTUT_START || TGT.st == TGTUT_INFO || TGT.st == TGTUT_END; }
/* BGM: 0x29 (sys_03) from the intro to the closing card, then 0x5f (the select music) */
static int tgtut_music(void) { return tgtut_on(); }

/* the ghost disc: card's +0x1a4 == 0 && isTargetFirstPlay (the caller checks the seen bit) */
static int tgtut_begin(int r35) {
  memset(&TGT, 0, sizeof TGT);
  TGT.r35 = r35;
  TGT.race_anim = anm_find("cons_p_tutorial");
  TGT.banner_anim = anm_find("menu_p_tutorial_bunsin_txt");
  if (!tut_open(TUT_TG_INTRO, r35)) return 0;
  TGT.st = TGTUT_INTRO;
  return 1;
}

/* one 60 Hz tick of the flow's own part (call it every menu step, BEFORE tut_active's return).
 * Returns 1 the frame the whole thing ends (tgtut_done: back to the first ghost battle page). */
static int tgtut_step(void) {
  TGT.t++;
  switch (TGT.st) {
  case TGTUT_INTRO:
    if (!tut_active()) TGT.st = tut_open(TUT_TG_START, TGT.r35) ? TGTUT_START : TGTUT_MENUS;
    return 0;
  case TGTUT_START:
    if (!tut_active()) TGT.st = TGTUT_MENUS; /* on to seqTgSearchTypeSelect (the menu's first page) */
    return 0;
  case TGTUT_INFO:
    if (!tut_active()) TGT.st = tut_open(TUT_TG_END, TGT.r35) ? TGTUT_END : TGTUT_BACK;
    return TGT.st == TGTUT_BACK;
  case TGTUT_END:
    if (!tut_active()) {
      TGT.st = TGTUT_BACK;
      return 1;
    }
    return 0;
  }
  return 0;
}

/* the race starts (seqRaceTarget with the tutorial flag) */
static void tgtut_race_start(void) {
  if (TGT.st == TGTUT_MENUS) TGT.st = TGTUT_RACE, TGT.t = 0;
}

/* the race's result is over: the arcade skips the coins / level up / title / mileage and plays
 * DressupInfo then End. 1 = taken (the caller shows nothing else, the menu steps tgtut_step) */
static int tgtut_after_race(void) {
  if (TGT.st != TGTUT_RACE && TGT.st != TGTUT_MENUS) return 0;
  TGT.st = tut_open(TUT_TG_INFO, TGT.r35) ? TGTUT_INFO : tut_open(TUT_TG_END, TGT.r35) ? TGTUT_END : TGTUT_BACK;
  return 1;
}

/* the flow is over (setTargetFirstPlay(0)): 1 once, then the state is off */
static int tgtut_done(void) {
  if (TGT.st != TGTUT_BACK) return 0;
  TGT.st = TGTUT_OFF;
  return 1;
}

/* a quit (the card out, the attract): the flag off, nothing kept (the seen bit stays unset in
 * the arcade too: setTargetFirstPlay(0) is only after the End card) */
static void tgtut_abort(void) { TGT.st = TGTUT_OFF; }

/* the clTgTutorialExplain banner (bunsin_txt, looped) over the ghost battle's menu pages while
 * the tutorial is on (draw it over a TG page, under tut_draw). tgtut_menus.inc may draw it
 * itself instead; then don't hook this. */
static void tgtut_draw_banner(int frame_t) {
  if (!tgtut_on() || tgtut_cards() || TGT.banner_anim < 0) return;
  int n = anm_frames(TGT.banner_anim);
  if (n > 0) anm_draw(TGT.banner_anim, frame_t % n, 1);
}

/* in the race: clInfoMessage's cons_p_tutorial (setAnime mode 6: looped) for the whole race */
static void tgtut_draw_race(int frame_t) {
  if (!tgtut_racing() || TGT.race_anim < 0) return;
  int n = anm_frames(TGT.race_anim);
  if (n > 0) anm_draw(TGT.race_anim, frame_t % n, 1);
}
