/* (lossnear) EPREUVE DE LA MANETTE VIRTUELLE (host/lossnear_pad.c), sans jeu ni manette : ce programme joue le role de
   l'application Lossnear - il cree le tube, envoie des trames - et verifie ce que le jeu en recevrait. Tourne sur la CI
   (.github/workflows/epreuves-lossnear.yml). Code de sortie : le nombre d'echecs.

   Construction : x86_64-w64-mingw32-clang -O2 -Ihost -Igpu/vk lossnear/tests/test_manette.c host/lossnear_pad.c */
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <math.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "lossnear_pad.h"
#include "vk_api.h"

/* ---- ce que le reste de ReLink fournirait au fichier ---- */
static volatile int options[64], options_posees[64];
int rx_option(int id, int otherwise) { return id >= 0 && id < 64 && options_posees[id] ? options[id] : otherwise; }
static void poser(int id, int v) { options[id] = v, options_posees[id] = 1; }
static void oublier(int id) { options_posees[id] = 0; }
void hos_log(const char *fmt, ...) {
    va_list a;
    va_start(a, fmt);
    printf("    [jeu] ");
    vprintf(fmt, a);
    printf("\n");
    fflush(stdout);
    va_end(a);
}
int64_t hos_now_ns(void) {
    LARGE_INTEGER f, c;
    QueryPerformanceFrequency(&f);
    QueryPerformanceCounter(&c);
    return (int64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart);
}
void hos_host_thread_register_self(const char *name) { (void)name; }

/* ---- les verdicts ---- */
static int epreuves, echecs;
static void verdict(int ok, const char *quoi) {
    epreuves++;
    if (!ok) echecs++;
    printf("%s  %s\n", ok ? "OK    " : "ECHEC ", quoi);
    fflush(stdout);
}
/* (le fil de lecture travaille de son cote : on attend que ce qu'on vient d'envoyer soit arrive, 2 s au plus) */
#define ATTENDRE(cond) do { for (int _i = 0; _i < 400 && !(cond); _i++) Sleep(5); } while (0)

/* ---- le cote application : le tube et ses trames, comme TrameManette ---- */
static HANDLE tube = INVALID_HANDLE_VALUE;
static wchar_t chemin[128];
static uint32_t numero;

typedef struct {
    uint32_t boutons;
    int16_t axes[6];
    float gyro[3], accel[3];
    uint8_t joueur, drapeaux;
    uint16_t version;
} Trame;

static Trame trame_repos(void) {
    Trame t;
    memset(&t, 0, sizeof t);
    t.accel[1] = 9.80665f;   /* (EtatManetteStandard.Gravite : la manette posee a plat) */
    t.version = 1;
    return t;
}
static void poser32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8), p[2] = (uint8_t)(v >> 16), p[3] = (uint8_t)(v >> 24); }
static void poser16(uint8_t *p, uint16_t v) { p[0] = (uint8_t)v, p[1] = (uint8_t)(v >> 8); }
static void coder(uint8_t out[64], const Trame *t) {
    memset(out, 0, 64);
    poser32(out, 0x564D4E4Cu);
    poser16(out + 4, t->version);
    poser16(out + 6, 64);
    poser32(out + 8, ++numero);
    poser32(out + 12, t->boutons);
    for (int i = 0; i < 6; i++) poser16(out + 16 + 2 * i, (uint16_t)t->axes[i]);
    for (int i = 0; i < 3; i++) {
        uint32_t u;
        memcpy(&u, &t->gyro[i], 4), poser32(out + 28 + 4 * i, u);
        memcpy(&u, &t->accel[i], 4), poser32(out + 40 + 4 * i, u);
    }
    out[52] = t->joueur, out[53] = t->drapeaux;
}
static int ecrire(const void *p, DWORD n) {
    DWORD w = 0;
    return WriteFile(tube, p, n, &w, NULL) && w == n;
}
static int envoyer(const Trame *t) {
    uint8_t b[64];
    coder(b, t);
    return ecrire(b, 64);
}
static int ouvrir_tube(void) {
    tube = CreateNamedPipeW(chemin, PIPE_ACCESS_OUTBOUND, PIPE_TYPE_BYTE | PIPE_WAIT, 1, 4096, 4096, 0, NULL);
    return tube != INVALID_HANDLE_VALUE;
}
static int attendre_client(void) { return ConnectNamedPipe(tube, NULL) || GetLastError() == ERROR_PIPE_CONNECTED; }

/* ---- ce que lit le jeu ---- */
static uint32_t raw;
static uint64_t sw;
static int32_t st[4];
static int lire(void) { return lossnear_pad_read(&raw, &sw, st); }
static int sw_vaut(uint64_t v) { return lire() && sw == v; }
static float tangage(const HosMotionSample *h) { return atan2f(h->st.direction[7], h->st.direction[4]); }

int main(void) {
    swprintf(chemin, 128, L"\\\\.\\pipe\\lossnear-epreuve-%lu", (unsigned long)GetCurrentProcessId());
    char canal[64];
    snprintf(canal, sizeof canal, "lossnear-epreuve-%lu", (unsigned long)GetCurrentProcessId());

    printf("== branchement\n");
    verdict(ouvrir_tube(), "l'application cree le tube avant le jeu");
    verdict(!lossnear_pad_connected() && !lire(), "sans tube lu, rien ne vient de la manette virtuelle");
    lossnear_pad_start(canal);
    lossnear_pad_start(canal);   /* (la seconde fois est sans effet) */
    verdict(attendre_client(), "le jeu se branche au tube");
    ATTENDRE(lossnear_pad_connected());
    verdict(lossnear_pad_connected(), "branchee");
    verdict(lire() && raw == 0 && sw == 0 && !st[0] && !st[1] && !st[2] && !st[3], "au repos avant la premiere trame");

    printf("== boutons, par position (disposition Nintendo)\n");
    static const struct { int sdl; int sw; const char *nom; } table[] = {
        {0, HOS_SW_B, "Sud -> B"}, {1, HOS_SW_A, "Est -> A"}, {2, HOS_SW_Y, "Ouest -> Y"}, {3, HOS_SW_X, "Nord -> X"},
        {4, HOS_SW_MINUS, "Retour -> Moins"}, {5, -1, "Guide -> rien"}, {6, HOS_SW_PLUS, "Start -> Plus"},
        {7, HOS_SW_LS, "clic gauche -> LS"}, {8, HOS_SW_RS, "clic droit -> RS"}, {9, HOS_SW_L, "L1 -> L"},
        {10, HOS_SW_R, "R1 -> R"}, {11, HOS_SW_UP, "croix haut"}, {12, HOS_SW_DOWN, "croix bas"},
        {13, HOS_SW_LEFT, "croix gauche"}, {14, HOS_SW_RIGHT, "croix droite"}};
    for (unsigned i = 0; i < sizeof table / sizeof *table; i++) {
        Trame t = trame_repos();
        t.boutons = 1u << table[i].sdl;
        envoyer(&t);
        ATTENDRE(lire() && raw == t.boutons);
        verdict(lire() && raw == t.boutons && sw == (table[i].sw < 0 ? 0 : 1ULL << table[i].sw), table[i].nom);
    }
    {
        Trame t = trame_repos();
        t.boutons = 1u << 15 | 1u << 20 | 1u << 31;   /* (des bits qu'aucun bouton SDL n'utilise ici) */
        envoyer(&t);
        ATTENDRE(lire() && raw == 0);
        verdict(lire() && raw == 0 && sw == 0, "les bits au-dela des 15 boutons sont ignores");
    }

    printf("== gachettes\n");
    {
        Trame t = trame_repos();
        t.axes[4] = 9000, t.axes[5] = 7000;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_ZL));
        verdict(sw_vaut(1ULL << HOS_SW_ZL), "gachette gauche a 9000 -> ZL, droite a 7000 -> rien (seuil 8000)");
        t.axes[4] = 0, t.axes[5] = 32767;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_ZR));
        verdict(sw_vaut(1ULL << HOS_SW_ZR) && raw == 1u << HOS_PAD_RTRIGGER, "gachette droite a fond -> ZR");
    }

    printf("== sticks\n");
    {
        Trame t = trame_repos();
        t.axes[0] = 1000, t.axes[1] = -2000, t.axes[2] = -32768, t.axes[3] = 32767;
        envoyer(&t);
        ATTENDRE(lire() && st[0] == 1000);
        verdict(lire() && st[0] == 1000 && st[1] == 2000 && st[2] == -32767 && st[3] == -32767,
                "Y retourne (SDL vers le bas, Switch vers le haut), -32768 borne a -32767, sans zone morte");
        t.axes[0] = 300, t.axes[1] = 0;
        envoyer(&t);
        ATTENDRE(lire() && st[0] == 300);
        verdict(lire() && st[0] == 300 && st[1] == 0, "une petite poussee passe telle quelle (la zone morte est celle de l'application)");
        poser(16, 1), poser(17, 1);
        verdict(lire() && st[2] == 32767 && st[3] == 32767, "l'inversion de la camera du menu Insert s'applique au stick droit");
        oublier(16), oublier(17);
    }

    printf("== Select + Start : le menu de l'application\n");
    {
        Trame t = trame_repos();
        t.boutons = 1u << 4 | 1u << 6 | 1u << 1;   /* Retour + Start + Est */
        envoyer(&t);
        ATTENDRE(lire() && raw == 1u << HOS_PAD_EAST);
        verdict(lire() && raw == 1u << HOS_PAD_EAST && sw == 1ULL << HOS_SW_A,
                "Retour + Start n'atteignent ni le menu Insert ni le jeu ; A tenu en meme temps passe");
        t.boutons = 1u << 6;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_PLUS));
        verdict(sw_vaut(1ULL << HOS_SW_PLUS), "Start seul arrive (Plus)");
    }

    printf("== trames abimees ou etrangeres\n");
    {
        Trame t = trame_repos();
        static const uint8_t bruit[5] = {1, 2, 3, 4, 5};
        ecrire(bruit, sizeof bruit);
        t.boutons = 1u << 1;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_A));
        verdict(sw_vaut(1ULL << HOS_SW_A), "cinq octets de bruit puis une trame : on se recale dessus");
        t.boutons = 1u << 0;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_B));
        verdict(sw_vaut(1ULL << HOS_SW_B), "et les suivantes restent alignees");

        Trame v2 = trame_repos();
        v2.version = 2, v2.boutons = 1u << 3;
        envoyer(&v2);
        Trame j2 = trame_repos();
        j2.joueur = 1, j2.drapeaux = 1, j2.boutons = 1u << 3;
        envoyer(&j2);
        Trame temoin = trame_repos();
        temoin.boutons = 1u << 2;   /* (Ouest -> Y : arrive APRES les deux autres, donc prouve qu'elles sont passees) */
        envoyer(&temoin);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_Y));
        verdict(sw_vaut(1ULL << HOS_SW_Y), "une trame d'une autre version et une trame du joueur 2 sont ignorees");

        uint8_t trois[64 * 3];
        Trame a = trame_repos(), b = trame_repos(), c = trame_repos();
        a.boutons = 1u << 1, b.boutons = 1u << 0, c.boutons = 1u << 3;
        coder(trois, &a), coder(trois + 64, &b), coder(trois + 128, &c);
        ecrire(trois, sizeof trois);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_X));
        verdict(sw_vaut(1ULL << HOS_SW_X), "trois trames collees en une ecriture : la derniere l'emporte");
    }

    printf("== gyroscope fabrique par l'application (souris, touches)\n");
    {
        HosMotionSample h[HOS_MOTION_HISTORY];
        Trame t = trame_repos();
        t.gyro[1] = 6.2831853f;   /* un tour par seconde de lacet, parfaitement regulier */
        const int64_t debut = hos_now_ns();
        while (hos_now_ns() - debut < 800000000LL) {   /* 0,8 s : plus que les 0,5 s ou ReLink apprendrait un biais */
            envoyer(&t);
            Sleep(4);
        }
        ATTENDRE(lossnear_pad_motion_history(h, HOS_MOTION_HISTORY) > 0);
        const int n = lossnear_pad_motion_history(h, HOS_MOTION_HISTORY);
        verdict(n > 0, "le jeu recoit des lectures du gyroscope");
        verdict(n > 0 && fabsf(h[0].st.gyro[1] - 1.0f) < 0.001f,
                "une rotation reguliere tenue 0,8 s arrive entiere (1 tour/s) : rien n'est pris pour un biais");
        verdict(n > 1 && h[0].n > h[1].n, "chaque trame est une lecture, meme identique a la precedente");

        t.gyro[1] = 0;
        envoyer(&t);   /* (le repos : le temps ecoule jusqu'ici ne compte pas dans le tangage qui suit) */
        const int64_t t0 = hos_now_ns();   /* (la premiere trame de tangage comptera le temps depuis celle-ci) */
        int64_t t1 = t0;
        Sleep(20);
        ATTENDRE(lossnear_pad_motion_history(h, 1) == 1 && fabsf(h[0].st.gyro[1]) < 1e-6f);
        const float avant = lossnear_pad_motion_history(h, 1) ? tangage(&h[0]) : 0;
        t.gyro[0] = 1.0f;   /* 1 rad/s de tangage pendant 0,5 s */
        while (hos_now_ns() - t0 < 500000000LL) {
            Sleep(4);
            envoyer(&t);
            t1 = hos_now_ns();
        }
        t.gyro[0] = 0;
        Sleep(4);
        envoyer(&t);
        ATTENDRE(lossnear_pad_motion_history(h, 1) == 1 && fabsf(tangage(&h[0]) - avant) > 0.1f);
        Sleep(50);   /* (que les dernieres trames soient lues) */
        lossnear_pad_motion_history(h, 1);
        const float attendu = -(float)((double)(t1 - t0) / 1e9);   /* (sv = -1 a 100 %, comme hos_motion_update) */
        const float pris = tangage(&h[0]) - avant;
        char ligne[160];
        snprintf(ligne, sizeof ligne, "le tangage s'integre : %.3f rad pour %.3f attendus (a 15 %% pres)", pris, attendu);
        verdict(fabsf(pris - attendu) < 0.15f * fabsf(attendu), ligne);
        const int64_t t2 = hos_now_ns();
        while (hos_now_ns() - t2 < 1200000000LL) {   /* 1,2 s immobile : ReLink ramenerait la visee vers la gravite */
            envoyer(&t);
            Sleep(4);
        }
        Sleep(20);
        lossnear_pad_motion_history(h, 1);
        verdict(fabsf(tangage(&h[0]) - avant - pris) < 0.02f, "immobile 1,2 s ensuite : la visee verticale reste ou elle est");

        poser(HOS_OPT_GYRO, 0);
        envoyer(&t);
        ATTENDRE(lossnear_pad_motion_history(h, HOS_MOTION_HISTORY) == 0);
        verdict(lossnear_pad_motion_history(h, HOS_MOTION_HISTORY) == 0, "commandes de mouvement eteintes dans le menu Insert : plus rien");
        oublier(HOS_OPT_GYRO);
    }

    printf("== gyroscope d'une vraie manette (accelerometre mesure)\n");
    {
        HosMotionSample h[HOS_MOTION_HISTORY];
        Trame t = trame_repos();
        /* (une main qui tient la manette tremble : des ecarts de quelques centiemes de rad/s, au-dessus du seuil ou ReLink
           la croirait posee et apprendrait le lacet comme un biais - ce qu'il doit faire pour un vrai capteur immobile) */
        for (int i = 0; i < 100; i++) {
            t.accel[0] = 0.3f + 0.01f * (float)(i % 3), t.accel[1] = 9.7f, t.accel[2] = 0.2f;
            t.gyro[0] = (i % 2 ? 0.05f : -0.05f), t.gyro[1] = 0.5f, t.gyro[2] = (i % 3 ? 0.03f : -0.03f);
            envoyer(&t);
            Sleep(4);
        }
        ATTENDRE(lossnear_pad_motion_history(h, HOS_MOTION_HISTORY) > 0);
        const int n = lossnear_pad_motion_history(h, HOS_MOTION_HISTORY);
        verdict(n > 0 && h[0].st.gyro[1] > 0.02f, "le filtre de ReLink s'applique et le lacet passe");
    }

    printf("== debranchement\n");
    {
        Trame t = trame_repos();
        t.boutons = 1u << 1;   /* A tenu au moment ou l'application s'en va */
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_A));
        CloseHandle(tube);
        tube = INVALID_HANDLE_VALUE;
        ATTENDRE(!lossnear_pad_connected());
        HosMotionSample h[1];
        verdict(!lossnear_pad_connected() && !lire() && !lossnear_pad_motion_history(h, 1),
                "tube ferme : debranchee, le jeu reprend les appareils du joueur");
        verdict(ouvrir_tube() && attendre_client(), "un tube rouvert : le jeu s'y rebranche seul");
        ATTENDRE(lossnear_pad_connected());
        verdict(lire() && sw == 0 && raw == 0, "et le A tenu avant la coupure ne reste pas enfonce");
        t.boutons = 1u << 0;
        envoyer(&t);
        ATTENDRE(sw_vaut(1ULL << HOS_SW_B));
        verdict(sw_vaut(1ULL << HOS_SW_B), "les trames suivantes arrivent");
    }

    printf("\n%d epreuves, %d echec%s\n", epreuves, echecs, echecs > 1 ? "s" : "");
    return echecs;
}
