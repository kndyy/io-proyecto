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
strategy=integrated
seed=20261109
max_evals=50000
max_neighbors=120
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
| `integrated` | greedy + ILS con VND `RELOCATE`, `SWAP` y `DISPATCH` | despacho AGV adaptativo |

`integrated` es la estrategia por defecto. Mantiene el simulador y las reglas físicas, y añade una búsqueda ILS/VND con prioridad adaptativa de AGVs y búsqueda del tamaño de lote. Las estrategias anteriores se conservan para comparación.

Cada estrategia tipo `vnd` prueba `batch_limit=1` y `batch_limit=C`, elige el mejor inicio factible y conserva el mejor resultado global. La búsqueda usa first improvement, 40 vecinos por muestra y hasta 1000 simulaciones por inicio. `integrated` usa `seed=20261109`, `max_evals=50000` y `max_neighbors=120` por defecto.

## Helper externo

`tools/io1_helper.py` es una herramienta de consola, separada del código C++, para comparar todas las soluciones:

```bash
python3 tools/io1_helper.py build
python3 tools/io1_helper.py test
python3 tools/io1_helper.py bench
python3 tools/io1_helper.py bench --strategies b1,b2,b3,s0,s1,s2,vnd,vnd-s0,vnd-s1,vnd-s2
python3 tools/io1_helper.py run --instance instances/instancia_10.io1 --strategy vnd --out results/internal/resultado_10.csv
python3 tools/validate_txt.py instances/instancia_01.io1 submissions/instancia_01.txt
python3 tools/diagnose_txt.py
python3 tools/make_submissions.py
```

`bench` escribe `results/results.csv`, imprime el ranking y genera `results/recommended_selector.conf` con la estrategia de menor razón media frente al mejor resultado de cada instancia.

## Envíos TXT para IO1Lab

El binario exporta el formato oficial con `--txt`:

```bash
./io1 --input instances/instancia_01.io1 --strategy vnd --txt submissions/instancia_01.txt
./io1 --input instances/instancia_01.io1 --from-txt warmstart/instancia_01.txt \
     --max-evals 30000 --max-neighbors 1000 --seed 20261106 \
     --txt submissions/instancia_01.txt
```

`--from-txt` recupera solo el orden de procesos por máquina de un envío previo y vuelve a simular el despacho de AGVs con el simulador actual.

Los envíos generados están en `submissions/`:

| Archivo | Media | Configuración |
|---|---:|---|
| `submissions/instancia_01.txt` | 359.050000 | 30000 eval, 1000 vecinos, seed 20261106, warm start |
| `submissions/instancia_02.txt` | 464.691965 | 30000 eval, 1000 vecinos, seed 20261106, warm start |
| `submissions/instancia_03.txt` | 577.834941 | 30000 eval, 1000 vecinos, seed 20261106, warm start |
| `submissions/instancia_04.txt` | 601.000415 | 30000 eval, 1000 vecinos, seed 20261106, warm start |
| `submissions/instancia_05.txt` | 727.121474 | 30000 eval, 1000 vecinos, seed 12345, warm start |
| `submissions/instancia_06.txt` | 1034.450920 | 30000 eval, 1000 vecinos, seed 9001, warm start |
| `submissions/instancia_07.txt` | 1248.372728 | 30000 eval, 1000 vecinos, seed 3, warm start |
| `submissions/instancia_08.txt` | 1490.944745 | 30000 eval, 1000 vecinos, seed 20261106, warm start |
| `submissions/instancia_09.txt` | 1558.192027 | 30000 eval, 1000 vecinos, seed 42, warm start |
| `submissions/instancia_10.txt` | 1549.173382 | 30000 eval, 1000 vecinos, seed 20261106, warm start |

`submissions/` es el histórico del primer enfoque. Las soluciones mejoradas y validadas en la plataforma están en `proposed_submissions/` y en `results/platform_validation.csv`.

`submissions/ejemplo_minimo.txt` reproduce el ejemplo canónico de un lote de la documentación. El ejemplo verificado de 1062 s no estaba incluido completo en los materiales recibidos.

Cada envío se valida con `tools/validate_txt.py`, que comprueba cabecera, columnas, recursos canónicos, capacidades, posiciones, movimientos, buffers y precedencias, y recalcula la media desde las entregas en `f`. `tools/make_submissions.py` regenera los diez archivos y `submissions/manifest.csv`.


## Validación oficial en IO1Lab

Grupo `FED`. Los diez `proposed_submissions/` fueron subidos con Playwright y aceptados por la plataforma el 2026-11-09. El detalle está en `results/platform_validation.csv`.

| Instancia | Valor aceptado | Mejor público | Gap |
|---|---:|---:|---:|
| 01 | 355.8000 | 327.1137 | +8.77% |
| 02 | 450.3642 | 412.6585 | +9.14% |
| 03 | 566.7783 | 483.0249 | +17.34% |
| 04 | 591.5508 | 528.4404 | +11.94% |
| 05 | 716.0324 | 606.0450 | +18.15% |
| 06 | 998.9354 | 760.2939 | +31.39% |
| 07 | 1207.7515 | 905.6949 | +33.35% |
| 08 | 1426.5020 | 1077.9824 | +32.33% |
| 09 | 1477.9573 | 989.9936 | +49.29% |
| 10 | 1488.6675 | 1001.9073 | +48.58% |

Estos valores son la mejor solución aceptada por instancia en la plataforma, no una prueba de optimalidad global. El histórico de intentos y el detalle del algoritmo integrado están en `ALGORITMO_INTEGRADO.md`, `results/integrated_comparison.csv` y `results/platform_validation.csv`.

## Diagnóstico de factibilidad

El checker rechazó el primer envío con:

```text
lot 3 starts PROCESS on M2 at 214.727 before machine release at 214.727
MACHINE_OR_BUFFER
```

El diagnóstico se ejecuta con `python3 tools/diagnose_txt.py`:

- `diagnostics/diagnostico_antes.txt`: 98 errores en las 10 instancias, todos `MACHINE_OR_BUFFER`.
- `diagnostics/diagnostico.txt`: 0 errores con tolerancia `1e-12` después de la corrección.
- `diagnostics/diagnostico_resumen.txt`: causa raíz y criterio de corrección.

Causa: el proceso siguiente podía empezar exactamente en el fin del anterior. Al imprimir con 9 decimales, el checker reconstruía el fin como `inicio_anterior + q/v`, que quedaba hasta `2.7e-10` por encima del nuevo `inicio` impreso, y rechazaba la igualdad numérica.

Corrección: se aplica un margen sistemático de `1e-8 s` en las transiciones recurso-liberado → acción dependiente (inicio de proceso tras proceso, entrega en alpha o desbloqueo; acción de AGV tras un MOVE) y se exporta con 12 decimales. No hay parches por instancia: es un invariante del simulador. El margen mínimo medido en los envíos es aproximadamente `1e-8 s`.

El validador TXT comprueba consistencia y factibilidad, no optimalidad. La calidad sigue dependiendo de la búsqueda; el siguiente paso es mejorar el objetivo, no la legalidad.

## Resultados actuales

Diez instancias oficiales, semilla `20261106`, presupuesto académico fijo.

| Instancia | B1 | B2 | B3 | S0 | S1 | S2 | VND | Mejora vs mejor baseline | CPU VND (ms) |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 01 | 524.10 | 449.20 | 453.00 | 401.93 | 401.93 | 392.77 | **367.61** | 18.16% | 30.4 |
| 02 | 770.46 | 681.16 | 567.55 | 518.37 | 518.37 | 564.70 | **488.19** | 13.98% | 22.1 |
| 03 | 1063.89 | 943.46 | 715.28 | 651.50 | 648.30 | 644.79 | **600.70** | 16.02% | 30.9 |
| 04 | 1380.06 | 1236.00 | 712.51 | 666.18 | 683.79 | 697.97 | **621.47** | 12.78% | 89.3 |
| 05 | 1796.55 | 1649.41 | 874.77 | 837.18 | 822.41 | 825.63 | **770.23** | 11.95% | 77.3 |
| 06 | 2253.57 | 2005.30 | 1214.78 | 1173.05 | 1155.76 | 1168.12 | **1100.86** | 9.38% | 100.4 |
| 07 | 2799.67 | 2433.14 | 1429.60 | 1417.94 | 1426.06 | 1389.59 | **1331.29** | 6.88% | 63.3 |
| 08 | 2436.29 | 2333.21 | 1717.29 | 1742.02 | 1744.86 | 1623.53 | **1596.17** | 7.05% | 189.1 |
| 09 | 2877.88 | 2824.82 | 1860.98 | 1780.03 | 1760.95 | 1798.54 | **1656.80** | 10.97% | 145.1 |
| 10 | 3276.23 | 3186.61 | 1767.67 | 1795.54 | 1744.65 | 1819.36 | **1620.88** | 8.30% | 340.8 |

| Estrategia | Factibles | Razón media vs mejor | CPU media (ms) | Evaluaciones |
|---|---:|---:|---:|---:|
| b1 | 10/10 | 1.87628 | 0.15 | 10 |
| b2 | 10/10 | 1.71001 | 0.21 | 10 |
| b3 | 10/10 | 1.13348 | 0.16 | 10 |
| s0 | 10/10 | 1.08028 | 0.38 | 20 |
| s1 | 10/10 | 1.07559 | 0.39 | 20 |
| s2 | 10/10 | 1.08236 | 3.20 | 20 |
| vnd | 10/10 | 1.00000 | 108.88 | 10984 |

El detalle por ejecución está en `results/results.csv`. `--out` genera un CSV interno; `--txt` genera el archivo oficial y `python3 tools/validate_txt.py <instancia.io1> <envio.txt>` lo valida antes de subirlo.

## Estructura

```text
main.cpp          lectura de argumentos, selector, self-tests y salida
instance.hpp      lectura y validación del formato .io1
simulator.hpp     simulación determinista de máquinas, buffers y AGVs
heuristic.hpp     baselines, construcciones, vecindades y VND
selector.conf     selector externo en tiempo de ejecución
tools/            comparación, generación, validación y diagnóstico
instances/        diez instancias oficiales
materials/        paquete original recibido
results/          resultados, comparativas y validación en plataforma
submissions/      histórico de envíos TXT
proposed_submissions/ soluciones integradas validadas en IO1Lab
warmstart/        soluciones previas usadas solo como punto de partida
diagnostics/      errores antes/después y resumen del diagnóstico
ALGORITMO_INTEGRADO.md  descripción del método ILS + VND + AGV
```
