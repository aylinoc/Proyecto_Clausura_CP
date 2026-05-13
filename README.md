# Proyecto Clausura Cómputo Paralelo por Aylin y Náyade

## Descripción del programa

Este proyecto implementa un contador de frecuencia de palabras en libros de texto. El programa descarga varios libros desde Project Gutenberg, limpia el texto, cuenta cuántas veces aparece cada palabra y genera dos matrices de frecuencia:

- `bolsa_serial.csv`: resultado obtenido con procesamiento serial.
- `bolsa_paralela.csv`: resultado obtenido con procesamiento paralelo usando MPI.

Cada CSV tiene el siguiente formato:

- Cada fila representa un libro.
- Cada columna representa una palabra encontrada en el vocabulario global.
- Cada celda indica cuántas veces aparece esa palabra en ese libro.

---

## Tecnologías usadas

- **C++**: lenguaje principal del programa.
- **MPI**: biblioteca para programación paralela con varios procesos.
- **Open MPI / MPICH**: implementación necesaria para compilar y ejecutar el programa con MPI.
- **STL de C++**:
  - `unordered_map` para contar palabras de forma eficiente.
  - `map` para ordenar resultados.
  - `set` para construir el vocabulario global.
  - `vector` para almacenar libros y resultados.
- **Regex (`<regex>`)**: limpieza del texto antes del conteo.
- **Curl**: descarga automática de libros desde Project Gutenberg.

---

## Estructura del proyecto

```text
Proyecto_Clausura_CP/
│
├── main.cpp                  # Código principal en C++ con versión serial y paralela
├── README.md                 # Documentación del proyecto
│
├── Libros/                   # Carpeta donde se guardan los libros descargados
│
└── Resultados/               # Carpeta donde se guardan los CSV finales
    ├── bolsa_serial.csv
    └── bolsa_paralela.csv
```
---

## ¿Cómo funciona el programa?

El programa realiza tres fases principales:

1. Descarga de libros

2. Conteo serial

3. Conteo paralelo con MPI

---

## ¿Cómo funciona la parte paralela?

La parte paralela usa un esquema **maestro-trabajador** con MPI.

### Proceso maestro (`rank 0`)

El proceso maestro se encarga de:

- Descargar los libros.
- Ejecutar la versión serial como referencia.
- Repartir libros entre los procesos trabajadores.
- Recibir los conteos parciales.
- Reconstruir los mapas de frecuencia enviados por los trabajadores.
- Reasignar nuevos libros a los trabajadores que terminan primero.
- Unificar todos los resultados.
- Generar el CSV paralelo final.

### Procesos trabajadores (`rank > 0`)

Cada trabajador se encarga de:

- Recibir la ruta de un libro.
- Leer el archivo asignado.
- Contar las palabras de ese libro.
- Enviar el resultado al maestro.
- Esperar otro libro o una señal de finalización.

### Distribución dinámica de trabajo

El programa no asigna todos los libros de golpe. Primero manda un libro a cada trabajador disponible. Cuando un trabajador termina, el maestro recibe su resultado usando:

```cpp
MPI_Recv(..., MPI_ANY_SOURCE, ...)
```

Esto permite que el maestro reciba al primer trabajador que termine, sin importar cuál sea. Después, si todavía quedan libros pendientes, le asigna otro inmediatamente.

Este método ayuda a balancear la carga porque algunos libros son más grandes que otros. Si un proceso termina rápido, no se queda esperando: recibe otro libro.

### Envío de resultados

Como no se puede mandar directamente un `unordered_map` por MPI, y para optimizar el envío, el programa convierte el mapa a texto usando el formato:

```text
palabra:cantidad
palabra:cantidad
palabra:cantidad
```

Después se envía en dos pasos:

1. Primero se manda el tamaño del texto serializado.
2. Luego se manda el contenido como arreglo de caracteres.

El maestro recibe ese texto, lo descompone línea por línea y reconstruye el mapa de frecuencias.

### Señal de finalización

Cuando ya no quedan libros por procesar, el maestro manda a cada trabajador una longitud igual a `-1`.

```cpp
int fin = -1;
MPI_Send(&fin, 1, MPI_INT, trabajador, 10, MPI_COMM_WORLD);
```

Cuando un trabajador recibe `-1`, termina su ciclo de trabajo y finaliza.


