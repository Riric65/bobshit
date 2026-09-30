# BobShit

**Soft, human-first interpreted language. Bilingual FR/EN. `.shit` files.**

```shit
# hello.shit
say "salut, c'est BobShit"
name = "Bob"
say "moi c'est", name
```

> Le code humain est du shit, et c'est ok. Le langage est là pour absorber le
> chaos, pas pour te moraliser.

---

## La philosophie

La plupart des langages te jettent une erreur fatale dès qu'une parenthèse
manque. BobShit part du principe inverse : **si l'intention est claire, on
exécute**.

```shit
# tout ce qui suit est un programme valide
x=1+ 2
say "x =" x
if x>2
  say "ok"
end
```

| Ça marche… | …parce que |
| --- | --- |
| `if x>2` sans parenthèses | optionnelles autour des conditions |
| `if x>2 then` sans `then` | `then` / `do` sont optionnels |
| `say "a", 1 2 3` | les virgules sont optionnelles |
| `[1 2 3]`, `{a: 1 b: 2}` | idem dans les collections |
| `say "unfinished` | la chaîne est fermée en fin de ligne |
| un `end` oublié | le bloc se ferme tout seul |
| un `]` oublié | fermeture implicite, une seule erreur |
| `10 / 0` | vaut `0`, pas de drame NaN |
| `variable_inexistante` | vaut `nil` |
| `42()` | vaut `nil` |
| `[1][99]` | vaut `nil` |
| `jUNK ? \` ~ ^` | caractères parasites ignorés |

Le mode soft est lisible : chaque pardon est accompagné d'un **warning** sur
stderr, jamais d'un silence. Et quand tu veux que les fautes comptent :

```sh
bobshit -s programme.shit     # strict: la première erreur arrête tout, exit 1
```

## Bilingue, vraiment

Chaque mot-clé anglais a un alias français, insensibles à la casse, et les
deux styles se mélangent dans le même fichier :

```shit
si x > 2 alors
  affiche "ok"
fin

pour i dans 1 jusqua 10 pas 2 faire
  affiche i
fin

tant que i > 0
  i = i - 1
fin

fonction double n
  retour n * 2
fin

essaie
  z = 5
  z[0] = 1
attrape e
  affiche "capturé:", e
fin
```

```
if/si   elif/elsif/sinon_si   else/sinon    then/alors    do/faire
end/fin/done                 while/tantque for/pour     in/dans
to/jusqua   step/pas         break/casse   continue/suivant
try/essaie   catch/attrape   fun/fn/def/func/fonction     return/retour/renvoie
say/print/echo/affiche/log   and/et        or/ou         not/non
true/vrai   false/faux       nil/null/none/rien
```

## Compiler

`bobshit 0.1.0`, une seule dépendance : un compilateur C11 et `libm`.

```sh
git clone https://github.com/Riric65/bobshit.git
cd bobshit/lang
make
./bobshit examples/hello.shit
```

Rien d'autre à installer. Le binaire n'a besoin de rien d'autre que de la libc.

## Utiliser

```sh
bobshit programme.shit             # exécuter
bobshit run programme.shit         # idem, en forme explicite
bobshit -e 'say "hi", 1+1'        # un liner
bobshit -i                         # REPL (les blocs multi-lignes sont gérés)
bobshit -s programme.shit          # strict
bobshit --hold programme.shit      # exécuter puis attendre Entrée
bobshit --ast programme.shit       # dumper l'AST
bobshit --tokens programme.shit    # dumper les tokens
bobshit --version
bobshit --help
```

Un fichier `.shit` peut être rendu exécutable avec un shebang :

```shit
#!/usr/bin/env bobshit
say "./fichier.shit"
```

## Le langage

```shit
# types
say type(42), type("a"), type(true), type(nil), type([1]), type({a: 1})
# num str bool nil list map

# listes 0-indexées, index négatif depuis la fin
xs = [10, 20, 30]
xs[0] = 99
xs[3] = 40            # soft: la liste grandit
push(xs, 50)
say pop(xs), xs[-1], len(xs)

# maps: ordre d'insertion conservé, dot access = sugar
user = {name: "Xem", score: 42, tags: ["a", "b"]}
say user.name, user["score"], user.tags[1]
user.level = 3
say keys(user), values(user), has(user, "name")

# fonctions, closures, lambdas
fun adder n
  fun(x) return x + n end
end
add5 = adder(5)
say add5(1)                       # 6

# sans return explicite, la dernière expression est renvoyée
sort([1, -2, 3], fun(a, b) b - a end)     # [3, 1, -2]

# récursion
fun fib n
  if n < 2 then return n end
  return fib(n - 1) + fib(n - 2)
end
say fib(15)                       # 610

# boucles
for i in 1 to 5 do say i end
for i in 0 to 10 step 2 do say i end
for x in [1, 2, 3] do say x * x end
for c in "bob" do say c end
for k, v in {a: 1, b: 2} do say k, "=", v end
for i, x in ["a", "b"] do say i, x end

# affectation composée, sur les trois cibles
x = 1; x += 10
xs[0] *= 5
user.score -= 2

# 40 builtins
say range(1, 5), sum(range(1, 100)), abs(-3), floor(3.7), round(3.456, 2)
say join(split("a,b,c", ","), "-"), upper("x"), replace("aaa", "a", "b")
```

La référence complète : **[lang/docs/LANGUAGE.md](lang/docs/LANGUAGE.md)** —
10 sections, la grammaire, la table des builtins, le tableau de ce que le
mode soft pardonne, et comment le runtime est construit.

## Architecture du runtime

```
lang/
  include/     common.h  lexer.h  ast.h  parser.h  value.h  eval.h
  src/         common.c  lexer.c  ast.c  parser.c  value.c  eval.c  main.c
  examples/    8 programmes de démonstration
  tests/       10 tests de régression + un fuzzer
  docs/        la référence du langage
  Makefile
```

```
source .shit
   │
   ▼
 lex()           → TokenStream*     lexer.c
   │
   ▼
 parse_program() → Node* (AST)      parser.c + ast.c
   │
   ▼
 eval_run()      → stdout           eval.c + value.c
```

Tree-walk, pas de bytecode : environ 3 900 lignes de C11, **aucune dépendance
hors `libm`**.

| Propriété | Valeur |
| --- | --- |
| Profondeur d'appels | 256 frames |
| Itérations par boucle | 1 000 000 |
| Garbage collector | aucun (documenté, assumé en v0.1) |
| Dépendances | `libm` |

## Développement

```sh
cd lang
make                     # compile
make test                # joue les 8 exemples
./tests/run.sh -v        # suite de régression, avec diff
./tests/run.sh --update  # régénère les .expected après un changement voulu
python3 tests/fuzz.py 42 300   # 300 programmes corrompus, aucun crash attendu
```

La CI tourne sur Linux (gcc, clang), macOS (x64 et arm64) et compile pour
Windows avec mingw-w64, plus une passe ASan/UBSan et un fuzzer.

### Modifier le langage

La suite de régression est la référence : `./tests/run.sh` dit si un
comportement a changé. Les fichiers `tests/*.expected` contiennent la sortie
attendue, stderr comprise — un changement volunteers se valide avec
`./tests/run.sh --update`, à relire dans le diff.

Les exemples de `lang/examples/` servent de documentation exécutable : s'ils
ne s'exécutent plus, c'est que quelque chose a bougé.

## Licence

**AGPL-3.0** — voir [LICENSE](LICENSE).

Si tu veux utiliser BobShit dans un programme, le modifier ou l'intégrer à un
service réseau, l'AGPL t'oblige à republier tes modifications sous AGPL.
C'est volontaire : c'est ce qui empêche quelqu'un de reprendre le langage,
de le modifier et de le refermer.

## Contributing

Les issues et PR sont ouvertes. Si tu touches au runtime, `lang/tests/run.sh`
doit rester vert.

## Remerciements

- à tous ceux qui ont signalé un bug, demandé un mot-clé en français, ou
  envoyé un exemple de `.shit` qui casse l'interpréteur — c'est comme ça que
  le mode soft s'améliore

## Pourquoi « BobShit » ?

Parce que le code humain est du shit, et c'est ok.
