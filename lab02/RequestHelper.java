import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.Map;

public class RequestHelper {

    // ------------------------------------------------------------------------
    // MÉTODOS AUXILIARES DE PARSEO
    // ------------------------------------------------------------------------

    public static void parseUriAndQueryParams(String rawUri, HttpRequestData requestData) {
        int queryIdx = rawUri.indexOf('?');
        if (queryIdx != -1) {
            requestData.setPath(rawUri.substring(0, queryIdx));
            String queryString = rawUri.substring(queryIdx + 1);
            parseFormUrlEncoded(queryString, requestData.getParams());
        } else {
            requestData.setPath(rawUri);
        }
    }

    public static void parseRequestBody(String contentType, String body, HttpRequestData requestData) {
        if (contentType == null || body == null || body.isEmpty())
            return;

        String cleanContentType = contentType.split(";")[0].trim().toLowerCase();

        switch (cleanContentType) {
            case "application/x-www-form-urlencoded":
                parseFormUrlEncoded(body, requestData.getParams());
                break;

            case "application/json":
                parseJsonBody(body, requestData.getParams());
                break;

            case "multipart/form-data":
                parseMultipartFormData(contentType, body, requestData.getParams());
                break;

            default:
                break;
        }
    }

    private static void parseFormUrlEncoded(String data, Map<String, String> targetMap) {
        String[] pairs = data.split("&");
        for (String pair : pairs) {
            if (pair.isEmpty())
                continue;
            String[] kv = pair.split("=", 2);
            try {
                String key = URLDecoder.decode(kv[0].trim(), StandardCharsets.UTF_8);
                String value = kv.length > 1 ? URLDecoder.decode(kv[1].trim(), StandardCharsets.UTF_8) : "";
                targetMap.put(key, value);
            } catch (Exception e) {
                targetMap.put(kv[0].trim(), kv.length > 1 ? kv[1].trim() : "");
            }
        }
    }

    private static void parseJsonBody(String body, Map<String, String> targetMap) {
        String clean = body.trim();
        if (clean.startsWith("{") && clean.endsWith("}")) {
            clean = clean.substring(1, clean.length() - 1).trim();
            String[] pairs = clean.split(",");
            for (String pair : pairs) {
                String[] kv = pair.split(":", 2);
                if (kv.length == 2) {
                    String key = kv[0].trim().replaceAll("^\"|\"$", "");
                    String value = kv[1].trim().replaceAll("^\"|\"$", "");
                    targetMap.put(key, value);
                }
            }
        }
    }

    private static void parseMultipartFormData(String contentTypeHeader, String body, Map<String, String> targetMap) {
        String boundaryMarker = "boundary=";
        int boundaryIdx = contentTypeHeader.indexOf(boundaryMarker);
        if (boundaryIdx == -1)
            return;

        String boundary = "--" + contentTypeHeader.substring(boundaryIdx + boundaryMarker.length()).trim();
        String[] parts = body.split(boundary);

        for (String part : parts) {
            if (part.trim().isEmpty() || part.trim().equals("--"))
                continue;

            String[] headerAndContent = part.split("\r\n\r\n", 2);
            if (headerAndContent.length < 2) {
                headerAndContent = part.split("\n\n", 2);
            }

            if (headerAndContent.length == 2) {
                String partHeader = headerAndContent[0];
                String partValue = headerAndContent[1].trim();

                if (partHeader.contains("name=")) {
                    int nameIdx = partHeader.indexOf("name=");
                    String nameSubstring = partHeader.substring(nameIdx + 5);
                    if (nameSubstring.startsWith("\"")) {
                        nameSubstring = nameSubstring.substring(1);
                        int closeQuote = nameSubstring.indexOf("\"");
                        if (closeQuote != -1) {
                            String paramName = nameSubstring.substring(0, closeQuote);
                            targetMap.put(paramName, partValue);
                        }
                    }
                }
            }
        }
    }
}
