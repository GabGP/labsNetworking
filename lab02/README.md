# Java Web Server

Este Laboratorio es una implementación de un servidor web en Java.
Incluye clases para manejar solicitudes y respuestas, así como para formatear y gestionar la ejecución del servidor.

## Requisitos

- Java Development Kit (JDK) 21 o superior
- Make (utilidad de compilación)

## Estructura del Laboratorio

- `Request.java`: Maneja las solicitudes entrantes.  `✨ SE PUEDE MODIFICAR ✨`
- `Response.java`: Maneja las respuestas del servidor. `✨ SE PUEDE MODIFICAR ✨`
- `FormatterWebServer.java`: Formatea los datos para la salida del servidor. `❌ NO MODIFICAR ❌`
- `ThreadServer.java`: Gestiona los hilos del servidor. `❌ NO MODIFICAR ❌`
- `Server.java`: Clase principal que inicia el servidor. `❌ NO MODIFICAR ❌`
- `Makefile`: Nos ayuda a limpiar, compilar y ejecutar nuestro código. `✨ SE PUEDE MODIFICAR ✨`

Puede crear más clases si lo desea para usarlas en `Request.java` o `Response.java`, no olvide agregar las nuevas clases en `Makefile`.

## Uso del Makefile

El `Makefile` incluido permite compilar y ejecutar el laboratorio de manera sencilla.

### Comandos

1. **Compilar todas las clases:**

    ```bash
    make
    ```

    o

    ```bash
    make classes
    ```

    Esto compilará todas las clases `.java` y generará los archivos `.class` correspondientes.

2. **Ejecutar el servidor:**

    ```bash
    make run
    ```
    Esto ejecutará la clase principal `Server`. 
	
	Puedes pasar argumentos adicionales utilizando la variable `ARGS`:

    ```bash
    make run ARGS="argumentos"
    ```
3. **Argumentos de Compilación:**

	Puedes pasar los siguientes argumentos para ejecutar el servidor:

	- `-port <int>`: Define el puerto en el cual el servidor estará esperando las solicitudes del cliente. `Por defecto: 1000`
	- `-threads <int>`: Define el número de Threads que utilizará el Threadpool. `Por defecto: 2`
	- `-delay <int>`: Define el tiempo de espera (en segundos) antes de reutilizar el Thread.  `Por defecto: 5`
	- `-help`: Muestra la ayuda y detalles sobre cómo usar el servidor. Si existe este argumento no inicia el servidor.

	No es necesario que definir todos los argumentos y tampoco su orden.

	```bash 
		make run ARGS="-port 1000 -threads 3 -delay 5 -help"
	```

	o si ya fue compilado y se quiere ejecutar directamente 

	```bash 
		java Server -port 1000 -threads 3 -delay 5 -help
	```

4. **Limpiar los archivos compilados:**

    ```bash
    make clean
    ```

    Esto eliminará todos los archivos `.class` generados y los registros del servidor en la carpeta `logs/`

## Ejemplo de Uso

Para compilar y ejecutar el servidor, sigue estos pasos:

```bash
make
make run
```

Para limpiar el proyecto:

```bash
make clean
```