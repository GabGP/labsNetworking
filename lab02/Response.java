import java.io.*;
import java.util.*;
import java.nio.file.*;
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

        /////////////////////////////////////////////////
        //                                             // 
        //   Procesar el cuerpo de la Respuesta aquí   //
        //                                             //
        /////////////////////////////////////////////////
        String fileData = new String(Files.readAllBytes(Paths.get("./www/index.html")));
        fileData = fileData.replace("{fieldTest_DEMO}", "El servidor cambió esto y agregó un número aleatorio: "+ (new Random()).nextInt(1000)  );
        ArrayList<String> response = new ArrayList<String>();
        response.add("HTTP/1.1 200 OK");
        response.add("Content-Type: text/html");
        response.add("ClaseCC8: Alumnos");
        response.add("Content-Length: " + fileData.length());
        response.add("");
        response.add(fileData);
        dataOut.print( response.stream().collect(Collectors.joining("\r\n")) );
        LOGGER.info("(" + nThreadServer + ") response: " + response ); 
        LOGGER.info("(" + nThreadServer + ") request: " + request );
        
        // ESTE ES UN EJEMPLO ESTATICO 
        // con lo minimo en el Header
        // Y RESPONDE LO MISMO A TODO

    }// sendData
}