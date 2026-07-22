import java.util.*;

/**
 * Objeto que contiene toda la información extraída
 * de la solicitud HTTP para ser procesada por Response.java
 */
public class HttpRequestData {
    private String method = "";
    private String path = "";
    private String rawUri = "";
    private String httpVersion = "";
    private Map<String, String> headers = new HashMap<>();
    private Map<String, String> params = new HashMap<>();
    private String body = "";
    private int errorCode = 200;
    private String errorMessage = "";
    private String rawRequestString = "";

    // Getters y Setters
    public String getMethod() {
        return method;
    }

    public void setMethod(String method) {
        this.method = method;
    }

    public String getPath() {
        return path;
    }

    public void setPath(String path) {
        this.path = path;
    }

    public String getRawUri() {
        return rawUri;
    }

    public void setRawUri(String rawUri) {
        this.rawUri = rawUri;
    }

    public String getHttpVersion() {
        return httpVersion;
    }

    public void setHttpVersion(String httpVersion) {
        this.httpVersion = httpVersion;
    }

    public Map<String, String> getHeaders() {
        return headers;
    }

    public Map<String, String> getParams() {
        return params;
    }

    public String getBody() {
        return body;
    }

    public void setBody(String body) {
        this.body = body;
    }

    public int getErrorCode() {
        return errorCode;
    }

    public void setErrorCode(int errorCode) {
        this.errorCode = errorCode;
    }

    public String getErrorMessage() {
        return errorMessage;
    }

    public void setErrorMessage(String errorMessage) {
        this.errorMessage = errorMessage;
    }

    public boolean hasError() {
        return errorCode >= 400;
    }

    public String getRawRequestString() {
        return rawRequestString;
    }

    public void setRawRequestString(String rawRequestString) {
        this.rawRequestString = rawRequestString;
    }

    @Override
    public String toString() {
        return this.rawRequestString;
    }
}
