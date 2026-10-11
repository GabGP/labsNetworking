''' #####################
Función para leer el archivo y crea estructura {Nodo1, Nodo2, Costo}
Retorna el nodo inicial y la estructura.
'''
def routingFile(file_path):
    routeDict = {}
    initNode = None
    
    with open(file_path, 'r', encoding='utf-8-sig') as file:
        # Ignorar líneas vacías y espacios
        lines = [l.replace(' ', '').strip() for l in file if l.strip()]
        if not lines: raise SystemExit("\n\t> El archivo está vacío.\n")
        # La primera línea siempre será el nodo inicial.
        initNode = lines[0]

        # Procesar enlaces en el formato: Nodo1-Nodo2:Costo
        for line in lines[1:]:
            try:
                partes = line.split(':')
                node1, node2 = partes[0].split('-')
                cost = int(partes[1])
            except (ValueError, IndexError):
                raise SystemExit(f"\n\t> Línea inválida '{line}', se espera Nodo1-Nodo2:Costo\n")
            
            # Definir el nodo en la estructura si no existe.
            if node1 not in routeDict:
                routeDict[node1] = {}
            if node2 not in routeDict:
                routeDict[node2] = {}
            
            # Asignar el costo entre nodos en ambos sentidos
            # Nodo1 -> Nodo2   o   Nodo2 -> Nodo1
            routeDict[node1][node2] = cost
            routeDict[node2][node1] = cost

    # Retorno: nodo inicial y la estructura
    return initNode, routeDict











