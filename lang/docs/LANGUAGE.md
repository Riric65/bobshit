# BobShit — référence du langage

Version `0.1.0`. Cette page décrit exactement ce que fait le runtime C de
`lang/`. Les exemples sont exécutables tels quels :

```sh
./bobshit -e 'say "hello"'
./bobshit examples/fizzbuzz.shit
```

---

## 1. Introduction

### 1.1 Vue d'ensemble

BobShit est un langage **interprété**, pensé *human-first* : la syntaxe se
fait discrète, les keywords existent en français et en anglais, et le mode
soft pardonne la plupart des fautes de frappe. Les sources portent
l'extension `.shit`.

```shit
# hello.shit
say "salut, c'est BobShit"
name = "Bob"
say "moi c'est", name
```

### 1.2 Philosophie : le mode soft

La plupart des langages te jettent une erreur fatale dès qu'une parenthèse
manque. BobShit part du principe inverse : *si l'intention est claire, on
exécute*.

```shit
# tout ce qui suit est valide
x=1+ 2
say "x =" x
if x>2
  say "ok"
end
```

Ce que le mode soft pardonne :

- parenthèses optionnelles autour des conditions `if` / `while`
- `then` / `do` / `alors` / `faire` optionnels
- virgules optionnelles dans `say a b`, les listes et les maps
- chaîne non fermée → fermée implicitement en fin de ligne
- `end` manquant → le bloc se ferme à la fin du bloc englobant
- `]` `}` `)` manquant → fermeture implicite, une seule erreur
- identifiant inconnu → `nil`
- division par zéro → `0`
- caractères junk (`?` `` ` `` `~` `^`) → ignorés avec un warning
- une ligne qui commence par `-` est une nouvelle instruction, pas la
  suite de la précédente

En mode strict (`bobshit -s`), tout ce qui précède devient une erreur et
le premier diagnostic arrête le programme avec le code de sortie `1`.

### 1.3 Installer

Une seule dépendance : un compilateur C11 et `libm`.

```sh
cd lang
make
./bobshit examples/hello.shit
```

Ou, pour l'installer dans `~/.local/bin` :

```sh
make -C lang install PREFIX="$HOME/.local"
```

### 1.4 CLI

```
bobshit <file.shit>          exécuter un fichier
bobshit run <file.shit>     idem, en forme explicite
bobshit -e <code>            exécuter un snippet
bobshit -i                   REPL
bobshit -s <file.shit>       mode strict
bobshit --hold <file.shit>   exécuter puis attendre Entrée
bobshit --ast <file.shit>    dumper l'AST
bobshit --tokens <file.shit> dumper les tokens
bobshit --version / --help
```

Un fichier peut être rendu exécutable avec un shebang :

```shit
#!/usr/bin/env bobshit
say "./fichier.shit"
```

### 1.5 Hello world

```shit
# hello.shit
say "salut, c'est BobShit"
name = "Bob"
say "moi c'est", name
```

---

## 2. Lexique

### 2.1 Commentaires

Trois styles, interchangeables :

```shit
# une ligne
// une ligne
/* un bloc,
   sur plusieurs lignes */
say "ok"   # commentaire de fin de ligne
```

### 2.2 Newlines

Les newlines sont des **séparateurs doux** : ni `;` ni fin de ligne ne sont
obligatoires. Le lexer décide si une ligne continue la précédente :

| Situation | Nouvelle instruction |
| --- | --- |
| ligne précédente terminée par un opérateur (`+ - * / % = , ( [ {` …) | non, ça continue |
| ligne suivante commençant par un opérateur (`+ * = < > ! & \| % . :`) | non, ça continue |
| ligne suivante commençant par `-` | **oui** (c'est un signe moins unaire) |
| accolades / crochets / parenthèses ouverts | non, ça continue |
| ligne suivante commençant par un mot-clé de début d'instruction | oui, même à l'intérieur d'un crochet oublié |
| ligne suivante ressemblant à `nom = ...` | oui, même à l'intérieur d'un crochet oublié |

```shit
total = 1 +
        2 +
        3          # total = 6

xs = [1,
      2,
      3]          # [1, 2, 3]

y = 10
- 5              # deux instructions : y vaut 10, puis -5 est ignoré
```

### 2.3 Nombres

```shit
say 42          # 42
say 3.14        # 3.14
say .5          # 0.5
say 1e3         # 1000
say 1_000       # 1000
say 0x1f        # 31
say 0b1011      # 11
say 0o17        # 15
```

Les nombres sont des flottants double. Un nombre entier s'affiche sans
décimale.

### 2.4 Chaînes

Guillemets simples ou doubles, mêmes règles. Échappements : `\n \t \r \0
\a \b \f \v \e \\ \" \'`. Un `\"` n'est pas un caractère, c'est un `"`.

```shit
s = "ligne1\nligne2"
say s
say 'guillemets simples'
say "guillemet échappé: \""
say "une chaîne \
     sur deux lignes"     # le backslash en fin de ligne continue
```

En soft, une chaîne non fermée se ferme en fin de ligne (avec un warning).

### 2.5 Mots réservés

Les keywords sont **insensibles à la casse** (`IF`, `If`, `if` sont la même
chose) et ne peuvent pas servir de noms de variables. Réservés :
`if si elif elsif sinon_si else sinon then alors do faire end fin done endif
finon while tantque for pour in dans to jusqua step pas break casse stop
continue suivant try essaie catch attrape fun fn def func function fonction
return retour renvoie say print echo affiche log and et or ou not non true
vrai false faux nil null none rien`.

Tous les autres identifiants sont libres, y compris avec des accents ou des
 caractères UTF-8 (`élan = "chat"` fonctionne). Le lookup des variables est
insensible à la casse : `Foo` et `foo` désignent la même variable. Les clés
de map, elles, sont sensibles à la casse.

---

## 3. Valeurs

### 3.1 Types

| Type | Littéraux | `type()` |
| --- | --- | --- |
| `nil` | `nil` `null` `none` `rien` | `nil` |
| `bool` | `true` `false` `vrai` `faux` | `bool` |
| `num` | `42`, `3.14` | `num` |
| `str` | `"x"` `'x'` | `str` |
| `list` | `[1, 2]` | `list` |
| `map` | `{a: 1}` | `map` |
| `fun` | `fun(a) ... end` | `fun` |
| `native` | `len`, `print`… | `native` |

```shit
say type(42), type("yo"), type([1,2]), type({a:1}), type(nil), type(true)
# num str list map nil bool
```

### 3.2 Truthiness

Falsy : `nil`, `false`, `0`, `""`, `[]`, `{}`. Tout le reste est truthy.

```shit
say !nil, !false, !0, !"", ![], !1, !"x", !{}
# true true true true true false false true
```

### 3.3 Variables

Pas de `let` / `var` : l'assignation crée la variable si elle n'existe pas.

```shit
x = 10
x = x + 1
name := "bob"      # := est accepté aussi
compteur = 0
Foo = 1
say foo            # 1: le lookup ignore la casse
```

Une affectation cherche la variable dans les portées englobantes avant d'en
créer une nouvelle locale : un paramètre qui porte le nom d'un global est
modifié localement, le global n'est pas touché.

### 3.4 Opérateurs

```
+  -  *  /  %      arithmétique
==  !=  <  >  <=  >=   comparaison
and / &&   or / ||   logique (court-circuit)
not / !            unaire
=  :=  +=  -=  *=  /=  %=   affectation
```

Les opérateurs logiques renvoient un **opérande**, pas un booléen :

```shit
say true and false   # false
say 1 or 0           # 1
say nil or "x"       # x
```

Surcharges de `+` :

```shit
say "bob" + "shit"   # bobshit
say 5 + "!"          # 5!
say [1,2] + [3]      # [1, 2, 3]
say [1,2] + 3        # [1, 2, 3]   (copie + push)
say {a:1} + {b:2}    # {a: 1, b: 2} (fusion)
```

Le reste coerce en nombre via une lecture « num-ish » : `num("42")` vaut
`42`, `num("bob")` vaut `0`, `num(nil)` vaut `0`, `num(true)` vaut `1`,
`num([1,2])` vaut `2` (la longueur). En soft, `10 / 0` vaut `0`.

Affectations composées, sur les trois cibles possibles :

```shit
x = 1
x += 10             # 11
xs = [1, 2]
xs[0] *= 5          # [5, 2]
m = {n: 1}
m.n -= 1            # {n: 0}
```

L'affectation accepte des cibles : identifiant, index, champ.

```shit
a = b = 5           # a = 5, b = 5
```

### 3.5 Chaînes comme valeurs

Les chaînes sont indexées **caractère par caractère** (UTF-8), à partir de
0, et les index négatifs comptent depuis la fin. Elles sont immuables :
`index_set` reconstruit une nouvelle chaîne.

```shit
say "héllo"[1]      # é
say "héllo"[-1]     # o
say len("héllo")    # 5  (points de code, pas octets)
```

### 3.6 Listes

```shit
xs = [10, 20, 30]
say xs[0]           # 10
xs[1] = 99
xs[3] = 40          # soft: la liste grandit ; strict: erreur
push(xs, 50)        # mutate et renvoie la liste
say pop(xs)         # 50
say len(xs), xs
say xs[-1]          # dernier élément
```

Les listes sont des références : `push` modifie la liste d'origine, `+`
renvoie une nouvelle liste.

### 3.7 Maps

```shit
user = {name: "Xem", score: 42}
say user.name       # Xem
say user["score"]   # 42
user.level = 3      # crée la clé
say keys(user)      # [name, score, level]
say values(user)    # [Xem, 42, 3]
say has(user, "name"), has(user, "nope")   # true false
say user.missing    # nil
del(user, "level")
say len(user)
```

L'ordre d'insertion est conservé. Les clés sont des noms, des chaînes ou des
nombres. Le point-virgule `obj.key` est du sucre pour `obj["key"]`.

---

## 4. Contrôle de flux

### 4.1 if / elif / else

```shit
n = 7
if n > 10 then
  say "grand"
elif n > 5
  say "moyen"
else
  say "petit"
end
```

`then` est optionnel, les parenthèses aussi, et `end`, `fin`, `done` ou `}`
ferment le bloc. `else if` est un alias de `elif`. Un bloc peut être écrit
entre accolades :

```shit
if n > 5 { say "ok" }
```

Version française :

```shit
si n == 0 alors
  say "zéro"
sinon_si n == 7 alors
  say "sept"
sinon
  say "autre"
fin
```

### 4.2 while

```shit
i = 0
while i < 3
  say i
  i = i + 1
end

# version FR
i = 0
tantque i < 3
  affiche i
  i = i + 1
fin
```

### 4.3 for : trois formes

```shit
# intervalle : for <var> in <début> to <fin> [step <pas>]
for i in 1 to 5
  say i             # 1 2 3 4 5
end
for i in 0 to 10 step 2
  say i             # 0 2 4 6 8 10
end
for i in 5 to 1 step -1
  say i             # 5 4 3 2 1
end
for i in 1 to 2 step 0.5
  say i             # 1 1.5 2
end

# parcours : for <var> in <itérable>
for x in [1, 2, 3]
  say x * x
end
for c in "bob"
  say c             # b o b
end
for k in {a: 1, b: 2}
  say k             # a b
end

# deux variables : (index, valeur) pour une liste, (clé, valeur) pour une map
for i, x in ["a", "b"]
  say i, x
end
for k, v in {a: 1}
  say k, v
end
```

`step 0` est une erreur (boucle infinie). Itérer sur `nil` est un warning en
soft, une erreur en strict.

### 4.4 break / continue

```shit
for i in 1 to 100
  if i == 5 then break end
  if i % 2 == 0 then continue end
  say i
end
```

Alias : `casse` / `stop` pour `break`, `suivant` pour `continue`. Un
`break` hors boucle est ignoré avec un warning.

### 4.5 try / catch

```shit
try
  xs = 5
  xs[0] = 1          # erreur : on n'écrit pas dans un nombre
catch err
  say "caught:", err
end
```

`err` reçoit le message sous forme de chaîne. Sans clause `catch`, le bloc
`try` ne fait rien de spécial. Un `return` dans un `try` n'est pas
intercepté par le `catch`.

---

## 5. Fonctions

### 5.1 Déclaration

```shit
fun add(a, b)
  return a + b
end

fun add2 a b        # parenthèses optionnelles
  return a + b
end

fn mul a b
  return a * b
end

def dec a b
  return a - b
end

fonction greet name
  say "yo", name
fin

func shout(txt)
  return upper(txt) + "!"
end
```

Sans `return` explicite, une fonction renvoie la valeur de sa **dernière
expression** (comme en Ruby). C'est ce qui rend les lambdas et les
comparateurs courts :

```shit
say sort([1, -2, 3], fun(a, b) b - a end)   # [3, 1, -2]
```

Une lambda anonyme n'a pas de nom : `fun(a, b) ... end` est une valeur.

### 5.2 Appel

```shit
say add(2, 3)
add(1, 2, 3, 4)     # les arguments en trop sont ignorés
say add(5)          # le second paramètre vaut nil -> 5
```

### 5.3 Portées et closures

Seul un appel de fonction crée une portée. `if`, `while`, `for` et `try`
partagent la portée courante, ce qui rend les scripts prévisibles :

```shit
if true
  y = 1
end
say y               # 1
```

Une fonction capture l'environnement où elle a été créée :

```shit
fun adder n
  fun(x) return x + n end
end
add5 = adder(5)
say add5(1)        # 6

fun counter()
  c = 0
  fun()
    c = c + 1
    return c
  end
end
tick = counter()
say tick(), tick(), tick()    # 1 2 3
```

### 5.4 Récursivité

```shit
fun factorial n
  if n <= 1 then return 1 end
  return n * factorial(n - 1)
end
say factorial(6)   # 720
```

La pile d'appels est plafonnée à 256 frames ; la dépassement est une
erreur catchable.

---

## 6. Entrées / sorties

```shit
say "hello", 1, true
print "alias de say"
echo "encore un alias"
affiche "alias FR"
log "alias moderne"
```

`say` accepte des virgules ou pas, et une virgule finale supprime l'espace
de fin :

```shit
say "a", "b"
say "a" "b"
say "sans espace",
```

`input` lit une ligne sur stdin :

```shit
name = input("ton nom? ")
say "salut", name
```

---

## 7. Builtins

### 7.1 Table

| Nom | Signature | Notes |
| --- | --- | --- |
| `len` | `len(x)` | longueur d'une chaîne (caractères), d'une liste, d'une map |
| `type` | `type(x)` | nom du type : `"num"`, `"str"`… |
| `str` | `str(x)` | texte de la valeur (une chaîne reste sans guillemets) |
| `num` | `num(x)` | coercion numérique, `0` si illisible |
| `int` | `int(x)` | idem, tranché vers zéro |
| `bool` | `bool(x)` | truthiness → `true` / `false` |
| `push` | `push(l, v)` | ajoute à la fin, renvoie la liste |
| `pop` | `pop(l)` | retire et renvoie le dernier élément |
| `insert` | `insert(l, i, v)` | insère à l'index `i` ; négatif compte depuis la fin (`insert(l, -1, v)` insère avant le dernier) |
| `remove` | `remove(l, i)` | retire l'index `i` et renvoie l'élément |
| `reverse` | `reverse(l)` | retourne sur place, renvoie la liste |
| `sort` | `sort(l)` ou `sort(l, fn)` | trie sur place, comparaison optionnelle |
| `index` | `index(x, v)` | position d'une valeur dans une liste, d'un sous-texte dans une chaîne ; `-1` sinon |
| `count` | `count(l, v)` | nombre d'occurrences |
| `keys` | `keys(m)` | liste des clés, dans l'ordre d'insertion |
| `values` | `values(m)` | liste des valeurs |
| `has` | `has(x, k)` | clé présente dans une map, valeur présente dans une liste |
| `del` | `del(m, k)` | supprime une clé, renvoie `true` / `false` |
| `range` | `range(a, b)` ou `range(a, b, step)` | liste **inclusive**, step négatif si `b < a` |
| `join` | `join(l, sep)` | concatène avec un séparateur |
| `split` | `split(s, sep)` | découpe ; `sep == ""` découpe en caractères |
| `upper` / `lower` | `upper(s)` | casse |
| `trim` | `trim(s)` | espaces en début / fin |
| `replace` | `replace(s, a, b)` | remplace toutes les occurrences |
| `starts` / `ends` | `starts(s, p)` | début / fin |
| `abs` | `abs(x)` | valeur absolue |
| `floor` / `ceil` | `floor(x)` | arrondi |
| `round` | `round(x)` ou `round(x, n)` | arrondi à `n` décimales |
| `sqrt` | `sqrt(x)` | racine carrée (négatif → 0) |
| `pow` | `pow(a, b)` | puissance |
| `min` / `max` | `min(a, b, …)` | variadique |
| `sum` | `sum(l)` | somme d'une liste |
| `input` | `input(prompt?)` | lit une ligne sur stdin |

### 7.2 Exemples

```shit
say range(1, 5)                 # [1, 2, 3, 4, 5]
say range(1, 10, 3)             # [1, 4, 7, 10]
say join(split("a,b,c", ","), "-")   # a-b-c
say sort(["pear", "fig"])       # [fig, pear]
say sum(range(1, 100))          # 5050
say abs(-3), floor(3.7), ceil(3.2), round(3.456, 2)   # 3 3 4 3.46
say min(3, 1, 2), max(3, 1, 2)  # 1 3
```

---

## 8. Runtime

### 8.1 Architecture

```
lang/
  include/     common.h  lexer.h  ast.h  parser.h  value.h  eval.h
  src/         common.c  lexer.c  ast.c  parser.c  value.c  eval.c  main.c
  examples/    *.shit
  tests/       *.shit + *.expected + run.sh
  Makefile
```

| Fichier | Rôle |
| --- | --- |
| `common.c` | mémoire, tampons, diagnostics, `longjmp` de `try/catch` |
| `lexer.c` | scanner, keywords FR/EN, commentaires, heuristiques de newlines |
| `ast.c` | constructors de nœuds, dumper (`--ast`), destructeur |
| `parser.c` | recursive descent, récupération d'erreurs |
| `value.c` | union taguée, listes, maps, chaîne d'environnements |
| `eval.c` | tree-walk, signaux de contrôle, builtins natifs |
| `main.c` | CLI : fichier, `-e`, REPL, `--ast`, `--tokens` |

### 8.2 Pipeline

```
source .shit
   │
   ▼
 lex()           → TokenStream*
   │
   ▼
 parse_program() → Node* (N_BLOCK)
   │
   ▼
 eval_run()      → stdout
```

### 8.3 Erreurs et récupération

| Situation | Message | Comportement soft |
| --- | --- | --- |
| bracket non fermé | `bobshit: warning: line N: missing ']'` | le bracket est fermé, l'exécution continue |
| `end` manquant | `missing 'end' at end of file` | le bloc se ferme |
| `end` en trop | `unexpected 'end' outside of a block` | ignoré |
| variable inconnue | `unknown variable 'x', treated as nil` | vaut `nil` |
| division par zéro | `division by zero, result is 0` | vaut `0` |
| index hors bornes | `list index out of range (size N)` / `string index out of range` | vaut `nil` |
| appel impossible | `'5' is a num, not a function, call ignored` | vaut `nil` |
| chaîne non fermée | `unterminated string, closed implicitly` | fermée en fin de ligne |
| caractère parasite | `ignored stray character '?'` | ignoré |
| boucle infinie | `loop budget exhausted` | `try/catch` |
| récursion infinie | `stack overflow: more than 256 nested calls` | `try/catch` |
| écriture invalide | `cannot assign into a num value` | `try/catch` |

Une erreur runtime non rattrapée s'affiche sur stderr et quitte avec le
code `1` :

```
runtime error: line 4: cannot assign into a num value
```

La CLI résume les warnings soft à la fin de l'exécution :

```
bobshit: 3 warning(s), run with -s to turn them into errors
```

### 8.4 Mots-clés FR / EN

```
if/si          elif/elsif/sinon_si     else/sinon        then/alors
do/faire       end/fin/done/endif      while/tantque
for/pour       in/dans                 to/jusqua         step/pas
break/casse/stop                       continue/suivant
try/essaie     catch/attrape
fun/fn/def/func/function/fonction      return/retour/renvoie
say/print/echo/affiche/log
and/et         or/ou                   not/non
true/vrai      false/faux              nil/null/none/rien
```

### 8.5 Grammaire

```
program  := stmt*
stmt     := say | if | while | for | fun | return | break | continue
          | try | block | assign | expr
assign   := (ident | index | dot) ('=' | ':=' | '+=' | '-=' | '*=' | '/=' | '%=') (assign | expr)
expr     := or
or       := and (('or' | '||') and)*
and      := cmp (('and' | '&&') cmp)*
cmp      := add (('==' | '!=' | '<' | '>' | '<=' | '>=') add)*
add      := mul (('+' | '-') mul)*
mul      := unary (('*' | '/' | '%') unary)*
unary    := ('!' | '-' | '+' | 'not') unary | postfix
postfix  := primary ( '(' args ')' | '[' expr ']' | '.' ident )*
primary  := NUM | STR | BOOL | NIL | IDENT | list | map | fun | '(' expr ')'
list     := '[' (expr (',' expr)*)? ']'
map      := '{' (KEY ':' expr (',' …)*)? '}'
fun      := 'fun' IDENT? params? block
```

### 8.6 Limites

| Limite | Valeur | Constante |
| --- | --- | --- |
| profondeur d'appels | 256 | `BS_MAX_FRAMES` |
| itérations de boucle | 1 000 000 | `BS_MAX_ITERATIONS` |
| récupération après bracket oublié | 32 tokens | `skip_to_closer` |
| profondeur de récursion du parseur | la pile C | — |
| garbage collector | aucun | — |

Pas de GC : la mémoire des valeurs n'est pas libérée avant la fin du
processus. Une boucle de 900 000 itérations consomme ~140 Mo.

---

## 9. Recettes

### 9.1 FizzBuzz

```shit
for i in 1 to 20
  if i % 15 == 0 then say "FizzBuzz"
  elif i % 3 == 0 then say "Fizz"
  elif i % 5 == 0 then say "Buzz"
  else say i
  end
end
```

### 9.2 map / filter / reduce

```shit
fun filter_even xs
  out = []
  for x in xs
    if x % 2 == 0 then push(out, x) end
  end
  return out
end

say filter_even(range(1, 10))     # [2, 4, 6, 8, 10]
```

### 9.3 Configuration

```shit
cfg = {
  host: "bob.xem.yt",
  port: 443,
  soft: true
}
if cfg.soft then say "running soft on", cfg.host end
```

### 9.4 Compteur de mots

```shit
counts = {}
for w in split("le chat dort sur le tapis", " ")
  n = 0
  if has(counts, w) then n = counts[w] end
  counts[w] = n + 1
end
say counts.chat     # 1
```

### 9.5 Validation

```shit
fun check c
  problems = []
  if not has(c, "host") then push(problems, "host manquant") end
  if c.port < 1 or c.port > 65535 then push(problems, "port invalide") end
  if len(problems) == 0 then return "ok"
  return join(problems, " / ")
end
say check({host: "x", port: 443})   # ok
say check({port: 99999})            # port invalide
```

---

## 10. FAQ

**Pourquoi « BobShit » ?** Parce que le code humain est du shit, et c'est
ok. Le langage est là pour absorber le chaos, pas pour te moraliser.

**C'est prod-ready ?** C'est une v0.1 jouable : runtime C, suite de tests,
exemples. Pas de modules, pas de GC, pas de threads. Parfait pour des
scripts et pour s'amuser.

**Pourquoi 0-indexé ?** Comme `xs[0]` pour le premier élément, et
`xs[-1]` pour le dernier : c'est la convention la moins alphaprouvable et la
plus simple à écrire partout.

**Puis-je écrire des programmes sérieux ?** Oui, tant qu'ils tiennent dans
la mémoire et n'utilisent pas plus de 256 niveaux d'appels. Pour le reste,
c'est un langage de script, pas un langage de systèmes.

**Comment Contributions ?** Le runtime est dans `lang/`, la suite de
régression dans `lang/tests/`. `./tests/run.sh --update` régénère les
fichiers `.expected` après une modification volontaire du comportement.
