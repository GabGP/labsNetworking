import java.io.*;
import java.util.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.logging.*;

public class Response {
    // ======================================
    // No modificar la firma de la función.
    public void sendData(Logger LOGGER, PrintStream dataOut, Integer nThreadServer, Object request) throws Exception {
        // ======================================
        // VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
        // Aquí va tu código para procesar la respuesta HTTP
        ///
        // Apartir de aquí puede modificar el código a su gusto,
        // pero no modifique la firma de la función.
        //

        // ----- Sección 3.1: castear el Request y manejar errores primero -----
        HttpRequestData req = (HttpRequestData) request;

        // Si la clase Request detectó un error (400/405/500...), responder de
        // inmediato con una página de error y NO continuar con el resto del flujo.
        if (req.hasError()) {
            ResponseHelper.sendError(LOGGER, dataOut, nThreadServer, req.getErrorCode(), req.getErrorMessage(),
                    req.getMethod());
            return;
        }

        // ----- Sección 3.2: resolver el path solicitado a un archivo real -----
        Path file = ResponseHelper.resolveFile(req);
        if (file == null) {
            // El path intentó salir de ./www/ (path traversal).
            ResponseHelper.sendError(LOGGER, dataOut, nThreadServer, 403, "Forbidden: " + req.getPath(),
                    req.getMethod());
            return;
        }
        if (!Files.exists(file) || Files.isDirectory(file)) {
            ResponseHelper.sendError(LOGGER, dataOut, nThreadServer, 404, "Not Found: " + req.getPath(),
                    req.getMethod());
            return;
        }

        // ----- Sección 3.4: leer el archivo como bytes y enviarlo binary-safe -----
        // Se lee SIEMPRE como byte[] para no corromper imágenes/fuentes, y el
        // Content-Length se calcula en bytes (no en caracteres).
        byte[] body = Files.readAllBytes(file);

        // ----- Sección 3.5: si es un .cc8, preprocesar los tags {key} del formulario
        // -----
        if (ResponseHelper.isCc8(file)) {
            body = ResponseHelper.preprocessTemplate(body, req.getParams());
        } else if (ResponseHelper.isHtml(file)) {
            // Demo original: reemplazar {fieldTest_DEMO} en las páginas HTML.
            String html = new String(body, StandardCharsets.UTF_8);
            html = html.replace("{fieldTest_DEMO}",
                    "El servidor cambió esto y agregó un número aleatorio: " + (new Random()).nextInt(1000));
            body = html.getBytes(StandardCharsets.UTF_8);
        }

        // Respuesta con el MISMO formato del código entregado:
        // [status, Content-Type, ClaseCC8, Content-Length, "", body]
        String bodyForLog = ResponseHelper.isTextual(file)
                ? new String(body, StandardCharsets.UTF_8)
                : "[binary " + body.length + " bytes]";

        ArrayList<String> response = new ArrayList<String>();
        response.add("HTTP/1.1 200 OK");
        response.add("Content-Type: " + ResponseHelper.contentTypeFor(file));
        response.add("ClaseCC8: Alumnos");
        response.add("Content-Length: " + body.length);
        response.add("");
        response.add(bodyForLog);

        // Enviar: cabeceras como texto + línea en blanco; el cuerpo se escribe como
        // bytes crudos para no corromper imágenes/fuentes.
        String head = String.join("\r\n", response.subList(0, 4)) + "\r\n\r\n";
        dataOut.write(head.getBytes(StandardCharsets.UTF_8));
        // Sección 3.6: HEAD envía exactamente las mismas cabeceras que GET
        // (incluyendo Content-Length) pero SIN el cuerpo.
        if (!"HEAD".equals(req.getMethod())) {
            dataOut.write(body);
        }
        dataOut.flush();

        LOGGER.info("(" + nThreadServer + ") response: " + response);
        LOGGER.info("(" + nThreadServer + ") request: " + request);

    }// sendData

}
