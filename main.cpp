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

/* --- FUNCIONES GLOBALES --- */

string limpiarTexto(string texto) {
    for (int i = 0; i < (int)texto.length(); i++) { 
        texto[i] = tolower((unsigned char)texto[i]);
    }
    // Regex que respeta ñ y acentos
    regex pattern("[^a-zñáéíóú\\s]"); 
    return regex_replace(texto, pattern, "");
}

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

void descargarLibro(string url, string nombreArchivo) {
    string comando = "curl -s -L " + url + " -o " + nombreArchivo;
    cout << "Descargando: " << nombreArchivo << "..." << endl;
    system(comando.c_str());
}

/* --- VERSIÓN SERIAL --- */

void ejecutarVersionSerial(const vector<string>& libros, double &tiempoSerialReferencia) {
    cout << "--- INICIANDO FASE SERIAL ---" << endl;
    auto inicio = chrono::high_resolution_clock::now();

    vector<map<string, int>> todosLosConteos; 
    set<string> vocabularioGlobal;

    for (const string& libro : libros) {
        map<string, int> conteoLibro = contarPalabras(libro);
        todosLosConteos.push_back(conteoLibro);
        for (auto const& [palabra, cantidad] : conteoLibro) {
            vocabularioGlobal.insert(palabra);
        }
    }

    // Cronómetro se detiene ANTES de escribir el CSV (Ley de Amdahl)
    auto fin = chrono::high_resolution_clock::now();
    chrono::duration<double> tiempo = fin - inicio;
    tiempoSerialReferencia = tiempo.count(); 
    
    cout << "Procesamiento Serial: " << tiempoSerialReferencia << " s" << endl;

    ofstream archivoSalida("bolsa_serial.csv");
    unsigned char bom[] = {0xEF, 0xBB, 0xBF}; 
    archivoSalida.write((char*)bom, sizeof(bom));
    
    archivoSalida << "Libro";
    for (const string& palabra : vocabularioGlobal) archivoSalida << "," << palabra;
    archivoSalida << "\n";

    for (size_t i = 0; i < libros.size(); ++i) {
        archivoSalida << libros[i];
        for (const string& palabra : vocabularioGlobal) {
            archivoSalida << "," << todosLosConteos[i][palabra];
        }
        archivoSalida << "\n";
    }
    archivoSalida.close();
}

/* --- FUNCIONES MPI --- */

void enviarMapa(map<string, int>& conteo, int destino) {
    int tam = (int)conteo.size();
    MPI_Send(&tam, 1, MPI_INT, destino, 0, MPI_COMM_WORLD);
    for (auto const& [palabra, cantidad] : conteo) {
        int len = (int)palabra.length();
        MPI_Send(&len, 1, MPI_INT, destino, 1, MPI_COMM_WORLD);
        MPI_Send(palabra.c_str(), len, MPI_CHAR, destino, 2, MPI_COMM_WORLD);
        MPI_Send(&cantidad, 1, MPI_INT, destino, 3, MPI_COMM_WORLD);
    }
}

map<string, int> recibirMapa(int origen) {
    map<string, int> conteo;
    int tam;
    MPI_Recv(&tam, 1, MPI_INT, origen, 0, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
    for (int i = 0; i < tam; i++) {
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

/* --- MAIN --- */

int main(int argc, char** argv) {
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    // Configuración de libros dinámicos
    vector<pair<string, string>> fuentes = {
        {"https://www.gutenberg.org/cache/epub/2000/pg2000.txt", "quijote.txt"},
        {"https://www.gutenberg.org/cache/epub/1342/pg1342.txt", "orgullo.txt"},
        {"https://www.gutenberg.org/cache/epub/11/pg11.txt", "alicia.txt"},
        {"https://www.gutenberg.org/cache/epub/1661/pg1661.txt", "holmes.txt"},
        {"https://www.gutenberg.org/cache/epub/76/pg76.txt", "huck.txt"}
    };

    vector<string> nombresArchivos;
    for(auto const& f : fuentes) nombresArchivos.push_back(f.second);

    double tiempoSerial = 0.0;

    if (rank == 0) {
        // Descarga inicial (Solo el Maestro)
        for (auto const& f : fuentes) descargarLibro(f.first, f.second);
        
        // Fase Serial
        ejecutarVersionSerial(nombresArchivos, tiempoSerial);
        cout << "--- FASE SERIAL FINALIZADA ---\n" << endl;
    }

    // Esperar a que los archivos existan y el Maestro termine lo serial
    MPI_Barrier(MPI_COMM_WORLD);

    // FASE PARALELA
    double t_par_inicio = MPI_Wtime();

    if (rank == 0) {
        cout << "--- INICIANDO FASE PARALELA CON " << size << " PROCESOS ---" << endl;
        
        // Repartir libros a trabajadores (Rank 1 en adelante)
        for (int i = 1; i < size && i <= (int)nombresArchivos.size(); i++) {
            string nombre = nombresArchivos[i-1];
            int len = (int)nombre.length();
            MPI_Send(&len, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
            MPI_Send(nombre.c_str(), len, MPI_CHAR, i, 11, MPI_COMM_WORLD);
        }

        // Recolectar resultados
        for (int i = 1; i < size && i <= (int)nombresArchivos.size(); i++) {
            recibirMapa(i); // El maestro recibe pero no procesa mas para el tiempo puro
        }
        
        double t_par_fin = MPI_Wtime();
        double tiempoParalelo = t_par_fin - t_par_inicio;

        cout << "\n========================================" << endl;
        cout << "RESUMEN DE RESULTADOS:" << endl;
        cout << "Tiempo Serial:   " << tiempoSerial << " s" << endl;
        cout << "Tiempo Paralelo: " << tiempoParalelo << " s" << endl;
        
        if (tiempoParalelo > 0) {
            double speedup = tiempoSerial / tiempoParalelo;
            cout << "Speed-up:        " << speedup << "x" << endl;
            cout << "Eficiencia:      " << (speedup / (size - 1)) * 100 << "%" << endl;
        }
        cout << "========================================\n" << endl;

    } else {
        // TRABAJADORES
        if (rank <= (int)nombresArchivos.size()) {
            int len;
            MPI_Recv(&len, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            char* buffer = new char[len + 1];
            MPI_Recv(buffer, len, MPI_CHAR, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[len] = '\0';

            map<string, int> miConteo = contarPalabras(string(buffer));
            enviarMapa(miConteo, 0);
            delete[] buffer;
        }
    }

    MPI_Finalize();
    return 0;
}