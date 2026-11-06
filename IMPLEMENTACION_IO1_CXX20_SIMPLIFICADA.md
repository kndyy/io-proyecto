# Proyecto IO1 2026-2 — Diseño algorítmico y encargo de implementación en C++20 (versión académica simplificada)

**Estado:** propuesta de implementación cerrada, pendiente de contrastación empírica y del formato oficial de IO1Lab.  
**Rol del agente:** escribir y depurar **únicamente** el código decidido aquí; no elegir otros algoritmos, métricas, formatos de plataforma ni nuevas dependencias.  
**Fuentes:** `Proyecto IO1 2026-2.pdf`, `Sem6_Capitulo3.pdf`, `SILABO_IN2003_2026-2.pdf` y diez instancias oficiales `.xlsx` (representadas en `.io1` para el prototipo).  
**Lenguaje:** ISO **C++20**, exclusivamente biblioteca estándar, compatible con Windows, Linux y macOS.

## 1. Qué se optimiza y qué exige el enunciado

Asignar **cada lote a una máquina compatible**, definir su orden de procesamiento y programar AGVs que transporten cada lote por `o → α_j → máquina j → β_j → f`.

- `p_ij = q_i / v[j, r_i]` si la máquina `j` procesa el tipo del lote `i`.
- Cada máquina procesa un solo lote sin interrupción.
- `α_j` y `β_j` tienen **capacidad 1 cada uno**. Al comenzar el procesamiento, `α_j` queda libre. Al finalizar, si `β_j` está lleno, la máquina queda **bloqueada** hasta liberar la salida; entonces transfiere automáticamente el lote a `β_j`.
- Todos los lotes parten de `o`, todos los AGVs empiezan vacíos en `o`, cada AGV admite hasta `C` lotes indivisibles. Carga/descarga instantáneas, viajes `τ[u][v]` y espera permitida.
- La finalización `c_i` **es la entrega en `f`**, no el término de procesamiento.
- Objetivo real: `min (Σ_i c_i)/n`. Solo comparar soluciones **factibles**.

**El PDF requiere explícitamente un método de búsqueda local** (§5, p. 7 del documento impreso, p. 9 del archivo). La evaluación asigna 10 % a construcción inicial, 20 % a vecindades, 20 % a búsqueda local, 20 % a dominio de conceptos y 30 % a resultados en plataforma. El código debe tener funciones cortas y claras (aprox. 15 líneas), sin bibliotecas de terceros (§7.2, p. 10 impresa). La declaración de IA es obligatoria (§7.1.1, p. 9 impresa).

**No se puede afirmar optimalidad global.** Las tres ideas llamadas “óptimas” en la conversación se presentan aquí como **tres ideas prometedoras**, susceptibles de comparación empírica.

**Escala verificada de los datos:**

| Instancia | Lotes | Máquinas | AGVs | C |
|---|---:|---:|---:|---:|
| 01 | 20 | 4 | 2 | 2 |
| 02 | 32 | 5 | 2 | 2 |
| 03 | 44 | 6 | 2 | 2 |
| 04 | 60 | 7 | 3 | 2 |
| 05 | 80 | 8 | 3 | 3 |
| 06 | 100 | 8 | 4 | 2 |
| 07 | 125 | 9 | 4 | 2 |
| 08 | 155 | 10 | 4 | 2 |
| 09 | 185 | 11 | 4 | 3 |
| 10 | 220 | 12 | 5 | 3 |

El programa debe admitir todos estos tamaños sin suponer capacidad `C=1`. La figura de ejemplo del PDF presenta una **solución factible** con media de `1062 s`; **no** es un óptimo certificado ni corresponde a las diez instancias oficiales.

## 2. Contraste riguroso con el curso

| Curso `Sem6_Capitulo3.pdf` | Cómo se utiliza en este proyecto |
|---|---|
| Diapositivas 15–18: solución inicial y construcción greedy | Construcción por inserción de lotes según una puntuación estimada de finalización. |
| Diapositivas 43 y 50: seleccionar vecino y *first/best improvement* | Usar **first improvement** para reducir evaluaciones del simulador. |
| Diapositivas 46–48: *exchange* / *2-opt* en TSP | Adaptar la idea de vecindad a **intercambio de lotes**; NO aplicar 2-opt de TSP a rutas de AGVs como si fueran un TSP clásico. |
| Diapositivas 52–55: hill climbing y óptimo local | Aceptar únicamente reducciones estrictas de la función real; reconocer que el resultado puede ser óptimo solo respecto de vecinos examinados. |
| Diapositivas 58–59: reinicios / ILS | Conservar tres construcciones iniciales distintas. No implementar ILS por ahora. |
| Diapositivas 60–61: VNS, intensificación y diversificación | **VND de dos vecindades** con orden fijo; diversidad de puntos iniciales, sin RVND ni mecanismos sofisticados. |

No se requiere implementar Simplex ni dualidad: las diapositivas de programación lineal son marco de modelamiento, pero el proyecto pide búsqueda local sobre un problema combinado de secuenciación y transporte.

## 3. Tres enfoques simples para usar como *baselines* y por qué no serán el método final

**Estos no son experimentos ya realizados.** Son hipótesis de comparación con limitaciones deducidas del modelo; solo se podrá declarar que “rindieron peor” cuando se ejecuten y midan.

| Baseline | Regla precisa | Limitación / motivo de descarte como solución final |
|---|---|---|
| **B1. FIFO con máquina más rápida** | Procesar lotes por ID ascendente; para cada lote, elegir la máquina compatible con menor `p_ij`; añadir al final de su cola. | Es extremadamente fácil, pero concentra carga en máquinas rápidas, ignora el tiempo de transporte y el impacto en `Σc_i`. |
| **B2. SPT puro** | Ordenar lotes por `min_j p_ij` ascendente; asignar cada lote a la máquina compatible de menor `p_ij`, sin considerar carga acumulada ni traslados. | SPT está relacionado con minimizar sumas de tiempos de terminación en modelos simples, pero aquí hay máquinas heterogéneas, buffers y AGVs; SPT puro no resuelve la coordinación. |
| **B3. Balanceo de carga puro** | Por ID, asignar cada lote a la máquina compatible que minimice `L_j + p_ij`, con `L_j = Σ p` ya asignado. | Mejora la distribución productiva pero supone que transporte/bloqueos son irrelevantes; el balance de carga no garantiza baja suma de entregas. |

**Comparación justa:** ejecutar B1–B3 con **el mismo simulador factible de AGVs**, mismo `batch_limit=C` y la misma regla de despacho, sin búsqueda local. Así la comparación aísla el efecto de la construcción inicial. No comparar un baseline inválido con una solución factible.

## 4. Tres ideas operacionales candidatas para revisión del equipo

**Importante sobre autoría:** estas son **candidatas para discusión del equipo**, no afirmaciones de que ya fueron concebidas por integrantes. Como las tres están escritas en este documento asistido por IA, **no atribuirlas retrospectivamente al equipo**. En `declaracion_IA.tex`, identificar como “equipo” solo aquellas ideas que los integrantes hayan concebido realmente y puedan documentar; si se originaron aquí, registrarlas como propuestas de IA posteriormente aceptadas, modificadas o rechazadas.

| Idea candidata | Intuición | Decisión adoptada |
|---|---|---|
| **E1. Lotes cortos primero** | Entregar pronto varios lotes pequeños puede disminuir `Σc_i`. | **Adaptar**: ordenar por menor tiempo estimado de finalización **incluyendo traslado**, no solo `p_ij`. |
| **E2. No saturar la máquina más rápida** | La cola de una máquina también importa. | **Adaptar**: considerar `L_j` al elegir máquina; **rechazar** la igualdad forzada de cargas porque los ritmos y distancias difieren. |
| **E3. Liberar buffers de salida para evitar bloqueos** | Una salida llena puede impedir que una máquina siga produciendo. | **Adoptar con límite**: dar prioridad a retirar lotes de `β_j` cuando hay bloqueo o salidas disponibles, sin abandonar indefinidamente abastecimiento de entrada. |

## 5. Tres ideas provenientes de IA, evaluadas y simplificadas

| Propuesta original de IA (especificación anterior) | Decisión para esta versión | Razón |
|---|---|---|
| **A1. GRASP + 12 reinicios + RCL** | **Rechazar GRASP completo**. Conservar **3 variantes sencillas de greedy**, solo una con aleatoriedad controlada. | Menos hiperparámetros y más fácil explicar el paso de construcción. |
| **A2. Cinco vecindades + RVND + ILS** | **Adaptar a VND con solo 2 vecindades:** reinsertar e intercambiar, usando *first improvement*. Quitar ILS, inversión de segmentos y vecindades de políticas. | Satisface expresamente el método de búsqueda local y está directamente alineado con lo enseñado en clase. |
| **A3. Planificación de AGVs con lotes múltiples, reservas y anticipación** | **Adaptar a despacho greedy** con prioridad de salida, capacidad `1` o `C`, y reserva simple de buffers. **Rechazar** planificación detallada de reposicionamiento anticipatorio en esta primera versión. | Preserva restricciones y permite aprovechar capacidad física sin escribir un optimizador de rutas completo. |

**Síntesis de la combinación:** la intuición productiva (E1/E2) determina prioridades de asignación; la intuición logística (E3) dirige los AGVs; la búsqueda local del curso (A2 simplificada) evalúa si cambiar las decisiones de máquina/orden reduce `Σc_i` **después** de simular todos los viajes. No confundir intuiciones con evidencia cuantitativa.

## 6. Tres componentes prometedores que formarán el algoritmo final

**P1 — Greedy consciente de producción y transporte.** Calcular, para cada lote `i`,

`h_i = min_{j compatible} [ p_ij + τ[o][α_j] + τ[β_j][f] ]`.

Tras elegir un lote, asignarlo a la máquina compatible que minimice

`score(i,j) = L_j + p_ij + τ[o][α_j] + τ[β_j][f]`,

con `L_j` igual a la suma de duraciones de los lotes ya asignados. Resolver empates por menor `j`. Esta puntuación es una **aproximación constructiva** y no sustituye la función objetivo real. En una misma máquina, la secuencia se define por el orden en que se insertan lotes.

**P2 — Simulación con prioridad de retiro de productos terminados.** Los AGVs se despachan dinámicamente con reservas de buffers; atienden salidas disponibles y, cuando no hay ninguna, abastecen entradas. Si hay varios lotes de salida disponibles, se pueden recoger hasta `C` en una ruta greedy sin esperar a que terminen otros. La decisión prioriza desbloqueos y lotes listos más antiguos. La construcción y la búsqueda **no necesitan calcular rutas exactas**: las genera el mismo simulador determinista.

**P3 — Mejorar asignación y secuencias mediante VND sencillo.** Dos movimientos: `RELOCATE` (sacar lote e insertarlo en otra posición/máquina compatible) y `SWAP` (intercambiar dos lotes de la misma máquina o de máquinas diferentes, siempre que ambas asignaciones sean compatibles). Cada vecino se simula completo; aceptar solo si disminuye el tiempo promedio real. Probar tres puntos iniciales para explorar distintas regiones.

## 7. Diseño cerrado del optimizador

### 7.1 Representación

`Candidate = vector<vector<int>> sequences` (una lista ordenada de IDs de lotes por máquina). Cada ID aparece exactamente una vez. No hay genes para tiempos o rutas; las rutas y tiempos se generan al **simular**. Para comparar los dos modos de despacho usar el parámetro adicional `batch_limit ∈ {1, C}`, común para viajes entrantes y salientes; esto es un parámetro **fijo durante cada búsqueda**, no una vecindad.

### 7.2 Tres soluciones iniciales (precisamente estas)

- **S0, greedy determinista de tiempo integral:** ordenar lotes por `h_i` ascendente, empate por ID.
- **S1, greedy SPT:** ordenar por `min_j p_ij` ascendente, empate por ID.
- **S2, greedy aleatorizado restringido:** mientras queden lotes, ordenarlos por `(h_i, ID)` y elegir uno uniformemente entre los primeros `max(1, ceil(0.2 * pendientes))`; repetir hasta terminar. Usar `std::mt19937_64` con semilla `20261106` y `rng() % tamañoRCL`; no usar `random_device`.

En los tres casos, **la asignación** de cada lote se hace por `score(i,j)` de P1 y se añade a la cola elegida. Por cada inicio `S0, S1, S2`, evaluar dos límites de carga `batch_limit=1` y `batch_limit=C` (si `C=1`, no duplicar); elegir la alternativa de menor media factible, empate a favor de `batch_limit=1`.

### 7.3 Vecindades y VND

- `N1 = RELOCATE`: elegir un lote, quitarlo de su máquina y colocarlo en cualquier posición de cualquier máquina compatible, incluida la misma; prohibir el movimiento identidad.
- `N2 = SWAP`: elegir dos lotes de posiciones diferentes (en una o dos máquinas); si cambian de máquina, ambos deben ser compatibles con sus destinos.
- No usar `N3`, RVND, ILS, algoritmo genético, *tabu search*, OR-Tools ni simplex.

**Algoritmo de búsqueda:** para cada punto inicial seleccionado, `N1 → N2` (*variable neighbourhood descent* de orden fijo):

```text
current = candidate_inicial_factible
k = 1
while k <= 2 y presupuesto_restante > 0:
    generar hasta 40 vecinos legales distintos de Nk
    para cada vecino, simular y comprobar factibilidad
    si se encuentra uno con media < media(current) - eps:
        current = vecino; k = 1; pasar a siguiente iteración
    si ninguno de los examinados mejora:
        k = k + 1
return current
```

`eps = 1e-9 * max(1, |media(current)|)`. Construir primero una **lista finita de movimientos legales**, con orden estable por ID de máquina, índice de origen, ID de máquina destino e índice de destino. Para cada exploración, barajar los índices de esa lista usando Fisher–Yates con `rng() % (k+1)` y revisar los primeros `min(40, cantidad_de_movimientos)` vecinos: así no existen repeticiones, rechazos infinitos ni decisiones no especificadas sobre cómo muestrear. Para `N2`, listar pares de posiciones en orden lexicográfico y descartar los incompatibles antes de barajar. El bucle de búsqueda consume una evaluación por cada vecino realmente simulado. **Se aplica first improvement; no aseverar que se ha alcanzado el óptimo local de una vecindad exhaustiva**, ya que se muestrean vecinos.

**Presupuesto académico fijo:** máximo **1000 llamadas a `simulate` por inicio**, más las 2 evaluaciones previas de sus políticas de carga; por tanto, hasta **3006 evaluaciones por instancia** cuando `C>1`. Parar temprano si N1 y N2 no producen mejora en sus muestras. Conservar el **mejor global de los tres resultados** y nunca aceptar un vecino inviable. No incluir paralelismo ni optimización incremental en esta versión.

### 7.4 Simulación determinista sencilla de eventos

Estructuras mínimas:

- `Machine`: estado `IDLE/PROCESSING/BLOCKED`, `input_lot`, `output_lot`, `current_lot`, `next_to_feed`, `input_reserved`, `output_reserved`.
- `AGV`: posición, carga `vector<int>`, libre/ocupado, `route` como lista de pasos `(destino, acción, lote)`.
- `Event`: tiempo `long double`, tipo `PROCESS_END` o `AGV_ARRIVE`, ID de recurso, contador para desempates.
- `Result`: `feasible`, `completion[i]`, `mean`, `blocked_time`, eventos de máquina y AGV necesarios para exportación.

Iniciar todos los estados libres en tiempo `0`, AGVs vacíos en `o`. Utilizar cola de prioridad por tiempo y desempate estable. En cada instante `t`:

1. Resolver todos los `PROCESS_END` con ID de máquina ascendente. Transferir inmediatamente a `β` si está libre; si no, dejar el lote en la máquina y marcar `BLOCKED`.
2. Resolver todas las llegadas de AGVs con ID ascendente. Ejecutar instantáneamente la acción: recoger en `o`, descargar en `α`, recoger de `β`, entregar en `f`; actualizar carga y reservas. Al retirar un lote de `β`, si había un lote bloqueado en la máquina, transferirlo a `β` en ese mismo instante.
3. En cada máquina `IDLE` con lote en `α`, comenzar procesamiento, liberar `α` y programar `PROCESS_END` a `t+p_ij`.
4. Para cada AGV libre por ID ascendente, crear y reservar una ruta si hay tareas, dando prioridad a `OUTBOUND` si existe al menos una `β` ocupada no reservada; si no, `INBOUND` con lotes pendientes. Programar la siguiente llegada del AGV.
5. Procesar las acciones de duración 0 en el mismo `t` hasta que no queden; luego avanzar al siguiente evento. Terminar cuando se hayan entregado **exactamente n lotes** en `f`.

**Rutas de entrada `INBOUND`:** elegir hasta `batch_limit` máquinas con `α` vacío/no reservado y lotes pendientes según sus secuencias; primero máquina `IDLE` (si existe), luego menor `p` del próximo lote, luego ID. Elegir nuevos destinos por menor `τ` desde el último `α`. Reservar al crear ruta. AGV va de su posición a `o`, recoge los lotes, recorre esos `α` en orden y descarga uno por ubicación. El lote pendiente de cada máquina es el **siguiente no alimentado de su secuencia**, contando también los ya reservados; actualizar ese índice al reservar.

**Rutas de salida `OUTBOUND`:** elegir hasta `batch_limit` buffers `β` actualmente ocupados/no reservados. Primera salida: preferir la máquina `BLOCKED`, después el lote más antiguo en `β`, después menor distancia desde el AGV, luego ID. Otras salidas: elegir vecino más cercano desde la última salida; no esperar para completar cupo. Reservar cada `β` al crear la ruta. Recoger los lotes, desplazarse a `f` y descargarlos juntos; asignar `c_i = t` al descargar en `f`.

**Reglas de seguridad simplificadoras (decisiones de diseño, no nuevas restricciones del enunciado):** solamente reservar **buffers que ya están libres** para entrada y **lotes ya presentes** en salida; nunca iniciar un viaje que deba esperar cargado frente a un buffer ocupado; un AGV jamás inicia otra ruta con carga; no ir a buscar un lote bloqueado dentro de una máquina; no preposicionar AGVs cuando no hay tareas; si un AGV no tiene tarea, espera en su última ubicación. Estos criterios favorecen factibilidad y claridad, a cambio de posiblemente desaprovechar oportunidades de optimización.

Un evento `AGV_ARRIVE` dispara la próxima etapa de su ruta; cada trayecto respeta exactamente `τ[actual][destino]`. El agente deberá implementar un bucle interno para eventos instantáneos (incluido `τ[u][u]=0`) sin duplicar acciones, con guardia de progreso finita. La política de despacho es determinista dadas las secuencias y `batch_limit`.

**Chequeo durante simulación:** compatibilidades, estado válido de cada lote, carga ≤ C, unicidad en `α/β`, lote único por máquina, rutas secuenciales, orden de cola, tiempos de transporte, una sola entrega por lote y objetivo calculado desde las entregas. Si falla una condición o queda atascado sin eventos y con lotes pendientes: resultado `feasible=false`, diagnóstico y no exportar.

### 7.5 Por qué no necesitamos un optimizador de AGVs separado

La función `simulate(instance, candidate, batch_limit)` es el **decoder**: a partir de las secuencias decide viajes de AGVs con reglas greedy deterministas y devuelve la media de `c_i`. Por ello `RELOCATE` o `SWAP` afectan simultáneamente producción, buffers, bloqueos y viajes. Es una **heurística integrada a través de la evaluación**, aunque no una optimización conjunta exacta.

## 8. Arquitectura mínima, sin paquetes ni sobreabstracción

```text
io1/
├── main.cpp            # lectura de argumentos, ejecución, impresión y archivos
├── instance.hpp        # estructuras, lectura de .io1 y validación de datos (header-only)
├── simulator.hpp       # eventos, AGVs, buffers y cálculo de c_i (header-only)
├── heuristic.hpp       # 3 greedy, RELOCATE, SWAP y VND (header-only)
├── README.md           # compilación y uso
└── instances/
    ├── instancia_01.io1
    └── ... instancia_10.io1
```

**No crear:** carpeta `src/`, separación `.cpp`/`.hpp` para todo, CMake obligatorio, biblioteca independiente, plugin, `io1_verify` separado, pruebas de cobertura, CI, generadores de documentación, framework de tests ni patrones de diseño. Usar **funciones libres y `struct`s simples**, no jerarquías de clases ni interfaces virtuales. En headers definir funciones no plantilla como `inline` por seguridad de enlace. Mantener variables de estado como campos de structs o locales; evitar globales mutables. Extraer helpers de nombre claro para respetar la sugerencia de funciones breves del PDF.

**Compilar en Linux/macOS:** `g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1`. También debe compilar con Clang equivalente y MSVC `/std:c++20` sin librerías extra.

**CLI mínima:**

```bash
./io1 --input instances/instancia_01.io1 --out resultado_01.csv --seed 20261106
./io1 --self-test
```

Archivo `.io1`: **formato interno** proporcionado por el paquete previamente normalizado, con `IO1_V1`, cabecera `n m K C`, secciones `LOTS`, `RATES`, `LOCATIONS`, `TIMES`, `END`. Leer mediante `std::ifstream` y `operator>>`; no intentar parsear `.xlsx` desde C++. El valor final se imprime también en consola.

## 9. Pruebas pequeñas y útiles (sin framework ni cobertura)

`--self-test` ejecuta **tres** verificaciones con `assert` o excepciones simples:

1. **Un lote:** AGV único, máquina única, `p=10`, `τ[o][α]=5`, `τ[α][β]=2`, `τ[β][f]=7`. Con el despacho reactivo sin preposicionamiento elegido aquí, salida del proceso en `15`, recogida de `β` en `17` y entrega en `24` (no en `22`, que requeriría anticipación del AGV). Comprobar media `24` y no redondear un `p` fraccionario en otra configuración.
2. **Bloqueo:** tres lotes en una máquina y AGV lento; forzar que un segundo proceso termine mientras `β` sigue ocupado. El tercero no debe comenzar hasta liberar el bloqueo. Verificar automáticamente este invariante.
3. **Smoke test de las diez instancias:** leer cada `.io1`, construir S0 y simular con `batch_limit=1` y con `C`; `n` entregas, media finita, compatibilidad, carga y buffers correctos. Este paso se ejecuta con un script/comando sencillo de consola; **no** requiere una infraestructura de testing.

En resultados finales, ejecutar cada una de las diez con B1, B2, B3 y el método final y producir, por instancia: `baseline_B1`, `baseline_B2`, `baseline_B3`, `S0`, `S1`, `S2`, `best_VND`, mejora %, tiempo de CPU. El prototipo no necesita medir cobertura ni generar reportes HTML.

## 10. Informe y atribución honesta de ideas

La plantilla oficial exige **Resumen, Revisión breve de literatura, Metodología, Resultados computacionales, Conclusiones**, máximo cuatro páginas más referencias y declaración de IA. No repetir la descripción general del problema.

Explicar de forma breve:

1. Tres reglas baselines y sus limitaciones **teóricas** antes de ejecutarlas; documentar luego si efectivamente fueron peores.
2. Por qué la construcción usa `(L_j+p_ij+transporte)` en vez de solamente `p_ij`.
3. La factibilidad del decoder y el efecto del bloqueo del buffer de salida.
4. Las dos vecindades y por qué un `RELOCATE` puede ayudar más que un `SWAP` (y viceversa).
5. Comparar `S0` contra `S1`, `S2` y después VND; presentar resultados de las diez instancias y costes de cómputo.
6. Declarar qué ideas **realmente** desarrollaron los integrantes, cuáles sugirió IA y qué adaptó el grupo. Este documento puede informar las decisiones del grupo, pero **no es prueba de autoría humana**.

**No escribir “tres ideas del equipo” sin participación real documentada.** Registrar decisiones tomadas en reunión, versiones de código y resultados de ablation para mostrar trabajo propio. La declaración es obligatoria y todos los integrantes deben revisarla y firmarla. Comprobar además la autorización expresa de IA según el sílabo y las instrucciones del profesor/Canvas.

## 11. Autograder IO1Lab: única especificación externa faltante

El PDF dice que IO1Lab ofrece instrucciones para construir y validar archivos de solución, pero **no incluye** la estructura exacta del archivo. No se verificó acceso público al esquema. Las diez instancias `.xlsx` solo describen **datos de entrada**. Por tanto:

- Guardar `resultado_XX.csv` **solo como reporte interno** provisional: `lot_id,machine_id,process_start,process_end,delivered_at`, un registro por lote, más `mean` en consola. **No afirmar que este CSV es válido para IO1Lab.**
- Mantener una lista sencilla de desplazamientos y eventos en `Result` con tiempos, AGV, origen, destino y carga; es la información mínima para escribir el formato oficial sin reescribir el optimizador.
- Cuando se cuente con la documentación oficial (o ejemplo aceptado), añadir **una sola función** `write_io1lab_output(...)` en `main.cpp`, siguiendo exactamente el esquema. Hasta entonces, imprimir `IO1LAB_FORMAT_PENDING` si se pide exportación oficial.
- Validar primero una solución de la instancia 01 en la plataforma; **solo la aceptación real** puede justificar decir “aprobado por autograder”. Luego validar las otras nueve.

No pedir a un agente que deduzca de manera especulativa el formato oficial, ni que confunda los archivos internos con el contrato de la plataforma.

## 12. Instrucciones terminantes para el agente implementador

1. Usa solo **cuatro archivos de código** (`main.cpp`, `instance.hpp`, `simulator.hpp`, `heuristic.hpp`). `README.md` e instancias son datos/documentación, no nuevas capas de software.
2. Implementa las reglas físicas del §1 y exactamente el simulador del §7.4; la **factibilidad prima sobre el score**.
3. Implementa B1, B2, B3 solo para comparación; implementa S0, S1, S2, `RELOCATE`, `SWAP` y VND del §7 para la solución final. No agregues estrategias que no estén aquí.
4. Limita a 1000 simulaciones por inicio y usa semilla fija por defecto. Conserva siempre la mejor solución factible conocida.
5. Mantén funciones concretas y cortas, nombres comprensibles, comentarios mínimos útiles, sin patrones empresariales ni cobertura automática.
6. Ejecuta los tres self-tests y las diez instancias. **No inventes mediciones**: deja tablas sin cifras hasta disponer de salidas del programa.
7. Deja la exportación IO1Lab pendiente hasta conocer su contrato real. Al terminar entrega **código, instrucciones, soluciones internas y tabla de resultados**.

**Criterio de éxito de esta fase:** ejecutable C++20 sencillo y reproducible, con método de búsqueda local verificable en las diez instancias, aunque no exista aún certificación de aceptación del archivo por IO1Lab. No afirmar optimalidad ni aprobación del portal sin evidencia.
