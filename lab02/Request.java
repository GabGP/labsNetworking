import java.io.*;
import java.util.*;
import java.util.logging.*;

public class Request {
    // ======================================
    // No modificar la firma de la función.
    public Object getData(Logger LOGGER, BufferedReader dataIn, Integer nThreadServer) throws Exception {
        // ======================================
        // VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
        // Aquí va tu código para procesar la solicitud HTTP
        ///
        // Apartir de aquí puede modificar el código a su gusto,
        // pero no modifique la firma de la función.
        //

        ArrayList<String> requestRawLines = new ArrayList<String>();
        HttpRequestData requestData = new HttpRequestData();

        try {
            // Procesar las solicitudes HTTP que llegan al servidor.
            String requestLine = dataIn.readLine();
            if (requestLine == null || requestLine.trim().isEmpty()) {
                requestData.setErrorCode(400);
                requestData.setErrorMessage("Bad Request: Empty request line");
                LOGGER.info("(" + nThreadServer + ") request: " + requestRawLines);
                return requestData;
            }

            requestRawLines.add(requestLine);
            String[] requestLineParts = requestLine.trim().split("\\s+");

            if (requestLineParts.length < 3) {
                requestData.setErrorCode(400);
                requestData.setErrorMessage("Bad Request: Malformed request line");
                LOGGER.info("(" + nThreadServer + ") request: " + requestRawLines);
                return requestData;
            }

            String method = requestLineParts[0].toUpperCase();
            String rawUri = requestLineParts[1];
            String httpVersion = requestLineParts[2];

            requestData.setMethod(method);
            requestData.setRawUri(rawUri);
            requestData.setHttpVersion(httpVersion);

            // Clasificar los métodos de solicitud (GET, POST, HEAD).
            if (!method.equals("GET") && !method.equals("POST") && !method.equals("HEAD")) {
                requestData.setErrorCode(405);
                requestData.setErrorMessage("Method Not Allowed: " + method);
            }

            // Separar la ruta del archivo y los parámetros QUERY (en peticiones GET)
            RequestHelper.parseUriAndQueryParams(rawUri, requestData);

            // Leer y almacenar los encabezados HTTP (Headers)
            String line;
            int contentLength = 0;
            String contentType = "";

            while ((line = dataIn.readLine()) != null) {
                if (line.trim().isEmpty())
                    break; // Fin de la sección de headers
                requestRawLines.add(line);

                int colonPos = line.indexOf(":");
                if (colonPos != -1) {
                    String headerName = line.substring(0, colonPos).trim();
                    String headerValue = line.substring(colonPos + 1).trim();

                    // Guardar los headers
                    requestData.getHeaders().put(headerName.toLowerCase(), headerValue);

                    if (headerName.equalsIgnoreCase("Content-Length")) {
                        try {
                            contentLength = Integer.parseInt(headerValue);
                        } catch (NumberFormatException ignored) {
                        }
                    } else if (headerName.equalsIgnoreCase("Content-Type")) {
                        contentType = headerValue;
                    }
                }
            }

            // Leer y procesar el cuerpo de la solicitud (Body)
            if (contentLength > 0) {
                char[] inBuffer = new char[contentLength];
                int totalRead = 0;
                while (totalRead < contentLength) {
                    int read = dataIn.read(inBuffer, totalRead, contentLength - totalRead);
                    if (read == -1)
                        break;
                    totalRead += read;
                }
                String body = new String(inBuffer, 0, totalRead);
                requestData.setBody(body);
                requestRawLines.add(body);

                // Extraer el cuerpo de la solicitud según Content-Type
                RequestHelper.parseRequestBody(contentType, body, requestData);
            } else {
                // Agregar String vacío si no hay contenido
                requestRawLines.add("");
            }

            // Log de la clase Request
            LOGGER.info("(" + nThreadServer + ") request: " + requestRawLines);
            requestData.setRawRequestString(String.join(" || ", requestRawLines));

        } catch (Exception e) {
            LOGGER.warning("(" + nThreadServer + ") Error parseando Request: " + e.getMessage());
            requestData.setErrorCode(500);
            requestData.setErrorMessage("Internal Server Error: " + e.getMessage());
            requestData.setRawRequestString(String.join(" || ", requestRawLines));
        }

        return requestData;
    }// getData

}