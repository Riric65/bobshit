# Changelog

Toutes les versions de BobShit. Le format suit [Keep a
Changelog](https://keepachangelog.com/fr/1.1.0/) et le versionnement est
sémantique.

## [0.1.1] — en cours

Le fond de 0.1.1 : un ramasse-miettes, parce que tout le reste en dépend. Une
boucle qui alloue sans fin ne rendait jamais la mémoire, `value.h` le disait
franchement : *everything is freed when the process exits*. C'est très rapide et
ça ne va pas dans un serveur, une boucle de jeu ou un shell.

### Ajouté

- **Ramasse-miettes** (`src/gc.c`) : balayage marque-et-compacte sur un tas de
  emplacements de taille fixe, avec un allocateur à aiguille qui rend
  l'allocation proche du C natif. Les racines sont précises pour l'environnement
  global et les portées de fonction en cours, et conservatrices pour la pile C,
  parce que l'interpréteur garde ses temporaires dans de simples variables
  locales. Balayage déclenché aux points sûrs, c'est-à-dire entre deux
  instructions de bloc, jamais au milieu d'une expression.
- `gc()` collecte et rend le nombre de valeurs libérées, `gc_stats()` rend
  `{live, freed, runs, bytes, enabled}`, `gc_off()` et `gc_on()` pour piloter
  la collecte depuis le code.
- `--gc` et `--no-gc` sur la ligne de commande. **Désactivée par défaut** : voir
  la section Known limitations.
- `tests/12_gc.shit` : 20 000 itérations, une liste, une chaîne et une closure
  qui doivent survivre, et des seuils sur ce qui est réellement repris.
- `CHANGELOG.md`, ce fichier, vérifié par la CI : si la version de
  `common.h` n'apparaît pas dans le changelog, la CI échoue.

### Corrigé

- Une faute pardonnée n'est plus annoncée comme une erreur. Le lexer disait
  `warning` pour ses pardons, le parser disait `parse error` pour les siens
  alors que le programme continuait. En mode soft, une faute absorbée est un
  warning ; `parse error` ne subsiste qu'en mode strict, où l'arrêt est réel.
- Une chaîne sans guillemet fermant n'avale plus le retour chariot d'une fin de
  ligne CRLF. C'est le principe même du mode soft, et un `.shit` enregistré par
  un éditeur Windows se lisait différemment d'un `.shit` en LF.
- La sortie redirigée a les mêmes octets sur toutes les plateformes : en mode
  texte, la CRT de Windows réécrivait chaque `\n` en `\r\n`, ce qui rendait la
  suite de régression, les tubes et tout diff inutilisables. La traduction n'est
  désactivée que si la sortie n'est pas une console, pour qu'une console reste
  lisible. La page de code de la console passe aussi en UTF-8, sinon les
  caractères accentués s'affichent de travers.
- Le ramasse-miettes avorte au lieu de parcourir la pile quand la plage entre le
  cadre courant et la borne n'est pas plausible. Sous AddressSanitizer, les
  cadres vivent dans une pile factice et l'écart mesurait 5 To ; la collecte
  était annulée au lieu delire de la mémoire non mappée.

### Known limitations

- **La collecte automatique est désactivée par défaut.** Elle fonctionne et
  reprend bien les rebuts : 240 000 valeurs libérées d'un coup sur une boucle
  de 20 000 tours, le vivant retombant à 55. Mais un collecteur conservateur
  garde tout ce que la pile machine pointe encore, et les cadres d'une
  itération précédente restent sur la pile. Après une longue boucle, une
  collecte unique peut donc laisser 20 000 valeurs marquées, et les suivantes ne
  les reprennent pas. Le correctif est un pile d'ombres explicite pour les
  temporaires de l'évaluateur, qui remplace le balayage conservateur : c'est un
  vrai chantier, pas un detail. `gc()` fonctionne dès maintenant et
  `gc_stats()` permet de mesurer.
- L'allocateur ne rend pas les pages à l'OS : le pic mémoire reste au maximum
  atteint, il ne redescend pas.
- Les `Env` capturés par une closure ne sont jamais libérés. C'est un fuite
  préexistante, la GC ne s'en occupe pas.

## [0.1.0] — 2026-09-30

Première version publique. Langage interprété soft, bilingue FR/EN, runtime C11
sans dépendance hors libm.

### Ajouté

- Mode soft : parenthèses, `then`, virgules, `end` et guillemets de fin de
  chaîne optionnels. Une faute devient un warning, jamais un arrêt.
- Chaque mot-clé anglais a un alias français, insensibles à la casse, et les
  deux styles se mélangent dans un même fichier.
- 40 fonctions natives, closures, lambdas, comparateurs, deux variables de
  boucle.
- CLI : fichier, `run`, `-e`, REPL, `-s`, `--hold`, `--ast`, `--tokens`.
- Installateurs : `.exe` Windows, `.deb`, `.rpm`, `.pkg` macOS, `PKGBUILD` Arch
  et `.pkg.tar.zst`, plus les binaires Linux glibc, musl statique et aarch64.
- 10 tests de régression, un fuzzer, une passe ASan/UBSan.

[0.1.1]: https://github.com/Riric65/bobshit/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/Riric65/bobshit/releases/tag/v0.1.0
