INF = float('inf')

'''
Función donde Inicia la Ejecución del Algoritmo de Dijkstra
Retorna lista de tuplas: (paso, "D(v),p(v)" por cada nodo != initNode en orden alfabético, nodo agregado a N')
'''
def init(initNode, routeDict):
	others = sorted(n for n in routeDict if n != initNode)
	# Paso 0: vecinos D(v) = c(u,v); el resto infinito (999) con p(v) = u
	D = {v: routeDict[initNode].get(v, INF) for v in others}
	p = {v: initNode for v in others}
	visited = [initNode]

	def row(step, added):
		return (step, *(f"{999 if D[v] == INF else D[v]},{p[v]}" for v in others), added)

	steps = [row(0, initNode)]
	while len(visited) < len(routeDict):
		# Mínimo D(w); empate -> primero alfabético (others está ordenado y min() conserva el primero)
		w = min((v for v in others if v not in visited), key=lambda v: D[v])
		visited.append(w)
		for v, cost in routeDict[w].items():
			# Empate de costo -> p(v) cambia al nuevo nodo w (por eso <=)
			if v not in visited and D[w] + cost <= D[v]:
				D[v], p[v] = D[w] + cost, w
		steps.append(row(len(visited) - 1, w))
	return steps


if __name__ == "__main__":
	# Ejemplo 2 del documento: empate D(h)=6 cambia p(h) a g
	topo2 = {'f': {'g': 5, 'h': 6}, 'g': {'f': 5, 'h': 1, 'i': 4}, 'h': {'g': 1, 'f': 6, 'i': 2}, 'i': {'g': 4, 'h': 2}}
	assert init('f', topo2) == [
		(0, "5,f", "6,f", "999,f", "f"),
		(1, "5,f", "6,g", "9,g", "g"),
		(2, "5,f", "6,g", "8,h", "h"),
		(3, "5,f", "6,g", "8,h", "i"),
	]
	print("ok")
