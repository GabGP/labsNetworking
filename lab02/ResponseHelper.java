import java.io.PrintStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.util.ArrayList;
import java.util.Map;
import java.util.logging.*;

public class ResponseHelper {

    // ------------------------------------------------------------------
    // Resuelve el path del Request a un archivo real dentro de ./www/.
    // - "/" o cualquier ruta que termine en "/" -> index.html de esa carpeta.
    // - Un directorio sin "/" final -> su index.html.
    // - Devuelve null si el path intenta salir de ./www/ (path traversal).
    // ------------------------------------------------------------------
    public static Path resolveFile(HttpRequestData req) {
        String path = req.getPath();
        if (path == null || path.isEmpty())
            path = "/";

        // Directorio -> index.html
        if (path.endsWith("/"))
            path = path + "index.html";

        // Quitar el "/" inicial para poder resolver de forma relativa a la raíz.
        String relative = path.startsWith("/") ? path.substring(1) : path;

        Path root = Paths.get("www").toAbsolutePath().normalize();
        Path resolved = root.resolve(relative).normalize();

        // Guard contra path traversal: el archivo debe permanecer dentro de la raíz.
        if (!resolved.startsWith(root))
            return null;

        // Si resolvió a un directorio (p.ej. "/test01" sin slash), usar su index.html.
        if (Files.isDirectory(resolved))
            resolved = resolved.resolve("index.html");

        return resolved;
    }// resolveFile

    // ------------------------------------------------------------------
    // Determina el Content-Type (MIME type) según la extensión del archivo.
    // Los .cc8 se sirven como HTML (igual que Apache convierte .php a HTML).
    // ------------------------------------------------------------------
    public static String contentTypeFor(Path file) {
        String name = file.getFileName().toString().toLowerCase();
        int dot = name.lastIndexOf('.');
        String ext = (dot == -1) ? "" : name.substring(dot + 1);

        switch (ext) {
            case "html":
            case "htm":
            case "cc8":
                return "text/html";
            case "css":
                return "text/css";
            case "txt":
                return "text/plain";
            case "js":
                return "application/javascript";
            case "json":
                return "application/json";
            case "png":
                return "image/png";
            case "jpg":
            case "jpeg":
                return "image/jpeg";
            case "gif":
                return "image/gif";
            case "svg":
                return "image/svg+xml";
            case "ico":
                return "image/x-icon";
            case "ttf":
                return "font/ttf";
            case "otf":
                return "font/otf";
            case "woff":
                return "font/woff";
            case "woff2":
                return "font/woff2";
            default:
                return "application/octet-stream";
        }
    }// contentTypeFor

    // ------------------------------------------------------------------
    // ¿El contenido es texto? (para decidir si se muestra el body en el LOG;
    // los binarios se registran como un marcador y no ensucian el log).
    // ------------------------------------------------------------------
    public static boolean isTextual(Path file) {
        String type = contentTypeFor(file);
        return type.startsWith("text/")
                || type.equals("application/javascript")
                || type.equals("application/json")
                || type.equals("image/svg+xml");
    }// isTextual

    // ------------------------------------------------------------------
    // ¿Es una plantilla .cc8? (HTML con tags {key} a preprocesar)
    // ------------------------------------------------------------------
    public static boolean isCc8(Path file) {
        return file.getFileName().toString().toLowerCase().endsWith(".cc8");
    }// isCc8

    // ------------------------------------------------------------------
    // ¿Es una página HTML? (para el demo {fieldTest_DEMO} del index)
    // ------------------------------------------------------------------
    public static boolean isHtml(Path file) {
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
    public static byte[] preprocessTemplate(byte[] raw, Map<String, String> params) {
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
    public static void sendError(Logger LOGGER, PrintStream dataOut, Integer nThreadServer,
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
    private static String reasonPhrase(int code) {
        switch (code) {
            case 400:
                return "Bad Request";
            case 403:
                return "Forbidden";
            case 404:
                return "Not Found";
            case 405:
                return "Method Not Allowed";
            case 500:
                return "Internal Server Error";
            default:
                return "Error";
        }
    }// reasonPhrase

}
