# Manual del lenguaje BCL-32

**Basic Command Language 32 — kernel v1.2**

Manual de referencia y guía de uso del lenguaje BCL-32 tal como lo implementa el
intérprete del núcleo (`bcl`). Basado en el libro *Manual de BCL-32* y contrastado
con la especificación del kernel y con el propio intérprete: todos los ejemplos de
este documento se han ejecutado y la salida que se muestra es la real (cada bloque
`bcl` va seguido, cuando procede, de un bloque `text` con lo que imprime).

> **Alcance.** Este manual cubre únicamente el **kernel**: el lenguaje y sus
> comandos integrados. Los módulos binarios (`.bll`: sistema Linux, JSON, bases de
> datos, interfaces gráficas, etc.) se distribuyen por separado y no se documentan
> aquí; solo se explica cómo los carga el kernel (sección 11). Por eso ningún
> ejemplo usa comandos de módulo (ficheros, temporizadores, red…).

## Tabla de contenidos

- [1. Introducción](#1-introducción)
- [2. Referencia rápida](#2-referencia-rápida)
- [3. Fundamentos del lenguaje](#3-fundamentos-del-lenguaje)
- [4. El evaluador `CALC`](#4-el-evaluador-calc)
- [5. Variables](#5-variables)
- [6. Entrada y salida](#6-entrada-y-salida)
- [7. Estructuras de control](#7-estructuras-de-control)
- [8. Arrays asociativos](#8-arrays-asociativos)
- [9. Listas](#9-listas)
- [10. Argumentos del programa](#10-argumentos-del-programa)
- [11. Módulos, `SOURCE` y el entorno de arranque](#11-módulos-source-y-el-entorno-de-arranque)
- [12. Procedimientos y ámbito](#12-procedimientos-y-ámbito)
- [13. Cadenas](#13-cadenas)
- [14. Manejo de errores](#14-manejo-de-errores)
- [15. Ejercicios e idiomas](#15-ejercicios-e-idiomas)
- [Apéndice: defectos conocidos del kernel (build 7)](#apéndice-defectos-conocidos-del-kernel-build-7)

## 1. Introducción

### 1.1 ¿Qué es BCL?

**BCL** (*Basic Command Language*) es un lenguaje de programación minimalista e
interpretado, construido sobre la idea de que **todo es una cadena de texto**. Es un
lenguaje de *scripting* orientado a comandos, parecido en espíritu a Tcl, con una
sintaxis directa y predecible.

Características principales:

- Todo valor es una cadena: no hay enteros, decimales ni listas como tipos.
- Sintaxis de comandos con número fijo de argumentos.
- Las palabras clave y los nombres de comando no distinguen mayúsculas.
- Las variables se leen con el prefijo `$`.
- La aritmética y la lógica numérica se delegan en el comando `CALC`.
- Los índices empiezan en **1** (cadenas, listas, vectores).
- Se extiende en tiempo de ejecución con módulos binarios (`LOAD`).

### 1.2 La filosofía «todo es una cadena»

```bcl
SET edad 25
```

La variable `edad` no guarda el número 25: guarda la cadena de dos caracteres `"25"`.
Cuando hace falta operar aritméticamente, `CALC` interpreta la cadena como número y
devuelve el resultado, que también es una cadena. No hay errores de conversión de
tipos y el comportamiento es siempre predecible.

| Concepto          | En otros lenguajes            | En BCL                                  |
|-------------------|-------------------------------|-----------------------------------------|
| Asignar variable  | `x = 10`                      | `SET x 10`                              |
| Suma              | `x = a + b`                   | `SET x [CALC $a + $b]`                  |
| Imprimir          | `print("Hola")`               | `PUTS "Hola"`                           |
| Condicional       | `if (cond) {...}`             | `IF cond THEN ... END`                  |
| Bucle `while`     | `while (cond) {...}`          | `IF cond DO ... LOOP`                   |
| Función           | `def f(a, b): ...`            | `PROC f WITH a b DO ... END`            |
| Manejo de errores | `try: ... except: ...`        | `TRY ... CATCH e ... END`               |

### 1.3 Cómo funciona el intérprete

El código no se compila: un lexer lo divide en tokens, el parser construye un árbol
(AST) y el evaluador lo recorre ejecutando cada comando. El AST solo tiene **dos
estructuras de control**: `IF … THEN` y el bucle `IF … DO` (más `BREAK` y `NEXT`).

### 1.4 Ejecutar programas

```bash
bcl programa.bcl                 # modo script
bcl programa.bcl arg1 arg2       # con argumentos (variable $ARGS)
bcl                              # modo interactivo (REPL)
bcl --version                    # versión y número de build
```

Opciones de la línea de órdenes:

| Opción                | Efecto                                                                 |
|-----------------------|------------------------------------------------------------------------|
| `--version`           | Muestra la versión del kernel y el build, y termina.                   |
| `--no-rc`             | No lee el fichero de inicio `$HOME/.bcl.rc` (equivale a `BCL_NO_RC=1`). |
| `--restricted`        | Modo restringido (equivale a `BCL_RESTRICTED=1`): sin `LOAD`/`UNLOAD`. |
| `--source-root DIR`   | En modo restringido, raíz fuera de la cual `SOURCE` falla (`BCL_SOURCE_ROOT`). |
| `--`                  | Fin de opciones; lo que sigue es el script.                            |

La sintaxis completa es `bcl [--restricted] [--no-rc] [--source-root DIR] [--] [script.bcl [args...]]`.

El REPL muestra un prompt con la versión y se cierra con `exit`, `quit`, `EXIT <código>`
o `Ctrl+D`:

```text
BCL-32 1.2> SET nombre "Ana"
BCL-32 1.2> PUTS "Hola, $nombre"
Hola, Ana
BCL-32 1.2> exit
```

Un error no capturado en un script imprime el diagnóstico por la salida de error
estándar y termina con código de salida **1**.

### 1.5 Tu primer programa

```bcl
# Mi primer programa en BCL-32
PUTS "Hola, Mundo"
```
```text
Hola, Mundo
```

La primera línea es un comentario; `PUTS` imprime su argumento seguido de un salto de
línea. Por convención los ficheros llevan extensión `.bcl`.

### 1.6 Estructura básica de un programa

Un programa es una secuencia de comandos, **uno por línea**: nombre del comando y
argumentos. Los corchetes `[...]` ejecutan el comando interior y usan su resultado
como argumento (sección 3.4).

```bcl
# Formato: COMANDO arg1 arg2 ...
SET x 42                    # asignar variable
PUTS "El valor es: $x"      # imprimir con variable
SET y [CALC $x * 2]         # calcular y guardar el resultado
PUTS "El doble es: $y"
EXIT 0                      # terminar el programa
```
```text
El valor es: 42
El doble es: 84
```

## 2. Referencia rápida

Resumen para consultar de un vistazo; cada punto se explica con detalle en las
secciones siguientes.

### 2.1 Reglas generales

1. Todo valor es una **cadena de texto**. No hay tipos.
2. Toda asignación se hace con `SET`; toda aritmética, con `CALC`.
3. Los **índices empiezan en 1** en cadenas, listas y vectores. El valor `0` significa
   «sin resultado».
4. Falso es `0` y la cadena vacía; **cualquier otro valor es verdadero**.
5. Los nombres de comando son **insensibles a mayúsculas**; los de variable, no.
6. Cada palabra clave de bloque ocupa su propia línea: no existe `IF … THEN … END` en
   una sola línea.
7. Leer una variable inexistente devuelve `""` y **no** da error.
8. Un nombre de variable continúa mientras haya letras, dígitos, `_` o `:`; cualquier
   otro carácter lo termina.
9. Ningún comando modifica sus argumentos: todos devuelven un valor nuevo.

### 2.2 Comandos del kernel

| Comando | Argumentos | Descripción |
|---|---|---|
| **Variables** | | |
| `SET` | `nombre valor…` | Asigna variable o elemento de array. Con varios valores, los une con espacios |
| `INCR` | `nombre [incremento]` | Suma 1 (o el incremento, negativo o decimal) a la variable |
| `INCRV` | `nombre incremento` | Suma el incremento indicado (sin validarlo) |
| `EXISTS` | `nombre` | `1` si la variable o el elemento existe |
| `UNSET` | `nombre` | Elimina variable, elemento o array completo |
| `GLOBAL` | `nombre` | Enlaza **un** nombre con el ámbito global |
| **Arrays** | | |
| `ARREXISTS` | `array` | `1` si el array tiene algún elemento |
| `ARRLENG` | `array` | Número de elementos |
| `ARRKEYS` | `array` | Lista de claves (orden no garantizado) |
| `ARRGET` | `array` | Lista plana `clave valor clave valor…` |
| `ARRSET` | `array lista` | Carga pares desde una lista plana |
| `ARRDEL` | `array clave` | Elimina un elemento (silencioso) |
| `ARRCLEAR` | `array` | Vacía el array |
| **Entrada/salida** | | |
| `PUTS` | `cadena…` | Imprime con salto de línea |
| `PUTSN` | `cadena…` | Imprime sin salto de línea |
| `GETS` | — | Lee una línea de la entrada estándar |
| **Aritmética** | | |
| `CALC` | `expresión` | Evalúa una expresión matemática o lógica (dominio real o complejo) |
| **Cadenas** (base 1, por carácter) | | |
| `STRLENG` | `cad` | Longitud en caracteres |
| `STREQ` | `cad1 cad2` | `1` si son idénticas, `0` si no |
| `STRGET` | `cad pos` | Carácter en `pos`, o `""` |
| `STRRANG` | `cad ini fin` | Subcadena, extremos incluidos |
| `STRAPP` | `cad1 cad2` | Concatenación (exactamente dos argumentos) |
| `STRINS` | `cad texto pos` | Inserta `texto` a partir de `pos` |
| `STRDEL` | `cad pos cant` | Borra `cant` caracteres desde `pos` |
| `STRFIND` | `cad sub` | Lista de todas las posiciones, o `0` |
| `STRFMT` | `"fmt" [val]` | Formatea con **una** conversión |
| `STRUPP` / `STRLOW` | `cad` | Mayúsculas / minúsculas (solo ASCII) |
| `STRREV` | `cad` | Invierte la cadena (UTF-8 correcto) |
| **Listas** (base 1) | | |
| `LSTLENG` | `lista` | Número de elementos |
| `LSTGET` | `lista pos` | Elemento en `pos`, o `""` |
| `LSTRANG` | `lista ini fin` | Sublista, extremos incluidos |
| `LSTAPP` | `lista1 lista2` | Concatena **dos listas** |
| `LSTINS` | `lista1 lista2 pos` | Inserta `lista2` antes de `pos` |
| `LSTDEL` | `lista pos cant` | Elimina `cant` elementos desde `pos` |
| `LSTFIND` | `lista elem` | Lista de todas las posiciones, o `0` |
| `LSTSORT` | `lista` | Ordena: números por valor, luego texto sin distinguir mayúsculas |
| `LSTFMT` | `texto sep` | **Trocea** `texto` por `sep` y devuelve la lista |
| **Control de flujo** | | |
| `IF … THEN … END` | `cond` | Condicional, con `ELSEIF` y `ELSE` opcionales |
| `IF … DO … LOOP` | `cond` | Bucle; el `ELSE` opcional se ejecuta si la condición se agota sin `BREAK` |
| `BREAK` | — | Sale del bucle más cercano |
| `NEXT` | — | Salta a la siguiente iteración |
| **Procedimientos** | | |
| `PROC` | `nom [WITH p… @opc…] DO … END` | Define un procedimiento |
| `RETURN` | `[valor]` | Devuelve un valor y finaliza; sin él se devuelve `""` |
| **Errores** | | |
| `TRY … CATCH v … END` | | Captura errores; `v` recibe el texto y `v_INFO(...)` los campos |
| `ERROR` | `"mensaje"` | Lanza un error capturable |
| **Módulos y scripts** | | |
| `SOURCE` | `"ruta.bcl"` | Ejecuta un script en el ámbito actual |
| `SOURCEDIR` | — | Directorio del fichero que se está evaluando |
| `LOAD` / `UNLOAD` | `"ruta.bll"` | Carga / descarga un módulo binario |
| **Sistema** | | |
| `EXIT` | `código` | Termina el programa con ese código de salida |

Palabras clave del lenguaje: `SET IF THEN ELSEIF ELSE END DO LOOP BREAK NEXT PROC WITH
RETURN GLOBAL TRY CATCH ERROR LOAD UNLOAD SOURCE EXIT INCR INCRV EXISTS UNSET`.

> Los comandos de eventos (`INT`, `TIMER`, `RTIMER`, `RUN`, `CANCELINT`, `CANCELTIMER`,
> `EVTDATA`, `EVTSOURCE`) **no** son del kernel: los registra el módulo de plataforma
> que se cargue con `LOAD`.

### 2.3 Secuencias de escape

Válidas dentro de cadenas entre comillas (y como argumentos sueltos):

| Secuencia | Resultado | Secuencia | Resultado |
|---|---|---|---|
| `\n` | Nueva línea | `\\` | Barra invertida |
| `\t` | Tabulador | `\"` | Comilla doble |
| `\r` | Retorno de carro | `\$` | Símbolo `$` literal |
| `\a` `\b` `\f` `\v` | Aviso, retroceso, salto de página, tab. vertical | `\[` `\]` | Corchetes literales |
| `\xHH` | Carácter en hexadecimal | `\#` | Almohadilla literal |
| `\uHHHH` | Carácter Unicode | `\0dd` | Carácter en octal |

### 2.4 `STRFMT` y `CALC` de un vistazo

`STRFMT`: forma `%[banderas][ancho][.precisión]tipo`, con **exactamente un** especificador
(o ninguno). Tipos: `s d i u f e E g G o x X c %%`. Banderas: `-` (izquierda), `0` (ceros),
`+` (signo), `#` (prefijo de base). Ancho y precisión ≤ 4096; sin `%n`, `*` ni `%ld`.
Detalle en la sección 13.5.

`CALC`: operadores de mayor a menor precedencia: funciones `f(x)`; `-` unario y `!`;
`**` (derecha a izquierda); `* / %`; `+ -`; `<< >>`; `< > <= >=`; `== !=`; `&`; `^`; `|`;
`&&`; `||`. Funciones reales: `abs acos asin atan atan2 bool ceil cos cosh double entier
exp floor fmod hypot int isqrt log log10 max min pow rand round sin sinh sqrt srand tan
tanh wide`. Complejos: `complex real imag conj arg polar norm` (y varias de las
anteriores se extienden a complejos). Bases: `255`, `0xFF`, `0377`, `0b1111`, `2.55e2`.
Detalle en la sección 4.

### 2.5 Diagnósticos del kernel

La cabecera de todo error tiene la forma `?FAC-SEV-CODE, resumen`: `FAC` es el
subsistema, `SEV` la severidad (`I` informativo, `W` advertencia, `F` fatal capturable
con `TRY`/`CATCH`, `U` fatal incondicional) y `CODE` un código estable.

| Diagnóstico | Qué suele significar |
|---|---|
| `?PAR-F-MISSTERM` | Falta un `END` o un `LOOP`; también aparece con la forma de una línea de `IF` |
| `?LEX-F-UNTERMSTR` / `?LEX-F-UNTERMSUB` | Comilla o corchete sin cerrar |
| `?EVAL-F-UNKCMD` | Comando o procedimiento desconocido |
| `?EVAL-F-BADARGS` | Número de argumentos incorrecto |
| `?EVAL-F-DEPTH` | Anidamiento o recursión sin caso base |
| `?EVAL-F-USERERR` | Error lanzado con tu propio `ERROR` |
| `?CALC-F-UNKNOWN` | `CALC` ha recibido algo que no es un número |
| `?CALC-F-DOMAIN` | División o módulo por cero |
| `?CALC-F-SYNTAX` / `?CALC-F-BADVALUE` | Expresión mal formada / operando fuera de dominio |
| `?SET-F-DUPLICATE` | Un nombre se usa a la vez como escalar y como array |
| `?UNSET-F-BADVALUE` | `UNSET` sobre un parámetro activo de un `PROC` |
| `?STRFMT-F-BADARGS` | La plantilla no tiene exactamente un hueco, o sobran valores |
| `?STRFMT-F-UNSUPPORTED` | Conversión o modificador no admitido |
| `?ARRSET-F-BADARGS` | Número impar de elementos en la lista de pares |
| `?LIST-F-SYNTAX` | Llaves desequilibradas en una lista |
| `?SRC-F-NOFILE` | `SOURCE` no encuentra el fichero |
| `?LOAD-F-NOTFOUND` / `?LOAD-F-OPENFAIL` | Módulo no hallado en `BCL_LIBS` / no se puede abrir |
| `?LOAD-F-RESTRICTED` | `LOAD`/`UNLOAD` en modo restringido |

### 2.6 Cadenas, listas y arrays: equivalencias

| Operación | Cadena | Lista | Array |
|---|---|---|---|
| Tamaño | `STRLENG` | `LSTLENG` | `ARRLENG` |
| Obtener un elemento | `STRGET` | `LSTGET` | `$arr(k)` |
| Rango | `STRRANG` | `LSTRANG` | — |
| Buscar | `STRFIND` | `LSTFIND` | `EXISTS` |
| Concatenar | `STRAPP` | `LSTAPP` | `SET arr(k) v` |
| Insertar | `STRINS` | `LSTINS` | — |
| Eliminar | `STRDEL` | `LSTDEL` | `ARRDEL` |
| Vaciar | — | — | `ARRCLEAR` |
| Ordenar | — | `LSTSORT` | — |
| Serializar | — | — | `ARRGET` |
| Deserializar | — | `LSTFMT` (trocea texto) | `ARRSET` |

### 2.7 Variables reservadas

| Variable | Contenido |
|---|---|
| `$ARGS` | Lista canónica con los argumentos de la línea de órdenes. Conserva argumentos vacíos, espacios y llaves. Siempre existe; sin argumentos es la lista vacía. |
| `$BCL_LIBS` | Directorios (separados por comas) donde `LOAD` busca módulos nombrados sin `/`. Valor inicial: la variable de entorno del mismo nombre. |

### 2.8 Plantilla de un programa

```bcl
#!/usr/bin/env bcl
# ============================================================
#  nombre.bcl --- qué hace este programa
#  Uso: nombre.bcl <argumentos>
# ============================================================

# --- 1. Módulos y bibliotecas -------------------------------
# LOAD   modulo.bll
# SOURCE utiles.bcl

# --- 2. Configuración ---------------------------------------
ARRSET config "verboso 0 salida /tmp/salida.txt"

# --- 3. Procedimientos --------------------------------------
PROC uso DO
    PUTS "Uso: nombre.bcl <argumentos>"
END

PROC main DO
    GLOBAL config
    PUTS "Trabajando (verboso=$config(verboso))..."
END

# --- 4. Programa principal ----------------------------------
IF [LSTLENG $ARGS] == 0 THEN
    uso
    EXIT 1
END

main
EXIT 0
```
```text
Trabajando (verboso=0)...
```

## 3. Fundamentos del lenguaje

### 3.1 Comentarios y mayúsculas

Un comentario empieza con `#` (fuera de una cadena entrecomillada) y llega al final de
la línea. Los **comandos y palabras clave** no distinguen mayúsculas; los **nombres de
variable** y las cadenas sí. La convención es escribir los comandos en MAYÚSCULAS.

```bcl
# Comentario de línea completa
puts "Hola"                      # comando en minúsculas: válido
PUTS "El símbolo # no inicia comentario aquí"
SET Nombre "Ana"
SET nombre "Juan"
PUTS $Nombre $nombre             # variables distintas
```
```text
Hola
El símbolo # no inicia comentario aquí
Ana Juan
```

### 3.2 Estructura de una línea de comando

Cada instrucción ocupa **una línea**: `COMANDO arg1 arg2 …`. Cada comando espera un
número exacto de argumentos; si se pasan de menos o de más, el intérprete da error
(`?EVAL-F-BADARGS`). Los argumentos con espacios se encierran entre comillas dobles; las
comillas son opcionales cuando no hay espacios.

```bcl
PUTS "Este es un texto con varios espacios"
SET x "10"          # igual que: SET x 10
```

`SET` y `PUTS`/`PUTSN` admiten varios argumentos y los unen con un espacio; casi todos los
demás comandos exigen un número fijo.

### 3.3 Sustitución de variables `$`

Antes de ejecutar un comando, el intérprete sustituye `$nombre` por el valor de la
variable, tanto en argumentos sueltos como dentro de cadenas con comillas.

```bcl
SET ciudad "Madrid"
PUTS "Bienvenido a $ciudad"
PUTS $ciudad
```
```text
Bienvenido a Madrid
Madrid
```

Si la variable **no existe**, se sustituye por la cadena vacía y no hay error. Por eso un
nombre mal escrito no se detecta solo; usa `EXISTS` cuando importe:

```bcl
PUTS "valor=<$indefinida>"
IF [EXISTS config_ruta] THEN
    PUTS $config_ruta
ELSE
    PUTS "No se ha configurado ninguna ruta"
END
```
```text
valor=<>
No se ha configurado ninguna ruta
```

#### Dónde termina el nombre de una variable

Tras un `$`, el nombre continúa mientras haya **letras, dígitos, `_` y `:`**. Cualquier
otro carácter (espacio, `.`, `,`, `-`, `/`, comillas, `\`…) lo termina.

```bcl
SET total 99
SET dir /tmp/datos
PUTS "El total es $total"
PUTS "El total es $total."
PUTS "Total: $total, gracias"
PUTS "Suma$total"
PUTS "$total-1"
PUTS "$dir/informe.txt"
PUTS "$total\nsiguiente linea"
```
```text
El total es 99
El total es 99.
Total: 99, gracias
Suma99
99-1
/tmp/datos/informe.txt
99
siguiente linea
```

> **Trampa: los dos puntos y el subrayado sí se pegan al nombre.** Los dos puntos se
> reservan para nombres compuestos como `$modulo:contador`:

```bcl
SET a 3
SET b 5
PUTS "$a:$b"                  # busca la variable "a:", que no existe
PUTS [STRAPP $a [STRAPP : $b]]
```
```text
5
3:5
```

No existe la forma `${nombre}`: se imprime literalmente. Para separar un nombre de un
`_` o `:` que le sigue, construye la cadena con `STRAPP`.

### 3.4 Sustitución de comandos `[...]`

Un comando entre corchetes se ejecuta y su **resultado** sustituye a los corchetes.
Pueden anidarse y usarse dentro de cadenas con comillas.

```bcl
SET resultado [CALC 3 + 4]
PUTS "3 + 4 = $resultado"
PUTS [CALC 10 * 10]
PUTS [STRLENG "hola"]
SET x [CALC [STRLENG "hola"] + 1]
PUTS "x vale $x y la longitud de 'mundo' es [STRLENG mundo]"
```
```text
3 + 4 = 7
100
4
x vale 5 y la longitud de 'mundo' es 5
```

El anidamiento está limitado para proteger la pila; al superarse el límite se produce
`?EVAL-F-DEPTH`. Un comando **sin** corchetes descarta su resultado: `STRUPP $s` no
cambia `$s`; hay que escribir `SET s [STRUPP $s]`.

### 3.5 Secuencias de escape

```bcl
PUTS "Primera linea\nSegunda linea"
PUTS "Nombre:\tAna"
PUTS "Ella dijo: \"hola\""
PUTS "El precio es \$5.00"
PUTS "\x41\x42\x43"
PUTS "Corchetes literales: \[x\]"
SET n mundo
PUTS "hola $n\nadios"         # el escape detrás de una variable es un escape
```
```text
Primera linea
Segunda linea
Nombre:	Ana
Ella dijo: "hola"
El precio es $5.00
ABC
Corchetes literales: [x]
hola mundo
adios
```

Un `[` dentro de comillas abre una sustitución de comandos: para escribirlo literalmente
hay que usar `\[` (y `\]`). Esto es importante en mensajes de uso como
`"Uso: prog \[-v\] FICHERO"`.

### 3.6 Resumen

Los programas se ejecutan de arriba abajo; solo `IF`, los bucles, `PROC` y los errores
alteran ese orden. Hay exactamente tres mecanismos de sustitución, y ocurren **antes** de
ejecutar el comando: variable `$nombre`, comando `[COMANDO]` y secuencias de escape.

## 4. El evaluador `CALC`

`CALC expresión` evalúa una expresión matemática o lógica en dominio real o complejo y
devuelve el resultado como cadena. La expresión puede contener espacios y variables
(`$a`). Se usa casi siempre con sustitución de comandos.

```bcl
PUTS [CALC 3 + 5]
PUTS [CALC 15 / 4]
PUTS [CALC 17 % 5]
PUTS [CALC 2 ** 10]
SET a 12
SET b 5
PUTS [CALC $a * $b - 1]
PUTS [CALC 0xFF + 0b1010]       # hex + binario
PUTS [CALC 0777]                # octal
PUTS [CALC 1.5e3]               # científica
PUTS [CALC 5 << 2]
PUTS [CALC 0b1100 & 0b1010]
PUTS [CALC 0b1100 | 0b1010]
PUTS [CALC 0b1100 ^ 0b1010]
```
```text
8
3.75
2
1024
59
265
511
1500
20
8
14
6
```

### 4.1 Operadores y precedencia

| Tipo | Operadores |
|---|---|
| Aritméticos | `+` `-` `*` `/` `%` `**` (potencia) y `-` unario |
| Comparación (resultado `1`/`0`) | `==` `!=` `<` `>` `<=` `>=` |
| Lógicos | `&&` `\|\|` `!` |
| Bit a bit | `&` `\|` `^` `<<` `>>` |

La tabla de precedencia está en la sección 2.4. `**` es asociativo por la
derecha (`2 ** 3 ** 2` vale 512). Los desplazamientos exigen una cuenta no negativa y,
en `<<`, operando izquierdo no negativo (si no, `?CALC-F-BADVALUE`). Ante la duda,
usa paréntesis:

```bcl
PUTS [CALC 2 + 3 * 4]
PUTS [CALC (2 + 3) * 4]
PUTS [CALC 2 ** 3 ** 2]
PUTS [CALC -(3 ** 2)]
PUTS [CALC 5 >= 5]
PUTS [CALC 1 && 0]
PUTS [CALC !0]
```
```text
14
20
512
-9
1
0
1
```

> **Cuidado con `-3 ** 2`.** El `-` unario liga más fuerte que `**`: se evalúa como
> `(-3) ** 2`, que pasa por el dominio complejo y puede dejar un residuo imaginario
> minúsculo (`9+2.20436e-15i`). Escribe `-(3 ** 2)` para obtener `-9`.

### 4.2 Precisión y formato del resultado

`CALC` trabaja en coma flotante de doble precisión, pero **el resultado se escribe con
6 cifras significativas** (formato `%g`). Los enteros de más de 6 cifras se redondean en
la cadena resultante, y ese valor redondeado es el que queda en las variables:

```bcl
PUTS [CALC 1.0 / 3.0]
PUTS [CALC 0.1 + 0.2]
PUTS [CALC 123456 * 3]
PUTS [CALC 1234567 + 0]
PUTS [CALC 2 ** 40]
PUTS [STRFMT "%.4f" [CALC 1.0 / 3.0]]
```
```text
0.333333
0.3
370368
1.23457e+06
1.09951e+12
0.3333
```

Para presentar resultados con la precisión que quieras usa `STRFMT` (sección 13.5);
no uses `CALC` para acumular enteros grandes exactos (identificadores, sumas de
control…): sobre cadenas habría que implementar la aritmética a mano.

### 4.3 Funciones

| Grupo | Funciones |
|---|---|
| Redondeo | `floor` `ceil` `round` `int` `entier` (igual que `int`) `abs` |
| Raíces y potencias | `sqrt` `isqrt` `pow(x,y)` `exp` `log` (natural) `log10` `hypot(x,y)` |
| Trigonometría (radianes) | `sin` `cos` `tan` `asin` `acos` `atan` `atan2(y,x)` `sinh` `cosh` `tanh` |
| Conversión y otras | `double` `wide` `bool` `max(x,y)` `min(x,y)` `fmod(x,y)` |
| Aleatorios | `rand()` entero 0..32767; `rand(hi)` real en [0,hi); `rand(lo,hi)`; `srand(n)` semilla |
| Complejos | `complex(a,b)` `real` `imag` `conj` `arg` `polar(r,t)` `norm` |

```bcl
PUTS [CALC floor(3.7)]
PUTS [CALC round(3.5)]
PUTS [CALC int(-3.9)]
PUTS [CALC isqrt(17)]
PUTS [CALC hypot(3, 4)]
PUTS [CALC max(3, 7)]
PUTS [CALC fmod(5.5, 2)]
PUTS [STRFMT "pi = %.10f" [CALC 4 * atan(1)]]
SET x 27
PUTS [STRFMT "%.1f" [CALC pow($x, 1.0 / 3.0)]]
```
```text
3
4
-3
4
5
7
1.5
pi = 3.1415900000
3.0
```

`rand()` **no** devuelve un decimal entre 0 y 1. Para un índice entre 1 y `n` usa
`int(rand(n)) + 1`. El generador es el de la biblioteca C: no es criptográfico.

### 4.4 Números complejos

El resultado sigue siendo una cadena en formato canónico (`a+bi`, `a-bi`, `bi`, `i`,
`-i`, `a`). El token `i` **sin `$`** es la unidad imaginaria. `==` y `!=` funcionan con
complejos; `<`, `>`, `<=`, `>=` dan error (compara módulos con `abs`).

```bcl
PUTS [CALC sqrt(-4)]
PUTS [CALC i * i]
PUTS [CALC complex(3,4)]
PUTS [CALC "3+4i" + "1-2i"]
PUTS [CALC abs("3+4i")]
PUTS [CALC conj("3+4i")]
PUTS [CALC norm("3+4i")]
PUTS [CALC "3+4i" == complex(3,4)]
PUTS [CALC log(-1)]
TRY
    PUTS [CALC "3+4i" < 5]
CATCH e
    PUTS $e
END
```
```text
2i
-1
3+4i
4+2i
5
3-4i
25
1
3.14159i
?CALC-F-DOMAIN, Ordering comparisons require real arguments
```

**Trampa con variables complejas:** al sustituir `$z` en una expresión, el texto `1+2i`
se pega tal cual y la precedencia puede cambiar el resultado. Pon paréntesis alrededor
de la sustitución:

```bcl
SET z [CALC complex(1,2)]
PUTS [CALC ($z) * ($z)]      # correcto: (1+2i)^2 = -3+4i
PUTS [CALC $z * $z]          # incorrecto: 1 + 2i*1 + 2i*i ... = 1+4i
```
```text
-3+4i
1+4i
```

### 4.5 Errores de `CALC`

La división y el módulo por cero **sí** son errores (`?CALC-F-DOMAIN`), igual que operar
con texto no numérico (`?CALC-F-UNKNOWN`) o una expresión mal formada (`?CALC-F-SYNTAX`).

```bcl
TRY
    PUTS [CALC 1 / 0]
CATCH e
    PUTS $e
END
TRY
    PUTS [CALC hola + 1]
CATCH e
    PUTS $e
END
TRY
    PUTS [CALC 5 +]
CATCH e
    PUTS $e
END
```
```text
?CALC-F-DOMAIN, Division by zero
?CALC-F-UNKNOWN, Unknown symbol
?CALC-F-SYNTAX, Unexpected token
```

## 5. Variables

### 5.1 `SET`

`SET nombre valor…` es la única forma de asignar. Con varios valores los une con un
espacio (útil para escribir listas literales). Todo se guarda como cadena.

```bcl
SET nombre "Maria"
SET edad 25
SET vacio ""
SET saludo "Buenos dias, $nombre"
SET lista uno dos tres
PUTS "$saludo ($edad) <$vacio> \[$lista\]"
PUTS $lista
SET edad 26                   # sobrescribe
PUTS $edad
```
```text
Buenos dias, Maria (25) <> [uno dos tres]
uno dos tres
26
```

Los nombres admiten letras, dígitos y `_` (y `:` para nombres compuestos), sin empezar
por dígito. Mayúsculas y minúsculas se distinguen: `$Total` y `$total` son distintas.

### 5.2 `INCR` e `INCRV`

`INCR nombre [incremento]` suma 1 por omisión; el incremento puede ser negativo o
decimal. `INCRV nombre incremento` es equivalente con el incremento obligatorio. Una
variable inexistente o no numérica se toma como 0. Con `INCR`, un incremento que no es
un número completo (`abc`, `5x`) da `?INCR-F-BADVALUE` y la variable no cambia; `INCRV` no
valida el incremento (un valor no numérico cuenta como 0).

```bcl
SET c 0
INCR c
INCR c
INCR c -1
INCR c 0.5
PUTS $c
SET paso 7
INCRV c $paso
PUTS $c
INCR nueva
PUTS $nueva
TRY
    INCR c abc
CATCH e
    PUTS $e
END
```
```text
1.5
8.5
1
?INCR-F-BADVALUE, Increment must be a number: abc
```

Como `CALC`, ambos comandos escriben el resultado con 6 cifras significativas.

### 5.3 `EXISTS` y `UNSET`

`EXISTS nombre` devuelve `1` o `0` y se usa para distinguir «no existe» de «vale vacío».
`UNSET` elimina una variable, un elemento de array (`UNSET a(k)`) o un array entero
(`UNSET a`); no falla si el nombre no existe.

```bcl
SET x 10
PUTS [EXISTS x]
PUTS [EXISTS missing]
SET ciudad(capital) "Madrid"
PUTS [EXISTS ciudad(capital)]
UNSET x
UNSET nunca_existio
PUTS [EXISTS x]
UNSET ciudad
PUTS [ARREXISTS ciudad]
```
```text
1
0
1
0
0
```

Los arrays (`SET a(k) v`, sección 8) usan nombres `a(k)`; un mismo nombre no puede ser a la
vez escalar y array (`?SET-F-DUPLICATE`). Variables con significado especial: `$ARGS`
(sección 10) y `BCL_LIBS` (sección 11).

## 6. Entrada y salida

Tres comandos: `PUTS`, `PUTSN` y `GETS`.

### 6.1 `PUTS` y `PUTSN`

`PUTS` imprime sus argumentos (unidos por espacios) y un salto de línea; `PUTSN` hace lo
mismo **sin** salto de línea, útil para prompts y para construir una línea por partes.

```bcl
PUTS "Hola, Mundo"
PUTS "Alumna: Lucia" "nota" 9.5
PUTS [CALC 12 ** 2]
PUTS ""                          # línea en blanco
PUTSN "Resultado: "
PUTSN [CALC 7 * 6]
PUTS " (calculado)"
PUTS "Col1\tCol2\tCol3"
```
```text
Hola, Mundo
Alumna: Lucia nota 9.5
144

Resultado: 42 (calculado)
Col1	Col2	Col3
```

### 6.2 `GETS`

`GETS` lee una línea completa de la entrada estándar (sin el salto final) y la devuelve.
Se usa con corchetes. No hay límite fijo de longitud.

```bcl
PUTSN "Primero: "
SET a [GETS]
PUTSN "Segundo: "
SET b [GETS]
PUTS ""
PUTS "La suma de $a y $b es [CALC $a + $b]"
```
```text
Primero: Segundo: 
La suma de 8 y 12 es 20
```

**Fin de la entrada.** Cuando la entrada se agota (`Ctrl+D` o un fichero redirigido que
termina), `GETS` devuelve la cadena vacía **en todas las llamadas siguientes**; no se
distingue del usuario que pulsa solo Enter. Un bucle que espera una palabra de salida
no termina nunca si la entrada se acaba antes. Añade siempre una salida por vacío:

```bcl
SET n 0
IF 1 DO
    SET d [GETS]
    IF [STREQ $d ""] THEN
        BREAK                    # fin de la entrada o línea en blanco
    END
    IF [STREQ $d "fin"] THEN
        BREAK
    END
    INCR n
    PUTS "dato $n -> $d"
LOOP
PUTS "leídos: $n"
```
```text
dato 1 -> uno
dato 2 -> dos
leídos: 2
```

`GETS` no valida el tipo: si el usuario escribe texto donde esperas un número, `CALC`
fallará (usa `TRY`, sección 14).

## 7. Estructuras de control

BCL tiene exactamente dos estructuras: el condicional `IF … THEN … END` y el bucle
`IF … DO … LOOP`, con `BREAK` y `NEXT`.

> **Una palabra clave de bloque por línea.** No existe la forma abreviada
> `IF c THEN cmd END`; da `?PAR-F-MISSTERM, Expected block terminator before end of
> file`. Lo mismo vale para `ELSEIF`, `ELSE`, `DO`, `LOOP`, `PROC`, `TRY` y `CATCH`. El
> error habla del final del fichero, pero lo que suele estar mal es la línea donde
> empieza el bloque.

### 7.1 La condición

La condición de `IF` no es una expresión de `CALC`: es un pequeño lenguaje propio.

- Una **comparación** `a OP b` con `OP` en `==` `!=` `<` `>` `<=` `>=`. Cada operando es
  `$variable`, un literal (con o sin comillas), un `[comando]` o un grupo entre
  paréntesis. Si **ambos** operandos son números se comparan numéricamente
  (`007 == 7` es verdadero); si no, como texto. `STREQ` fuerza la comparación de texto.
- Un **valor solo** (`IF $flag`, `IF [STRFIND $t "x"]`) es verdadero salvo que sea `""`
  o `"0"` (ojo: `0.0` es verdadero).
- Se combinan con `&&`, `||`, `!` y paréntesis.
- **No hay aritmética dentro de la condición.** Para operar, usa `[CALC …]`. Lo que
  sobra tras el primer operando se ignora **sin error**, así que `IF $n % 2 == 0`
  equivale a `IF $n` y casi siempre es verdadero.

```bcl
SET n 7
IF $n % 2 == 0 THEN              # MAL: equivale a IF $n (7 es verdadero)
    PUTS "mal: dice que 7 es par"
END
IF [CALC $n % 2] == 0 THEN       # BIEN
    PUTS "par"
ELSE
    PUTS "impar"
END
SET x 15
IF $x > 10 && $x < 20 THEN
    PUTS "entre 10 y 20"
END
IF [STREQ 007 7] THEN
    PUTS "iguales como texto"
ELSEIF 007 == 7 THEN
    PUTS "iguales como número"
END
IF ! $nada THEN
    PUTS "una variable inexistente es falsa"
END
```
```text
mal: dice que 7 es par
impar
entre 10 y 20
iguales como número
una variable inexistente es falsa
```

### 7.2 `IF … THEN … ELSEIF … ELSE … END`

`ELSEIF` y `ELSE` son opcionales (puede haber varios `ELSEIF`); `END` es obligatorio.
Los bloques se anidan libremente.

```bcl
PROC califica WITH nota DO
    IF $nota >= 9 THEN
        RETURN "Sobresaliente"
    ELSEIF $nota >= 7 THEN
        RETURN "Notable"
    ELSEIF $nota >= 5 THEN
        RETURN "Aprobado"
    ELSE
        RETURN "Suspendido"
    END
END
PUTS [califica 9.5]
PUTS [califica 6]
PUTS [califica 3]
SET anio 2024
IF [CALC $anio % 4 == 0 && ($anio % 100 != 0 || $anio % 400 == 0)] THEN
    PUTS "$anio es bisiesto"
END
```
```text
Sobresaliente
Aprobado
Suspendido
2024 es bisiesto
```

> **Limitación conocida (build 7).** Dentro de un grupo entre paréntesis de una condición
> `IF`, si el primer operando ya decide el resultado (`(0 && x)`, `(1 || x)`), el kernel
> no analiza el resto y da `?EVAL-F-UNTERMCOND, Unterminated condition group`. Evítalo
> moviendo la lógica compuesta a un único `[CALC …]`, como en el ejemplo anterior.

### 7.3 El bucle `IF … DO … LOOP`

Se ejecuta mientras la condición sea verdadera; se evalúa **antes** de cada vuelta. El
`ELSE` opcional se ejecuta cuando el bucle termina porque la condición se hizo falsa
(también si lo era desde el principio) y **se omite si se sale con `BREAK`**.

```bcl
SET i 1
IF $i <= 3 DO
    PUTS "Iteracion $i"
    INCR i
ELSE
    PUTS "fin natural del bucle"
LOOP
SET cuenta 3
IF $cuenta > 0 DO
    PUTSN "$cuenta "
    INCRV cuenta -1
LOOP
PUTS "Despegue!"
```
```text
Iteracion 1
Iteracion 2
Iteracion 3
fin natural del bucle
3 2 1 Despegue!
```

### 7.4 `BREAK` y `NEXT`

`BREAK` sale del bucle más interno; `NEXT` salta a la siguiente iteración. El bucle
infinito controlado es `IF 1 DO … BREAK … LOOP`. El `ELSE` permite detectar si una
búsqueda tuvo éxito:

```bcl
SET lista "pedro ana marta juan"
SET i 1
SET total [LSTLENG $lista]
IF $i <= $total DO
    SET elem [LSTGET $lista $i]
    IF $elem == "ana" THEN
        PUTS "Encontrado en posicion $i"
        BREAK
    END
    INCR i
ELSE
    PUTS "No se encontro"
LOOP
SET n 0
IF $n < 10 DO
    INCR n
    IF [CALC $n % 2] == 0 THEN
        NEXT
    END
    PUTSN "$n "
LOOP
PUTS ""
```
```text
Encontrado en posicion 2
1 3 5 7 9 
```

## 8. Arrays asociativos

Una lista guarda datos **por posición**; un array los guarda **por nombre** (clave). No es
un tipo nuevo: son variables corrientes de nombre `base(clave)`, más un índice interno
que permite preguntar por el conjunto. Claves y valores son cadenas.

| | Lista | Array |
|---|---|---|
| Acceso | por posición (1, 2, 3…) | por clave |
| Orden | se conserva | **no garantizado** |
| Ideal para | secuencias, colas | configuraciones, diccionarios, contadores, cachés |

### 8.1 Crear, leer y claves dinámicas

No se declara: la primera asignación lo crea. Se lee con `$nombre(clave)`; la clave puede
venir de una variable (se resuelve primero). Una clave inexistente da `""` sin error.

```bcl
SET ciudad(nombre) "Madrid"
SET ciudad(pais) "Espana"
PUTS $ciudad(nombre)
PUTS "Capital de $ciudad(pais)"
SET campo "pais"
PUTS $ciudad($campo)
PUTS "<$ciudad(inexistente)>"
```
```text
Madrid
Capital de Espana
Espana
<>
```

Evita claves con espacios (usa `nombre_completo`).

### 8.2 Comandos `ARR*`

Reciben el **nombre** del array, sin `$` y sin paréntesis: `[ARRLENG config]`.

| Comando | Devuelve |
|---|---|
| `ARREXISTS a` | `1` si tiene algún elemento |
| `ARRLENG a` | número de elementos |
| `ARRKEYS a` | lista de claves (orden no garantizado) |
| `ARRGET a` | lista plana `clave valor clave valor…` |
| `ARRSET a lista` | carga pares (añade o sobrescribe; no borra lo anterior) |
| `ARRDEL a clave` | elimina un elemento (silencioso si no existe) |
| `ARRCLEAR a` | vacía el array |

`ARRDEL a k` equivale a `UNSET a(k)` y `ARRCLEAR a` a `UNSET a`. `EXISTS a(k)` pregunta
por una clave concreta; `ARREXISTS a`, por si hay algún elemento.

```bcl
ARRSET stock "tornillos 250 tuercas 80 arandelas 1200"
PUTS [ARRLENG stock]
PUTS [EXISTS stock(tuercas)]
PUTS [EXISTS stock(clavos)]
PUTS [LSTSORT [ARRKEYS stock]]
ARRDEL stock arandelas
ARRDEL stock inexistente
PUTS [ARRLENG stock]
SET copia_plana [ARRGET stock]          # serializar
ARRSET otro $copia_plana                # clonar
PUTS $otro(tuercas)
ARRCLEAR stock
PUTS [ARREXISTS stock]
TRY
    ARRSET malo "a b c"
CATCH e
    PUTS $e
END
```
```text
3
1
0
arandelas tornillos tuercas
2
80
0
?ARRSET-F-BADARGS, List must contain key/value pairs
```

### 8.3 Recorrer un array

Se piden las claves y se recorre la lista; **ordénalas** si la salida debe ser reproducible.

```bcl
SET config(host) "localhost"
SET config(puerto) "8080"
SET claves [LSTSORT [ARRKEYS config]]
SET i 1
IF $i <= [LSTLENG $claves] DO
    SET k [LSTGET $claves $i]
    PUTS "$k = $config($k)"
    INCR i
LOOP
```
```text
host = localhost
puerto = 8080
```

### 8.4 Vectores y matrices

Con claves numéricas consecutivas (desde 1) se obtiene un vector; con claves compuestas
(`i,j`) una matriz. `ARRLENG` cuenta el total de elementos.

```bcl
SET v(1) "alfa"
SET v(2) "beta"
SET idx [CALC [ARRLENG v] + 1]
SET v($idx) "gamma"            # añadir al final
PUTS "$v(1) $v(2) $v(3) (total [ARRLENG v])"
SET M(1,1) 1
SET M(1,2) 2
SET M(2,1) 3
SET M(2,2) 4
SET i 2
SET j 1
PUTS "M($i,$j) = $M($i,$j); elementos: [ARRLENG M]"
```
```text
alfa beta gamma (total 3)
M(2,1) = 3; elementos: 4
```

### 8.5 Arrays y `GLOBAL`

Dentro de un `PROC` las variables son locales; para trabajar sobre un array global se
enlaza su **nombre base** con `GLOBAL`. Con arrays el nombre no tiene que existir ya.
Ejemplo: una pila y un contador de frecuencias.

```bcl
PROC apilar WITH valor DO
    GLOBAL pila
    SET pila([CALC [ARRLENG pila] + 1]) $valor
END
PROC desapilar DO
    GLOBAL pila
    SET idx [ARRLENG pila]
    IF $idx == 0 THEN
        ERROR "La pila esta vacia"
    END
    SET valor $pila($idx)
    ARRDEL pila $idx
    RETURN $valor
END
apilar uno
apilar dos
apilar tres
PUTS [desapilar]
PUTS [desapilar]
PUTS [ARRLENG pila]

SET palabras "sol luna sol mar sol luna"
SET n [LSTLENG $palabras]
SET i 1
IF $i <= $n DO
    SET p [LSTGET $palabras $i]
    IF [EXISTS cuenta($p)] THEN
        INCR cuenta($p)
    ELSE
        SET cuenta($p) 1
    END
    INCR i
LOOP
SET claves [LSTSORT [ARRKEYS cuenta]]
SET i 1
IF $i <= [LSTLENG $claves] DO
    SET k [LSTGET $claves $i]
    PUTS [STRAPP [STRFMT "%-6s" $k] $cuenta($k)]
    INCR i
LOOP
```
```text
tres
dos
1
luna  2
mar   1
sol   3
```

`GLOBAL` enlaza **un solo nombre por línea**; para dos arrays globales, dos líneas.
Tras `GLOBAL pila`, el nombre queda reservado como array: un `SET pila "x"` posterior
daría `?SET-F-DUPLICATE`.

## 9. Listas

Una lista es una cadena cuyos elementos se separan por espacios; los nueve comandos
`LST*` la interpretan así. No hay tipo lista: puedes escribirla a mano, leerla de
una entrada o construirla con `LSTAPP`.

```bcl
SET colores "rojo verde azul"
PUTS [STRLENG $colores]        # como texto: 16 caracteres
PUTS [LSTLENG $colores]        # como lista: 3 elementos
PUTS [LSTGET $colores 2]
```
```text
15
3
verde
```

### 9.1 Elementos con espacios: llaves

Un elemento con espacios se delimita con `{}` (las llaves no forman parte del elemento).
`{}` es un elemento **vacío** y cuenta. Una llave literal se escribe `\{` o `\}`.

```bcl
SET dias {lunes santo} martes miercoles
PUTS [LSTLENG $dias]
PUTS [LSTGET $dias 1]
SET v {} alfa beta
PUTS "[LSTLENG $v] elementos; el primero es <[LSTGET $v 1]>"
SET l uno \{dos\} tres
PUTS [LSTGET $l 2]
PUTS [LSTAPP "{lunes santo} martes" "{otro dia}"]    # forma canónica
TRY
    PUTS [LSTLENG "{uno dos"]
CATCH e
    PUTS $e
END
```
```text
3
lunes santo
3 elementos; el primero es <>
{dos}
{lunes santo} martes {otro dia}
?LIST-F-SYNTAX, Unterminated braced list item
```

Son listas inválidas (`?LIST-F-SYNTAX`): un grupo abierto sin cerrar, texto pegado tras
`}` (`{uno}dos`) y una llave suelta sin escapar. Todo lo que genera el kernel (incluido
`$ARGS`) usa la forma canónica con llaves.

### 9.2 Medir, leer, recortar

`LSTGET` fuera de rango devuelve `""` (la posición 0 no existe); `LSTRANG` recorta los
extremos que se salen y devuelve `""` si `ini > fin`.

```bcl
SET semana "lunes martes miercoles jueves viernes"
PUTS [LSTLENG ""]
PUTS [LSTLENG "   "]
PUTS "<[LSTGET $semana 99]>"
PUTS [LSTRANG $semana 1 3]
PUTS [LSTRANG $semana 4 99]
PUTS "<[LSTRANG $semana 3 1]>"
```
```text
0
0
<>
lunes martes miercoles
jueves viernes
<>
```

### 9.3 Construir y modificar

**`LSTAPP` concatena dos listas**, no «añade un elemento»: un valor con espacios aporta
varios elementos. Si puede contenerlos, envuélvelo en llaves. `LSTINS lista1 lista2 pos`
inserta *antes* de `pos` (argumentos: original, lo que se inserta, posición).
`LSTDEL lista pos cant` borra hasta el final si `cant` se pasa.

```bcl
SET xs "uno dos"
SET xs [LSTAPP $xs "tres"]
SET xs [LSTAPP $xs "{cuatro cinco}"]
PUTS "$xs -> [LSTLENG $xs] elementos"
SET l "a b c"
PUTS [LSTINS $l "X Y" 2]
PUTS [LSTINS $l "X" 99]
SET l "a b c d e"
PUTS [LSTDEL $l 2 3]
PUTS [LSTDEL $l 4 99]
```
```text
uno dos tres {cuatro cinco} -> 4 elementos
a X Y b c
a b c X
a e
a b c
```

### 9.4 Buscar y ordenar

`LSTFIND lista elem` devuelve **todas** las posiciones (o `0`), comparando elementos
completos; sirve como condición de pertenencia. `LSTSORT` ordena los números por valor
y después el texto sin distinguir mayúsculas; solo hay orden ascendente.

```bcl
SET l "rojo verde rojo azul rojo"
PUTS [LSTFIND $l "rojo"]
PUTS [LSTFIND $l "roj"]
IF [LSTFIND "leer escribir borrar" escribir] THEN
    PUTS "Accion permitida."
END
PUTS [LSTSORT "10 9 2 -3 1.5"]
PUTS [LSTSORT "3 pepe 10 Ana"]
```
```text
1 3 5
0
Accion permitida.
-3 1.5 2 9 10
3 10 Ana pepe
```

### 9.5 `LSTFMT`: de texto a lista

Pese al nombre, **no formatea**: *trocea* un texto por un separador (de uno o más
caracteres). Los separadores consecutivos producen elementos vacíos; un separador vacío
deja el texto entero como un solo elemento.

```bcl
SET l [LSTFMT "rojo,verde,azul" ","]
PUTS "$l ([LSTLENG $l])"
PUTS [LSTFMT "a::b::c" "::"]
PUTS [LSTFMT "/usr/local/bin" "/"]
PUTS [LSTLENG [LSTFMT "  hola  mundo " " "]]
PUTS [LSTLENG "  hola  mundo "]
```
```text
rojo verde azul (3)
a b c
{} usr local bin
6
2
```

### 9.6 Recorrer una lista y recetas

El patrón central: guardar la longitud, un contador desde 1 y `LSTGET`.

```bcl
SET colores {rojo oscuro} verde azul
SET n [LSTLENG $colores]
SET i 1
IF $i <= $n DO
    PUTS "$i -> [LSTGET $colores $i]"
    INCR i
LOOP

PROC lst_join WITH lista sep DO
    SET n [LSTLENG $lista]
    IF $n == 0 THEN
        RETURN ""
    END
    SET salida [LSTGET $lista 1]
    SET i 2
    IF $i <= $n DO
        SET salida [STRAPP $salida [STRAPP $sep [LSTGET $lista $i]]]
        INCR i
    LOOP
    RETURN $salida
END
PUTS [lst_join "rojo verde azul" ", "]
```
```text
1 -> rojo oscuro
2 -> verde
3 -> azul
rojo, verde, azul
```

## 10. Argumentos del programa

Todo lo que se escribe tras el nombre del script queda en la variable reservada `$ARGS`,
una **lista canónica** (siempre definida; vacía si no hay argumentos). El nombre del
script **no** forma parte de ella: el primer argumento es el 1. Al ser una lista se
conservan argumentos vacíos, con espacios y con llaves.

```bash
bcl args.bcl alpha "" "dos tres" "con{llave}" -v
```

```bcl
PUTS "Recibidos: [LSTLENG $ARGS]"
PUTS "ARGS = $ARGS"
SET n [LSTLENG $ARGS]
SET i 1
IF $i <= $n DO
    PUTS "$i -> <[LSTGET $ARGS $i]>"
    INCR i
LOOP
```
```text
Recibidos: 5
ARGS = alpha {} {dos tres} {con\{llave\}} -v
1 -> <alpha>
2 -> <>
3 -> <dos tres>
4 -> <con{llave}>
5 -> <-v>
```

Lee siempre con `LSTLENG` y `LSTGET`; trocear `$ARGS` a mano con `STRFIND` o `LSTFMT`
rompe las protecciones que el intérprete ya ha puesto.

### 10.1 Validar y terminar con `EXIT`

`EXIT código` termina el programa **entero** (desde un procedimiento o un bucle también)
y devuelve el código al sistema. Convención: `0` correcto; `1` error genérico (también el
de un error no capturado); otros valores, con el significado que documentes. Un programa
que llega al final sin `EXIT` devuelve 0.

```bcl
# Uso: suma.bcl <numero1> <numero2>
IF [LSTLENG $ARGS] != 2 THEN
    PUTS "Uso: suma.bcl <numero1> <numero2>"
    EXIT 2
END
PUTS [CALC [LSTGET $ARGS 1] + [LSTGET $ARGS 2]]
```
```text
Uso: suma.bcl <numero1> <numero2>
(código de salida: 2)
```

Con `7 5` imprime `12` y termina con 0. Para comprobar que los datos son números,
envuelve `CALC` en `TRY` (sección 14): `CALC` lanza `?CALC-F-UNKNOWN` con texto no numérico.

> **Defecto conocido (build 7).** Si antes se ha capturado cualquier error con
> `TRY`/`CATCH`, un `EXIT n` posterior con `n` distinto de 0 termina con código 1 y vuelve
> a imprimir en `stderr` el error ya capturado, en lugar de salir con `n`. `EXIT 0` y
> `EXIT n` sin errores capturados previos funcionan bien. Mientras no se corrija, no
> dependas de códigos de salida distintos de 1 en programas que capturan errores.

### 10.2 Interpretar opciones

El patrón es recorrer `$ARGS` decidiendo qué es cada elemento; una opción con valor
consume un argumento extra con otro `INCR i`. Los corchetes del mensaje de uso se escriben
`\[` y `\]`.

```bcl
SET verboso 0
SET veces 1
SET texto ""
SET n [LSTLENG $ARGS]
SET i 1
IF $i <= $n DO
    SET a [LSTGET $ARGS $i]
    IF [STREQ $a "-v"] THEN
        SET verboso 1
    ELSEIF [STREQ $a "-n"] THEN
        INCR i
        SET veces [LSTGET $ARGS $i]
    ELSEIF [STREQ $a "--ayuda"] THEN
        PUTS "Uso: opciones.bcl \[-v\] \[-n VECES\] TEXTO"
        EXIT 0
    ELSE
        SET texto $a
    END
    INCR i
LOOP
IF $verboso THEN
    PUTS "Repitiendo $veces veces '$texto'"
END
SET k 1
IF $k <= $veces DO
    PUTS $texto
    INCR k
LOOP
```
```text
Repitiendo 3 veces 'hola'
hola
hola
hola
```

### 10.3 Scripts ejecutables

Una primera línea `#!/usr/bin/env bcl` es para BCL un comentario y para el sistema
operativo el intérprete a usar. Con `chmod +x` el script se invoca como una orden más.
Recuerda que la shell expande `*`, `$` y las comillas antes de que BCL vea los argumentos.

## 11. Módulos, `SOURCE` y el entorno de arranque

### 11.1 `SOURCE` y `SOURCEDIR`

`SOURCE "ruta.bcl"` ejecuta otro fichero **en el ámbito actual y en ese momento**: todo
su código de nivel superior se ejecuta, y sus procedimientos y variables quedan
disponibles. Si falla, el diagnóstico conserva el fichero hijo, la línea y la cadena de
inclusiones.

```bcl
# utils.bcl: biblioteca de utilidades
PROC titulo WITH texto DO
    PUTS "== $texto =="
END
SET version_utils 2
```

```bcl
SOURCE "utils.bcl"
titulo "Mi programa"
PUTS "utils v$version_utils"
```
```text
== Mi programa ==
utils v2
```

Las rutas relativas de `SOURCE` se resuelven respecto al **directorio de trabajo**, no
al fichero que contiene la instrucción. Para que el programa funcione lance desde donde
se lance, usa `SOURCEDIR`, que devuelve el directorio del fichero en evaluación (`.` si el
nombre no lleva directorio):

```bcl
SOURCE [STRAPP [SOURCEDIR] "/utils.bcl"]
titulo "Ruta robusta"
TRY
    SOURCE [STRAPP [SOURCEDIR] "/opcional.bcl"]
CATCH e
    PUTS "Aviso: $e_INFO(CODE)"
END
```
```text
== Ruta robusta ==
Aviso: NOFILE
```

### 11.2 Módulos binarios: `LOAD`, `UNLOAD` y `BCL_LIBS`

Un módulo `.bll` es una biblioteca dinámica que añade comandos al intérprete. **Los
módulos no forman parte del kernel y se distribuyen por separado**; este manual no
documenta ninguno. Lo que el kernel ofrece es:

- `LOAD ruta.bll` carga el módulo; sus comandos quedan disponibles al instante. Devuelve
  una línea de estado `LOAD OK module=<ruta resuelta> version=<versión>` (`unknown` si el
  módulo no declara versión). `UNLOAD` lo descarga y retira sus comandos.
- Un nombre **con `/`** se usa tal cual. Un nombre **sin `/`** se busca, por orden de
  prioridad, en los directorios de la variable `BCL_LIBS` (separados por comas; se
  ignoran espacios y entradas vacías; una entrada `~/` se expande con `$HOME`). Gana el
  primero que contiene el fichero. Si `BCL_LIBS` está definida y no aparece, `?LOAD-F-NOTFOUND`
  lista dónde se buscó; sin `BCL_LIBS` no hay búsqueda (el nombre pasa al cargador del
  sistema). Valor inicial: la variable de entorno `BCL_LIBS`.
- Si dos módulos definen el mismo comando, vale el último cargado; al hacer `UNLOAD`, el
  comando vuelve al integrado del kernel.
- Un módulo puede publicar comandos propios, incluso de eventos (`INT`, `TIMER`, `RUN`…).

Para escribir un módulo en C consulta `doc/bcl_external_bll_interface_v1_1.md`.

### 11.3 El fichero de inicio `$HOME/.bcl.rc`

Al arrancar, el intérprete ejecuta `$HOME/.bcl.rc` (si existe) **antes que cualquier otra
instrucción**: antes del script, de sus `SOURCE` y de la sesión interactiva, en el
ámbito global (lo que define queda disponible). `$ARGS` ya está definida. Sirve para fijar
`BCL_LIBS`, procedimientos comunes o módulos que se quieren siempre cargados.

| Situación | Qué ocurre |
|---|---|
| No existe el fichero | Nada: el arranque sigue |
| Error en el fichero | Se muestra el error y el intérprete termina con código 1 sin ejecutar el script |
| `EXIT n` en el fichero | El intérprete termina con ese código, sin ejecutar el script |
| `bcl --no-rc` o `BCL_NO_RC` | No se lee |
| `bcl --restricted` / `BCL_RESTRICTED=1` | No se lee |
| Sin `HOME` | No se lee |

```bash
$ cat ~/.bcl.rc
PUTS rc-ran
PUTS "ARGS=$ARGS"
$ bcl main.bcl a b            # main.bcl contiene: PUTS main
rc-ran
ARGS=a b
main
$ bcl --no-rc main.bcl
main
```

Un script portable no debe depender del fichero de inicio de cada usuario: pruébalo con
`bcl --no-rc`. (Si el `.bcl.rc` hace `LOAD` de un módulo y el script lo repite, falla con
módulo ya cargado.)

### 11.4 Versión y modo restringido

`bcl --version` imprime `BCL-32 v1.2 build N`. En **modo restringido**
(`--restricted` o `BCL_RESTRICTED=1`) `LOAD`/`UNLOAD` están deshabilitados
(`?LOAD-F-RESTRICTED`) y `SOURCE` solo puede cargar ficheros dentro de la raíz fijada con
`--source-root` o `BCL_SOURCE_ROOT` (si no, `?SRC-F-RESTRICTED`). Úsalo cuando el script
proceda de una fuente no confiable.

## 12. Procedimientos y ámbito

Un procedimiento es un bloque con nombre que se invoca como cualquier otro comando
(para BCL no hay diferencia entre `PUTS` y un `PROC` tuyo).

```text
PROC nombre DO
    sentencias
END

PROC nombre WITH obligatorio… @opcional… DO
    sentencias
    RETURN valor
END
```

Hay que definirlo **antes** de llamarlo (el fichero se lee de arriba abajo; si no,
`?EVAL-F-UNKCMD`). Estructura recomendada: `LOAD`/`SOURCE`, luego todos los `PROC`, al final
el programa principal. Redefinir es legal: gana la última definición.

### 12.1 Parámetros

Los argumentos se asignan **por posición**; el número se comprueba en cada llamada y el
mensaje da el rango (obligatorios..obligatorios+opcionales). Un parámetro con `@` es
opcional; todos los que sigan deben serlo también (si no, `?PAR-F-PROCDEF` al definir).
Un opcional omitido **no se crea**: se distingue de «pasado vacío» con `EXISTS`.

```bcl
PROC rectangulo WITH ancho alto DO
    PUTS [STRFMT "Area: %g" [CALC $ancho * $alto]]
END
rectangulo 5 3

PROC conectar WITH host @puerto @timeout DO
    SET p 8080                       # valores por defecto
    SET t 30
    IF [EXISTS puerto] THEN
        SET p $puerto
    END
    IF [EXISTS timeout] THEN
        SET t $timeout
    END
    PUTS "$host puerto=$p timeout=$t"
END
conectar "srv.local"
conectar "srv.local" 9000
conectar "srv.local" 9000 5

PROC vacio_o_no WITH a @b DO
    IF [EXISTS b] THEN
        PUTS "b pasado, vale <$b>"
    ELSE
        PUTS "b no pasado"
    END
END
vacio_o_no 1
vacio_o_no 1 ""
TRY
    rectangulo 1
CATCH e
    PUTS $e
END
```
```text
Area: 15
srv.local puerto=8080 timeout=30
srv.local puerto=9000 timeout=30
srv.local puerto=9000 timeout=5
b no pasado
b pasado, vale <>
?EVAL-F-BADARGS, rectangulo: expected between 2 and 2 arguments, got 1
```

### 12.2 `RETURN`

`RETURN valor` termina el procedimiento y devuelve el valor; sin `RETURN` (o con `RETURN`
a secas) devuelve `""`. Sirve para salidas tempranas («cláusulas de guarda»). Como todo
es una cadena, se pueden devolver listas; para un array, devuelve `[ARRGET a]` o usa
`GLOBAL`. Sin corchetes, el valor devuelto se descarta.

```bcl
PROC suma WITH a b DO
    RETURN [CALC $a + $b]
END
PUTS [suma 100 [suma 1 2]]

PROC solo_imprime DO
    PUTS "hago algo"
END
SET r [solo_imprime]
PUTS "resultado=<$r>"
```
```text
103
hago algo
resultado=<>
```

### 12.3 Ámbito de las variables

Cada procedimiento tiene su **propio ámbito**: lo que crea dentro se queda dentro, y
**no ve** las variables globales (leerlas da `""` sin error, lo que pasa desapercibido).
Lo preferible es pasar los datos como parámetros.

```bcl
SET titulo "Informe"
SET mensaje "soy exterior"
PROC prueba DO
    SET mensaje "soy interior"
    PUTS "dentro: $mensaje, titulo=<$titulo>"
END
prueba
PUTS "fuera: $mensaje"
```
```text
dentro: soy interior, titulo=<>
fuera: soy exterior
```

`GLOBAL nombre` enlaza **un** nombre con la variable global del mismo nombre (dos nombres
en una línea dan `?EVAL-F-BADARGS`). Para un escalar, **créalo antes** en el ámbito
global: si no existe, `GLOBAL` lo trata como array y un `SET` escalar posterior falla con
`?SET-F-DUPLICATE`.

```bcl
SET contador 0
PROC incrementar DO
    GLOBAL contador
    INCR contador
END
incrementar
incrementar
PUTS $contador

PROC mal DO
    GLOBAL nuevo
    SET nuevo 5
END
TRY
    mal
CATCH e
    PUTS $e
END
```
```text
2
?SET-F-DUPLICATE, Variable 'nuevo' already exists as array
```

Los parámetros están protegidos: `UNSET` de un parámetro activo da
`?UNSET-F-BADVALUE, Cannot remove active parameter 'n'`; copia el valor a otra variable.

### 12.4 Recursividad

Todo procedimiento recursivo necesita un caso base y avanzar hacia él. Sin ellos, el
kernel corta al llegar al límite de anidamiento con `?EVAL-F-DEPTH` en lugar de agotar la
pila.

```bcl
PROC factorial WITH n DO
    IF $n <= 1 THEN
        RETURN 1
    END
    RETURN [CALC $n * [factorial [CALC $n - 1]]]
END
PUTS [factorial 5]
PUTS [factorial 10]

PROC infinita WITH n DO
    RETURN [infinita [CALC $n + 1]]
END
TRY
    infinita 1
CATCH e
    PUTS $e
END
```
```text
120
3.6288e+06
?EVAL-F-DEPTH, Subcommand nesting exceeds limit
```

Recuerda la sección 4.2: `[factorial 10]` se muestra como `3.6288e+06` (6 cifras
significativas), no como 3628800.

Buen diseño: una responsabilidad por procedimiento, parámetros mejor que `GLOBAL`,
devolver valores en vez de imprimirlos y agrupar lo reutilizable en un fichero para `SOURCE`.

## 13. Cadenas

Cinco reglas: (1) posiciones desde 1; (2) se cuenta por **carácter**, no por byte (UTF-8);
(3) **ningún comando modifica la cadena original**: devuelven una nueva (sin `SET` y
corchetes el resultado se pierde); (4) `0` significa «no hay resultado»; (5) pedir algo fuera de la
cadena no es un error: da `""` y los rangos se recortan.

### 13.1 Medir, comparar y extraer

`STREQ` compara texto estrictamente (mayúsculas y espacios cuentan, `007` ≠ `7`). Para
ignorar mayúsculas, normaliza antes con `STRLOW`. `STRRANG` incluye ambos extremos
(devuelve `fin - ini + 1` caracteres).

```bcl
SET texto "Hola mundo"
PUTS [STRLENG "canción"]
PUTS [STREQ "hola" "HOLA"]
PUTS [STREQ [STRLOW "SI"] "si"]
PUTS [STRGET $texto 1]
PUTS "<[STRGET $texto 99]>"
PUTS [STRRANG $texto 1 4]
PUTS [STRRANG $texto 6 [STRLENG $texto]]
PUTS [STRRANG "Hola" 3 99]
PUTS [STRRANG "Hola" 0 4]
SET vacio [STRRANG "Hola" 7 3]
PUTS "<$vacio>"
```
```text
7
0
1
H
<>
Hola
mundo
la
Hola
<>
```

### 13.2 Unir, insertar y borrar

`STRAPP` une **exactamente dos** cadenas (para tres, anida). `STRINS cad texto pos`: el
orden es cadena, texto, posición. `STRDEL cad pos cant`. No existe «reemplazar»: se
construye con `STRDEL` + `STRINS`.

```bcl
PUTS [STRAPP "20" "26"]
PUTS [STRAPP "Ada" [STRAPP " " "Lovelace"]]
PUTS [STRINS "Hla" "o" 2]
PUTS [STRINS "abc" "X" 0]
PUTS [STRINS "abc" "X" 99]
PUTS [STRDEL "Hola mundo" 5 1]
PUTS [STRDEL "abc" 2 99]
SET s "Hola mundo"
SET s [STRDEL $s 6 5]
SET s [STRINS $s "BCL" 6]
PUTS $s
```
```text
2026
Ada Lovelace
Hola
Xabc
abcX
Holamundo
a
Hola BCL
```

### 13.3 Buscar

`STRFIND cad sub` devuelve **todas** las posiciones (también solapadas) como lista, o `0`.
Funciona como condición; pero no compares con `== 3`: puede haber varias.

```bcl
PUTS [STRFIND "bananana" "na"]
PUTS [STRFIND "aaa" "aa"]
PUTS [STRFIND "hola" "zz"]
SET linea "ERROR: disco lleno"
IF [STRFIND $linea "ERROR"] THEN
    PUTS "La linea reporta un error."
END
SET pos [STRFIND "el gato y el perro y el pez" "el"]
PUTS "[LSTLENG $pos] apariciones; la primera en [LSTGET $pos 1]"
```
```text
3 5 7
1 2
0
La linea reporta un error.
3 apariciones; la primera en 1
```

### 13.4 Transformar

`STRUPP` y `STRLOW` convierten solo ASCII (`ó` y `Ñ` no cambian); `STRREV` respeta UTF-8.

```bcl
PUTS [STRUPP "Hola"]
PUTS [STRLOW "Hola"]
PUTS [STRREV "Hola"]
PUTS [STRUPP "canción"]
PUTS [STRREV "canción"]
```
```text
HOLA
hola
aloH
CANCIóN
nóicnac
```

### 13.5 `STRFMT`

`STRFMT "plantilla" [valor]`: texto con **un único** especificador
`%[banderas][ancho][.precisión]tipo` (tipos y banderas en la sección 2.4). El ancho es un
mínimo (rellena por la izquierda; `-` alinea a la izquierda), la precisión son los
decimales (o el máximo de caracteres en `%s`).

```bcl
PUTS [STRFMT "Codigo: %05d" 42]
PUTS [STRFMT "Usuario %s conectado" "ana"]
PUTS [STRFMT "|%8s|" "ab"]
PUTS [STRFMT "|%-8s|" "ab"]
PUTS [STRFMT "%.2f" 3.14159]
PUTS [STRFMT "%.3s" "abcdefg"]
PUTS [STRFMT "%08.2f" 3.5]
PUTS [STRFMT "%+d" 7]
PUTS [STRFMT "%#x" 255]
PUTS [STRFMT "%X" 255]
PUTS [STRFMT "%o" 8]
PUTS [STRFMT "%e" 12345.7]
PUTS [STRFMT "%c" 65]
PUTS [STRFMT "100%% seguro"]
PUTS [STRFMT "%d" 3.9]
PUTS [STRFMT "%d" "hola"]
```
```text
Codigo: 00042
Usuario ana conectado
|      ab|
|ab      |
3.14
abc
00003.50
+7
0xff
FF
10
1.234570e+04
A
100% seguro
3
0
```

Una plantilla con **dos** huecos es un error (se rechaza antes de ejecutarse). Para varios
valores, encadena llamadas con `STRAPP`:

```bcl
PROC fila WITH nombre unidades precio DO
    SET s [STRFMT "%-12s" $nombre]
    SET s [STRAPP $s [STRFMT "%5d" $unidades]]
    RETURN [STRAPP $s [STRFMT "%10.2f" $precio]]
END
PUTS [fila "Tornillos" 250 0.03]
PUTS [fila "Arandelas" 1200 0.01]
```
```text
Tornillos     250      0.03
Arandelas    1200      0.01
```

`STRFMT` valida la plantilla antes de aplicarla (sin `%n`, `*` ni `%ld`; ancho y precisión
≤ 4096). Un valor que no encaja no es error: `%d` con `hola` da `0` y con `3.9` da `3`
(trunca).

Se rechazan (`?STRFMT-F-BADARGS`): dos o más especificadores, un literal con valor y más de
dos argumentos; (`?STRFMT-F-UNSUPPORTED`): tipos desconocidos (`%y`), `%n`, `*`, `%ld`;
(`?STRFMT-F-BADVALUE`): ancho o precisión > 4096.

```bcl
TRY
    PUTS [STRFMT "%s tiene %d" "ana" 3]
CATCH e
    PUTS $e
END
TRY
    PUTS [STRFMT "%y" 1]
CATCH e
    PUTS $e
END
```
```text
?STRFMT-F-BADARGS, Expected 1 or 2 arguments, got 3
?STRFMT-F-UNSUPPORTED, Unsupported conversion specifier 'y'
```

### 13.6 Recetas

```bcl
PROC sustituir WITH texto viejo nuevo DO      # primera aparición
    SET pos [STRFIND $texto $viejo]
    IF $pos == 0 THEN
        RETURN $texto
    END
    SET p [LSTGET $pos 1]
    RETURN [STRINS [STRDEL $texto $p [STRLENG $viejo]] $nuevo $p]
END
PROC trim WITH s DO
    SET ini 1
    SET fin [STRLENG $s]
    IF $ini <= $fin && [STREQ [STRGET $s $ini] " "] DO
        INCR ini
    LOOP
    IF $fin >= $ini && [STREQ [STRGET $s $fin] " "] DO
        INCRV fin -1
    LOOP
    RETURN [STRRANG $s $ini $fin]
END
PUTS [sustituir "Hola mundo" "mundo" "BCL"]
SET limpio [trim "   hola   "]
PUTS "<$limpio>"
```
```text
Hola BCL
<hola>
```

No anides comillas dobles dentro de comillas dobles (`"[trim "  x "]"` cierra la cadena
exterior): guarda antes el valor en una variable, como arriba.

## 14. Manejo de errores

Si un error no se captura, el intérprete imprime el diagnóstico por `stderr`
(`Error: fichero:línea:col: ?FAC-SEV-CODE, texto`, más `In:` y `Trace:` si procede) y
termina con código 1. Casos típicos: comando desconocido, número de argumentos
incorrecto, `CALC` con texto, plantilla de `STRFMT` inválida, llaves desequilibradas,
bloque sin cerrar, `ERROR` propio, `SOURCE`/`LOAD` de un fichero inexistente.

**No son errores:** leer una variable inexistente (da `""`), pedir un carácter o elemento
fuera de rango (da `""`), borrar algo que no existe. **Sí son errores** la división y el
módulo por cero en `CALC`.

### 14.1 `TRY … CATCH var … END`

Si algo falla dentro de `TRY`, la ejecución salta a `CATCH`; `var` recibe el texto
legible del diagnóstico. Lo que sigue al fallo dentro de `TRY` no se ejecuta.

```bcl
TRY
    PUTS "Antes del error"
    ERROR "fallo controlado"
    PUTS "Esto no se ejecuta"
CATCH err
    PUTS "Capturado: $err"
END
PUTS "El programa continua"
```
```text
Antes del error
Capturado: ?EVAL-F-USERERR, fallo controlado
El programa continua
```

### 14.2 `ERROR` y propagación

`ERROR "mensaje"` lanza un error `?EVAL-F-USERERR`. Un error no capturado dentro de un
procedimiento sube por la pila de llamadas hasta el primer `TRY` que lo capture.

```bcl
PROC dividir WITH a b DO
    IF $b == 0 THEN
        ERROR "No se puede dividir por cero"
    END
    RETURN [CALC $a / $b]
END
PROC nivel2 DO
    PUTS [dividir 8 0]          # no captura: se propaga
END
TRY
    PUTS [dividir 10 4]
    nivel2
CATCH e
    PUTS "Capturado arriba: $e"
END
```
```text
2.5
Capturado arriba: ?EVAL-F-USERERR, No se puede dividir por cero
```

### 14.3 Campos estructurados: `var_INFO(...)`

Además del texto, el kernel crea el array `var_INFO` (así no choca con el escalar
`var`). Campos: `TEXT` `SUMMARY` `FACILITY` `SEVERITY` `CODE` `FILE` `LINE` `COL` `PROC`
(procedimiento activo) `EVENT` `TRACE` `OSCODE` `OSMSG` `DISPLAY`.

```bcl
PROC p DO
    ERROR "fallo de ejemplo"
END
TRY
    p
CATCH err
    PUTS $err
    PUTS "$err_INFO(FACILITY) $err_INFO(SEVERITY) $err_INFO(CODE)"
    PUTS "$err_INFO(FILE) linea $err_INFO(LINE) col $err_INFO(COL)"
    PUTS "proc=$err_INFO(PROC) resumen=$err_INFO(SUMMARY)"
END
```
```text
?EVAL-F-USERERR, fallo de ejemplo
EVAL F USERERR
ejemplo.bcl linea 2 col 11
proc=p resumen=fallo de ejemplo
```

### 14.4 Patrones

Valida entradas con `TRY` + `CALC`; captura donde puedas hacer algo útil; no uses `TRY`
para el flujo normal; no dejes un `CATCH` vacío. Mensajes específicos
(`ERROR "El radio no puede ser negativo: $r"`).

```bcl
SET valores "-5 16 1500"
SET i 1
IF $i <= 3 DO
    SET v [LSTGET $valores $i]
    TRY
        IF $v < 0 THEN
            ERROR "valor negativo: $v"
        END
        SET r [STRFMT "%.4f" [CALC sqrt($v)]]
        PUTS "sqrt($v) = $r"
    CATCH e
        PUTS "aviso: $e"
    END
    INCR i
LOOP
```
```text
aviso: ?EVAL-F-USERERR, valor negativo: -5
sqrt(16) = 4.0000
sqrt(1500) = 38.7298
```

El formato del diagnóstico (`?FAC-SEV-CODE`) está garantizado para los comandos nativos del
kernel; un módulo puede usar sus propios mensajes. Recuerda el defecto de `EXIT n`
tras un error capturado (sección 10.1).

## 15. Ejercicios e idiomas

Programas completos, ejecutados tal cual.

### 15.1 FizzBuzz

```bcl
SET i 1
IF $i <= 15 DO
    IF [CALC $i % 15] == 0 THEN
        PUTS "FizzBuzz"
    ELSEIF [CALC $i % 3] == 0 THEN
        PUTS "Fizz"
    ELSEIF [CALC $i % 5] == 0 THEN
        PUTS "Buzz"
    ELSE
        PUTS $i
    END
    INCR i
LOOP
```
```text
1
2
Fizz
4
Buzz
Fizz
7
8
Fizz
Buzz
11
Fizz
13
14
FizzBuzz
```

### 15.2 Máximo común divisor y números primos

```bcl
PROC mcd WITH a b DO
    IF $b == 0 THEN
        RETURN $a
    END
    RETURN [mcd $b [CALC $a % $b]]
END
PROC es_primo WITH n DO
    IF $n < 2 THEN
        RETURN 0
    END
    SET d 2
    IF [CALC $d * $d] <= $n DO
        IF [CALC $n % $d] == 0 THEN
            RETURN 0
        END
        INCR d
    LOOP
    RETURN 1
END
PUTS [mcd 48 18]
SET primos ""
SET n 2
IF $n <= 30 DO
    IF [es_primo $n] THEN
        SET primos [LSTAPP $primos $n]
    END
    INCR n
LOOP
PUTS $primos
```
```text
6
2 3 5 7 11 13 17 19 23 29
```

### 15.3 Palíndromo con dos índices

```bcl
PROC palindromo WITH s DO
    SET i 1
    SET j [STRLENG $s]
    IF $i < $j DO
        IF [STREQ [STRGET $s $i] [STRGET $s $j]] == 0 THEN
            RETURN 0
        END
        INCR i
        INCRV j -1
    LOOP
    RETURN 1
END
PUTS [palindromo "reconocer"]
PUTS [palindromo "hola"]
```
```text
1
0
```

### 15.4 Idiomas de referencia

| Necesidad | Idioma |
|---|---|
| Contador | `INCR i` / `INCRV total $x` |
| Acumular texto | `SET s [STRAPP $s "x"]` desde `SET s ""` |
| Añadir a una lista | `SET l [LSTAPP $l "{$elem}"]` (envuelto si puede tener espacios) |
| Bucle infinito controlado | `IF 1 DO … BREAK … LOOP` |
| Búsqueda con «no encontrado» | bucle con `BREAK` y bloque `ELSE` |
| Pertenencia a un conjunto | `SET conj(x) 1` y `EXISTS conj(x)` |
| Valor por defecto | `SET p 8080` y, si `[EXISTS puerto]`, `SET p $puerto` (sección 12.1) |
| Condición con aritmética | `IF [CALC $n % 2] == 0 THEN` |
| Comparar texto exacto | `IF [STREQ $a $b] THEN` |
| Orden descendente | `LSTSORT` y recorrer del final al principio |
| Salir ante el fin de la entrada | `BREAK` si `[STREQ $d ""]` tras cada `GETS` (sección 6.2) |
| Portabilidad | probar con `bcl --no-rc` |

## Apéndice: defectos conocidos del kernel (build 7)

Verificados al preparar este manual; conviene tenerlos presentes hasta que se corrijan.

1. **`EXIT n` (n ≠ 0) tras un error capturado** termina con código 1 e imprime de nuevo el
   error ya capturado (sección 10.1).
2. **Grupos con cortocircuito en `IF`**: `IF (0 && x) …` o `IF (1 || x) …` dan
   `?EVAL-F-UNTERMCOND` (sección 7.2).
3. **Precisión**: `CALC`, `INCR` e `INCRV` escriben 6 cifras significativas (sección 4.2).
4. **`-3 ** 2`** se evalúa como `(-3) ** 2` con residuo imaginario (sección 4.1).

## Apéndice: notas para Windows

Este manual es común a los núcleos de Linux y Windows. En Windows:

- El fichero de inicio es `%USERPROFILE%\.bcl.rc` (donde el manual dice `$HOME/.bcl.rc`).
- En `BCL_LIBS` los directorios se separan con comas como siempre (`C:\bcl\lib,D:\mods`);
  un nombre que contiene `/` o `\` se usa tal cual.
- Las operaciones enteras de `CALC` son de 64 bits en todas las plataformas.
- Los módulos nativos (`.bll`) todavía no están disponibles, así que los comandos de módulos
  de la sección 11 solo importarán en una versión futura.
- Los ejemplos que leen argumentos o rutas funcionan sin cambios; usa `bcl.exe script.bcl`
  en lugar de `./bin/bcl script.bcl`.
