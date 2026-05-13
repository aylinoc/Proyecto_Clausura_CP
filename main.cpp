#include <iostream>
#include <fstream>        // Para el manejo de archivos de entrada/salida
#include <string>
#include <vector>
#include <map>            // Para el mapa ordenado final
#include <regex>          // Para la limpieza de texto con expresiones regulares
#include <chrono>
#include <set>            // Para gestionar el vocabulario único global
#include <sstream>        // Para la deserialización de strings (istringstream)
#include <mpi.h>
#include <unordered_map>  // Para conteo rápido (Hash Table O(1))
#include <algorithm>      // Para el ordenamiento de resultados finales

using namespace std;


//FUNCIONES GLOBALES 

// Funcion de limpieza: Normaliza cada palabra antes de contarla. 
//Se pasa todo a minúsculas y se eliminan signos, números u otros caracteres que no aportan al conteo de palabras.
string limpiarTexto(string texto) {
    for (int i = 0; i < (int)texto.length(); i++) {
        texto[i] = tolower((unsigned char)texto[i]);
    }
    // Se conservan letras normales, acentos, ñ y espacios.
    static const regex pattern("[^a-zñáéíóú\\s]");
    return regex_replace(texto, pattern, "");
}

// Función de conteo: Lee un archivo completo y cuenta cuántas veces aparece cada palaba
// Se usa unordered_mao porque permite actualizar el conteo de forma rápida
unordered_map<string, int> contarPalabras(string nombreArchivo) {
    unordered_map<string, int> frecuencias;
    ifstream archivo(nombreArchivo);
    string palabra;

    // Si el archivo no existe o no se pudo abrir, regresa el mapa vacío
    if (!archivo.is_open()) {
        cerr << "Error al abrir: " << nombreArchivo << endl;
        return frecuencias;
    }

    // Lee palabra por palabra separando espacios
    while (archivo >> palabra) {
        palabra = limpiarTexto(palabra);
        if (!palabra.empty()) {
            frecuencias[palabra]++;
        }
    }
    archivo.close();
    return frecuencias;
}

// Función para descargar los libros: Baja los libros desde Project Gutenberg y los guarda en la carpeta Libros
void descargarLibro(string url, string nombreArchivo) {
    string ruta    = "Libros/" + nombreArchivo;
    string comando = "curl -s -L " + url + " -o " + ruta;
    cout << "Descargando: " << nombreArchivo << "..." << endl;
    system(comando.c_str());
}


// FUNCION PARA MPI - PARTE PARALELA

// Serializa el mapa: Convierte la estructura de datos en un flujo de bytes (string)
void enviarMapa(unordered_map<string, int>& conteo, int destino) {
    string serializado;
    for (auto const& [palabra, cantidad] : conteo) {
        // Formato simple para enviar todo el mapa: "palabra:cantidad\n"
        serializado += palabra + ":" + to_string(cantidad) + "\n";
    }
    int len = (int)serializado.size();
    // Paso 1: Enviar metadato (tamaño del string) para que el receptor prepare su buffer
    MPI_Send(&len, 1, MPI_INT, destino, 0, MPI_COMM_WORLD);
    // Paso 2: Enviar el contenido real (el string serializado)
    MPI_Send(serializado.c_str(), len, MPI_CHAR, destino, 1, MPI_COMM_WORLD);
}


// FUNCIÓN PARA IMPLEMENTAR LA VERSIÓN SERIAL

void ejecutarVersionSerial(const vector<string>& libros, double& tiempoSerialReferencia) {
    cout << "Iniciando versión serial" << endl;
    auto inicio = chrono::high_resolution_clock::now();

    // Guarda el conteo de cada libro por separado
    vector<unordered_map<string, int>> todosLosConteos;
    set<string> vocabularioGlobal;

    // Se procesa cada libro uno por uno
    for (const string& libro : libros) {
        // Para cada libro crea un mapa con sus conteos
        unordered_map<string, int> conteoLibro = contarPalabras(libro);
        todosLosConteos.push_back(conteoLibro);
        // Cada palabra encontrada se agrega al vocabulario general
        for (auto const& [palabra, cantidad] : conteoLibro) {
            vocabularioGlobal.insert(palabra);
        }
    }

    // Detenemos la medición del tiempo, para qur corresponda solo al procesamiento del texto (por Ley de Amhdal)
    auto fin = chrono::high_resolution_clock::now();
    chrono::duration<double> tiempo = fin - inicio;
    tiempoSerialReferencia = tiempo.count();

    cout << "Procesamiento Serial: " << tiempoSerialReferencia << " s" << endl;

    ofstream archivoSalida("Resultados/bolsa_serial.csv");
    // Para que Excel abra correctamente los caracteres con acentos y ñ
    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
    archivoSalida.write((char*)bom, sizeof(bom));

    archivoSalida << "Libro";
    for (const string& palabra : vocabularioGlobal) archivoSalida << "," << palabra;
    archivoSalida << "\n";

    // Cada fila representa un libro y cada columna la frecuencia de una palabra
    for (size_t i = 0; i < libros.size(); ++i) {
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

// MAIN 

int main(int argc, char** argv) {
    // Inicia el entorno de ejecución paralelo
    MPI_Init(&argc, &argv);
    int rank, size;
    MPI_Comm_rank(MPI_COMM_WORLD, &rank); // ID del proceso actual
    MPI_Comm_size(MPI_COMM_WORLD, &size); // Total de procesos lanzados

    // Lista de libros a descargar y procesar
    vector<pair<string, string>> fuentes = {
        {"https://www.gutenberg.org/cache/epub/84/pg84.txt", "libro1"},
        {"https://www.gutenberg.org/cache/epub/2701/pg2701.txt", "libro2"},
        {"https://www.gutenberg.org/cache/epub/1342/pg1342.txt", "libro3"},
        {"https://www.gutenberg.org/cache/epub/1184/pg1184.txt", "libro4"},
        {"https://www.gutenberg.org/cache/epub/1513/pg1513.txt", "libro5"},
        {"https://www.gutenberg.org/cache/epub/25851/pg25851.txt", "libro6"},
        {"https://www.gutenberg.org/cache/epub/2554/pg2554.txt", "libro7"}
    };

    vector<string> nombresArchivos;
    for (auto const& f : fuentes) nombresArchivos.push_back("Libros/" + f.second);

    double tiempoSerial = 0.0;

    // El proceso 0 actua como maestro, solo el descarga los libros y ejecuta la versión serial
    if (rank == 0) {
        for (auto const& f : fuentes) descargarLibro(f.first, f.second);
        ejecutarVersionSerial(nombresArchivos, tiempoSerial);
        cout << "Fase serial finalizada\n" << endl;
    }

    // Barrera de sincronización: Todos los procesos esperen aquí hasta que el maestro termine la fase serial
    MPI_Barrier(MPI_COMM_WORLD);

    // FASE PARALELA
    double t_par_inicio = MPI_Wtime();

    if (rank == 0) {
        cout << "Iniciando fase paralela con " << size << " procesos" << endl;

        // Se guarda el nombre del libro junto con su conteo
        vector<pair<string, map<string, int>>> conteosParalelos;

        int librosEnviados  = 0;
        int librosRecibidos = 0;
        int numLibros       = (int)nombresArchivos.size();

        // Registra qué libro tiene asignado cada worker en este momento
        map<int, string> libroDeWorker;

        // Distribución inicial: El maestro asigna un libro a cada proceso disponible
        for (int i = 1; i < size && librosEnviados < numLibros; i++) {
            string nombre = nombresArchivos[librosEnviados];
            libroDeWorker[i] = nombre; // registrar asignación
            librosEnviados++;
            int len = (int)nombre.length();

            // Se manda primero la longitud de la ruta y luego la ruta como texto
            // Tags 10 y 11 usados específicamente para instrucciones de control (Nombres de archivos)
            MPI_Send(&len,           1,   MPI_INT,  i, 10, MPI_COMM_WORLD);
            MPI_Send(nombre.c_str(), len, MPI_CHAR, i, 11, MPI_COMM_WORLD);
        }

        // El maestro también puede trabajar si todavía quedan libros sin asignar
        bool maestroTrabajando = false;
        map<string, int> conteoMaestro;
        string libroMaestro;

        if (librosEnviados < numLibros) {
            libroMaestro = nombresArchivos[librosEnviados++];

            unordered_map<string, int> tmp = contarPalabras(libroMaestro);
            conteoMaestro = map<string, int>(tmp.begin(), tmp.end());

            maestroTrabajando = true;
            librosRecibidos++;
        }

        // Bucle de Escucha Dinámica
        while (librosRecibidos < numLibros) {
            MPI_Status status;

            int len;
            // Recibe de CUALQUIER proceso que haya terminado (el primero que llegue)
            MPI_Recv(&len, 1, MPI_INT, MPI_ANY_SOURCE, 0, MPI_COMM_WORLD, &status);
            // Con status se identifica cuál worker fue el que terminó
            int trabajadorLibre = status.MPI_SOURCE;

            // Recibe el string serializado con el conteo del libro
            vector<char> buffer(len + 1);
            MPI_Recv(buffer.data(), len, MPI_CHAR, trabajadorLibre, 1, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[len] = '\0';

            // Deserialización: Reconstruye el mapa a partir del string recibido y líneas tipo "palabra:cantidad"
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

            // Se guarda el resultado con el nombre del libro que tenía asignado ese worker
            conteosParalelos.push_back({libroDeWorker[trabajadorLibre], conteo});
            librosRecibidos++;

            // Reasignación Inmediata: Si hay más libros,se aprovecha el worker que queda libre.
            if (librosEnviados < numLibros) {
                string ruta = nombresArchivos[librosEnviados];
                libroDeWorker[trabajadorLibre] = ruta; // actualizar asignación
                librosEnviados++;
                int rlen = (int)ruta.length();
                MPI_Send(&rlen,        1,    MPI_INT,  trabajadorLibre, 10, MPI_COMM_WORLD);
                MPI_Send(ruta.c_str(), rlen, MPI_CHAR, trabajadorLibre, 11, MPI_COMM_WORLD);
            }
        }

        // Si el maestro también procesó un libro, también se agrega a los resultados
        if (maestroTrabajando) {
            conteosParalelos.push_back({libroMaestro, conteoMaestro});
        }

        // Ordenar para que queden en orden alfabetico
        sort(conteosParalelos.begin(), conteosParalelos.end(),
            [&nombresArchivos](const pair<string, map<string,int>>& a,
                               const pair<string, map<string,int>>& b) {
                auto posA = find(nombresArchivos.begin(), nombresArchivos.end(), a.first);
                auto posB = find(nombresArchivos.begin(), nombresArchivos.end(), b.first);
                return posA < posB;
            });

        // Finalización: Envía (len = -1) a cada worker como señal de muerte
        for (int i = 1; i < size; i++) {
            int fin = -1;
            MPI_Send(&fin, 1, MPI_INT, i, 10, MPI_COMM_WORLD);
        }

        // Detiene la medición del tiempo y calcula el tiempo total de la parte paralela
        double t_par_fin     = MPI_Wtime();
        double tiempoParalelo = t_par_fin - t_par_inicio;

        // Generar CSV paralelo 
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

                // Se escribe una fila por libro
                for (const auto& [nombre, mapa] : conteosParalelos) {
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

        // Impresión de tiempos y cálculo de Speed-up
        cout << "RESUMEN DE RESULTADOS:" << endl;
        cout << "Tiempo Serial:   " << tiempoSerial   << " s" << endl;
        cout << "Tiempo Paralelo: " << tiempoParalelo << " s" << endl;

        if (tiempoParalelo > 0) {
            double speedup = tiempoSerial / tiempoParalelo;
            cout << "Speed-up:        " << speedup << "x" << endl;
        }

    } else {
        while (true) {
            int len;
            // Cada worker espera instrucción del maestro
            MPI_Recv(&len, 1, MPI_INT, 0, 10, MPI_COMM_WORLD, MPI_STATUS_IGNORE);

            if (len == -1) break; // Señal de fin

            // Recibe el nombre del archivo a procesar
            char* buffer = new char[len + 1];
            MPI_Recv(buffer, len, MPI_CHAR, 0, 11, MPI_COMM_WORLD, MPI_STATUS_IGNORE);
            buffer[len] = '\0';

            //El worker cuenta las palabras de su libro y regresa el mapa al maestro
            unordered_map<string, int> miConteo = contarPalabras(string(buffer));
            enviarMapa(miConteo, 0);
            delete[] buffer;
        }
    }

    MPI_Finalize();
    return 0;
}