#include "Connectivity.h"

using namespace Connectivity;

void namedPipeClass::createServer()
{
#ifdef _WIN32
	bool pipeOpened = false;
	int tries = 0;
	HANDLE hPipeTemp;
	Logging::LoggerService::LogDebug("Connecting to named pipe");

	while (!pipeOpened)
	{
		hPipeTemp = CreateFile("\\\\.\\pipe\\languageConnectionPipe", GENERIC_ALL, 0, nullptr, OPEN_EXISTING, 0, nullptr);

		if (hPipeTemp == INVALID_HANDLE_VALUE)
		{
			tries++;
			continue;
		}

		pipeOpened = true;

		DWORD mode = PIPE_READMODE_MESSAGE;

		SetNamedPipeHandleState(hPipeTemp, &mode, nullptr, nullptr);

		this->hPipe = hPipeTemp;
	}
#else
#ifdef __ANDROID__
    // An in-process socketpair replaces the desktop launcher socket.
    if (const char* value = std::getenv("HYRULE_IPC_FD")) {
        char* end = nullptr;
        long descriptor = std::strtol(value, &end, 10);
        if (end == value || *end || descriptor < 0 || descriptor > 0x7fffffff)
            throw std::runtime_error("Invalid Android launcher socket descriptor");
        this->hPipe = static_cast<SOCKET>(descriptor);
        unsetenv("HYRULE_IPC_FD");
        return;
    }
    throw std::runtime_error("Android launcher did not provide an IPC socket");
#endif
    const char* configuredPath = std::getenv("MILKBAR_IPC_PATH");
    const std::string socketPath = configuredPath
        ? configuredPath
        : "/tmp/milkbar-launcher-" + std::to_string(getuid()) + ".sock";

    SOCKET socketFd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (socketFd == INVALID_SOCKET)
        throw std::runtime_error("Could not create Milk Bar IPC socket");
#ifdef __APPLE__
    int noSigPipe = 1;
    if (setsockopt(socketFd, SOL_SOCKET, SO_NOSIGPIPE, &noSigPipe, sizeof(noSigPipe)) != 0) {
        closesocket(socketFd);
        throw std::runtime_error("Could not configure Milk Bar IPC socket");
    }
#endif

    sockaddr_un endpoint{};
    endpoint.sun_family = AF_UNIX;
    if (socketPath.size() >= sizeof(endpoint.sun_path)) {
        closesocket(socketFd);
        throw std::runtime_error("Milk Bar IPC socket path is too long");
    }
    std::strncpy(endpoint.sun_path, socketPath.c_str(), sizeof(endpoint.sun_path) - 1);

    for (int attempt = 0; attempt < 100; ++attempt) {
        if (::connect(socketFd, reinterpret_cast<sockaddr*>(&endpoint), sizeof(endpoint)) == 0) {
            this->hPipe = socketFd;
            return;
        }
        Sleep(50);
    }

    closesocket(socketFd);
    throw std::runtime_error("Could not connect to Milk Bar Launcher IPC at " + socketPath);
#endif
}
