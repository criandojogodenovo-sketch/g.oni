# G.oni Script — Referência da Linguagem

Linguagem de script própria do G.oni Llumni, inspirada em GDScript.
Interpretada (árvore de sintaxe percorrida por um interpretador assíncrono),
com **corrotinas nativas via `await`**.

## Sintaxe básica

```python
# comentário
var velocidade = 45.0          # variável (tipagem dinâmica)
const MAX_PULOS = 3            # constante
signal pulou                   # declaração de sinal

func on_ready():               # função (blocos por indentação, 4 espaços)
    print("olá " + self.name)

func on_process(delta):
    if velocidade > 10:
        self.rotate_y(velocidade * delta)
    elif velocidade > 0:
        velocidade -= 1
    else:
        pass
```

## Eventos nativos (métodos chamados pela engine)

| Método | Quando | Argumentos |
|---|---|---|
| `on_ready()` | início do play / spawn | — |
| `on_process(delta)` | todo quadro | `delta` (segundos) |
| `on_collision_enter(other, info)` | novo contato | `other` (objeto), `info` (ponto/normal) |
| `on_collision_exit(other)` | fim do contato | `other` |
| `on_input(kind, x, y)` | toque na tela | `kind` ("down"), posição px |
| `on_destroy()` | antes de destruir | — |

> `on_ready`, `on_process` e declarações `signal` são chamadas diretas;
> os demais são conectados automaticamente como sinais.

## Corrotinas

```python
func on_ready():
    print("esperando...")
    await wait(1.5)              # espera 1,5s de tempo de jogo
    await signal("pulou")        # aguarda o próximo disparo do sinal
    print("continuou!")
```

`await` suspende a função sem travar a engine. `on_process` reentrante é
protegido (uma invocação pendente pula a próxima).

## Acesso a objetos da cena

```python
$Player                        # objeto por caminho (relativo a self)
$"/Câmera"                     # caminho absoluto desde a raiz
get_node("Player/Corpo")       # idem, por função
self.position.x = 2.0          # escreve direto no transform (referência viva)
self.position = vec3(1, 2, 3)  # substitui posição
self.rotation                  # Euler em RADIANOS (use set_rotation_deg)
```

Métodos de objeto:

| Chamada | Efeito |
|---|---|
| `self.translate(x, y, z)` | desloca |
| `self.set_position(x, y, z)` | define posição |
| `self.set_rotation_deg(x, y, z)` | rotação em graus |
| `self.rotate_y(graus)` / `rotate_x` / `rotate_z` | gira |
| `self.set_scale(x, y, z)` | escala |
| `self.look_at(x, y, z)` | aponta para alvo |
| `self.distance_to(other)` | distância de mundo |
| `self.apply_impulse(x, y, z)` | impulso físico |
| `self.apply_force(x, y, z)` | força contínua |
| `self.velocity` | velocidade (corpo rígido) |
| `self.get_controller()` | CharacterController (cápsula) |
| `self.play_animation("nome")` | toca clipe |
| `self.destroy()` | destrói (diferido) |

CharacterController:

```python
func on_process(delta):
    var cc = self.get_controller()
    cc.move(input_axis("move_x"), input_axis("move_y"), delta, input_pressed("jump"))
```

## Sinais

```python
signal dano

func on_ready():
    connect("dano", quando_dano)      # conecta função do próprio script

func quando_dano(qtd):
    print("levou " + str(qtd))

func on_collision_enter(other, info):
    emit("dano", 10)                  # dispara
```

## Classes

```python
class Inimigo:
    var hp = 100
    func ferir(d):
        self.hp -= d
        return self.hp

class Chefe extends Inimigo:
    var hp = 500                      # sobrescreve campo

func on_ready():
    var e = Inimigo.new()
    e.ferir(10)
    print(e.hp)                       # 90
```

## Coleções e controle

```python
var lista = [1, 2, 3]
lista.push(4)
var d = {"nome": "oni", "hp": 10}

for i in range(10):
    if i in lista:
        break

var n = 0
while n < 5:
    n += 1
```

## Funções globais

**Console:** `print(...)` · `str(v)` · `num(s)` · `int(v)` · `len(v)` · `range(a, b, c)`

**Matemática:** `abs floor ceil round sqrt sin cos tan atan2 pow min max clamp lerp sign rand rand_range` · `vec3(x, y, z)`

**Tempo/async:** `time()` · `delta` · `wait(segundos)` [await] · `signal(nome)` [await] · `wait_signal(nome, obj?)` [await]

**Sinais:** `emit(nome, ...)` · `connect(nome, callback, obj?)` · `disconnect(nome, cb)`

**Cena:** `get_node(caminho)` · `find_by_name(nome)` · `find_by_tag(tag)` → lista · `spawn(prefab, x?, y?, z?)` · `destroy(obj)` · `queue_free(obj)` · `release(obj)` (devolve ao pool)

**Física:** `raycast(ox, oy, oz, dx, dy, dz, dist?)` → dicionário `{object, point, normal, distance}` ou null

**Entrada:** `input_pressed("jump")` · `input_axis("move_x")` · `touch_x() touch_y() touch_dx() touch_dy()`

**Animação:** `play_animation(objeto, "nome")`

## Operadores

`+ - * / %` (números, strings, listas, `vec3` com escalar) · `== != < > <= >=` · `and or not` · `in` (lista/dict/string) · atribuições `= += -= *= /=`

Comentários: `#`. Blocos por **indentação** (tab = 4 espaços). Continuação de linha com `\`.
