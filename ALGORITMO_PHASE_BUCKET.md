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

## Búsqueda y diversificación

- Inicialización: `integrated` con un tercio de las evaluaciones.
- Explorar los 11 sesgos b entre -5 y 5, para cada capacidad de 1 a C, preservando la mejor solución.
- VND: movimientos `RELOCATE`, `SWAP` y cambio de sesgo/capacidad, aceptando una mejora estricta de F.
- ILS: perturbar 2–6 lotes o destruir/reinsertar una fracción; reoptimizar, conservar siempre el mejor encontrado; aceptar ocasionalmente otra solución factible como estado de exploración.
- Evaluación siempre desde el simulador determinista; el TXT se escribe únicamente para el candidato final.

## Verificación y seguimiento

```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
python3 tools/phase_monitor.py --evals 2500 --neighbors 35 --seed 20261109
./io1 --input instances/instancia_09.io1 --strategy phase-bucket --seed 20261109 --max-evals 50000 --max-neighbors 120 --txt /tmp/phase09.txt
python3 tools/validate_txt.py instances/instancia_09.io1 /tmp/phase09.txt
```

El monitor valida los diez TXT con `tools/validate_txt.py`, compara frente a la referencia aceptada en IO1Lab y frente a `integrated`, guarda CSV de mejora/tiempo de CPU y conserva artefactos temporales en GitHub Actions. No sube resultados a IO1Lab, no cambia `selector.conf`, no sustituye `submissions/`, y no introduce lectura de TXT intermedios en C++.

## Alcance de las conclusiones

Se ha observado mejora local en nueve de diez instancias respecto a los mejores envíos aceptados con 2 500 evaluaciones. No hay garantía de optimalidad ni demostración de que el enfoque sea superior para todas las semillas o presupuestos. La validación de IO1Lab sigue siendo necesaria antes de publicar nuevos resultados.

## Auditoría de alns-joint

En el cálculo del score de `buildInputPhase` faltaba `tau[o][alpha_semilla]`. Se añadió el tramo para puntuar rutas de forma consistente con sus desplazamientos físicos. El validador local confirma restricciones, pero no certifica que el score de rutas prediga bien el promedio de finalización; ese desajuste explica parte de las limitaciones de la estrategia mixta.
