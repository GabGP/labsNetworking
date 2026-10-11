# Laboratorio #8: LinkState

En este laboratorio se proporcionan los siguientes archivos para facilitar la implementación:


* `LinkState.py` inicio de programa.
* `read.py` función para la lectura de archivo.
* `show.py` funciones para el despliegue de información.
* `dijkstra.py` implementación de Algoritmo Dijkstra
* `README.md`
* `test/`
	* `topo1.txt`
	* `topo2.txt`
	* `topo3.txt`
	* `topo4.txt`
	* `topo5.txt`
	* `topo6.txt`
	* `topo7.txt`
* `docs/`
	* `Hoja Topologías.pdf`
	* `Laboratorio #8 - Link State.pdf`

- La carpeta `test/` estan todas las topologías a ejecutar en su laboratorio de la forma: 

```bash
python LinkState.py test/topo1.txt
```

**NOTA:** 
Siéntete libre de modificar cualquiera de los archivos según tu conveniencia, siempre manteniendo un orden adecuado.
El nodo de inicio siempre será la primera letra en orden alfabético, ya que los nombres pueden ser asignados arbitrariamente. Si se utilizan números, el valor menor será el inicial. Para este laboratorio, esta condición siempre se cumplirá, lo que evita la necesidad de implementar un manejo de despligue adicional de columnas para mostrar la tabla de pasos del algoritmo.


## Modo de Ejecución

Para ejecutar el programa, utiliza el siguiente comando en la terminal:

```bash
python LinkState.py <param1>
```

Reemplaza `<param1>` con el archivo de topología que desees utilizar. Por ejemplo, puedes usar uno de los archivos disponibles: `topo1.txt`, `topo2.txt`, ..., hasta `topo7.txt`.

## Archivos de Topología

El laboratorio incluye los siguientes archivos de topología:

### Ejemplos del Documento de Laboratorio
- `topo1.txt`: Ejemplo 1
- `topo2.txt`: Ejemplo 2
- `topo3.txt`: Ejemplo 3

### Ejercicios de la Hoja de Topología
- `topo4.txt`: Ejercicio 1
- `topo5.txt`: Ejercicio 2
- `topo6.txt`: Ejercicio 3

### Topología para Argumentar el Resultado
- `topo7.txt`

## Instalación de Python 3

Para ejecutar el proyecto, necesitarás tener Python 3 instalado en tu sistema. A continuación se indican las instrucciones de instalación para diferentes sistemas operativos:

### Windows
1. Descarga el instalador de Python desde [python.org](https://www.python.org/downloads/).
2. Ejecuta el instalador y asegúrate de marcar la opción "Add Python to PATH".
3. Completa la instalación siguiendo las instrucciones en pantalla.

### Ubuntu
Puedes instalar Python 3 utilizando el siguiente comando en la terminal:

```bash
sudo apt update
sudo apt install python3
```

### MacOS
Para instalar Python 3 en MacOS, puedes utilizar Homebrew. Si no tienes Homebrew instalado, primero instálalo desde [brew.sh](https://brew.sh/) y luego ejecuta:

```bash
brew install python
```

## Revisión de Versión de Python

Antes de ejecutar el programa, verifica que estés utilizando Python 3. Puedes comprobar la versión instalada con el siguiente comando en la terminal:

```bash
python --version
```

Si tienes Python 3 instalado bajo otra variable, utiliza:

```bash
python3 --version
```

Asegúrate de que la versión sea 3.x.x para garantizar la compatibilidad del código.

El código entregado en el laboratorio incluye varias validaciones, entre ellas, una para comprobar la versión de Python.










