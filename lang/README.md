# BobShit — runtime C de référence

Interpréteur du langage **BobShit**, en C11, sans dépendance hors `libm`.
Les sources du langage portent l'extension `.shit`.

```
lang/
  include/     common.h  lexer.h  ast.h  parser.h  value.h  eval.h
  src/         common.c  lexer.c  ast.c  parser.c  value.c  eval.c  main.c
  examples/    *.shit      programmes de démonstration
  tests/       *.shit + *.expected + run.sh   suite de régression
  Makefile
```

## Compiler

`bobshit 0.1.1`, une seule dépendance : un compilateur C11 et `libm`.

```sh
cd lang
make                # produit ./bobshit
make run            # hello world
make test           # joue tous les exemples
./tests/run.sh -v   # suite de régression, avec diff
```

Dépendances : un compilateur C11 (`gcc` ou `clang`) et `libm`.

## Utiliser

```sh
./bobshit examples/hello.shit     # exécuter un fichier
./bobshit run examples/hello.shit # idem, en forme explicite
./bobshit -e 'say "hi", 1+1'      # exécuter un snippet
./bobshit -i                      # REPL
./bobshit -s examples/algo.shit   # mode strict: plus aucune indulgence
./bobshit --hold f.shit           # exécuter puis attendre Entrée (double-clic Windows)
./bobshit --ast examples/hello.shit     # dump de l'AST
./bobshit --tokens examples/hello.shit  # dump des tokens
./bobshit --help
```

Un fichier `.shit` peut être rendu exécutable avec un shebang :

```shit
#!/usr/bin/env bobshit
say "lance-moi avec ./fichier.shit"
```

## Pipeline

```
source .shit
   │
   ▼
 lexer()        → TokenStream*      (src/lexer.c)
   │
   ▼
 parse_program()→ Node* (N_BLOCK)   (src/parser.c, src/ast.c)
   │
   ▼
 eval_run()     → stdout            (src/eval.c)
```

Pas de bytecode : un tree-walk simple, facile à instrumenter et à porter
(comme le port JS du playground).

## Le mode soft

Le runtime est **soft par défaut** : l'intention prime, la syntaxe se fait
discrète. Concrètement :

| Cas | Mode soft | Mode strict (`-s`) |
| --- | --- | --- |
| Parenthèses autour d'une condition | optionnelles | acceptées aussi |
| `then` / `do` | optionnels | acceptés aussi |
| Virgules dans `say`, listes, maps | optionnelles | acceptées aussi |
| `end` manquant | le bloc se ferme tout seul | erreur |
| `]` `}` `)` manquant | fermeture implicite + un warning | arrêt |
| Chaîne non fermée | fermée en fin de ligne | erreur |
| Caractère parasite (`?` `` ` `` `~`) | ignoré + warning | erreur |
| Identifiant inconnu | `nil` + warning | erreur |
| Division par zéro | `0` + warning | erreur |
| Index hors bornes | `nil` | erreur |
| Appel d'un non-fonction | `nil` + warning | erreur |

En mode strict, le premier diagnostic interrompt le programme avec un code
de sortie `1`. En mode soft, le programme continue et compte ses warnings
(la CLI les résume à la fin).

## Limites

- Profondeur d'appels : 256 frames (`BS_MAX_FRAMES`).
- Budget de boucle : 1 000 000 itérations (`BS_MAX_ITERATIONS`).
- Pas de GC : les valeurs vivantes ne sont pas libérées avant la fin du
  processus (une boucle arithmétique coûte ~150 octets par itération).
- Pas de modules, pas de threads, pas d'E/S fichier (à part `input`).

## Le langage en 30 secondes

```shit
# hello.shit
say "salut, c'est BobShit"
name = "Bob"
say "moi c'est", name

# soft: ni `end` ni parenthèses obligatoires
x = 1 + 2
if x > 2
  say "ok"
end

# fonctions
fun add(a, b)
  return a + b
end
say add(2, 3)

# listes (indexées à partir de 0) et maps
xs = [10, 20, 30]
push(xs, 40)
say pop(xs), xs[-1]

user = {name: "Xem", score: 42}
say user.name, user["score"]
user.level = 3
say keys(user)

# boucles
for i in 1 to 5
  say i
end
for i in 0 to 10 step 2
  say i
end
for c in "bob"
  say c
end

# la même chose en français
si x > 2 alors
  affiche "ça marche"
fin
```

La référence complète est dans [`docs/LANGUAGE.md`](docs/LANGUAGE.md).
