#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <chrono> 
#include <set>
#include <mpi.h>

using namespace std;


/* Definición de funciones globales */

// Función para limpiar el texto (quitar mayusculas, espacios, puntuación y números)
string limpiarTexto(string texto) {
    for (int i = 0; i < texto.length(); i++) { 
        texto[i] = tolower(texto[i]);
    }
   
    regex pattern("[^a-zñáéíóú\\s]"); // decidimos considerar tambien la ñ y acentos 
    return regex_replace(texto, pattern, "");
}

// Función para hacer el conteo de las palabras
map<string, int> contarPalabras(string nombreArchivo) {
    map<string, int> frecuencias;
    ifstream archivo(nombreArchivo);
    string palabra;

    if (!archivo.is_open()) {
        cerr << "Error al abrir: " << nombreArchivo << endl;
        return frecuencias;
    }

    while (archivo >> palabra) {
        palabra = limpiarTexto(palabra);
        if (!palabra.empty()) { 
            frecuencias[palabra]++;
        }
    }
    archivo.close();
    return frecuencias;
}


/* Implementación de la versión SERIAL */

void ejecutarVersionSerial(vector<string> libros) {
    cout << " Iniciando Versión Serial " << endl;
    auto inicio = chrono::high_resolution_clock::now();

    
    vector<map<string, int>> todosLosConteos; 
    set<string> vocabularioGlobal; // se usa set para que solo inserte palabras que no existian

    for (const string& libro : libros) {
        map<string, int> conteoLibro = contarPalabras(libro);
        todosLosConteos.push_back(conteoLibro);

        for (auto const& [palabra, cantidad] : conteoLibro) {
            vocabularioGlobal.insert(palabra);
        }
    }

    // Generar el archivo CSV de salida  
    ofstream archivoSalida("bolsa_serial.csv");
    unsigned char bom[] = {0xEF, 0xBB, 0xBF}; // para que el csv detecte correctamente la ñ
    archivoSalida.write((char*)bom, sizeof(bom));
    
    // Escribir encabezados (palabras)
    archivoSalida << "Libro";
    for (const string& palabra : vocabularioGlobal) {
        archivoSalida << "," << palabra;
    }
    archivoSalida << "\n";

    // Escribir filas (conteo por libro)
    for (size_t i = 0; i < libros.size(); ++i) {
        archivoSalida << libros[i];
        for (const string& palabra : vocabularioGlobal) {
            archivoSalida << "," << todosLosConteos[i][palabra]; // Si no existe, map pone 0
        }
        archivoSalida << "\n";
    }
    archivoSalida.close();

    auto fin = chrono::high_resolution_clock::now();
    chrono::duration<double> tiempo = fin - inicio;
    cout << "Versión Serial completada en: " << tiempo.count() << " segundos." << endl;
}


/* Funciones que son para MPI */

void enviarMapa(map<string, int>& conteo, int destino) {
    int tamaño = conteo.size();
    // 1. Enviar cuántas palabras únicas hay
    MPI_Send(&tamaño, 1, MPI_INT, destino, 0, MPI_COMM_WORLD);

    for (auto const& [palabra, cantidad] : conteo) {
        int len = palabra.length();
        // 2. Enviar longitud de la palabra, la palabra misma y su conteo
        MPI_Send(&len, 1, MPI_INT, destino, 1, MPI_COMM_WORLD);
        MPI_Send(palabra.c_str(), len, MPI_CHAR, destino, 2, MPI_COMM_WORLD);
        MPI_Send(&cantidad, 1, MPI_INT, destino, 3, MPI_COMM_WORLD);
    }
}

map<string, int> recibirMapa(int origen) {
    map<string, int> conteo;
    int tamaño;
    MPI_Recv(&tamaño, 1, MPI_INT, origen, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

    for (int i = 0; i < tamaño; i++) {
        int len, cantidad;
        MPI_Recv(&len, 1, MPI_INT, origen, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        
        char* buffer = new char[len + 1];
        MPI_Recv(buffer, len, MPI_CHAR, origen, 2, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buffer[len] = '\0';
        
        MPI_Recv(&cantidad, 1, MPI_INT, origen, 3, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        conteo[string(buffer)] = cantidad;
        delete[] buffer;
    }
    return conteo;
}

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    vector<string> libros = {"libro1.txt", "libro2.txt", "libro3.txt"};

    // =======================================================
    // FASE 1: VERSIÓN SERIAL (Solo el Maestro)
    // =======================================================
    if (rank == 0) {
        cout << "--- INICIANDO FASE SERIAL ---" << endl;
        ejecutarVersionSerial(libros); 
        cout << "--- FASE SERIAL FINALIZADA ---\n" << endl;
    }

    // SINCRONIZACIÓN: Nadie pasa de aquí hasta que el Maestro termine lo serial
    MPI_Barrier(MPI_COMM_WORLD);

    // =======================================================
    // FASE 2: VERSIÓN PARALELA (Todos los procesos)
    // =======================================================
    
    // El cronómetro paralelo empieza para todos al mismo tiempo después de la barrera
    double t_par_inicio = MPI_Wtime();

    if (rank == 0) {
        // Lógica del Maestro para repartir y recolectar
        cout << "--- INICIANDO FASE PARALELA CON " << size << " PROCESOS ---" << endl;
        
        vector<map<string, int>> conteosParalelos;
    
        // Repartir libros a los trabajadores
        // Suponiendo que tienes al menos (size-1) libros
        for (int i = 1; i < size; i++) {
            string nombre = libros[i-1];
            int len = nombre.length();
            MPI_Send(&len, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
            MPI_Send(nombre.c_str(), len, MPI_CHAR, i, 11, MPI_COMM_WORLD);
        }

        // Recolectar resultados
        for (int i = 1; i < size; i++) {
            conteosParalelos.push_back(recibirMapa(i));
        }

        // Opcional: Aquí podrías generar el "bolsa_paralela.csv" 
        // usando 'conteosParalelos' para comparar con el serial.
        
        double t_par_fin = MPI_Wtime();
        cout << "Tiempo Paralelo Único: " << t_par_fin - t_par_inicio << "s" << endl;
    } else {
        // Lógica de los Trabajadores
        int len;
        // 1. Recibir el nombre del libro que me toca
        MPI_Recv(&len, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        char* buffer = new char[len + 1];
        MPI_Recv(buffer, len, MPI_CHAR, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
        buffer[len] = '\0';

        // 2. Hacer el conteo (usa la función que ya tenemos)
        map<string, int> miConteo = contarPalabras(string(buffer));

        // 3. Regresar el mapa al maestro
        enviarMapa(miConteo, 0);
        delete[] buffer;
    }

    MPI_Finalize();
    return 0;
}