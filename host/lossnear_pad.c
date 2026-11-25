/* (lossnear) LA MANETTE VIRTUELLE DE LA CONSOLE LOSSNEAR. Ajout du fork TanyaLaleuve/ProjectReLink, GPL-3.0 comme le
   reste de ReLink.

   L'application Lossnear calcule ce que fait le joueur - sa manette, son clavier, sa souris, traduits par SON preset - et
   l'envoie par un tube nomme, 250 fois par seconde. Ce fichier ne fait que lire le tube : tout le mappage vit dans
   l'application, jamais ici. Meme tube et meme trame que les forks Lossnear de Ryujinx et de melonDS (TrameManette cote
   application), 64 octets petit-boutistes :

     0-3   "LNMV" (0x564D4E4C)     4-5   version (1)     6-7   taille (64)     8-11  numero de trame
     12-15 boutons, bit N = SDL_GamepadButton N : pour N de 0 a 14 c'est aussi HOS_PAD_* N, le meme ordre (pad_sdl.c)
     16-27 six axes int16, ordre SDL_GamepadAxis : sticks Y positif vers le BAS, gachettes de 0 a 32767
     28-39 gyroscope, trois float32, rad/s, repere SDL
     40-51 accelerometre, trois float32, m/s2, repere SDL ; (0, 9.80665, 0) exactement : pas de vrai capteur
     52    joueur (0 = joueur 1)   53    drapeaux        54-63 reserve

   --virtual-pad-pipe <nom> le demarre (hos_main.c). TANT QUE LE TUBE EST BRANCHE, le jeu ne lit plus les manettes, le
   clavier ni la souris du joueur pour ses boutons et ses sticks (nn_hid.c, read_input) : deux mappages a la fois
   appuieraient deux boutons pour un geste. La manette virtuelle compte aussi quand la fenetre du jeu n'est pas devant :
   c'est l'application qui decide quand elle parle (son « robinet »), et jouer sur un ecran pendant qu'un autre a le
   focus doit marcher. Restent a ReLink : ses raccourcis (Insert/F1, F11, F12), son menu Insert, et la vibration, qui
   part toujours vers les manettes que SDL a ouvertes (pad_sdl.c). Debranche, tout redevient comme avant.

   Le gyroscope. Le filtre de ReLink (hos_motion.h) est fait pour un vrai capteur : il apprend comme un biais toute
   rotation parfaitement reguliere, ramene la visee verticale vers l'inclinaison que mesure l'accelerometre, et ignore
   une lecture identique a la precedente. Un gyroscope fabrique par l'application (la souris ou des touches vers le
   gyroscope) est exactement cela : regulier, sans gravite, repete. L'application l'annonce d'elle-meme, puisqu'elle
   envoie alors l'accelerometre d'une manette posee a plat, (0, 9.80665, 0) au bit pres. Dans ce cas les vitesses vont
   telles quelles au jeu, sans biais ni rappel ; avec un vrai accelerometre, le filtre de ReLink s'applique comme pour
   une manette SDL. */
#include "lossnear_pad.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../gpu/vk/vk_api.h"   /* (HOS_PAD_*, HOS_SW_*, HOS_OPT_GYRO*) */

void hos_log(const char *fmt, ...);   /* hos.h */
int64_t hos_now_ns(void);             /* hos.h */
int rx_option(int id, int otherwise); /* gpu/vk/vk_menu.cpp : les reglages du menu Insert */

#define LNMV_SIGNATURE 0x564D4E4Cu
#define LNMV_VERSION 1
#define LNMV_TAILLE 64
#define LNMV_BOUTONS 0x7FFFu        /* SDL_GamepadButton 0 a 14 = HOS_PAD_SOUTH a HOS_PAD_RIGHT */
#define LNMV_SEUIL_GACHETTE 8000    /* celui de pad_sdl.c */

/* LES BOUTONS DE LA SWITCH PAR POSITION, et non les reglages du joueur (pad_bind de nn_hid.c) : le preset de
   l'application a deja tout decide. C'est la table par defaut de ReLink (pad_default), une disposition Nintendo : A a
   droite, B en bas, X en haut, Y a gauche. La « prise » que declare l'adaptateur Lossnear range chaque sortie de la
   Switch a la place que cette table relit. */
static const signed char vers_switch[HOS_SW_N] = {
    HOS_PAD_EAST, HOS_PAD_SOUTH, HOS_PAD_NORTH, HOS_PAD_WEST, HOS_PAD_LSTICK, HOS_PAD_RSTICK, HOS_PAD_LSHOULDER,
    HOS_PAD_RSHOULDER, HOS_PAD_LTRIGGER, HOS_PAD_RTRIGGER, HOS_PAD_START, HOS_PAD_BACK, HOS_PAD_LEFT, HOS_PAD_UP,
    HOS_PAD_RIGHT, HOS_PAD_DOWN};

static uint16_t lire16(const uint8_t *p) { return (uint16_t)(p[0] | p[1] << 8); }
static uint32_t lire32(const uint8_t *p) { return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24; }
static float lire_f32(const uint8_t *p) {
    const uint32_t u = lire32(p);
    float f;
    memcpy(&f, &u, sizeof f);
    return f;
}
static int32_t borne(int32_t v) { return v < -32767 ? -32767 : v > 32767 ? 32767 : v; }

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

static SRWLOCK verrou = SRWLOCK_INIT;
static volatile LONG branchee;
static uint32_t etat_boutons;
static int16_t etat_axes[6];
static HosMotionSample historique[HOS_MOTION_HISTORY];
static int tete, nombre, mouvement_vivant;
static int trace;   /* LOSSNEAR_TRACE_MANETTE=1 : chaque changement au journal, pour les epreuves de l'application */

/* ---- le gyroscope : le fil de lecture est seul a toucher a ce qui suit, jusqu'a l'historique (sous le verrou) ---- */
static HosMotion filtre;
static int mode_mouvement = -1;   /* -1 rien encore, 0 vrai capteur (le filtre de ReLink), 1 fabrique par l'application */
static int64_t mouvement_ns, lecture_ns, lectures;

static void mouvement_coupe(void) {
    mode_mouvement = -1;
    AcquireSRWLockExclusive(&verrou);
    mouvement_vivant = 0, nombre = 0;
    ReleaseSRWLockExclusive(&verrou);
}

static void mouvement(const float accel[3], const float gyro[3], int64_t now) {
    if (!rx_option(HOS_OPT_GYRO, 1)) {   /* (les commandes de mouvement eteintes dans le menu Insert) */
        if (mode_mouvement >= 0) mouvement_coupe();
        return;
    }
    const int fabrique = accel[0] == 0.0f && accel[1] == HOS_MOTION_G && accel[2] == 0.0f;
    const float sens_h = (float)rx_option(HOS_OPT_GYRO_SENS_X, 100) / 100.0f,
                sens_v = (float)rx_option(HOS_OPT_GYRO_SENS_Y, 100) / 100.0f;
    const int inv_h = rx_option(HOS_OPT_GYRO_INV_X, 0), inv_v = rx_option(HOS_OPT_GYRO_INV_Y, 0);
    int neuf = 0;
    if (fabrique != mode_mouvement) {   /* (une autre source : un filtre neuf) */
        hos_motion_reset(&filtre);
        mode_mouvement = fabrique, mouvement_ns = now, neuf = 1;
        hos_log("lossnear: virtual pad motion: %s", fabrique ? "made by the app (rates as they come)" : "a real sensor (ReLink's filter)");
    }
    const int64_t dt_ns = now - mouvement_ns;
    mouvement_ns = now;
    if (fabrique) {
        /* Les memes calculs que hos_motion_update pour une manette a plat (le haut = +Y, la droite = +X), sans biais, sans
           rappel vers la gravite, et chaque trame compte : une touche tenue fait tourner tant qu'elle est tenue. */
        float dt = (float)((double)dt_ns / 1e9);
        if (!(dt >= 0)) dt = 0;
        if (dt > HOS_MOTION_DT_MAX) dt = HOS_MOTION_DT_MAX;
        const float sh = sens_h * (inv_h ? -1.0f : 1.0f), sv = -sens_v * (inv_v ? -1.0f : 1.0f);
        filtre.rates[0] = gyro[0], filtre.rates[1] = gyro[1], filtre.rates[2] = gyro[2];
        filtre.pitch = hos_motion_wrap(filtre.pitch + gyro[0] * sv * dt);
        hos_motion_rest(&filtre.out, filtre.pitch);
        filtre.out.gyro[1] = gyro[1] * sh / HOS_MOTION_TAU;
    } else if (!hos_motion_update(&filtre, accel, gyro, (float)((double)dt_ns / 1e9), sens_h, sens_v, inv_h, inv_v) && !neuf) {
        return;   /* (une lecture repetee n'est pas une lecture : son temps va dans la suivante) */
    }
    HosMotionSample r;
    r.st = filtre.out;
    r.n = ++lectures;
    r.dt_ns = neuf ? 5000000 : now - lecture_ns;
    lecture_ns = now;
    AcquireSRWLockExclusive(&verrou);
    tete = (tete + 1) % HOS_MOTION_HISTORY;
    historique[tete] = r;
    if (neuf) nombre = 0;
    if (nombre < HOS_MOTION_HISTORY) nombre++;
    mouvement_vivant = 1;
    ReleaseSRWLockExclusive(&verrou);
}

/* Une trame complete et reconnue : seul le joueur 1 compte, Ocarina of Time n'en a qu'un. */
static void appliquer(const uint8_t *t) {
    if (t[52] != 0) return;
    const uint32_t boutons = lire32(t + 12);
    int16_t axes[6];
    float gyro[3], accel[3];
    for (int i = 0; i < 6; i++) axes[i] = (int16_t)lire16(t + 16 + 2 * i);
    for (int i = 0; i < 3; i++) gyro[i] = lire_f32(t + 28 + 4 * i), accel[i] = lire_f32(t + 40 + 4 * i);
    AcquireSRWLockExclusive(&verrou);
    const uint32_t avant = etat_boutons;
    etat_boutons = boutons;
    memcpy(etat_axes, axes, sizeof etat_axes);
    ReleaseSRWLockExclusive(&verrou);
    if (trace && boutons != avant) hos_log("lossnear: virtual pad buttons %05X", (unsigned)boutons);
    mouvement(accel, gyro, hos_now_ns());
}

/* RIEN NE RESTE ENFONCE : un bouton tenu au moment ou le tube casse le resterait a jamais. */
static void relacher(void) {
    AcquireSRWLockExclusive(&verrou);
    etat_boutons = 0;
    memset(etat_axes, 0, sizeof etat_axes);
    ReleaseSRWLockExclusive(&verrou);
    mouvement_coupe();
}

/* LE FIL DE LECTURE. L'application cree le tube AVANT de lancer le jeu, donc il existe deja ; on s'y branche, on lit, et
   s'il casse - l'application qui se ferme, un tube rouvert - on relache tout et on s'y rebranche. Jamais d'arret du jeu
   pour autant. */
static DWORD WINAPI boucle(LPVOID arg) {
    const wchar_t *chemin = (const wchar_t *)arg;
    void hos_host_thread_register_self(const char *name);   /* mkw_glue.c */
    hos_host_thread_register_self("lossnear virtual pad");
    uint8_t tampon[LNMV_TAILLE * 9];   /* (huit trames d'un coup au plus, comme l'application les envoie, plus une entamee) */
    size_t rempli = 0;
    int annonce = 1;
    for (;;) {
        HANDLE tube = CreateFileW(chemin, GENERIC_READ, 0, NULL, OPEN_EXISTING, 0, NULL);
        if (tube == INVALID_HANDLE_VALUE) {
            const DWORD err = GetLastError();
            if (annonce) hos_log("lossnear: virtual pad: no pipe yet (error %lu), waiting", (unsigned long)err), annonce = 0;
            if (err == ERROR_PIPE_BUSY) WaitNamedPipeW(chemin, 500);
            else Sleep(200);
            continue;
        }
        annonce = 1, rempli = 0;
        InterlockedExchange(&branchee, 1);
        hos_log("lossnear: virtual pad connected");
        for (;;) {
            DWORD lus = 0;
            if (!ReadFile(tube, tampon + rempli, (DWORD)(sizeof tampon - rempli), &lus, NULL) || !lus) break;
            rempli += lus;
            /* Les trames arrivent collees : on les decoupe, et ce qui ne commence pas par la signature fait avancer d'un
               octet - on se recale sur la suivante plutot que de lire de travers tout ce qui vient apres. */
            size_t debut = 0;
            while (rempli - debut >= LNMV_TAILLE) {
                const uint8_t *t = tampon + debut;
                if (lire32(t) == LNMV_SIGNATURE && lire16(t + 4) == LNMV_VERSION && lire16(t + 6) == LNMV_TAILLE) {
                    appliquer(t);
                    debut += LNMV_TAILLE;
                } else {
                    debut++;
                }
            }
            memmove(tampon, tampon + debut, rempli - debut);
            rempli -= debut;
        }
        CloseHandle(tube);
        relacher();
        InterlockedExchange(&branchee, 0);
        hos_log("lossnear: virtual pad disconnected");
    }
    return 0;
}

void lossnear_pad_start(const char *canal) {
    static volatile LONG demarree;
    if (!canal || !*canal || InterlockedExchange(&demarree, 1)) return;
    const char *t = getenv("LOSSNEAR_TRACE_MANETTE");
    trace = t && !strcmp(t, "1");
    /* Le nom seul, comme l'application le donne : le prefixe des tubes nommes est ajoute ici. */
    static wchar_t chemin[320];
    static const wchar_t prefixe[] = L"\\\\.\\pipe\\";
    size_t n = 0;
    for (; prefixe[n]; n++) chemin[n] = prefixe[n];
    for (const char *c = canal; *c && n + 1 < sizeof chemin / sizeof *chemin; c++) chemin[n++] = (wchar_t)(unsigned char)*c;
    chemin[n] = 0;
    hos_log("lossnear: virtual pad: pipe %s", canal);
    HANDLE h = CreateThread(NULL, 0, boucle, chemin, 0, NULL);   /* (il vit autant que le jeu) */
    if (!h) {
        hos_log("lossnear: virtual pad: its thread could not start (error %lu)", (unsigned long)GetLastError());
        return;
    }
    SetThreadPriority(h, THREAD_PRIORITY_ABOVE_NORMAL);
    CloseHandle(h);
}

int lossnear_pad_connected(void) { return branchee != 0; }

int lossnear_pad_read(uint32_t *raw, uint64_t *sw, int32_t stick[4]) {
    if (!branchee) return 0;
    uint32_t b;
    int16_t a[6];
    AcquireSRWLockShared(&verrou);
    b = etat_boutons;
    memcpy(a, etat_axes, sizeof a);
    ReleaseSRWLockShared(&verrou);
    uint32_t r = b & LNMV_BOUTONS;
    if (a[4] > LNMV_SEUIL_GACHETTE) r |= 1u << HOS_PAD_LTRIGGER;
    if (a[5] > LNMV_SEUIL_GACHETTE) r |= 1u << HOS_PAD_RTRIGGER;
    /* Select + Start ouvre le menu de l'APPLICATION, qui le guette sur la vraie manette. Les premieres trames du geste
       arrivent ici avant que l'application ne ferme son robinet : ni le menu Insert (qui s'ouvre aussi sur Retour +
       Start) ni le jeu ne doivent les recevoir, comme pads_read le fait deja pour le jeu. */
    const uint32_t accord = 1u << HOS_PAD_BACK | 1u << HOS_PAD_START;
    if ((r & accord) == accord) r &= ~accord;
    *raw = r;
    uint64_t s = 0;
    for (int i = 0; i < HOS_SW_N; i++)
        if (r >> vers_switch[i] & 1) s |= 1ULL << i;
    *sw = s;
    /* (Y retourne : SDL le compte vers le bas, la Switch vers le haut. L'inversion de la camera reste celle du menu
       Insert, comme pour une manette SDL ; la zone morte est deja appliquee par l'application, liaison par liaison) */
    const int inv_x = rx_option(16, 0), inv_y = rx_option(17, 0);
    stick[0] = borne(a[0]);
    stick[1] = -borne(a[1]);
    stick[2] = inv_x ? -borne(a[2]) : borne(a[2]);
    stick[3] = inv_y ? borne(a[3]) : -borne(a[3]);
    return 1;
}

int lossnear_pad_motion_history(HosMotionSample *out, int max) {
    if (!branchee) return 0;
    AcquireSRWLockShared(&verrou);
    const int n = !mouvement_vivant ? 0 : nombre < max ? nombre : max;
    for (int i = 0; i < n; i++) out[i] = historique[(tete - i + HOS_MOTION_HISTORY) % HOS_MOTION_HISTORY];
    ReleaseSRWLockShared(&verrou);
    return n;
}

#else   /* (Linux : pas encore ecrite, les tubes nommes y sont autres. Le jeu se comporte comme sans l'option) */

void lossnear_pad_start(const char *canal) {
    if (canal && *canal) hos_log("lossnear: virtual pad: not written for this system yet");
}
int lossnear_pad_connected(void) { return 0; }
int lossnear_pad_read(uint32_t *raw, uint64_t *sw, int32_t stick[4]) {
    (void)raw, (void)sw, (void)stick;
    (void)vers_switch, (void)lire_f32, (void)borne;
    return 0;
}
int lossnear_pad_motion_history(HosMotionSample *out, int max) {
    (void)out, (void)max;
    return 0;
}

#endif
