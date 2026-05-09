#include <iostream>
#include <fstream>
#include <string>
#include <vector>
#include <map>
#include <regex>
#include <chrono> 
#include <set>

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

int main() {
    // Por ahora, crea manualmente archivos libro1.txt, libro2.txt en tu carpeta
    vector<string> libros = {"libro1.txt", "libro2.txt", "libro3.txt"};

    ejecutarVersionSerial(libros);

    return 0;
}