# Remote Terminal System

A complete Windows-based C++ remote terminal solution consisting of a client-server architecture that allows users to execute commands on remote machines through an interactive console interface. The system features real-time command execution with persistent shell sessions and asynchronous communication.

## System Overview

This remote terminal system provides secure, real-time command execution across network connections with the following components:

### 🖥️ **kClient** - Terminal Client
- Interactive command-line interface for remote command execution
- Asynchronous communication with real-time response display
- Thread-safe operations with concurrent input/output handling
- Automatic reconnection capabilities

### 🖲️ **kServer** - Terminal Server  
- Multi-client TCP server supporting concurrent connections
- Persistent shell sessions for each client with maintained state
- Real-time output streaming with timestamp logging
- Secure command execution through isolated shell processes

### ⚙️ **Core Features**

- **Persistent Shell Sessions**: Each client gets a dedicated CMD shell that maintains state between commands
- **Real-time Output Streaming**: Commands execute immediately with live output feedback
- **Multi-Client Support**: Server can handle multiple simultaneous client connections
- **Thread-Safe Architecture**: Robust multi-threaded design with proper synchronization
- **Custom Protocol**: Efficient TCP-based communication with message delimiting
- **Cross-Directory Navigation**: Shell sessions maintain working directory state
- **Timestamped Logging**: All server responses include timestamps for audit trails

## Prerequisites

- Windows 10/11
- Visual Studio 2019 or later (with C++17 support)
- Windows SDK 10.0

## Building

Both the server and client components are included in a single Visual Studio solution.

### Build Both Components

```cmd
# Navigate to the kClient directory
cd kClient

# Open the solution in Visual Studio
# The kClient.sln contains both kServer and kClient projects
```

**Using Visual Studio:**
1. Open `kClient/kClient.sln` in Visual Studio
2. Select your preferred configuration (Debug/Release) and platform (x86/x64)
3. Build the entire solution (Ctrl+Shift+B)

This will build both:
- **kServer.exe** (in `kServer/x64/Release/` or `kServer/x64/Debug/`)
- **kClient.exe** (in `kClient/x64/Release/` or `kClient/x64/Debug/`)

## Usage

### 1. Start the Server

```cmd
# Run the server (listens on port 27015 by default)
cd kServer/x64/Release
kServer.exe
```

Server output:
```
Remote Terminal Server Starting...
Server initialized and listening on port 27015
Waiting for client connections...
```

### 2. Connect with Client

```cmd
# Connect to localhost (default)
cd kClient/x64/Release
kClient.exe

# Connect to a specific server
kClient.exe 192.168.1.100
```

### 3. Interactive Commands

Once connected, you'll see the `remote>` prompt where you can:

- Execute any Windows command available on the remote server
- Navigate directories with `cd` - state is preserved between commands
- Use `pwd` to show current directory
- Type `exit` or `quit` to disconnect and close the client
- All responses appear in real-time with timestamps

### Example Session

```
Remote Terminal Client Starting...
Connected to server at 127.0.0.1:27015

Remote Terminal Client (Async Mode)
Type commands to execute on the remote server.
Responses will appear automatically as they arrive.
Type 'exit' or 'quit' to disconnect.

remote> dir
[14:30:15] Welcome to Remote Terminal Server!
[14:30:15] Shell session initialized.
[14:30:15] Volume in drive C has no label.
 Volume Serial Number is 1234-5678

 Directory of C:\Projects

14/01/2024  02:30 PM    <DIR>          .
14/01/2024  02:30 PM    <DIR>          ..
14/01/2024  02:25 PM    <DIR>          remoteTerminal

remote> cd remoteTerminal
[14:30:20] 

remote> pwd  
[14:30:22] C:\Projects\remoteTerminal

remote> echo Hello World
[14:30:25] Hello World

remote> exit
[14:30:28] Goodbye!
Client shutting down...
```

## Configuration

The system uses the following default settings (defined in `common.h`):

- **Default Port**: `27015`
- **Buffer Size**: `4096` bytes
- **Protocol Marker**: `\n<<END_OF_RESPONSE>>\n`
- **Shell Type**: Windows CMD (cmd.exe)
- **Output Polling**: 50ms intervals for real-time responsiveness

## System Architecture

### Server Architecture (kServer)

1. **Initialization**: Sets up Windows Sockets and creates listening socket
2. **Client Acceptance**: Accepts multiple concurrent client connections
3. **Per-Client Handling**: Each client gets a dedicated thread and persistent shell
4. **Shell Management**: 
   - Creates isolated CMD process with redirected stdin/stdout/stderr
   - Maintains working directory state between commands
   - Monitors output continuously using named pipes
5. **Output Streaming**: Real-time output delivery with timestamp prefixes
6. **Cleanup**: Graceful shutdown of shells and socket connections

### Client Architecture (kClient)

1. **Initialization**: Sets up Windows Sockets (Winsock) infrastructure
2. **Connection**: Establishes TCP connection to specified server address
3. **Dual Threading**: 
   - Main thread handles user input and command sending
   - Background thread continuously receives and displays server responses
4. **Protocol Handling**: Uses custom end-of-response markers for message delimiting
5. **Thread Synchronization**: Mutex-protected console output for clean display
6. **Cleanup**: Graceful shutdown of connections and threads on exit

## Project Structure

```
remoteTerminal/
├── common.h                 # Shared protocol definitions
├── kServer/                 # Server Component
│   ├── kServer.cpp          # Server main entry point  
│   ├── RemoteTerminalServer.h   # Server class interface
│   ├── RemoteTerminalServer.cpp # Server implementation
│   ├── PersistentShell.h    # Shell management interface
│   ├── PersistentShell.cpp  # Shell process handling
│   └── kServer.vcxproj      # Server project file
└── kClient/                 # Client Component
    ├── kClient.cpp          # Client main entry point
    ├── RemoteTerminalClient.h   # Client class interface
    ├── RemoteTerminalClient.cpp # Client implementation
    ├── kClient.sln          # Visual Studio solution (contains both projects)
    └── kClient.vcxproj      # Client project file
```

## Technical Details

### Common Components
- **Language**: C++17
- **Platform**: Windows (uses Winsock2 and Win32 API)
- **Protocol**: TCP sockets with custom message delimiting
- **Dependencies**: ws2_32.lib (Windows Sockets library)

### Server (kServer)
- **Threading**: Multi-threaded server with std::thread and std::atomic
- **Process Management**: Win32 CreateProcess API for shell spawning
- **IPC**: Named pipes for stdin/stdout/stderr redirection
- **Shell Integration**: Persistent CMD process per client session
- **Output Monitoring**: Non-blocking pipe reading with PeekNamedPipe

### Client (kClient)
- **Threading**: std::thread with std::mutex for thread-safe console output
- **Networking**: Asynchronous socket communication
- **User Interface**: Console-based interactive terminal
- **Input Handling**: Non-blocking command input with real-time response display

## Error Handling

### Server Error Handling
- Network initialization and binding failures
- Shell process creation and management errors
- Client connection drops and cleanup
- Pipe communication failures
- Multi-client resource management

### Client Error Handling
- Network initialization failures
- Connection timeouts and errors
- Socket communication issues
- Thread synchronization problems
- Graceful shutdown scenarios

## Security Considerations

- **Shell Isolation**: Each client gets an isolated CMD process
- **Process Boundaries**: Server runs shell commands in separate processes
- **Network Security**: TCP communication (consider adding encryption for production use)
- **Resource Management**: Automatic cleanup of processes and handles on disconnect

## Performance Features

- **Asynchronous I/O**: Non-blocking operations for real-time responsiveness
- **Efficient Output Streaming**: 50ms polling intervals balance responsiveness with CPU usage
- **Multi-Client Scalability**: Server handles multiple concurrent connections
- **Persistent Sessions**: Shell state maintained between commands eliminates process startup overhead

## Contributing

When contributing to this remote terminal system:

1. **Protocol Compatibility**: Ensure compatibility with the existing protocol (see `common.h`)
2. **Thread Safety**: Maintain thread safety in all multi-threaded operations
3. **Resource Management**: Proper cleanup of handles, processes, and sockets
4. **Cross-Component Testing**: Test both client and server components together
5. **Error Handling**: Comprehensive error handling for network and system failures
6. **Documentation**: Update documentation for any protocol or interface changes

## License

Please specify your license here.

## Installation & Deployment

### Server Deployment
1. Build the server component
2. Ensure Windows Firewall allows connections on port 27015
3. Run `kServer.exe` on the target machine
4. Server will automatically accept multiple client connections

### Client Usage
1. Build the client component  
2. Connect to server using IP address: `kClient.exe <server_ip>`
3. Use the interactive terminal for remote command execution

---

**Note**: This is a complete remote terminal system. The server must be running and accessible before client connections can be established. For production use, consider adding authentication and encryption mechanisms. 