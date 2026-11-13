# VU-mètre ESP32 HUB75

VU-mètre stéréo autonome (sans wifi) sur ESP32 et panneau(x) LED HUB75.

![Les modes d'affichage](docs/rendu.png)

*Aperçu simulé sur une dalle 128x64 (généré par `docs/rendu.py`) ; les points gris figurent les LED éteintes. Autres tailles : [128x128](docs/rendu-128x128.png) (2 dalles empilées) et [256x64](docs/rendu-256x64.png) (2 dalles côte à côte).*

- Entrée ligne analogique : GPIO36 (L) / GPIO39 (R), polarisée ~1,3 V, lue par l'ADC interne via I2S0.
- Panneau sur I2S1 (mêmes broches que l'horloge ClockV1).
- Dispositions : 64x64, 2x64x64, 128x64, 2x128x64 (côte à côte), 128x128 (2 dalles 128x64 l'une sous l'autre : idéal pour le Jeu).
- Modes d'affichage : Spectre, Spectre St (crêtes gauche rouge / droite blanche, jaune si égales), Miroir, Stéréo, Aiguilles (cadrans bleus), Jeu (shoot'em up qui joue tout seul, réagit à la musique), Puissance (cadrans noirs rétroéclairés façon ampli, aiguille rouge, voyant PEAK), Watts (cadrans bleu clair, échelle en watts et décibels), et Auto qui les fait défiler toutes les 20 s (le Jeu seulement quand la démo est active).
- Mode démo : cavalier permanent entre GPIO18 et la masse, ou touche `d` du menu série → musique synthétisée en interne (batterie, basse, arpège stéréo), sans source audio. Se met ou s'enlève à chaud.

## Menu du moniteur série

Moniteur série à **115200 bauds** : le menu s'affiche au démarrage, puis avec `?`. Taper la touche puis Entrée.

```
===== VU-metre v1.39 =====
Dalle : 128x64   Mode : Auto   Luminosite : 3/5
Demo  : arretee
-------------------------------
  1 : dalle 64x64
  2 : dalle 2x64x64
  3 : dalle 128x64  <- actuelle
  4 : dalle 2x128x64
  5 : dalle 128x128
  d : demo marche / arret
  m : mode d'affichage suivant
  l : luminosite suivante
  ? : afficher ce menu
```

- `1` à `5` : choix de la dalle, enregistré, l'ESP32 redémarre.
- `d` : lance ou arrête la démo, choix mémorisé après redémarrage (le cavalier GPIO18 force la démo quel que soit ce réglage).

Détails, outils pour se connecter et exemples : [docs/menu-serie.md](docs/menu-serie.md).

## Le mode Jeu en 128x128

Shoot'em up d'arcade original qui joue tout seul (2 dalles 128x64 l'une sous l'autre, touche `5` du menu) :
3 espèces d'ennemis (la méduse demande 2 tirs), piqués en courbe, SCORE et RECORD (gardé en mémoire),
vies et vague en bas. Les ennemis s'éclairent sur les basses et les étoiles accélèrent avec le volume.

![Le mode Jeu en 128x128](docs/rendu-jeu.png)

Câblage : [docs/branchement.png](docs/branchement.png) (généré par `docs/branchement.py`).

Compilation : `./build.ps1` (arduino-cli, ESP32 classique) → `vumeter.bin`.

> Pas encore testé sur le matériel.

## Firmware

Binaire prêt à flasher (un seul binaire pour toutes les dalles, à l'adresse 0x10000) :
[firmware/vumeter_v1.39.bin](firmware/vumeter_v1.39.bin) — MD5 `c2e9706b91897a7e8103ec1c9f731637`
