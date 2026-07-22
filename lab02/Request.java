import java.io.*;
import java.util.*;
import java.util.stream.*;
import java.util.logging.*;

public class Request {
    // ======================================
    // No modificar la firma de la función.
    public Object getData (Logger LOGGER, BufferedReader dataIn, Integer nThreadServer) throws Exception  {
    // ======================================
    // VVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVVV
    // Aquí va tu código para procesar la solicitud HTTP
    ///
    // Apartir de aquí puede modificar el código a su gusto, 
    // pero no modifique la firma de la función.
    //
        ArrayList<String> request = new ArrayList<String>();
        Integer contentLength = 0;
        String line;
        while ((line = dataIn.readLine()) != null) {
            if (line.trim().isEmpty()) break;
            if ((line.split(":"))[0].trim().equals("Content-Length")) contentLength = Integer.valueOf((line.split(":"))[1].trim());
            request.add(line);
        }// while line
        /// POST Body
        char[] inBuffer = new char[contentLength];
        int inputMessageLength = dataIn.read(inBuffer, 0, contentLength);
        String inputMessage = new String(inBuffer, 0, inputMessageLength);
        request.add(inputMessage); // Agregar Post Body
        LOGGER.info("(" + nThreadServer + ") request: " + request );

        /////////////////////////////////////////////////
        //                                             //
        //   Procesar el cuerpo de la solicitud aquí   //
        //                                             //
        /////////////////////////////////////////////////
        String rq = request.stream().collect(Collectors.joining(" || ")); // UN EJEMPLO


        return rq; // Retornar la lista o procesar según sea necesario en Response.java

    }// getData
}