#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <chrono> 
#include <set>
#include <mpi.h>
#include <unordered_map>

using namespace std;

/* --- FUNCIONES GLOBALES --- */

string limpiarTexto(string texto) {
    for (int i = 0; i < (int)texto.length(); i++) { 
        texto[i] = tolower((unsigned char)texto[i]);
    }
    // Regex que respeta ñ y acentos
    static const regex pattern("[^a-zñáéíóú\\s]"); 
    return regex_replace(texto, pattern, "");
}

unordered_map<string, int> contarPalabras(string nombreArchivo) {
    unordered_map<string, int> frecuencias;
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

/* --- FUNCIONES MPI --- */

void enviarMapa(unordered_map<string, int>& conteo, int destino) {
    int tam = (int)conteo.size();
    MPI_Send(&tam, 1, MPI_INT, destino, 0, MPI_COMM_WORLD);
    for (auto const& [palabra, cantidad] : conteo) {
        int len = (int)palabra.length();
        MPI_Send(&len, 1, MPI_INT, destino, 1, MPI_COMM_WORLD);
        MPI_Send(palabra.c_str(), len, MPI_CHAR, destino, 2, MPI_COMM_WORLD);
        MPI_Send(&cantidad, 1, MPI_INT, destino, 3, MPI_COMM_WORLD);
    }
}

unordered_map<string, int> recibirMapa(int origen) {
    unordered_map<string, int> conteo;
    int tam;
    MPI_Status status;

    MPI_Recv(&tam, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
    origen = status.MPI_SOURCE;
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

void descargarLibro(string url, string nombreArchivo) {
    string ruta = "Libros/" + nombreArchivo;
    string comando = "curl -s -L " + url + " -o " + ruta;
    cout << "Descargando: " << nombreArchivo << "..." << endl;
    system(comando.c_str());
}

/* --- VERSIÓN SERIAL --- */

void ejecutarVersionSerial(const vector<string>& libros, double &tiempoSerialReferencia) {
    cout << "--- INICIANDO FASE SERIAL ---" << endl;
    auto inicio = chrono::high_resolution_clock::now();

    vector<unordered_map<string, int>> todosLosConteos; 
    set<string> vocabularioGlobal;

    for (const string& libro : libros) {
        unordered_map<string, int> conteoLibro = contarPalabras(libro);
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

    ofstream archivoSalida("Resultados/bolsa_serial.csv");
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
    for(auto const& f : fuentes) nombresArchivos.push_back("Libros/" + f.second);

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

        vector<unordered_map<string, int>> conteosParalelos;
        int librosEnviados = 0;
        int librosRecibidos = 0;
        int numLibros = (int)nombresArchivos.size();

        // Repartir libros a trabajadores (Rank 1 en adelante)
                for (int i = 1; i < size && librosEnviados < numLibros; i++) {
                    string nombre = nombresArchivos[librosEnviados++];
                    int len = (int)nombre.length();
                    MPI_Send(&len, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
                    MPI_Send(nombre.c_str(), len, MPI_CHAR, i, 11, MPI_COMM_WORLD);
                }

        bool maestroTrabajando = false;
        unordered_map<string, int> conteoMaestro;
        if (librosEnviados < numLibros) {
            string miRuta = nombresArchivos[librosEnviados++];
            cout << "Maestro aprovechando tiempo extra con: " << miRuta << endl;
            conteoMaestro = contarPalabras(miRuta);
            maestroTrabajando = true;
            librosRecibidos++;
        }

        // --- ESCUCHA DINÁMICA ---
        while (librosRecibidos < numLibros) {
            int trabajadorLibre;
            conteosParalelos.push_back(recibirMapa(trabajadorLibre));
            librosRecibidos++;

            // Si aún quedan libros, le mandamos otro al que acaba de terminar
            if (librosEnviados < numLibros) {
                string ruta = nombresArchivos[librosEnviados++];
                int len = (int)ruta.length();
                MPI_Send(&len, 1, MPI_INT, trabajadorLibre, 10, MPI_COMM_WORLD);
                MPI_Send(ruta.c_str(), len, MPI_CHAR, trabajadorLibre, 11, MPI_COMM_WORLD);
            }
        }

        // Agregar el conteo del maestro al final si es que trabajó
        if(maestroTrabajando) conteosParalelos.push_back(conteoMaestro);

        // --- SEÑAL DE FIN ---
        // Mandamos un -1 para que los trabajadores salgan de su ciclo while
        for (int i = 1; i < size; i++) {
            int fin = -1;
            MPI_Send(&fin, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
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

        // --- GENERACIÓN DEL ARCHIVO PARALELO (Fuera del tiempo) ---
        cout << "Generando bolsa_paralela.csv..." << endl;
        
        // Creamos un vocabulario global para el paralelo
        set<string> vocabularioParalelo;
        for (const auto& mapa : conteosParalelos) {
            for (auto const& [palabra, cant] : mapa) {
                vocabularioParalelo.insert(palabra);
            }
        }

        ofstream archivoPar("Resultados/bolsa_paralela.csv");
        unsigned char bom[] = {0xEF, 0xBB, 0xBF}; 
        archivoPar.write((char*)bom, sizeof(bom));
        
        archivoPar << "Libro";
        for (const string& p : vocabularioParalelo) archivoPar << "," << p;
        archivoPar << "\n";

        for (size_t i = 0; i < conteosParalelos.size(); ++i) {
            archivoPar << nombresArchivos[i];
            for (const string& p : vocabularioParalelo) {
                archivoPar << "," << conteosParalelos[i][p];
            }
            archivoPar << "\n";
        }
        archivoPar.close();
        cout << "¡Archivo 'bolsa_paralela.csv' listo para comparar!" << endl;

    } else {
        // --- CICLO DEL TRABAJADOR ---
        while (true) {
            int len;
            MPI_Recv(&len, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (len == -1) break; // Recibió señal de que ya no hay más libros

            char* buffer = new char[len + 1];
            MPI_Recv(buffer, len, MPI_CHAR, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[len] = '\0';

            unordered_map<string, int> miConteo = contarPalabras(string(buffer));
            enviarMapa(miConteo, 0);
            delete[] buffer;
        }
    }

    MPI_Finalize();
    return 0;
}