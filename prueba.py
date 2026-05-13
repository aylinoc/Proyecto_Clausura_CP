from pathlib import Path
import csv

# Este script asume esta estructura:
# tu_proyecto/
# ├── contar_modern_resultados.py
# └── resultados/
#     ├── bolsa_paralela.csv
#     └── bolsa_serial.csv

CARPETA_RESULTADOS = Path(__file__).parent / "Resultados"

ARCHIVOS = [
    "bolsa_paralela.csv",
    "bolsa_serial.csv",
]

PALABRA = "modern"


def contar_modern_en_csv(ruta_csv):
    """
    Busca la columna exacta 'modern' dentro del CSV y suma sus valores.
    Esto cuenta cuántas veces aparece la palabra modern según el conteo del archivo.
    """
    with open(ruta_csv, mode="r", encoding="utf-8-sig", errors="replace", newline="") as archivo:
        lector = csv.DictReader(archivo)

        if PALABRA not in lector.fieldnames:
            raise ValueError(f"No encontré la columna '{PALABRA}' en {ruta_csv}")

        conteo_total = 0
        desglose = {}

        # La primera columna tiene el nombre del libro: libro1, libro2, etc.
        primera_columna = lector.fieldnames[0]

        for fila in lector:
            libro = fila[primera_columna]
            conteo = int(fila[PALABRA])
            desglose[libro] = conteo
            conteo_total += conteo

    return conteo_total, desglose


def main():
    total_general = 0

    print(f"Conteo de la palabra exacta: {PALABRA}\n")

    for nombre_archivo in ARCHIVOS:
        ruta_csv = CARPETA_RESULTADOS / nombre_archivo

        if not ruta_csv.exists():
            raise FileNotFoundError(f"No encontré el archivo: {ruta_csv}")

        total_archivo, desglose = contar_modern_en_csv(ruta_csv)
        total_general += total_archivo

        print(f"Archivo: {nombre_archivo}")
        print(f"Total: {total_archivo}")
        print("Desglose por libro:")

        for libro, conteo in desglose.items():
            print(f"  {libro}: {conteo}")

        print()

    print(f"Total general en ambos archivos: {total_general}")


if __name__ == "__main__":
    main()
