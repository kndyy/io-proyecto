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
strategy=phase-bucket
seed=20261109
max_evals=50000
max_neighbors=120
phase_global=auto
phase_lookahead=0
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
| `alns-joint` | ALNS con destrucción/reparación, prioridades por lote y rutas AGV mixtas | experimental |
| `phase-bucket` | ILS + despacho de AGVs guiado por fase y viajes de longitud adaptativa | por defecto, validada |
| `phase-joint` | BEJ-AGV: enumeración acotada de rutas completas + asignación conjunta de AGVs | experimental, no promovida |
| `phase-learned` | CEM aprende los pesos de las features de despacho sobre rutas phase-bucket | experimental, no promovida |

`phase-bucket` es la estrategia por defecto: superó los diez resultados históricos y los diez TXT fueron aceptados por IO1Lab. `integrated` y `alns-joint` se conservan como comparadores; `alns-joint` es experimental y sus rutas mixtas mostraron bloqueo excesivo. Mantiene el simulador y las reglas físicas, y añade una búsqueda ILS/VND con prioridad adaptativa de AGVs y búsqueda del tamaño de lote.

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
| 01 | 337.4091 | 326.6046 | +3.31% |
| 02 | 438.2044 | 404.0021 | +8.47% |
| 03 | 520.8947 | 475.9221 | +9.45% |
| 04 | 565.9533 | 525.9226 | +7.61% |
| 05 | 654.5154 | 591.3411 | +10.68% |
| 06 | 881.8119 | 754.2413 | +16.91% |
| 07 | 1073.4233 | 901.9320 | +19.01% |
| 08 | 1294.2152 | 1077.9824 | +20.06% |
| 09 | 1273.7104 | 958.7442 | +32.85% |
| 10 | 1300.1083 | 993.2925 | +30.89% |

Estos valores son la mejor solución aceptada por instancia en la plataforma, no una prueba de optimalidad global. El histórico de intentos y el detalle del algoritmo integrado están en `ALGORITMO_INTEGRADO.md`, `results/integrated_comparison.csv` y `results/platform_validation.csv`.

## Experimento ALNS con rutas mixtas

`alns-joint` implementa la especificación de ALNS con rutas `I`, `O` y `M`, prioridades por lote y tres reinicios. Es correcto y todos sus TXT pasan el validador, pero no supera a `integrated` salvo en la instancia 01, donde su TXT de 351.0 fue aceptado y reemplazó al de 355.8:

| Instancia | integrated | alns-joint 10k | alns-joint 50k |
|---|---:|---:|---:|
| 01 | 355.80 | 352.25 | 351.00 |
| 06 | 998.94 | 1165.86 | 1132.85 |
| 10 | 1488.67 | 1869.51 | 1836.46 |

La comparación completa está en `results/alns_joint_comparison.csv` y el análisis en `ALGORITMO_ALNS_RUTAS_MIXTAS.md`. El diagnóstico es que el decoder de rutas mixtas aumenta el bloqueo de `β`; más evaluaciones no cierran la brecha. La ablación del mismo ALNS con el decoder histórico sí mejora ligeramente, así que el siguiente trabajo debe centrarse en la política de AGVs, no en más iteraciones.

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
tools/            comparación, validación, diagnóstico, bench_joint, learn_dispatch,
                  rollout_first, counterfactual y key_ablation
instances/        diez instancias oficiales
materials/        paquete original recibido
results/          resultados, comparativas y validación en plataforma
submissions/      histórico de envíos TXT
proposed_submissions/ soluciones integradas validadas en IO1Lab
warmstart/        soluciones previas usadas solo como punto de partida
diagnostics/      errores antes/después y resumen del diagnóstico
ALGORITMO_INTEGRADO.md  descripción del método ILS + VND + AGV
ALGORITMO_ALNS_RUTAS_MIXTAS.md  método ALNS experimental y diagnóstico
```

## Estrategia phase-bucket (experimental)

Basada en el enfoque de asignación de tareas por fases de producción y agrupación (*bucketing*) de Bergmann, Kovács y Váncza (2026), adaptado al objetivo específico de minimizar la media de finalización de lotes y a buffers de capacidad unitaria.

La política construye rutas separadas de abastecimiento y retiro: **no usa rutas mixtas**. Cada AGV libre considera semillas para ambas fases, evalúa prefijos de ruta con 1 a C lotes y selecciona la ruta con mejor relación entre urgencia productiva y desplazamiento. La evaluación de salida penaliza bloqueos y demoras de lotes; la de entrada penaliza máquinas desabastecidas. El bucle ILS conserva la mejor solución factible y adapta el sesgo de fases, el tamaño de lote y las secuencias de máquinas. Con `phase_global=auto` se ejecutan dos variantes de asignación AGV (orden por ID y mejor ajuste global) y se conserva la mejor por instancia. `phase_lookahead=1` permite reservar una salida futura y esperar en `β` cuando no hay tareas actuales; ayuda en algunas instancias y perjudica en otras, por lo que la configuración final es por instancia (ver `results/platform_validation.csv`).

La salida oficial se genera únicamente al finalizar, y la optimización mantiene el estado en memoria. No reemplazar TXT aceptados hasta verificar que las nuevas medias mejoran en IO1Lab.

Prueba local:

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --input instances/instancia_09.io1 --strategy phase-bucket --seed 20261109 --max-evals 50000 --max-neighbors 120 --phase-global auto --txt /tmp/phase09.txt
python3 tools/validate_txt.py instances/instancia_09.io1 /tmp/phase09.txt
python3 tools/phase_monitor.py --evals 2500 --neighbors 35 --seed 20261109 --out monitor_output
```

El workflow `.github/workflows/phase-monitor.yml` compila, ejecuta los self-tests, compara las diez instancias y guarda CSV/TXT en artefactos temporales de Actions; la corrida diaria es corta y las actualizaciones del código ejecutan una comparación de 50 000 evaluaciones por instancia. **No envían nada a IO1Lab ni hacen commits de resultados.** El monitor diario usa semillas de ejecución variables y retiene los archivos 30 días. La estrategia predeterminada en `selector.conf` es `phase-bucket`. Los diez TXT de 50k fueron enviados con Playwright y aceptados por IO1Lab; la publicación manual no se automatiza.

## Estrategia `phase-learned` (experimental, no promovida)

`phase-learned` conserva el generador y la factibilidad de `phase-bucket`, pero reemplaza el ranking de rutas por una combinación lineal de nueve features: score antiguo, bloqueos, máquinas ociosas, viaje vacío/cargado, fill, edad de salidas, espera y exposición. Los pesos se optimizan offline con CEM (`tools/learn_dispatch.cpp`) sobre secuencias congeladas; el buscador ILS/VND existente se reutiliza. El binario carga los pesos con `--policy-file`.

Resultado de la gate A2 tras corregir el bug de `best.score` en la asignación global:

- Entrenamiento separado: 09 con asignación global y 10 con `auto`; snapshots congelados de 20k, cinco semillas, tres para entrenamiento y dos para holdout.
- Se midieron las distribuciones de las nueve features, se normalizaron las features 1–8 y se probó cada coeficiente individualmente en `±0.2, ±0.05, ±0.01`.
- Coordenadas y CEM reparado (64 candidatos, 10 élites, 40 generaciones) mantuvieron el baseline `{1,0,0,0,0,0,0,0,0}` como mejor vector. El holdout dio ratio `1.000` en 09 y 10.
- No hubo mejora de 1.5% en ninguna de las dos instancias; la gate A2 falla y no se promueve.
- Los pesos `{1,0,...}` con `phase-learned` reproducen exactamente `phase-bucket` en las regresiones global 0 y global 1 a 1k.

Rollout: se probó una versión preliminar de una sola decisión (primer despacho, replay completo desde `t=0`) en `tools/rollout_first.cpp`. No mejoró 09/10; las alternativas fueron iguales o peores. El rollout multievento con estado clonable no se implementó en esta iteración. No se envió nada a IO1Lab.

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/learn_dispatch.cpp -o learn_dispatch
./learn_dispatch 9 20000
./learn_dispatch 10 20000
./io1 --input instances/instancia_09.io1 --strategy phase-learned \
  --seed 20261109 --max-evals 20000 --max-neighbors 120 \
  --policy-file experiments/local/policy_9.txt
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/rollout_first.cpp -o rollout_first
./rollout_first 9 20261109 5000
```

Los resultados de entrenamiento y rollout quedan en `experiments/local/` (ignorado por git).

## Oráculo contrafactual y gate Q0

El simulador expone `DispatchAudit` y `DispatchOverride` opcionales. La traza solo se activa para candidatos `phase-bucket` y no altera la simulación normal. `tools/counterfactual_dispatch.cpp` congela una agenda, elige acciones críticas, reejecuta desde `t=0` con una sola acción sustituida y escribe filas de resultados; `tools/bench_counterfactual.py` ejecuta la matriz.

Verificación:

- Traza contra simulación normal: media y vector de finalizaciones idénticos a `1e-9` en las 32 agendas congeladas.
- El control que fuerza la acción baseline reproduce exactamente la agenda original.
- Un override inválido (AGV o fingerprint incorrectos) se rechaza explícitamente y no se reporta como alternativo.
- Todas las alternativas ejecutadas fueron factibles.

Resultados del gate Q0:

| Etapa | Snapshots | Alternativas válidas | Mejoras ≥0.5% | Semillas con mejora (09 / 10) | Bandas (09 / 10) | Veredicto |
|---|---:|---:|---:|---:|---|---|
| 5k | 20 | 1196 | 5 | 0/5 / 1/5 | ninguna / 1,9,11 | FAIL |
| 20k | 12 | 720 | 0 | 0/3 / 0/3 | ninguna / ninguna | FAIL |

El gate exige 3 de 5 semillas con ≥0.5% en cada instancia y dos bandas; no se cumple. No se implementa el rollout con clonación de estado. Los CSV quedan en `experiments/local/q0_5k_final/` y `experiments/local/q0_20k_final/` (ignorados por git).

## Fallback K1 — transporte por claves

Se añadió la puntuación opt-in por claves por lote y una ablación de 120 vectores aleatorios por semilla/modo sobre agendas congeladas. Las claves neutras reproducen exactamente `phase-bucket`.

| Instancia | Semillas con mejor candidato ≥0.5% | Mejor ganancia |
|---|---:|---:|
| 09 | 0/5 | 0.098% |
| 10 | 1/5 | 0.619% |

K1 exige 3 de 5 semillas; falla. No se implementó la búsqueda memética completa ni se expuso `phase-keys` como estrategia; el selector y los TXT aceptados permanecen intactos. Resultados locales en `experiments/local/key_ablation_final.csv`.

Comandos:

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/counterfactual_dispatch.cpp -o /tmp/io1-counterfactual
python3 tools/bench_counterfactual.py --freeze-evals 5000 --output experiments/local/q0_5k_final
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic -I. tools/key_ablation.cpp -o /tmp/key_ablation
/tmp/key_ablation --instance 09 --seed 20261109 --freeze-evals 5000 --mode 0 --samples 120 --strength 0.75
```

## Estrategia `phase-joint` (experimental, no promovida)

Implementa BEJ-AGV sobre el simulador existente: enumera rutas completas de 1 a C paradas con un shortlist acotado, conserva la ruta greedy anterior como candidata y resuelve una asignación conjunta de AGVs con branch-and-bound de dos máscaras. No cambia el simulador físico, el TXT ni `selector.conf`.

Resultados de las etapas manuales:

- Ablación con la misma agenda congelada (09, 10, 06, 08 × tres semillas): `R` y `J` fueron peores que `G0`/`G1` en todas las combinaciones; el porcentaje de eventos con más de un AGV libre fue 9–23%.
- Etapa C (20k) en 09/10: `phase-joint` ganó en 2 de 6 ejecuciones por ≥2%, pero perdió en una y el tiempo fue 8–12× el baseline de igual presupuesto. La compuerta de runtime falla y la de calidad no es consistente.
- Fallbacks: F1 (solo salidas enumeradas) y F2 (desbloqueo duro) fueron peores en 09/10 a 5k. No se promueve ni se envió nada a IO1Lab.

Diagnóstico reproducible:

```bash
./io1 --input instances/instancia_09.io1 --dispatch-ablation --seed 20261109
./io1 --input instances/instancia_09.io1 --strategy phase-joint --seed 20261109 --max-evals 5000 --max-neighbors 120
python3 tools/bench_joint.py --stage A --output experiments/local
```

Los resultados crudos quedan en `experiments/local/` (ignorado por git). Las banderas `--phase-outbound-only` y `--phase-hard-unblock` existen solo para las ablaciones de fallback; no son el valor por defecto.

Referencia: Júlia Bergmann, András Kovács y József Váncza, “AGV scheduling in shopfloor logistics of semiconductor assembly”, *Journal of Manufacturing Systems*, 87 (2026), 27–47, DOI: 10.1016/j.jmsy.2026.05.004. La función de prioridad, las vecindades y el bloqueo son adaptaciones del proyecto, no una reproducción exacta del algoritmo del artículo.

