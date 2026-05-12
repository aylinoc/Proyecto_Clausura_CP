#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <chrono>
#include <set>
#include <sstream>
#include <mpi.h>
#include <unordered_map>
#include <algorithm>

using namespace std;

/* --- FUNCIONES GLOBALES --- */

string limpiarTexto(string texto) {
    for (int i = 0; i < (int)texto.length(); i++) {
        texto[i] = tolower((unsigned char)texto[i]);
    }
    static const regex pattern("[^a-zñáéíóú\\s]");
    return regex_replace(texto, pattern, "");
}

// unordered_map para conteo rápido O(1)
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

/* --- FUNCIONES MPI ---
   Todo el mapa se serializa en UN string "palabra:cantidad\n"
   y se manda en 2 mensajes en lugar de 4 por palabra. */

void enviarMapa(unordered_map<string, int>& conteo, int destino) {
    string serializado;
    for (auto const& [palabra, cantidad] : conteo) {
        serializado += palabra + ":" + to_string(cantidad) + "\n";
    }
    int len = (int)serializado.size();
    MPI_Send(&len, 1, MPI_INT, destino, 0, MPI_COMM_WORLD);
    MPI_Send(serializado.c_str(), len, MPI_CHAR, destino, 1, MPI_COMM_WORLD);
}

void descargarLibro(string url, string nombreArchivo) {
    string ruta    = "Libros/" + nombreArchivo;
    string comando = "curl -s -L " + url + " -o " + ruta;
    cout << "Descargando: " << nombreArchivo << "..." << endl;
    system(comando.c_str());
}

/* --- VERSIÓN SERIAL --- */

void ejecutarVersionSerial(const vector<string>& libros, double& tiempoSerialReferencia) {
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
        // Quitar prefijo "Libros/" para que el CSV muestre solo el nombre del archivo
        string nombreCorto = libros[i];
        size_t slash = nombreCorto.rfind('/');
        if (slash != string::npos) nombreCorto = nombreCorto.substr(slash + 1);
        archivoSalida << nombreCorto;
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

    vector<pair<string, string>> fuentes = {
        {"https://www.gutenberg.org/cache/epub/2000/pg2000.txt", "quijote.txt"},
        {"https://www.gutenberg.org/cache/epub/1342/pg1342.txt", "orgullo.txt"},
        {"https://www.gutenberg.org/cache/epub/11/pg11.txt",     "alicia.txt"},
        {"https://www.gutenberg.org/cache/epub/1661/pg1661.txt", "holmes.txt"},
        {"https://www.gutenberg.org/cache/epub/76/pg76.txt",     "huck.txt"},
        {"https://www.gutenberg.org/cache/epub/145/pg145.txt",    "middlemarch.txt"},
        {"https://www.gutenberg.org/cache/epub/2701/pg2701.txt", "moby_dick.txt"},
        {"https://www.gutenberg.org/cache/epub/1513/pg1513.txt", "romeo_juliet.txt"},
        {"https://www.gutenberg.org/cache/epub/4300/pg4300.txt", "ulysses.txt"},
        {"https://www.gutenberg.org/cache/epub/2600/pg2600.txt", "war_and_peace.txt"}
    };

    vector<string> nombresArchivos;
    for (auto const& f : fuentes) nombresArchivos.push_back("Libros/" + f.second);

    double tiempoSerial = 0.0;

    if (rank == 0) {
        for (auto const& f : fuentes) descargarLibro(f.first, f.second);
        ejecutarVersionSerial(nombresArchivos, tiempoSerial);
        cout << "--- FASE SERIAL FINALIZADA ---\n" << endl;
    }

    MPI_Barrier(MPI_COMM_WORLD);

    // ── FASE PARALELA ─────────────────────────────────────────────────────────
    double t_par_inicio = MPI_Wtime();

    if (rank == 0) {
        cout << "--- INICIANDO FASE PARALELA CON " << size << " PROCESOS ---" << endl;

        // Par (nombre del libro, conteo) para mantener la correspondencia correcta
        vector<pair<string, map<string, int>>> conteosParalelos;

        int librosEnviados  = 0;
        int librosRecibidos = 0;
        int numLibros       = (int)nombresArchivos.size();

        // Registra qué libro tiene asignado cada worker en este momento
        map<int, string> libroDeWorker;

        // Primera ronda: un libro por worker
        for (int i = 1; i < size && librosEnviados < numLibros; i++) {
            string nombre = nombresArchivos[librosEnviados];
            libroDeWorker[i] = nombre; // registrar asignación
            librosEnviados++;
            int len = (int)nombre.length();
            MPI_Send(&len,           1,   MPI_INT,  i, 10, MPI_COMM_WORLD);
            MPI_Send(nombre.c_str(), len, MPI_CHAR, i, 11, MPI_COMM_WORLD);
        }

        // El maestro procesa un libro extra si sobran
        bool maestroTrabajando = false;
        map<string, int> conteoMaestro;
        string libroMaestro;
        if (librosEnviados < numLibros) {
            libroMaestro = nombresArchivos[librosEnviados++];
            cout << "Maestro procesando: " << libroMaestro << endl;
            unordered_map<string, int> tmp = contarPalabras(libroMaestro);
            conteoMaestro = map<string, int>(tmp.begin(), tmp.end());
            maestroTrabajando = true;
            librosRecibidos++;
        }

        // Escucha dinámica: recibe de cualquier worker con MPI_ANY_SOURCE
        while (librosRecibidos < numLibros) {
            MPI_Status status;

            // Recibir tamaño desde cualquier worker que haya terminado
            int len;
            MPI_Recv(&len, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);

            // Saber exactamente quién terminó
            int trabajadorLibre = status.MPI_SOURCE;

            // Recibir el contenido del mapa
            vector<char> buffer(len + 1);
            MPI_Recv(buffer.data(), len, MPI_CHAR, trabajadorLibre, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[len] = '\0';

            // Deserializar "palabra:cantidad\n"
            map<string, int> conteo;
            istringstream ss(string(buffer.data(), len));
            string linea;
            while (getline(ss, linea)) {
                if (linea.empty()) continue;
                size_t pos = linea.rfind(':');
                if (pos != string::npos) {
                    conteo[linea.substr(0, pos)] = stoi(linea.substr(pos + 1));
                }
            }

            // Guardar con el nombre correcto del libro (no asumir orden)
            conteosParalelos.push_back({libroDeWorker[trabajadorLibre], conteo});
            librosRecibidos++;

            // Si quedan libros, asignar otro al worker que quedó libre
            if (librosEnviados < numLibros) {
                string ruta = nombresArchivos[librosEnviados];
                libroDeWorker[trabajadorLibre] = ruta; // actualizar asignación
                librosEnviados++;
                int rlen = (int)ruta.length();
                MPI_Send(&rlen,        1,    MPI_INT,  trabajadorLibre, 10, MPI_COMM_WORLD);
                MPI_Send(ruta.c_str(), rlen, MPI_CHAR, trabajadorLibre, 11, MPI_COMM_WORLD);
            }
        }

        // Agregar conteo del maestro si trabajó
        if (maestroTrabajando) {
            conteosParalelos.push_back({libroMaestro, conteoMaestro});
        }

        // Ordenar para que queden en el mismo orden que nombresArchivos (igual que el serial)
        sort(conteosParalelos.begin(), conteosParalelos.end(),
            [&nombresArchivos](const pair<string, map<string,int>>& a,
                               const pair<string, map<string,int>>& b) {
                auto posA = find(nombresArchivos.begin(), nombresArchivos.end(), a.first);
                auto posB = find(nombresArchivos.begin(), nombresArchivos.end(), b.first);
                return posA < posB;
            });

        // Señal de fin a todos los workers
        for (int i = 1; i < size; i++) {
            int fin = -1;
            MPI_Send(&fin, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
        }

        double t_par_fin      = MPI_Wtime();
        double tiempoParalelo = t_par_fin - t_par_inicio;

        cout << "\n========================================" << endl;
        cout << "RESUMEN DE RESULTADOS:" << endl;
        cout << "Tiempo Serial:   " << tiempoSerial   << " s" << endl;
        cout << "Tiempo Paralelo: " << tiempoParalelo << " s" << endl;
        if (tiempoParalelo > 0) {
            double speedup = tiempoSerial / tiempoParalelo;
            cout << "Speed-up:        " << speedup << "x" << endl;
            cout << "Eficiencia:      " << (speedup / (size - 1)) * 100 << "%" << endl;
        }
        cout << "========================================\n" << endl;

        // Generar CSV paralelo (fuera del tiempo medido)
        cout << "Generando bolsa_paralela.csv..." << endl;

        set<string> vocabularioParalelo;
        for (const auto& [nombre, mapa] : conteosParalelos)
            for (auto const& [palabra, cant] : mapa)
                vocabularioParalelo.insert(palabra);

        ofstream archivoPar("Resultados/bolsa_paralela.csv");
        unsigned char bom[] = {0xEF, 0xBB, 0xBF};
        archivoPar.write((char*)bom, sizeof(bom));

        archivoPar << "Libro";
        for (const string& p : vocabularioParalelo) archivoPar << "," << p;
        archivoPar << "\n";

        // Usar el nombre guardado en el par, no el índice del vector
        for (const auto& [nombre, mapa] : conteosParalelos) {
            // Quitar prefijo "Libros/" para que el CSV muestre solo el nombre del archivo
            string nombreCorto = nombre;
            size_t slash = nombreCorto.rfind('/');
            if (slash != string::npos) nombreCorto = nombreCorto.substr(slash + 1);
            archivoPar << nombreCorto;
            for (const string& p : vocabularioParalelo) {
                auto it = mapa.find(p);
                archivoPar << "," << (it != mapa.end() ? it->second : 0);
            }
            archivoPar << "\n";
        }
        archivoPar.close();
        cout << "¡Archivo 'bolsa_paralela.csv' listo!" << endl;

    } else {
        // ── CICLO DEL TRABAJADOR ─────────────────────────────────────────────
        while (true) {
            int len;
            MPI_Recv(&len, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (len == -1) break; // Señal de fin

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