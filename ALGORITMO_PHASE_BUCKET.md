# Phase-aware bucketing para AGVs — Investigación de Operaciones I

## Estado e intención

Estrategia experimental `phase-bucket` en `main`, implementada en C++20, sin dependencias externas. Conserva las estrategias originales (`integrated`, `alns-joint`, VND y baselines), el simulador de eventos y el formato TXT ya aceptado.

El cambio es en el **decoder de despacho de AGVs**, no en las restricciones: prioriza desbloqueo de máquinas, desabastecimiento y entregas según fase productiva, y escoge dinámicamente entre 1 y C cargas por recorrido. Las rutas siempre tienen una única fase: **entrada** (`o -> alpha`) o **salida** (`beta -> f`), para evitar la congestión observada en rutas mixtas.

## Fundamento bibliográfico

- J. Bergmann, A. Kovács y J. Váncza (2026), "AGV scheduling in shopfloor logistics of semiconductor assembly", *Journal of Manufacturing Systems* **87**, 27–47. DOI: [10.1016/j.jmsy.2026.05.004](https://doi.org/10.1016/j.jmsy.2026.05.004). El artículo explora agrupaciones (*bucketing*) y prioridades conscientes de la fase productiva para logística AGV en ensamblaje de semiconductores.
- Liang, Zhou y Jiang (2024 publicación en línea; volumen de 2025), "Integrated scheduling of production and material delivery for the intelligent manufacturing system", *International Journal of Production Research*. DOI: [10.1080/00207543.2024.2363435](https://doi.org/10.1080/00207543.2024.2363435). Justifica coordinar secuencias de producción y despacho mediante búsqueda por vecindades.
- R. Ruiz y T. Stützle (2007), "A simple and effective iterated greedy algorithm for the permutation flowshop scheduling problem", *European Journal of Operational Research* **177**(3), 2033–2049. DOI: [10.1016/j.ejor.2005.12.009](https://doi.org/10.1016/j.ejor.2005.12.009). Sustenta destrucción y reinserción como mecanismo de diversificación.

La fórmula implementada es una **adaptación propia al enunciado IO1**. No reproduce exactamente las políticas del artículo de Bergmann et al., cuyo experimento principal mide rendimiento de producción; IO1 minimiza promedio de finalizaciones.

## Modelo matemático

Con los lotes `i ∈ I`, máquinas compatibles `j ∈ J`, vehículos `k ∈ K`, procesamientos `p_ij = q_i / v_{j,r_i}`, distancia temporal `tau[u][v]`, y entrega final `c_i`:

`min F = (sum_i c_i) / |I|`

Restricciones: asignación única y compatible, una sola operación por máquina, no interrupción, buffers alpha/beta unitarios, bloqueo si beta no está libre, desplazamientos según tau, y carga AGV nunca mayor a C. Inicio: máquinas y buffers vacíos, AGVs en o.

Una solución es `X = (secuencias_máquina, modo_transporte, sesgo_fase, limite_carga)`.

## Heurística de despacho de rutas

En cada evento, para cada AGV libre:

1. Generar semillas factibles de entrada en alpha y de salida en beta. Ignorar buffers reservados.
2. Para cada semilla, extender con la máquina factible de mayor score hasta longitud C y evaluar cada prefijo (1…C).
3. Elegir el prefijo y tipo de ruta de mayor puntaje `S(R)`.
4. Reservar todos los buffers de la ruta ganadora, emitir eventos MOVE/PICKUP/DELIVER, y actualizar estados al llegar.

Definimos `H = max(1, medianTau)`, `b ∈ [-5,5]` como sesgo de salida, `t_j` como duración acumulada de viaje hasta visitar la máquina j y `T_R` como duración total de la ruta. `w_j = now - outputSince_j` mide permanencia en beta.

Para cada recogida de salida de máquina j:

`U_out(j) = H*(16 si BLOCKED, en otro caso 6) + 2H*(hay trabajo pendiente) + 0.2*w_j + b*H - 0.3*t_j`

Para cada abastecimiento de entrada de la máquina j:

`U_in(j) = H*(10 si IDLE, en otro caso 5) + H/(1+p_ij/H) - b*H - 0.35*max(0, t_j-ready_j) - 4H*(beta ocupado)`

con `ready_j = 0` si IDLE, el tiempo residual de procesamiento si PROCESSING y `2H` si BLOCKED.

El score normalizado de la ruta es:

`S(R) = [sum_{j∈R} U(j) - 1.5*T_R] / [H+T_R]`.

Los coeficientes son parámetros heurísticos fijos de la implementación, no parámetros físicos ni resultados estimados por un paper. El sesgo b y el límite de carga se comparan mediante **simulaciones reales**; la función objetivo que determina la aceptación continúa siendo la media exacta de entregas, nunca el score aproximado.

### Lookahead de salida

Con `phase_lookahead=1` se permite una semilla de salida futura cuando no hay tareas actuales: la máquina está procesando, `β` está libre y el lote actual aún no terminó. El AGV reserva esa salida, espera en su posición si hace falta y programa el viaje para llegar a `β` cuando termina el proceso. La extensión de la ruta solo usa salidas ya presentes; no se encadenan varias esperas futuras.

El lookahead ayuda en 01, 02, 04, 06, 07 y 08, pero perjudica en 03, 05, 09 y 10. Por eso la configuración final es por instancia y `phase_lookahead=0` queda como valor por defecto.

## Búsqueda y diversificación

- Inicialización: `integrated` con un tercio de las evaluaciones.
- Explorar los 11 sesgos b entre -5 y 5, para cada capacidad de 1 a C, preservando la mejor solución.
- VND: movimientos `RELOCATE`, `SWAP` y cambio de sesgo/capacidad, aceptando una mejora estricta de F.
- ILS: perturbar 2–6 lotes o destruir/reinsertar una fracción; reoptimizar, conservar siempre el mejor encontrado; aceptar ocasionalmente otra solución factible como estado de exploración.
- Evaluación siempre desde el simulador determinista; el TXT se escribe únicamente para el candidato final.
- Consistencia: el candidato de `integrated` se vuelve a simular con el decoder de fase antes de compararlo. Así la búsqueda nunca mezcla medias de decoders distintos.
- Asignación AGV: `phase_global=0` despacha en orden de ID; `phase_global=1` elige globalmente el mejor par AGV-ruta; `phase_global=auto` ejecuta ambas variantes y conserva la mejor por instancia. En `auto`, `--max-evals` se aplica a cada variante.

## Verificación y seguimiento

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
python3 tools/phase_monitor.py --evals 2500 --neighbors 35 --seed 20261109
./io1 --input instances/instancia_09.io1 --strategy phase-bucket --seed 20261109 --max-evals 50000 --max-neighbors 120 --phase-global auto --txt /tmp/phase09.txt
python3 tools/validate_txt.py instances/instancia_09.io1 /tmp/phase09.txt
```

El monitor valida los diez TXT con `tools/validate_txt.py`, compara frente a la referencia aceptada en IO1Lab y frente a `integrated`, guarda CSV de mejora/tiempo de CPU y conserva artefactos temporales en GitHub Actions. No sube resultados a IO1Lab, no cambia `selector.conf`, no sustituye `submissions/`, y no introduce lectura de TXT intermedios en C++.

## Alcance de las conclusiones

En la prueba reproducible de 50 000 evaluaciones con semilla 20261109 se superaron los diez mejores valores aceptados previamente. La comparación siguió usando los diez verificadores de factibilidad. Esta tabla es evidencia de una ejecución, no una garantía de optimalidad ni de superioridad para cualquier semilla.

| Instancia | Referencia inicial | Phase-bucket final | Configuración |
|---|---:|---:|---|
| 01 | 351.0000 | 337.4091 | auto 50k, lookahead |
| 02 | 450.3642 | 438.2044 | auto 50k, lookahead |
| 03 | 566.7783 | 520.8947 | auto 50k, seed 55555 |
| 04 | 591.5508 | 565.9533 | auto 50k, lookahead |
| 05 | 716.0324 | 654.5154 | id 50k |
| 06 | 998.9354 | 902.4970 | auto 50k, lookahead |
| 07 | 1207.7515 | 1073.4233 | auto 50k, lookahead |
| 08 | 1426.5020 | 1294.2152 | auto 50k, lookahead |
| 09 | 1477.9573 | 1273.7104 | global 500k |
| 10 | 1488.6675 | 1300.1083 | auto 50k, seed 12345 |

El benchmark inicial de 50k está en [GitHub Actions — monitor 50k](https://github.com/kndyy/io-proyecto/actions/runs/42674605687). Después se probaron los dos modos de asignación, lookahead, semillas alternativas y presupuestos de 100k/500k donde aportaban. Los diez TXT finales fueron enviados con Playwright y aceptados por IO1Lab; los valores oficiales y la configuración por instancia están en `results/platform_validation.csv`.

## Auditoría de alns-joint

En el cálculo del score de `buildInputPhase` faltaba `tau[o][alpha_semilla]`. Se añadió el tramo para puntuar rutas de forma consistente con sus desplazamientos físicos. El validador local confirma restricciones, pero no certifica que el score de rutas prediga bien el promedio de finalización; ese desajuste explica parte de las limitaciones de la estrategia mixta.
