import java.io.*;
import java.net.*;

public class ServerTCP {
    public static void main(String[] args) throws IOException {
        String msg = "";
        int port = 5001;
        try (ServerSocket server = new ServerSocket(port)) {
            System.out.println("Server Port: " + port + " on " + server.getInetAddress().getHostAddress() );
            while (true) {
                Socket client = server.accept();
                System.out.println("Connected client: " + client.getInetAddress().getHostAddress() );
                BufferedReader in = new BufferedReader(new InputStreamReader(client.getInputStream()));
                PrintWriter out = new PrintWriter(client.getOutputStream(), true);
                while (true) {
                    msg = in.readLine();
                    out.println(msg);
                    System.out.println("Message: " + msg);
                    if (msg.contains("EXIT")) { break; }
                }
                client.close();
                System.out.println("Disconnected client: " + client.getInetAddress().getHostAddress() );
                if (msg.contains("OFF")) { break; }
            }
            System.out.println("Server finished.");
        }
    }
}
