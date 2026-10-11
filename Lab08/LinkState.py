import sys
import os
import read
import show
import dijkstra

def validations():
    # Validar versión de Python
    if sys.version_info[0] != 3: print("\n\t> No estás utilizando Python 3\n"); sys.exit(1)

    # Validar argumentos
    if len(sys.argv) < 2: print("\n\t> Use: python LinkState.py <param1>\n"); sys.exit(1)

    # Validar existencia del archivo
    global file_path 
    file_path = sys.argv[1]
    if not os.path.exists(file_path): print(f"\n\t> El archivo {file_path} no existe.\n"); sys.exit(1)


# Inicio de Programa 
if __name__ == "__main__":
    # La consola de Windows (cp1252) no puede imprimir '∞'
    sys.stdout.reconfigure(encoding="utf-8")
    sys.stderr.reconfigure(encoding="utf-8")
    validations()

    # Lectura de Archivo TXT
    initNode, routeDict = read.routingFile(file_path)

    # Imprime la Tabla de Ruteo
    show.routeStruct(initNode, routeDict)

    ###################################################
    # Lo siguiente es una sugerenica de implementación

    # Retorna y ejecuta los Pasos con el Algoritmo Dijkstra
    stepsCalcs = dijkstra.init(initNode, routeDict)

    # Imprime los Pasos generados del Algoritmo
    show.stepsCalculatingRoute(initNode, routeDict, stepsCalcs)












