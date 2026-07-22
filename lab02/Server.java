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
import java.net.*;
import java.util.*;
import java.util.concurrent.*;
import static java.lang.System.out;

public class Server {
	private ServerSocket server;
	private Integer nThreads, portServer, delay;
	private ThreadPoolExecutor pool;

	public Server (Integer nThreads, Integer portServer, Integer delay){
		this.nThreads =     (Optional.ofNullable(nThreads).orElse(-1) == -1)?    2 : nThreads;
		this.portServer = (Optional.ofNullable(portServer).orElse(-1) == -1)? 1000 : portServer;
		this.delay = 	      (Optional.ofNullable(delay).orElse(-1) == -1)?    5 : delay;
	}// Server
	
	public void start() throws Exception {
		out.println("\n### Server was started on port " + portServer + " ###");
		out.println("### Threadpool started with " + nThreads + " threads ###");
		out.println("### Thread termination delay is " + delay + " seconds ###\n");
		server = new ServerSocket(portServer);
		pool = (ThreadPoolExecutor) Executors.newFixedThreadPool(nThreads);
		for (int i = 0; i < nThreads; i++) {
			ThreadServer threadServer = new ThreadServer(i, server, delay);
			pool.execute(threadServer);
		}
		pool.shutdown();
	}// start

}