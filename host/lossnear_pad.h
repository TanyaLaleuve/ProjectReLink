/* (lossnear) La manette virtuelle de la console Lossnear : voir lossnear_pad.c. Ajout du fork TanyaLaleuve/ProjectReLink,
   GPL-3.0 comme le reste de ReLink. */
#pragma once
#include <stdint.h>
#include "hos_motion.h"

/* Demarre la lecture du tube <canal> (le nom seul, sans \\.\pipe\), sur un fil a part. Sans effet la seconde fois. */
void lossnear_pad_start(const char *canal);

/* Vrai tant que l'application Lossnear est branchee au bout du tube. */
int lossnear_pad_connected(void);

/* L'etat present, 0 si le tube n'est pas branche (rien n'est alors ecrit) :
   raw   : 1 << HOS_PAD_* par position, comme pad_raw_now (ce que lit le menu Insert) ;
   sw    : 1 << HOS_SW_*, les boutons de la Switch, par la table fixe de lossnear_pad.c ;
   stick : lx, ly, rx, ry dans la convention de la Switch (Y vers le haut), sans zone morte (l'application l'applique).
   La combinaison Select + Start (Retour + Start) en est retiree : c'est celle qui ouvre le menu de l'application. */
int lossnear_pad_read(uint32_t *raw, uint64_t *sw, int32_t stick[4]);

/* Les dernieres lectures du gyroscope pour GetSixAxisSensorStates, la plus recente d'abord, comme
   hos_sdl_motion_history : 0 sans tube, ou avec les commandes de mouvement eteintes. */
int lossnear_pad_motion_history(HosMotionSample *out, int max);
