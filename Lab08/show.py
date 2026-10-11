''' #####################
Función Muestra la estructura {Nodo1, Nodo2, Costo}
Valores no definidos coloca el simbolo ∞
'''
def routeStruct(initNode, routeDict):
    nodes = sorted(routeDict.keys())
    fixWidth = 2

    # Función Separador Entrelineado de la Tabla
    def create_separator(length):
        return "+" + "+".join(["-" * (fixWidth + 2)] * length) + "+"
    
    print("\n\tTabla: Costos - Nodo a Nodo\n\tNodo Inicial: " + initNode)
    # Inicio de la tabla
    print(create_separator(len(nodes) + 1))
    # Encabezado de la tabla
    print("|" + f" ".rjust(fixWidth + 2) + "|" + "".join(f" {node} ".rjust(fixWidth + 2) + "|" for node in nodes) )
    # Separador del encabezado
    print(create_separator(len(nodes) + 1))

    # Filas de la Tabla
    for node1 in nodes:
        print( f"| {node1.rjust(fixWidth)} |" + "".join(f" {str(routeDict[node1].get(node2, '∞')).rjust(fixWidth)} |" for node2 in nodes) )
        # Separador entre lineas
        print(create_separator(len(nodes) + 1))



''' #####################
Función Muestra todos pasos del Algoritmo Dijkstra
Valores no definidos coloca el simbolo ∞
'''
def stepsCalculatingRoute(initNode, routeDict, stepsCalcs):
    nodes = sorted(routeDict.keys())
    fixWidth = 4

    # Función Separador Entrelineado de la Tabla
    def create_separator(length):
        ##  número de ancho
        ##  + 6 + ... + 11 + ... + 4 +
        return "+" + "-"*(length+2) + "+" + "+".join(["-"*(length+7)] * (len(nodes)-1)) + "+" + "-"*(length) + "+"

    print("\n\tTabla: Pasos de Algoritmo Dijkstra\n\tNodo Inicial: " + initNode)
    # Separador del encabezado
    print(create_separator(fixWidth))
    # Imprime los Nodos en N
    lenseparator = len(create_separator(fixWidth))-(fixWidth+6)
    lenheader = len(", ".join(nodes))
    print( "|    N | " + ", ".join(nodes) + f"".ljust(lenseparator-lenheader) + "|" )
    # Separador del encabezado
    print(create_separator(fixWidth))

    # Imprime el Header
    print("| Paso " + "".join(f"| D({node}),p({node}) " for node in nodes if node != initNode) + "| N' |")
    # Separador del encabezado
    print(create_separator(fixWidth))

    # Filas de la Tabla
    for node1 in stepsCalcs:
        ##  ##  número de ancho
        ##  | 4 | ... | 11 | ... | 4 |
        print( f"| {str(node1[0]).rjust(fixWidth)} |" + "".join(f" {node} ".rjust(fixWidth + 7) + "|" for node in node1[1:-1]) + f"{str(node1[-1]).rjust(fixWidth-1)} |" )
        print(create_separator(fixWidth))

    


