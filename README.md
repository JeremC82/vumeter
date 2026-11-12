# VU-mètre ESP32 HUB75

VU-mètre stéréo autonome (sans wifi) sur ESP32 et panneau(x) LED HUB75.

![Les 6 modes d'affichage](docs/rendu.png)

*Aperçu simulé sur une dalle 128x64 (généré par `docs/rendu.py`).*

- Entrée ligne analogique : GPIO36 (L) / GPIO39 (R), polarisée ~1,3 V, lue par l'ADC interne via I2S0.
- Panneau sur I2S1 (mêmes broches que l'horloge ClockV1).
- Dispositions : 64x64, 2x64x64, 128x64, 2x128x64.
- Bouton mode GPIO33/BOOT : appui court = mode d'affichage, 1-5 s = luminosité, 5 s = disposition du panneau (secours).
- Bouton dalle GPIO34 (même broche que le bouton reset wifi de l'horloge ClockV1 : bouton vers le 3,3 V + résistance 10 k vers la masse) : chaque appui passe à la disposition suivante (64x64 → 2x64x64 → 128x64 → 2x128x64), appliquée 2 s après le dernier appui (redémarrage, choix mémorisé).
- Mode démo : cavalier permanent entre GPIO18 et la masse → musique synthétisée en interne (batterie, basse, arpège stéréo), sans source audio. Se met ou s'enlève à chaud.

Câblage : [docs/branchement.png](docs/branchement.png) (généré par `docs/branchement.py`).

Compilation : `./build.ps1` (arduino-cli, ESP32 classique) → `vumeter.bin`.

> Pas encore testé sur le matériel.

## Firmware (v1.33)

Binaires prêts à flasher (un seul binaire pour toutes les dalles, à l'adresse 0x10000) :

| Fichier | Usage | MD5 |
|---|---|---|
| [firmware/vumeter_v1.33.bin](firmware/vumeter_v1.33.bin) | normal (démo par cavalier GPIO18) | `e5c9e6805ca78f75ff92b36ce527bfdc` |
| [firmware/vumeter_v1.33_demo.bin](firmware/vumeter_v1.33_demo.bin) | démo toujours active, sans cavalier ni source audio | `6e7b064cd55ddca19bb53cd401cdf90e` |

Versions précédentes dans `firmware/`. Compilation de la version démo : `./build.ps1 -Demo`.
