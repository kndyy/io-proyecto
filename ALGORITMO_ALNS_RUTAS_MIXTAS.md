# ALNS con rutas mixtas (`alns-joint`)

## Estado

Implementación experimental y correcta. No se selecciona por defecto: `selector.conf` sigue apuntando a `integrated` porque, en las diez instancias, `alns-joint` solo mejora a `integrated` en la instancia 01. Todos los TXT generados pasan `tools/validate_txt.py`.

## Idea

Extiende el ILS integrado con una búsqueda ALNS sobre:

- secuencia y asignación de lotes a máquinas;
- prioridad de entrada `priorityIn[i]`;
- prioridad de salida `priorityOut[i]`;
- rutas AGV mixtas `I`, `O` y `M`.

El simulador de eventos sigue siendo el único decoder y el único evaluador. No se cargan TXT durante la búsqueda.

## Rutas mixtas

- `I`: `pos → o (PICKUP de a lotes) → α_j1 (DROP) → … → α_ja (DROP)`.
- `O`: `pos → β_j1 (PICKUP) → … → β_jb (PICKUP) → f (DROP de b lotes)`.
- `M`: `pos → o (PICKUP de a lotes) → α_j1 … α_ja → β_h1 (PICKUP) → … → β_hb (PICKUP) → f (DROP de b lotes)`.

En `M` se descargan todos los insumos antes de recoger salidas, por lo que la carga máxima es `max(a,b) ≤ C`. Las reservas de `α` y `β` se aplican solo a la ruta ganadora.

## Decoder

Para cada AGV libre:

1. Se calculan urgencias de entrada y salida con las fórmulas del handoff.
2. Se toman hasta cinco semillas de entrada y cinco de salida.
3. Se construyen rutas `I`, `O` y `M` con extensión greedy.
4. Se elige la ruta de mayor score `ΣU - 0.35·L`.
5. Se reservan buffers y se generan los eventos reales.

El processing, bloqueo, capacidad y entregas se mantienen idénticos al simulador histórico.

## ALNS

- Inicialización: `integrated` con el 10% del presupuesto, más `S0`, `S1`, `S2` y el candidato integrado re-simulados con rutas mixtas.
- Operadores:
  - `D1` aleatorio + reinserción cheapest;
  - `D2` crítico + regret-2;
  - `D3` máquina bloqueada + regret-2;
  - `D4` shock de prioridades de entrada/salida.
- Aceptación: mejoras siempre; empeoramientos con probabilidad `exp(-Δ/T)`.
- Pesos adaptativos por operador cada 50 intentos.
- Intensificación VND `RELOCATE`/`SWAP` cada 2500 evaluaciones o al mejorar el mejor global.
- Tres reinicios con semillas `seed`, `seed XOR 0x9E3779B97F4A7C15`, `seed XOR 0xD1B54A32D192ED03`.
- `--max-evals` es el presupuesto total, no por reinicio.
- La simulación final con log se ejecuta una sola vez para exportar el TXT.

## Comandos

```bash
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
./io1 --input instances/instancia_06.io1 --strategy alns-joint \
     --seed 20261109 --max-evals 50000 --max-neighbors 120 \
     --txt /tmp/alns_06.txt
python3 tools/validate_txt.py instances/instancia_06.io1 /tmp/alns_06.txt
```

## Resultados

| Instancia | integrated | alns 10k | alns 50k |
|---|---:|---:|---:|
| 01 | 355.80 | 352.25 | 351.00 |
| 02 | 450.36 | 490.21 | 489.57 |
| 03 | 566.78 | 619.48 | 595.13 |
| 04 | 591.55 | 654.33 | 654.42 |
| 05 | 716.03 | 792.89 | 776.90 |
| 06 | 998.94 | 1165.86 | 1132.85 |
| 07 | 1207.75 | 1396.95 | 1383.35 |
| 08 | 1426.50 | 1642.99 | 1556.87 |
| 09 | 1477.96 | 1815.04 | 1745.69 |
| 10 | 1488.67 | 1869.51 | 1836.46 |

Resultados completos: `results/alns_joint_comparison.csv`.

## Diagnóstico

La misma secuencia de máquinas evaluada con el decoder histórico y con el decoder mixto:

| Instancia | Histórico | Rutas mixtas | Bloqueo histórico | Bloqueo mixto |
|---|---:|---:|---:|---:|
| 01 | 355.80 | 377.62 | 0 | 0 |
| 06 | 998.94 | 1347.49 | 133.03 | 3149.16 |
| 10 | 1492.50 | 2063.07 | 1592.86 | 9744.14 |

El problema no está en el número de iteraciones: en la instancia 10, `alns-joint` con 300000 evaluaciones llega a 1813.34, todavía lejos de `integrated` (1488.67). El decoder de rutas mixtas genera más bloqueo y retrasa salidas; la búsqueda ALNS no puede compensarlo.

Ablación: el mismo marco ALNS usando el decoder histórico, sin rutas mixtas, alcanzó 1478.48 en la instancia 10 con 10000 evaluaciones. Eso confirma que:

- el marco ALNS sí puede iterar y mejorar;
- el cuello de botella es el decoder de rutas mixtas;
- la siguiente mejora debe estar en la política de AGVs, no en más evaluaciones ni en más prioridades.

## Dónde están las ganancias

1. **Salidas bloqueadas**: priorizar siempre el retiro de `β` bloqueado antes de abastecer entradas.
2. **Rutas combinadas**: las rutas `M` retrasan la recogida de salidas; medir si aportan o solo aumentan bloqueo.
3. **Batching**: comparar viajes separados contra viajes combinados con el mismo estado.
4. **Lookahead**: estimar el efecto de una recogida en el próximo bloqueo, no solo la distancia.
5. **Asignación de máquinas**: el ALNS sobre secuencias mejora poco; la asignación inicial y el balanceo de carga siguen siendo decisivos.
6. **Selección por instancia**: `alns-joint` gana solo en 01; una cartera por instancia puede recuperar unas décimas, no cerrar la brecha de 30–50%.

## Referencias

- Liang, T., Zhou, L., & Jiang, Z. (2025). Integrated scheduling of production and material delivery for the intelligent manufacturing system. International Journal of Production Research, 63(3), 882–903.
- Røpke, S., & Pisinger, D. (2006). An Adaptive Large Neighborhood Search Heuristic for the Pickup and Delivery Problem with Time Windows. Transportation Science, 40(4), 455–472.
- Shaw, P. (1998). Using Constraint Programming and Local Search Methods to Solve Vehicle Routing Problems. CP98, LNCS 1520, 417–431.
- Material del curso: `Sem6_Capitulo3.pdf`, `Proyecto IO1 2026-2.pdf`.

La adaptación al modelo de buffers de capacidad 1, bloqueo y media `Σc_i/n` es propia y no está validada por esos artículos para estas instancias.
