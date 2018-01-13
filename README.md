# Lappenchat server for Windows

Lappenchat server targeting Windows, implemented as a command and a service and using only native Windows APIs.


##  Was zum Teufel?
Für das Semesterprojekt des vom Professor Siegfried Rump erteilten Lehrfaches Prozedurale Programmierung, das im ersten Semester stattfindet, haben wir uns für eine Chat-Implementierung entschieden. Das Projekt besteht aus einem Server, der Nachrichten von den Clients bekommt und weitergibt, und einigen Clients, die Kontakt mit dem Server aufnehmen und die dem Benutzer dazu dienen, mit anderen zu kommunizieren. Dies ist nun der Windows Server. Der Name begreift das etwas beleidigende Wort »Lappen« ein, das einem der Mitglieder des Projektteams an einem unseligen Tage von sehr witzigen Kommilitonen beschert wurde.

##  Installation

###  Dependencies
The programs don't require any libraries beyond those included in a standard Windows installation. They both have to be linked against `ws2_32.dll`. Additionally, the service requires `advapi32.dll`.

###  Configuration
The build scripts accompanying this program and used in its development include defaults which should work in all cases. Thus, for a quick check of this project, you could possibly do without setting any build parameter.

Should you want to customize the build parameters or to adjust them to suit your environment, you can do so in a `tup.config` file.

###  Build
The [tup build system](http://gittup.org/tup/) manages the build process.

###  Installation
The server is implemented both as a command and as a Windows service.

The command is mainly meant for a quick check. Should it be desired to install it, however, its installation involves nothing more than copying it to the desired location.

As for the service, its installation comprises the usual setup of a new service, which can be carried out with the SC.EXE command as follows:

    $  sc config lappenchat-server DisplayName= "Lappenchat server" depend= tcpip binPath= "exePath"

The placeholder _exePath_ is the absolute path of the Lappenchat server executable.


##  Usage
The command can be managed from a command prompt. To start it:

    $  lappenchat-server-command [OPTIONS]

To stop it, hit CTRL-C.

The service can be managed as any other Windows service.

In a command prompt:

    $  sc start lappenchat-server [OPTIONS]

Then, to stop it:

    $  sc stop lappenchat-server

In both cases, _OPTIONS_ is a placeholder for any options you might want to pass to the server.

###  Options
Options are specified as service start parameters, in the following format: the option code is preceded by a single dash and any required arguments are provided as separate parameters following it. An example:

    $  sc start lappenchat-server -l logFilePath -p Port -t nThreads

A list of the options currently supported follows:

| Code  | Availability    | Meaning
|-------|-----------------|----------------------
|  l    |         service | **Path to the log file**. If this option is not specified, logs will not be saved anywhere.
|  p    | command+service | **Port number to listen on**. The default is 3144.
|  t    | command+service | **Number of threads** to spawn and use in handling connection requests and server traffic.

Some options are only supported by the service.
