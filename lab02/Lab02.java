/* **********************************************************************************
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░███╗░░██╗░█████╗░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░████╗░██║██╔══██╗░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░██╔██╗██║██║░░██║░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░██║╚████║██║░░██║░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░██║░╚███║╚█████╔╝░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░╚═╝░░╚══╝░╚════╝░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░░
░░░░░░░░░░███╗░░░███╗░█████╗░██████╗░██╗███████╗██╗░█████╗░░█████╗░██████╗░░░░░░░░░░░
░░░░░░░░░░████╗░████║██╔══██╗██╔══██╗██║██╔════╝██║██╔══██╗██╔══██╗██╔══██╗░░░░░░░░░░
░░░░░░░░░░██╔████╔██║██║░░██║██║░░██║██║█████╗░░██║██║░░╚═╝███████║██████╔╝░░░░░░░░░░
░░░░░░░░░░██║╚██╔╝██║██║░░██║██║░░██║██║██╔══╝░░██║██║░░██╗██╔══██║██╔══██╗░░░░░░░░░░
░░░░░░░░░░██║░╚═╝░██║╚█████╔╝██████╔╝██║██║░░░░░██║╚█████╔╝██║░░██║██║░░██║░░░░░░░░░░
░░░░░░░░░░╚═╝░░░░░╚═╝░╚════╝░╚═════╝░╚═╝╚═╝░░░░░╚═╝░╚════╝░╚═╝░░╚═╝╚═╝░░╚═╝░░░░░░░░░░
 * **********************************************************************************
 */
import java.util.*;
import static java.lang.System.out;

public class Lab02 {

    public static void main(String[] args) {
		HashMap<String, Integer> map = new HashMap<String, Integer>();
		for(int i = 0; i < args.length; i=i+2) {
			try{
				map.put( args[i].trim(), Integer.valueOf(args[i+1].trim()) );
			} catch(Exception e){
				map.put(args[i].trim(), -1);
				if( args[i].trim().equals("-help") ) i--;
			}
        }
		if ( map.containsKey("-help") ){
			out.println("Usage: java Server [options...]");
			out.println(" -threads <int> Defines the NUMBER of threads that Threadpool will use.");
			out.println(" -port <int>    Define the PORT on which the server will be waiting for the client.");
			out.println(" -delay <int>   Defines the waiting time (seconds) before reuse the thread.");
			out.println(" -help          Get help.");
		} else {
			Server newServer = new Server( map.get("-threads"), map.get("-port"), map.get("-delay") );
			try {
				newServer.start();
			} catch(Exception e){
				e.printStackTrace();
			}
		}
	}// main

}// Lab02