# Explicación del Laboratorio #02: Servidor Web HTTP (RFC 2616)

Este documento detalla los requerimientos del [Laboratorio #02](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Laboratorio%20%2302.pdf), analiza la arquitectura y estado del código existente en la solución, y proporciona una guía paso a paso para completar la implementación requerida.

---

## 1. ¿Qué pide el laboratorio?

El objetivo principal del laboratorio es desarrollar un servidor web multi-hilo en Java capaz de procesar peticiones HTTP entrantes y enviar respuestas HTTP conformes al estándar **RFC 2616 (HTTP/1.1)**.

### Requerimientos Clave:

1. **Manejo de Conexiones Concurrente**:
   - El servidor debe escuchar conexiones TCP en un puerto configurable y gestionarlas mediante un `ThreadPoolExecutor` con hilos de ejecución concurrentes (esto ya lo provee la plantilla base).

2. **Procesamiento de Solicitudes HTTP (`Request.java`)**:
   - **Lectura completa**: Leer encabezados (Headers) y cuerpo (Body) de las peticiones TCP.
   - **Métodos soportados**: Clasificar y manejar métodos `GET`, `POST` y `HEAD`.
   - **Almacenamiento de Headers**: Almacenar encabezados en una estructura de datos adecuada (`HashMap` / `Map`).
   - **Extracción de Parámetros/Body**:
     - Extraer parámetros `QUERY` de URLs en peticiones `GET` (`?key=value&...`).
     - Extraer y procesar el cuerpo en peticiones `POST` soportando distintos tipos de contenido:
       - `application/x-www-form-urlencoded`
       - `application/json`
       - `multipart/form-data`
   - **Gestión de errores de Request**: Detectar sintaxis inválida (400 Bad Request) o métodos no permitidos (405 Method Not Allowed).

3. **Generación de Respuestas HTTP (`Response.java`)**:
   - **Enrutamiento Dinámico**: Buscar y servir los archivos solicitados que se encuentran en el directorio local `./www/`.
   - **MIME Types / Content-Type**: Asignar el encabezado `Content-Type` correcto según la extensión del archivo (`.html`, `.css`, `.js`, `.json`, `.jpeg`, `.png`, `.svg`, fuentes, etc.).
   - **Content-Length exacto**: Calcular y adjuntar la longitud exacta en bytes del cuerpo enviado.
   - **Preprocesamiento de plantillas `.cc8`**:
     - Los archivos con extensión `.cc8` son plantillas HTML que contienen etiquetas en formato `{keyValue}`.
     - El servidor debe reemplazar estas etiquetas por los valores correspondientes enviados en el formulario (ya sea mediante parámetros `GET` o en el cuerpo `POST`).
   - **Manejo de Métodos**:
     - `GET`: Retornar headers + cuerpo procesado/recurso.
     - `HEAD`: Retornar exactamente los mismos headers que `GET` (incluyendo `Content-Length`), pero **sin enviar el cuerpo de la respuesta**.
     - `POST`: Procesar parámetros entrantes, reemplazar etiquetas en la plantilla `.cc8` objetivo y retornar la respuesta HTML.
   - **Respuestas de Error**:
     - `404 Not Found`: Si el recurso no existe en `./www/`.
     - `405 Method Not Allowed`: Si se recibe un método no soportado.
     - `400 Bad Request`: Si la petición no se puede parsear.
     - `500 Internal Server Error`: En caso de fallas inesperadas de I/O o ejecución.

4. **Escenarios de Prueba Proporcionados (`./www/`)**:
   - `index.html`: Página índice principal.
   - `test01/`: Recursos estáticos (hojas de estilo CSS, imágenes, fuentes).
   - `test02/`: Procesamiento de formularios `GET` y `POST` con plantillas `.cc8`.
   - `test03/`: Sitio web completo con funcionalidad dinámica.

5. **Restricciones de Código**:
   - ❌ **NO modificar**: [Server.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Server.java), [ThreadServer.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/ThreadServer.java) ni [FormatterWebServer.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/FormatterWebServer.java).
   - ✔️ **SI modificar / implementar**: [Request.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Request.java) y [Response.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Response.java) (respetando sus firmas de método). Se pueden crear clases auxiliares si es necesario.

---

## 2. ¿Qué hace el código actual y cómo lo hace?

El proyecto ya cuenta con una estructura base funcional y algunas clases auxiliares creadas para separar responsabilidades.

### Arquitectura Actual:

1. **[Lab02.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Lab02.java)**:
   - Contiene la función `main`. Parsea los argumentos por consola (`-port`, `-threads`, `-delay`, `-help`) y crea la instancia de `Server`.

2. **[Server.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Server.java)**:
   - Inicializa el `ServerSocket` en el puerto especificado (por defecto `1000`) y crea un pool de hilos (`Executors.newFixedThreadPool`). Instancia y ejecuta `ThreadServer` para cada hilo.

3. **[ThreadServer.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/ThreadServer.java)**:
   - Ciclo principal de atención a clientes. En cada iteración:
     1. Llama a `serverSocket.accept()` esperando una conexión TCP.
     2. Prepara un `BufferedReader` (`dataIn`) y `PrintStream` (`dataOut`).
     3. Configura el logger en la carpeta `logs/`.
     4. Ejecuta:
        ```java
        Object requestDataObj = threadRequest.getData(LOGGER, dataIn, nThreadServer);
        threadResponse.sendData(LOGGER, dataOut, nThreadServer, requestDataObj);
        ```
     5. Cierra el socket del cliente y descansa el hilo según el `delay`.

4. **[HttpRequestData.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/HttpRequestData.java)** *(Clase auxiliar de datos)*:
   - Encapsula los datos de la petición HTTP procesada: `method`, `path`, `rawUri`, `httpVersion`, un `Map<String, String>` de `headers`, un `Map<String, String>` de `params` (GET query y POST body params), `body`, `errorCode` y `errorMessage`.

5. **[RequestHelper.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/RequestHelper.java)** *(Clase auxiliar de parseo)*:
   - Contiene funciones estáticas para:
     - `parseUriAndQueryParams`: Separar el path del query string `?key=val` y decodificar URL.
     - `parseRequestBody`: Identificar el `Content-Type` de la petición y llamar a:
       - `parseFormUrlEncoded`: Parsea `key1=val1&key2=val2`.
       - `parseJsonBody`: Parsea objetos JSON simples `{ "key": "value" }`.
       - `parseMultipartFormData`: Parsea partes separadas por `boundary` para extracción de campos.

6. **[Request.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Request.java)**:
   - **Estado actual**: Implementa `getData(...)`.
   - **Cómo funciona**:
     - Lee la primera línea (Request Line: `METHOD PATH HTTP/VERSION`).
     - Valida si el método es `GET`, `POST` o `HEAD`.
     - Parsea los encabezados hasta encontrar la línea en blanco (`\r\n`).
     - Si hay `Content-Length > 0`, lee la cantidad exacta de caracteres del cuerpo.
     - Llama a `RequestHelper` para parsear parámetros en `params`.
     - Devuelve un objeto `HttpRequestData`.

7. **[Response.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Response.java)**:
   - **Estado actual**: **Es una implementación estática de ejemplo**.
   - **Cómo funciona**:
     - Lee únicamente el archivo `./www/index.html`.
     - Reemplaza de forma fija una etiqueta `{fieldTest_DEMO}` por un número aleatorio.
     - Retorna siempre una respuesta fija `HTTP/1.1 200 OK` con `Content-Type: text/html`.
     - **Ignora por completo la ruta solicitada (`path`), el método HTTP (`GET`, `POST`, `HEAD`), los archivos dinámicos `.cc8`, otros tipos de archivos (CSS, JS, imágenes) y el manejo de errores (404, 405, etc.)**.

---

## 3. ¿Qué falta y cómo implementarlo?

Para completar el laboratorio es necesario transformar [Response.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Response.java) de una respuesta estática a un motor de servicio de archivos y plantillas HTTP completo.

### Plan de Implementación Paso a Paso:

#### 3.1. Obtención y Normalización del Recurso Solicitado
- Castear el parámetro `Object request` a `HttpRequestData`.
- Si `requestData.hasError()` es verdadero (error detectado durante `Request`), generar inmediatamente una respuesta de error correspondiente (e.g. 400 Bad Request o 405 Method Not Allowed).
- Extraer el `path` de `requestData`.
- Si la ruta es `/` o está vacía, redireccionar/mapear por defecto a `/index.html`.
- **Seguridad**: Validar la ruta para evitar ataques de salto de directorio (*Path Traversal*, e.g., prohibir el uso de `..`).

#### 3.2. Mapeo de MIME Types (Content-Type)
Crear un método auxiliar en `Response.java` (o en una clase helper) que determine el `Content-Type` según la extensión del archivo:

| Extensión | MIME Type |
| :--- | :--- |
| `.html`, `.cc8` | `text/html; charset=utf-8` |
| `.css` | `text/css` |
| `.js` | `application/javascript` |
| `.json` | `application/json` |
| `.jpg`, `.jpeg` | `image/jpeg` |
| `.png` | `image/png` |
| `.gif` | `image/gif` |
| `.svg` | `image/svg+xml` |
| `.ico` | `image/x-icon` |
| `.woff`, `.woff2`, `.ttf` | `font/woff`, `font/woff2`, `font/ttf` |
| Otros / Binarios | `application/octet-stream` |

#### 3.3. Lectura de Archivos y Preprocesamiento de Plantillas `.cc8`
- Verificar si el archivo existe en `./www` + `path`.
- Si el archivo **no existe**:
  - Construir una respuesta `HTTP/1.1 404 Not Found`.
  - Enviar un cuerpo HTML indicando que el recurso no fue encontrado.
- Si el archivo **existe**:
  - Detectar si es un archivo de plantilla (`.cc8` o HTML procesable).
  - **Para archivos `.cc8`**:
    1. Leer el contenido como texto (`String`).
    2. Recorrer el mapa `requestData.getParams()`.
    3. Reemplazar cada ocurrencia de `{key}` con su respectivo `value`.
    4. *(Opcional)* Reemplazar cualquier etiqueta `{key}` no enviada por una cadena vacía `""` o mantenerla limpiamente.
    5. Convertir el texto resultante a bytes en codificación `UTF-8`.
  - **Para archivos estáticos/binarios** (imágenes, CSS, JS, etc.):
    1. Leer los bytes crudos directamente usando `Files.readAllBytes(path)`.

#### 3.4. Manejo de Métodos HTTP (`GET`, `POST`, `HEAD`)
- **GET**: Retorna la línea de estado (`HTTP/1.1 200 OK`), los encabezados (`Content-Type`, `Content-Length`, `Connection: close`, etc.) y el arreglo de bytes del cuerpo.
- **POST**: Realiza el procesamiento de datos del formulario (almacenados en `requestData.getParams()`), aplica las sustituciones en la plantilla `.cc8` solicitada y envía los encabezados más el cuerpo HTML procesado.
- **HEAD**: Construye exactamente los mismos encabezados que una respuesta `GET` (incluyendo la cabecera `Content-Length` calculada sobre el cuerpo), pero **NO envía los bytes del cuerpo** al cliente.

#### 3.5. Envío de Datos al Cliente mediante `PrintStream` / `OutputStream`
Para evitar corromper archivos binarios (como imágenes y fuentes PNG/JPEG/WOFF):
1. Escribir los encabezados HTTP como texto terminado en `\r\n`.
2. Incluir una línea vacía `\r\n` para señalar el fin de los encabezados.
3. Si el método **NO** es `HEAD` y el cuerpo no está vacío, escribir los bytes crudos directamente en el flujo de salida (`dataOut.write(byteBuffer)`).
4. Asegurarse de hacer `dataOut.flush()`.

#### 3.6. Logging de Solicitudes y Respuestas
Mantener el formato de log esperado por el servidor:
```java
LOGGER.info("(" + nThreadServer + ") response: " + responseHeadersList);
LOGGER.info("(" + nThreadServer + ") request: " + requestData);
```

---

## Resumen de Pasos para Trabajar en el Lab

1. Actualizar [Response.java](file:///C:/Users/gdavd/Documents/Galileo/2026/Semestre2/CC8/labs/labsNetworking/lab02/Response.java) implementando la lógica de búsqueda de archivos, sustitución de plantillas `.cc8`, soporte `HEAD/GET/POST` y manejo de respuestas de error.
2. Compilar el proyecto utilizando la herramienta Make:
   ```bash
   make clean
   make
   ```
3. Ejecutar el servidor web:
   ```bash
   make run ARGS="-port 1000 -threads 4"
   ```
4. Probar en el navegador accediendo a:
   - `http://localhost:1000/` (Index)
   - `http://localhost:1000/test01/index.html` (Prueba de CSS e imágenes)
   - `http://localhost:1000/test02/index.html` (Prueba de formularios `.cc8` GET y POST)
   - `http://localhost:1000/test03/index.html` (Sitio web completo)
