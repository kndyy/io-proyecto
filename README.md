# io-proyecto

Implementación en C++20 de asignación de lotes, secuenciación por máquina y despacho de AGVs.
Solo usa la biblioteca estándar.

Repositorio: https://github.com/kndyy/io-proyecto

## Compilar y verificar

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
```

`--self-test` comprueba el caso de un lote, el bloqueo del buffer de salida y una simulación de las diez instancias.

## Selector externo

El algoritmo se elige en tiempo de ejecución con `selector.conf`. Ese archivo no se compila ni forma parte del binario:

```ini
strategy=vnd
seed=20261106
max_evals=1000
max_neighbors=40
```

También puede vivir fuera del repositorio:

```bash
./io1 --input instances/instancia_01.io1 --selector /ruta/externa/selector.conf
```

La línea de comandos tiene prioridad sobre el selector:

```bash
./io1 --input instances/instancia_01.io1 --strategy s2 --seed 20261106
```

Para cambiar de solución no hace falta recompilar.

## Estrategias disponibles

| Nombre | Construcción | Búsqueda local |
|---|---|---|
| `b1` | FIFO, máquina más rápida | no |
| `b2` | SPT, máquina más rápida | no |
| `b3` | FIFO, balanceo de carga | no |
| `s0` | orden por altura `h_i`, asignación por `score(i,j)` | no |
| `s1` | orden SPT, asignación por `score(i,j)` | no |
| `s2` | greedy aleatorizado restringido, asignación por `score(i,j)` | no |
| `vnd` | mejores de `s0`, `s1`, `s2` con ambos límites de carga | RELOCATE + SWAP |
| `vnd-s0` | solo inicio `s0` | RELOCATE + SWAP |
| `vnd-s1` | solo inicio `s1` | RELOCATE + SWAP |
| `vnd-s2` | solo inicio `s2` | RELOCATE + SWAP |

Cada estrategia con búsqueda local prueba `batch_limit=1` y `batch_limit=C`, elige el mejor inicio factible y conserva el mejor resultado global. La búsqueda usa first improvement, 40 vecinos por muestra y hasta 1000 simulaciones por inicio.

## Helper externo

`tools/io1_helper.py` es una herramienta de consola, separada del código C++, para comparar todas las soluciones:

```bash
python3 tools/io1_helper.py build
python3 tools/io1_helper.py test
python3 tools/io1_helper.py bench
python3 tools/io1_helper.py bench --strategies b1,b2,b3,s0,s1,s2,vnd,vnd-s0,vnd-s1,vnd-s2
python3 tools/io1_helper.py run --instance instances/instancia_10.io1 --strategy vnd --out results/internal/resultado_10.csv
```

`bench` escribe `results/results.csv`, imprime el ranking y genera `results/recommended_selector.conf` con la estrategia de menor razón media frente al mejor resultado de cada instancia.

## Envíos TXT para IO1Lab

El binario exporta el formato oficial con `--txt`:

```bash
./io1 --input instances/instancia_01.io1 --strategy vnd --txt submissions/instancia_01.txt
```

Los envíos generados están en `submissions/`:

| Archivo | Media | Configuración |
|---|---:|---|
| `submissions/instancia_01.txt` | 359.050000 | 1000 eval, 200 vecinos, seed 20261106 |
| `submissions/instancia_02.txt` | 464.691964 | 30000 eval, 1000 vecinos, seed 9001 |
| `submissions/instancia_03.txt` | 577.834940 | 10000 eval, 500 vecinos, seed 9001 |
| `submissions/instancia_04.txt` | 602.349363 | 10000 eval, 500 vecinos, seed 3 |
| `submissions/instancia_05.txt` | 715.949648 | 30000 eval, 1000 vecinos, seed 9001 |
| `submissions/instancia_06.txt` | 1039.823463 | 10000 eval, 500 vecinos, seed 9001 |
| `submissions/instancia_07.txt` | 1258.691111 | 30000 eval, 1000 vecinos, seed 55555 |
| `submissions/instancia_08.txt` | 1487.776149 | 30000 eval, 1000 vecinos, seed 9001 |
| `submissions/instancia_09.txt` | 1562.029433 | 30000 eval, 1000 vecinos, seed 9001 |
| `submissions/instancia_10.txt` | 1549.265496 | 30000 eval, 1000 vecinos, seed 3 |

`submissions/ejemplo_minimo.txt` reproduce el ejemplo canónico de un lote de la documentación. El ejemplo verificado de 1062 s no estaba incluido completo en los materiales recibidos.

Cada envío se valida con `tools/validate_txt.py`, que comprueba cabecera, columnas, recursos canónicos, capacidades, posiciones, movimientos, buffers y precedencias, y recalcula la media desde las entregas en `f`. `tools/make_submissions.py` regenera los diez archivos y `submissions/manifest.csv`.

## Resultados actuales

Diez instancias oficiales, semilla `20261106`, presupuesto académico fijo.

| Instancia | B1 | B2 | B3 | S0 | S1 | S2 | VND | Mejora vs mejor baseline | CPU VND (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 01 | 524.10 | 449.20 | 453.00 | 401.93 | 401.93 | 392.77 | **367.61** | 18.16% | 19.3 |
| 02 | 770.46 | 681.16 | 564.46 | 518.37 | 518.37 | 564.70 | **488.19** | 13.51% | 19.9 |
| 03 | 1063.89 | 943.46 | 715.28 | 650.01 | 648.30 | 644.79 | **600.70** | 16.02% | 35.4 |
| 04 | 1380.06 | 1236.00 | 710.20 | 666.18 | 683.79 | 697.97 | **629.76** | 11.33% | 34.1 |
| 05 | 1796.55 | 1650.40 | 874.77 | 828.15 | 863.41 | 828.10 | **774.55** | 11.46% | 48.9 |
| 06 | 2224.82 | 1986.91 | 1214.78 | 1173.05 | 1183.54 | 1168.12 | **1113.39** | 8.35% | 54.5 |
| 07 | 2745.05 | 2524.94 | 1429.60 | 1417.94 | 1425.80 | 1387.94 | **1314.55** | 8.05% | 141.2 |
| 08 | 2436.29 | 2333.21 | 1718.34 | 1742.02 | 1744.52 | 1628.46 | **1600.04** | 6.88% | 136.7 |
| 09 | 2877.88 | 2789.09 | 1859.62 | 1693.10 | 1760.95 | 1785.95 | **1618.11** | 12.99% | 253.2 |
| 10 | 3276.10 | 3186.61 | 1810.99 | 1795.54 | 1708.63 | 1838.49 | **1636.28** | 9.65% | 225.0 |

| Estrategia | Factibles | Razón media vs mejor | CPU media (ms) | Evaluaciones |
|---|---:|---:|---:|---:|
| b1 | 10/10 | 1.86757 | 0.11 | 10 |
| b2 | 10/10 | 1.71160 | 0.16 | 10 |
| b3 | 10/10 | 1.13450 | 0.12 | 10 |
| s0 | 10/10 | 1.07290 | 0.29 | 20 |
| s1 | 10/10 | 1.08054 | 0.27 | 20 |
| s2 | 10/10 | 1.08261 | 3.00 | 20 |
| vnd | 10/10 | 1.00000 | 96.81 | 10689 |

El detalle por ejecución está en `results/results.csv`. `--out` genera un CSV interno; `--txt` genera el archivo oficial y `python3 tools/validate_txt.py <instancia.io1> <envio.txt>` lo valida antes de subirlo.

## Estructura

```text
main.cpp          lectura de argumentos, selector, self-tests y salida
instance.hpp      lectura y validación del formato .io1
simulator.hpp     simulación determinista de máquinas, buffers y AGVs
heuristic.hpp     baselines, construcciones, vecindades y VND
selector.conf     selector externo en tiempo de ejecución
tools/            comparación, generación y validación de envíos
instances/        diez instancias oficiales
materials/        paquete original recibido
results/          resultados y selector recomendado
submissions/      envíos TXT por instancia y ejemplo mínimo
```
