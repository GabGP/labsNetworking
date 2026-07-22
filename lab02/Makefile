JFLAGS = -g
JC = javac
JVM= java
.SUFFIXES: .java .class
.java.class:
		$(JC) $(JFLAGS) $*.java

CLASSES = \
		HttpRequestData.java \
		Request.java \
		RequestHelper.java \
		Response.java \
		FormatterWebServer.java \
		ThreadServer.java \
		Server.java \
		Lab02.java

MAIN = Lab02

default: classes

classes: $(CLASSES:.java=.class)

run: $(MAIN).class
		$(JVM) $(MAIN) $(ARGS)

clean:
		$(RM) *.class
		$(RM) logs/*