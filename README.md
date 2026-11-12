# VU-mètre ESP32 HUB75

VU-mètre stéréo autonome (sans wifi) sur ESP32 et panneau(x) LED HUB75.

- Entrée ligne analogique : GPIO36 (L) / GPIO39 (R), polarisée ~1,3 V, lue par l'ADC interne via I2S0.
- Panneau sur I2S1 (mêmes broches que l'horloge ClockV1).
- Dispositions : 64x64, 2x64x64, 128x64, 2x128x64.
- Bouton GPIO33/BOOT : appui court = mode, 1-5 s = luminosité, 5 s = disposition du panneau.
- Mode démo : cavalier permanent entre GPIO18 et la masse → musique synthétisée en interne (batterie, basse, arpège stéréo), sans source audio. Se met ou s'enlève à chaud.

Câblage : [docs/branchement.png](docs/branchement.png) (généré par `docs/branchement.py`).

Compilation : `./build.ps1` (arduino-cli, ESP32 classique) → `vumeter.bin`.

> Pas encore testé sur le matériel.

## Firmware

Binaire prêt à flasher : [firmware/vumeter_v1.1.bin](firmware/vumeter_v1.1.bin)
(un seul binaire pour toutes les dalles, à flasher à l'adresse 0x10000).
MD5 : `0ae9fb159a7cc13c57da47a376aeaa61` (v1.0 toujours disponible dans `firmware/`)
