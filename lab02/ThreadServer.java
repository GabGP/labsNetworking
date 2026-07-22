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
import java.io.*;
import java.net.*;
import java.nio.file.*;
import java.util.*;
import java.text.*;
import java.util.logging.*;

public class ThreadServer implements Runnable {
	private Integer nThreadServer;
	private ServerSocket serverSocket;
	private Integer delay;
	private Request threadRequest = new Request();
	private Response threadResponse = new Response();
	private SimpleDateFormat dateFormat = new SimpleDateFormat("yyMMddHHmm");
	private String logsFolder = "logs";
	
	public ThreadServer(Integer id, ServerSocket server, Integer delay) {
		this.nThreadServer = id;
		this.serverSocket = server;
		this.delay = delay;
	}// ThreadServer
	
	public void run() {
		try {
			Long time1, time2;
			BufferedReader dataIn;
			PrintStream dataOut;
			
			Logger LOGGER = Logger.getAnonymousLogger();
			LOGGER.setUseParentHandlers(false);
			Files.createDirectories(Paths.get(logsFolder));
			FileHandler fileHandler = new FileHandler(logsFolder + File.separator + "T"+ nThreadServer + "-" + dateFormat.format((new Date()).getTime()) + ".log");
			ConsoleHandler consoleHandler = new ConsoleHandler();
			fileHandler.setFormatter(new FormatterWebServer());
			consoleHandler.setFormatter(new FormatterWebServer());
			LOGGER.addHandler(fileHandler);
			LOGGER.addHandler(consoleHandler);
			
			while (true) {
				LOGGER.info("("+nThreadServer+") > Thread waiting for new client....");
				Socket clientSocket = serverSocket.accept();
				LOGGER.info("("+nThreadServer+") > Thread Accepting connection >");
				dataIn = new BufferedReader(new InputStreamReader(clientSocket.getInputStream()));
				dataOut = new PrintStream(clientSocket.getOutputStream(), true);
				time1 = (new Date()).getTime();
				threadResponse.sendData(LOGGER, dataOut, nThreadServer, threadRequest.getData(LOGGER, dataIn, nThreadServer) );
				clientSocket.close();
				time2 = (new Date()).getTime();
				LOGGER.info("("+nThreadServer+") > Thread :: Time: " + (time2 - time1) + " milliseconds");
				LOGGER.info("("+nThreadServer+") > END #");
				Thread.sleep(delay * 1000);// Delay time to release thread
			}// while true
		} catch (Exception e) {
			e.printStackTrace();
		}
	}// run
}