#pragma once
#define WIN32_LEAN_AND_MEAN

#include <Windows.h>
#include <WinSock2.h>
#include <WS2tcpip.h>
#include <inttypes.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <fcntl.h>

#include "common.h"
#include "socket.h"
#include "list.h"

#pragma comment (lib, "Ws2_32.lib")

DWORD WINAPI startServer(LPVOID lpParam) {
	char buf[16384];
	DWORD bytesRecv;
	DWORD flags;
	FD_SET writeSet;
	FD_SET readSet;
	short port = 5678;
	SOCKET servSock;
	SOCKET tempSock;
	listNode *tempNode;
	struct listHead clientSocks;
	struct sockaddr_in servAddr;
	struct socketThreadArgs *sta;
	struct timeval tv;
	WSADATA wsa;
	WSABUF wbuf;
	ULONG nonBlock;

	sta = (struct socketThreadArgs*) lpParam;

	listInit(&clientSocks);
	wbuf.buf = buf;
	wbuf.len = sizeof(buf);

	if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0)
	{
		fprintf(stderr, "WSAStartup failed. Error: %d\n", WSAGetLastError());
		running = 0;
		goto done;
	}

	if ((servSock = WSASocket(AF_INET, SOCK_STREAM, 0, NULL, 0, WSA_FLAG_OVERLAPPED)) == INVALID_SOCKET) {
		fprintf(stderr, "WSASocket failed. Error: %d\n", WSAGetLastError());
		running = 0;
		goto done;
	}
	memset(&servAddr, 0, sizeof(servAddr));
	servAddr.sin_family = AF_INET;
	servAddr.sin_port = htons(port);
	servAddr.sin_addr.s_addr = INADDR_ANY;

	if (bind(servSock, (const struct sockaddr *)&servAddr, sizeof(servAddr)) == SOCKET_ERROR)
	{
		fprintf(stderr, "bind failed. Error: %d\n", WSAGetLastError());
		running = 0;
		goto done;
	}

	if (listen(servSock, 1)) {
		fprintf(stderr, "listen failed. Error: %d\n", WSAGetLastError());
		running = 0;
		goto done;
	}

	nonBlock = 1;
	if (ioctlsocket(servSock, FIONBIO, &nonBlock) == SOCKET_ERROR) {
		fprintf(stderr, "ioctlsocket failed. Error: %d\n", WSAGetLastError());
		running = 0;
		goto done;
	}

	fprintf(stderr, "Server ready.\n");
	while (running) {
		FD_ZERO(&readSet);
		FD_ZERO(&writeSet);
		FD_SET(servSock, &readSet);
		tv.tv_sec = 1;
		tv.tv_usec = 0;

		tempNode = clientSocks.first;
		while (NULL != tempNode) {
			FD_SET(tempNode->data, &readSet);
			tempNode = tempNode->next;
		}

		if (select(0, &readSet, &writeSet, NULL, &tv) == SOCKET_ERROR) {
			fprintf(stderr, "select failed. Error: %d\n", WSAGetLastError());
			running = 0;
			goto done;
		}
		
		// check if new connection incoming
		if (FD_ISSET(servSock, &readSet)) {
			if ((tempSock = accept(servSock, NULL, NULL)) != INVALID_SOCKET) {
				nonBlock = 1;
				if (ioctlsocket(tempSock, FIONBIO, &nonBlock) == SOCKET_ERROR) {
					fprintf(stderr, "ioctlsocket failed. Error: %d\n", WSAGetLastError());
					running = 0;
					goto done;
				}
				// add socket to list
				listAdd(&clientSocks, tempSock);
			}
			else {
				if (WSAGetLastError() != WSAEWOULDBLOCK) {
					fprintf(stderr, "accept failed. Error: %d\n", WSAGetLastError());
					running = 0;
					goto done;
				}
			}
		}
		else {
			tempNode = clientSocks.first;
			// iterate sockets
			while (NULL != tempNode) {
				if (FD_ISSET(tempNode->data, &readSet)) {
					flags = 0;
					// recv from socket
					wbuf.len = sizeof(buf);
					if (WSARecv(tempNode->data, &wbuf, 1, &bytesRecv, &flags, NULL, NULL) == SOCKET_ERROR) {
						// connection lost, remove the socket
						if (WSAGetLastError() != WSAEWOULDBLOCK) {
							fprintf(stderr, "WSARecv failed. Error: %d\n", WSAGetLastError());
							closesocket(tempNode->data);
							tempNode = tempNode->next;
							listRemove(&clientSocks, tempNode);
							continue;
						}
						else {

						}
					}
					else {
						// connection closed
						if (bytesRecv == 0) {
							closesocket(tempNode->data);
							tempNode = tempNode->next;
							listRemove(&clientSocks, tempNode);
							continue;
						}
						// get current position
						if (strncmp(buf, "GET", 3) == 0 && bytesRecv >= 3) {
							memcpy(buf, sta->llhr, 3 * sizeof(double));
							wbuf.len = 3 * sizeof(double);
							if (WSASend(tempNode->data, &wbuf, 1, &bytesRecv, 0, NULL, NULL) == SOCKET_ERROR) {
								fprintf(stderr, "WSASend failed. Error: %d\n", WSAGetLastError());
								closesocket(tempNode->data);
								tempNode = tempNode->next;
								listRemove(&clientSocks, tempNode);
								continue;
							}
						}
						// set new position
						else if (strncmp(buf, "SET", 3) == 0 && bytesRecv >= (3 + 3 * sizeof(double))) {
							pthread_mutex_lock(sta->mutex);
							memcpy(sta->llhr, buf + 3, 3 * sizeof(double));
							sta->update = 1;
							pthread_mutex_unlock(sta->mutex);
						}
					}
				}
				tempNode = tempNode->next;
			}
		}
	}

	done:

	running = 0;

	// close server socket
	closesocket(servSock);

	// close client socks
	tempNode = clientSocks.first;
	while (NULL != tempNode) {
		closesocket(tempNode->data);
		tempNode = tempNode->next;
	}

	WSACleanup();
	return 0;
}


