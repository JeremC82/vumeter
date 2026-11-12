# Menu du moniteur série

Le VU-mètre se règle depuis un ordinateur relié en USB à l'ESP32, avec un simple moniteur série.
Le menu permet de choisir le type de dalle, de lancer ou d'arrêter la démo, et de changer le mode
d'affichage ou la luminosité. Les réglages sont gardés en mémoire (ils survivent à une coupure de
courant et à un reflash du firmware).

## Se connecter

Réglages du port série : **115200 bauds**, fin de ligne au choix (Entrée, `\n` ou `\r\n` : tout est accepté).

| Outil | Comment faire |
|---|---|
| Arduino IDE | Outils → Moniteur série, choisir **115200 baud** en bas à droite |
| VS Code (extension Serial Monitor) | Onglet *Serial Monitor*, choisir le port COM et **115200**, puis *Start Monitoring* |
| PuTTY | Connection type *Serial*, port `COMx`, speed **115200** |
| arduino-cli | `arduino-cli monitor -p COMx -c baudrate=115200` |

Le port `COMx` est celui qui apparaît quand on branche l'ESP32 (Gestionnaire de périphériques → Ports).

## Le menu

Il s'affiche **au démarrage** de l'ESP32 et à chaque fois qu'on tape `?` (ou `h`) puis Entrée :

```
===== VU-metre v1.34 =====
Dalle : 128x64   Mode : Auto   Luminosite : 3/5
Demo  : arretee
-------------------------------
  1 : dalle 64x64
  2 : dalle 2x64x64
  3 : dalle 128x64  <- actuelle
  4 : dalle 2x128x64
  d : demo marche / arret
  m : mode d'affichage suivant
  l : luminosite suivante
  ? : afficher ce menu
Tapez une touche puis Entree.
```

Si le moniteur a été ouvert après le démarrage, le menu n'est pas visible : taper `?` puis Entrée.

## Les commandes

| Touche | Effet | Mémorisé |
|---|---|---|
| `1` | Dalle **64x64** (1 dalle 64x64, écran 64 x 64) | oui, redémarre |
| `2` | Dalle **2x64x64** (2 dalles 64x64 chaînées, écran 128 x 64) | oui, redémarre |
| `3` | Dalle **128x64** (1 dalle 128x64, écran 128 x 64) | oui, redémarre |
| `4` | Dalle **2x128x64** (2 dalles 128x64 chaînées, écran 256 x 64) | oui, redémarre |
| `d` | Démo marche / arrêt : musique interne (batterie, basse, arpège stéréo), sans source audio | oui |
| `m` | Mode d'affichage suivant : Auto → Barres → Spectre → Miroir → Aiguilles → Stéréo → Onde | oui |
| `l` | Luminosité suivante (5 niveaux) | oui |
| `?` ou `h` | Réaffiche le menu et l'état actuel | — |

Les majuscules marchent aussi (`D`, `M`, `L`, `H`). Une touche inconnue affiche
`Touche 'x' inconnue, tapez ? pour le menu.`

## Exemples

Changer de dalle (ici passage en 2 dalles 128x64) :

```
4
Dalle 2x128x64 enregistree, redemarrage...
VU-metre v1.34
Dalle 2x128x64, couleurs 6 bits
===== VU-metre v1.34 =====
Dalle : 2x128x64   Mode : Auto   Luminosite : 3/5
...
```

La ligne `couleurs N bits` indique la profondeur de couleur retenue : elle baisse toute seule
(de 8 à 4 bits) quand la dalle demande beaucoup de mémoire. Si même 4 bits ne suffisent pas,
l'ESP32 repasse en 64x64 au lieu de rester bloqué.

Lancer puis arrêter la démo :

```
d
Demo lancee (memorise)
d
Demo arretee (memorise)
```

L'écran affiche aussi `Demo` ou `Entree audio` pendant 1,5 s.

## Démo et cavalier GPIO18

La démo est en marche si **le cavalier GPIO18 – GND est en place OU si elle a été lancée par le menu**.
Avec le cavalier en place, `d` ne peut donc pas l'arrêter ; le menu le signale :

```
d
Demo arretee (memorise)
Attention : le cavalier GPIO18 garde la demo en marche.
```

La ligne `Demo :` du menu indique d'où vient la démo : `(cavalier GPIO18 en place)` ou `(lancee depuis ce menu)`.

## Premier démarrage

Sans réglage enregistré, le VU-mètre démarre en **64x64**, mode **Auto**, luminosité **3/5**, démo
arrêtée. Sur une autre dalle l'image est décalée ou en morceaux : ouvrir le moniteur série et taper
le chiffre de la bonne dalle.
