# ReLink pour Lossnear

Ce dépôt est un fork de [pixl27/ProjectReLink](https://github.com/pixl27/ProjectReLink), le recompilateur PC du remake
Switch 2 d'Ocarina of Time. Il est adapté à Lossnear, une console de jeu maison pour PC. Licence GPL-3.0, comme
l'original.

## Les règles du fork

- **Les ajouts sont purement additifs.** On ne supprime rien de l'amont et on touche ses fichiers le moins possible.
  Un fork qui ajoute se rebase ; un fork qui supprime entre en conflit à chaque version.
- **Les branches** :
  - `main` est un miroir de l'amont et ne reçoit aucun commit ;
  - `lossnear/fork` porte nos ajouts (branche par défaut) ;
  - `essai/**` sert aux branches d'essai.
- **On ne pousse jamais d'étiquette `v*` ici.** Le workflow de l'amont (`windows.yml`) publierait une release.
- **Aucun jeu ne vient ici.** Ni le `.nup`, ni le code traduit, ni le dossier fabriqué. La fabrication se fait sur le
  PC du joueur, comme dans ReLink. Les épreuves de la CI (`epreuves-lossnear.yml`) tournent sans jeu et ne publient
  rien.

## Les ajouts

### `--virtual-pad-pipe <nom>` : la manette virtuelle

Fichiers : `host/lossnear_pad.c` et `host/lossnear_pad.h`. Points d'accroche : `host/hos_main.c` (l'option) et
`host/nn_hid.c` (`read_input` et `GetSixAxisSensorStates`).

L'application Lossnear lit les manettes, le clavier et la souris du joueur. Elle les traduit par le preset de son
profil et envoie le résultat dans un tube nommé, 250 fois par seconde. Le tube et la trame sont les mêmes que dans les
forks Lossnear de Ryujinx et de melonDS (64 octets, « LNMV », version 1).

- **Tant que le tube est branché, le jeu ne lit plus les appareils du joueur** pour ses boutons et ses sticks. Deux
  mappages à la fois appuieraient deux boutons pour un geste.
- **Ce qui reste à ReLink** : ses raccourcis (Insert/F1, F11, F12), son menu Insert, que la manette virtuelle peut
  parcourir, et la vibration, qui part toujours vers les manettes que SDL a ouvertes.
- **Les boutons suivent une table fixe**, par position (disposition Nintendo). Les réglages de touches de
  `relink_options.ini` ne s'appliquent pas : le preset de Lossnear a déjà tout décidé.
- **La zone morte des sticks est celle de Lossnear.** L'inversion de la caméra reste celle du menu Insert.
- **Select + Start (Retour + Start) n'atteint ni le jeu ni le menu Insert.** C'est la combinaison qui ouvre le menu
  de Lossnear.
- **La manette virtuelle compte même quand la fenêtre du jeu n'est pas devant.** C'est Lossnear qui décide quand elle
  parle.
- **Le gyroscope a deux cas.** Si Lossnear envoie un accéléromètre exactement « posé à plat » (0 ; 9,80665 ; 0), le
  gyroscope a été fabriqué (souris, touches) : ses vitesses vont telles quelles au jeu. Le filtre de ReLink les aurait
  prises pour un biais et aurait ramené la visée vers la gravité. Avec un vrai accéléromètre, le filtre de ReLink
  s'applique comme pour une manette SDL.
- **Débranché, tout redevient comme avant.** Rien ne reste enfoncé, et le jeu se rebranche seul si le tube revient.
- `LOSSNEAR_TRACE_MANETTE=1` écrit chaque changement de boutons dans `relink_log.txt`.

Pas encore écrit pour Linux : l'option n'y a pas d'effet.

### Déjà dans ReLink, sans retouche

- `--user <dossier>` : un dossier de sauvegardes par profil.
- `RELINK_NO_UPDATE_CHECK=1` : pas de recherche de mise à jour.
- Le premier démarrage écrit son avancement dans `relink_log.txt` (« about N min left »).

## La publication

Ce fork ne publie rien lui-même : aucune release, aucune étiquette. Les outils que Lossnear installe (case Switch 2 de
l'écran des émulateurs) sont assemblés et publiés par le dépôt de l'application, workflow
`publier-relink.yml`, lancé à la main avec trois entrées : la version amont dont part le fork (`v0.7.9.4`), l'empreinte
SHA-256 de son paquet Windows officiel, et la branche ou le commit du fork (`lossnear/fork`).

Le paquet publié est le paquet officiel, vérifié fichier par fichier contre le source de son étiquette, avec par-dessus
les fichiers que ce fork change dans `assets`, `gpu`, `host`, `nx2`, `scripts`, `third_party` et `tools`. Le reste du
fork (`lossnear/`, ce fichier, la CI) n'y entre pas. L'archive du source du commit est publiée à côté.

**Un fichier de `gpu/` changé fait recompiler le rendu chez le joueur** (environ 500 fichiers en plus) : la DLL
précompilée de l'amont ne correspond plus à sa clé. Les ajouts vont donc dans `host/` tant que c'est possible.

Première publication : `0.7.9.4-lossnear.2026.11.25-4aabefb` (la manette virtuelle).
