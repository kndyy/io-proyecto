# Algoritmo integrado: ILS + VND + despacho de AGVs (C++20)

## Motivación y relación con el curso
El capítulo 3 contiene construcción greedy, first improvement, hill climbing, ILS y vecindades variables (diapositivas 18, 43 y 52–61). El capítulo 4 estudia programación lineal, simplex y dualidad; no se utiliza para resolver directamente esta programación combinatoria de máquinas y transportes con bloqueo.

## Implementación
Se mantiene el simulador de eventos y todas las reglas de compatibilidad, buffers, bloqueo y capacidad. Un candidato conserva una secuencia por máquina, un indicador de prioridad AGV adaptativa y un sesgo entero de prioridad. El tamaño del lote por viaje está en la búsqueda como un entero entre 1 y C.

- Construcción: greedy S0 por duración de proceso y trayectos.
- V1 RELOCATE: mover un lote a otra posición/máquina compatible.
- V2 SWAP: intercambiar lotes compatibles.
- V3 DISPATCH: variar priority adaptive, sesgo entre -4 y +4, y batch de 1 a C.
- VND: first improvement, reiniciar V1 después de cada mejora; seguir hasta no mejorar en las tres vecindades.
- ILS: perturbar de 2 a 6 lotes, volver a ejecutar VND, conservar siempre la mejor solución; permitir ocasionalmente un óptimo local inferior como estado actual para diversificar.

La fórmula de urgencia de salida estima bloqueo y espera en buffer; la de entrada estima tiempo de máquina ociosa. Las rutas permanecen idénticas a las del simulador original para minimizar riesgo de validación. Con adaptive=false se conserva el despachador anterior.

## Ejecutar
```sh
g++ -std=c++20 -O2 -Wall -Wextra -Wpedantic main.cpp -o io1
./io1 --self-test
./io1 --input instances/instancia_01.io1 --strategy integrated --seed 20261109 --max-evals 30000 --max-neighbors 120 --txt out.txt
python3 tools/validate_txt.py instances/instancia_01.io1 out.txt
```

**No se usa ningún TXT como entrada de la optimización integrada**: los estados y simulaciones viven en memoria y el TXT se exporta una sola vez al final. `selector.conf` configura la estrategia por defecto. Las otras estrategias se mantienen para comparación.

## Verificaciones
La compilación C++20, self-test y validación de los diez casos se ejecutaron desde el workflow de esta rama. Los números experimentales y el presupuesto están en `results/integrated_benchmark.log`. Los resultados aceptados previamente del proyecto permanecen en `submissions/` y no deben reemplazarse si el nuevo resultado es peor.

## Alcance
Es una metaheurística, no un optimizador exacto; «mejor público» es una referencia de ranking, no una prueba de optimalidad global. Se requiere declarar con precisión el uso de IA en `declaracion_IA.tex`, como exige el proyecto.
