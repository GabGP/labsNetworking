import java.io.*;
import java.util.*;
import java.nio.file.*;
import java.nio.charset.StandardCharsets;
import java.util.stream.*;
import java.util.logging.*;

public class Response {
    // ======================================
    // No modificar la firma de la función.
    public void sendData (Logger LOGGER, PrintStream dataOut, Integer nThreadServer, Object request) throws Exception {
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
            sendError(LOGGER, dataOut, nThreadServer, req.getErrorCode(), req.getErrorMessage(), req.getMethod());
            return;
        }

        // ----- Sección 3.2: resolver el path solicitado a un archivo real -----
        Path file = resolveFile(req);
        if (file == null) {
            // El path intentó salir de ./www/ (path traversal).
            sendError(LOGGER, dataOut, nThreadServer, 403, "Forbidden: " + req.getPath(), req.getMethod());
            return;
        }
        if (!Files.exists(file) || Files.isDirectory(file)) {
            sendError(LOGGER, dataOut, nThreadServer, 404, "Not Found: " + req.getPath(), req.getMethod());
            return;
        }

        // ----- Sección 3.4: leer el archivo como bytes y enviarlo binary-safe -----
        // Se lee SIEMPRE como byte[] para no corromper imágenes/fuentes, y el
        // Content-Length se calcula en bytes (no en caracteres).
        byte[] body = Files.readAllBytes(file);

        // ----- Sección 3.5: si es un .cc8, preprocesar los tags {key} del formulario -----
        if (isCc8(file)) {
            body = preprocessTemplate(body, req.getParams());
        } else if (isHtml(file)) {
            // Demo original: reemplazar {fieldTest_DEMO} en las páginas HTML.
            String html = new String(body, StandardCharsets.UTF_8);
            html = html.replace("{fieldTest_DEMO}", "El servidor cambió esto y agregó un número aleatorio: "+ (new Random()).nextInt(1000)  );
            body = html.getBytes(StandardCharsets.UTF_8);
        }

        // Respuesta con el MISMO formato del código entregado:
        // [status, Content-Type, ClaseCC8, Content-Length, "", body]
        String bodyForLog = isTextual(file)
                ? new String(body, StandardCharsets.UTF_8)
                : "[binary " + body.length + " bytes]";

        ArrayList<String> response = new ArrayList<String>();
        response.add("HTTP/1.1 200 OK");
        response.add("Content-Type: " + contentTypeFor(file));
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

        LOGGER.info("(" + nThreadServer + ") response: " + response );
        LOGGER.info("(" + nThreadServer + ") request: " + request );

    }// sendData

    // ------------------------------------------------------------------
    // Resuelve el path del Request a un archivo real dentro de ./www/.
    //   - "/" o cualquier ruta que termine en "/" -> index.html de esa carpeta.
    //   - Un directorio sin "/" final -> su index.html.
    //   - Devuelve null si el path intenta salir de ./www/ (path traversal).
    // ------------------------------------------------------------------
    private Path resolveFile(HttpRequestData req) {
        String path = req.getPath();
        if (path == null || path.isEmpty()) path = "/";

        // Directorio -> index.html
        if (path.endsWith("/")) path = path + "index.html";

        // Quitar el "/" inicial para poder resolver de forma relativa a la raíz.
        String relative = path.startsWith("/") ? path.substring(1) : path;

        Path root = Paths.get("www").toAbsolutePath().normalize();
        Path resolved = root.resolve(relative).normalize();

        // Guard contra path traversal: el archivo debe permanecer dentro de la raíz.
        if (!resolved.startsWith(root)) return null;

        // Si resolvió a un directorio (p.ej. "/test01" sin slash), usar su index.html.
        if (Files.isDirectory(resolved)) resolved = resolved.resolve("index.html");

        return resolved;
    }// resolveFile

    // ------------------------------------------------------------------
    // Determina el Content-Type (MIME type) según la extensión del archivo.
    // Los .cc8 se sirven como HTML (igual que Apache convierte .php a HTML).
    // ------------------------------------------------------------------
    private String contentTypeFor(Path file) {
        String name = file.getFileName().toString().toLowerCase();
        int dot = name.lastIndexOf('.');
        String ext = (dot == -1) ? "" : name.substring(dot + 1);

        switch (ext) {
            case "html": case "htm": case "cc8": return "text/html";
            case "css":   return "text/css";
            case "txt":   return "text/plain";
            case "js":    return "application/javascript";
            case "json":  return "application/json";
            case "png":   return "image/png";
            case "jpg": case "jpeg": return "image/jpeg";
            case "gif":   return "image/gif";
            case "svg":   return "image/svg+xml";
            case "ico":   return "image/x-icon";
            case "ttf":   return "font/ttf";
            case "otf":   return "font/otf";
            case "woff":  return "font/woff";
            case "woff2": return "font/woff2";
            default:      return "application/octet-stream";
        }
    }// contentTypeFor

    // ------------------------------------------------------------------
    // ¿El contenido es texto? (para decidir si se muestra el body en el LOG;
    // los binarios se registran como un marcador y no ensucian el log).
    // ------------------------------------------------------------------
    private boolean isTextual(Path file) {
        String type = contentTypeFor(file);
        return type.startsWith("text/")
                || type.equals("application/javascript")
                || type.equals("application/json")
                || type.equals("image/svg+xml");
    }// isTextual

    // ------------------------------------------------------------------
    // ¿Es una plantilla .cc8? (HTML con tags {key} a preprocesar)
    // ------------------------------------------------------------------
    private boolean isCc8(Path file) {
        return file.getFileName().toString().toLowerCase().endsWith(".cc8");
    }// isCc8

    // ------------------------------------------------------------------
    // ¿Es una página HTML? (para el demo {fieldTest_DEMO} del index)
    // ------------------------------------------------------------------
    private boolean isHtml(Path file) {
        String name = file.getFileName().toString().toLowerCase();
        return name.endsWith(".html") || name.endsWith(".htm");
    }// isHtml

    // ------------------------------------------------------------------
    // Sección 3.5: Preprocesa un archivo .cc8 (HTML con tags {key}).
    // Reemplaza cada {key} por el valor del campo de formulario correspondiente
    // (los params ya vienen unificados desde el QUERY del GET o el body del POST).
    // Cualquier tag {key} que el formulario no envió se limpia (queda vacío)
    // para no mostrarlo crudo al usuario.
    // ------------------------------------------------------------------
    private byte[] preprocessTemplate(byte[] raw, Map<String, String> params) {
        String html = new String(raw, StandardCharsets.UTF_8);

        for (Map.Entry<String, String> e : params.entrySet()) {
            html = html.replace("{" + e.getKey() + "}", e.getValue());
        }

        // Limpiar tags {identificador} que hayan quedado sin reemplazar.
        html = html.replaceAll("\\{[A-Za-z0-9_]+\\}", "");

        return html.getBytes(StandardCharsets.UTF_8);
    }// preprocessTemplate

    // ------------------------------------------------------------------
    // Envía una respuesta de error HTTP con una página HTML mínima,
    // con status line, Content-Type y Content-Length (en bytes) correctos.
    // ------------------------------------------------------------------
    private void sendError(Logger LOGGER, PrintStream dataOut, Integer nThreadServer,
                           int code, String message, String method) throws Exception {
        String reason = reasonPhrase(code);
        String detail = (message == null || message.isEmpty()) ? reason : message;

        String htmlBody = "<!DOCTYPE html>\r\n"
                + "<html lang=\"es\">\r\n"
                + "<head><meta charset=\"UTF-8\"><title>" + code + " " + reason + "</title></head>\r\n"
                + "<body>\r\n"
                + "    <h1>" + code + " " + reason + "</h1>\r\n"
                + "    <p>" + detail + "</p>\r\n"
                + "</body>\r\n"
                + "</html>";

        byte[] bodyBytes = htmlBody.getBytes(StandardCharsets.UTF_8);

        ArrayList<String> response = new ArrayList<String>();
        response.add("HTTP/1.1 " + code + " " + reason);
        response.add("Content-Type: text/html");
        response.add("Content-Length: " + bodyBytes.length);
        response.add("");
        response.add(htmlBody);

        // Cabeceras como texto + cuerpo como bytes crudos (Content-Length exacto).
        String head = String.join("\r\n", response.subList(0, 3)) + "\r\n\r\n";
        dataOut.write(head.getBytes(StandardCharsets.UTF_8));
        // HEAD nunca lleva cuerpo, ni siquiera en respuestas de error.
        if (!"HEAD".equals(method)) {
            dataOut.write(bodyBytes);
        }
        dataOut.flush();

        LOGGER.info("(" + nThreadServer + ") response: " + response);
        LOGGER.warning("(" + nThreadServer + ") error " + code + ": " + detail);
    }// sendError

    // Frase de estado (reason phrase) según el código HTTP.
    private String reasonPhrase(int code) {
        switch (code) {
            case 400: return "Bad Request";
            case 403: return "Forbidden";
            case 404: return "Not Found";
            case 405: return "Method Not Allowed";
            case 500: return "Internal Server Error";
            default:  return "Error";
        }
    }// reasonPhrase
}
