/* (v2.4) the versus' HP BATTLE (VS_MODE_HP), after the first Wangan Midnight arcade's (2001,
 * Genki / Namco) battle gauge, Tokyo Xtreme Racer's SP gauge: every car has a gauge; trailing the
 * leader past a free distance drains it (the more gap, the faster), hits (a wall, a car: a sudden
 * loss of speed) cost some; 0 is out. The last one in wins; at the goal / time up the leader of
 * those still in. Every machine runs the same rule on the race's positions (its own and the link
 * rivals' odometers), its own KO deciding its own race: out, its race ends at once.
 * who: 0 this machine's car, 1..3 link rival who-1 (car_race vs_gap's). */
#ifndef V24_HP_H
#define V24_HP_H

int hp_active(void);   /* this race is an HP battle (a versus, VS_MODE_HP) and it has started */
float hp_of(int who);  /* the gauge, 0..1 (1 for a car not in the race) */
int hp_out(int who);   /* out (KO'd, or gone from the link): its KO's turn, 1 the first; 0 in */

#endif
