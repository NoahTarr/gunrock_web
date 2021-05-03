#include <stdlib.h>
#include <unistd.h>
#include <assert.h>
#include <signal.h>

#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <sstream>
#include <deque>

#include "HTTPRequest.h"
#include "HTTPResponse.h"
#include "HttpService.h"
#include "HttpUtils.h"
#include "FileService.h"
#include "MySocket.h"
#include "MyServerSocket.h"
#include "dthread.h"

using namespace std;

unsigned int PORT = 8080;
unsigned int THREAD_POOL_SIZE = 1;
unsigned int BUFFER_SIZE = 1;
string BASEDIR = "static";
string SCHEDALG = "FIFO";
string LOGFILE = "/dev/null";

vector<HttpService *> services;
deque<MySocket *> socketBuffer;
vector<pthread_t *> threadPool;

pthread_mutex_t socketBufferLock = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t socketBufferHasSocket = PTHREAD_COND_INITIALIZER;
pthread_cond_t socketBufferHasRoom = PTHREAD_COND_INITIALIZER;

void createThreadPool();
void producer(MySocket *client);
void *consumer(void *arg);

HttpService *find_service(HTTPRequest *request)
{
    // find a service that is registered for this path prefix
    for (unsigned int idx = 0; idx < services.size(); idx++)
    {
        if (request->getPath().find(services[idx]->pathPrefix()) == 0)
        {
            return services[idx];
        }
    }

    return NULL;
}

void invoke_service_method(HttpService *service, HTTPRequest *request, HTTPResponse *response)
{
    stringstream payload;

    // invoke the service if we found one
    if (service == NULL)
    {
        // not found status
        response->setStatus(404);
    }
    else if (request->isHead())
    {
        payload << "HEAD " << request->getPath();
        sync_print("invoke_service_method", payload.str());
        cout << payload.str() << endl;
        service->head(request, response);
    }
    else if (request->isGet())
    {
        payload << "GET " << request->getPath();
        sync_print("invoke_service_method", payload.str());
        cout << payload.str() << endl;
        service->get(request, response);
    }
    else
    {
        // not implemented status
        response->setStatus(405);
    }
}

void handle_request(MySocket *client)
{
    HTTPRequest *request = new HTTPRequest(client, PORT);
    HTTPResponse *response = new HTTPResponse();
    stringstream payload;

    // read in the request
    bool readResult = false;
    try
    {
        payload << "client: " << (void *)client;
        sync_print("read_request_enter", payload.str());
        readResult = request->readRequest();
        sync_print("read_request_return", payload.str());
    }
    catch (...)
    {
        // swallow it
    }

    if (!readResult)
    {
        // there was a problem reading in the request, bail
        delete response;
        delete request;
        sync_print("read_request_error", payload.str());
        return;
    }

    HttpService *service = find_service(request);
    invoke_service_method(service, request, response);

    // send data back to the client and clean up
    payload.str("");
    payload.clear();
    payload << " RESPONSE " << response->getStatus() << " client: " << (void *)client;
    sync_print("write_response", payload.str());
    cout << payload.str() << endl;
    client->write(response->response());

    delete response;
    delete request;

    payload.str("");
    payload.clear();
    payload << " client: " << (void *)client;
    sync_print("close_connection", payload.str());
    client->close();
    delete client;
}

int main(int argc, char *argv[])
{
    signal(SIGPIPE, SIG_IGN);
    int option;

    while ((option = getopt(argc, argv, "d:p:t:b:s:l:")) != -1)
    {
        switch (option)
        {
        case 'd':
            BASEDIR = string(optarg);
            break;
        case 'p':
            PORT = atoi(optarg);
            break;
        case 't':
            THREAD_POOL_SIZE = atoi(optarg);
            break;
        case 'b':
            BUFFER_SIZE = atoi(optarg);
            break;
        case 's':
            SCHEDALG = string(optarg);
            break;
        case 'l':
            LOGFILE = string(optarg);
            break;
        default:
            cerr << "usage: " << argv[0] << " [-p port] [-t threads] [-b buffers]" << endl;
            exit(1);
        }
    }

    set_log_file(LOGFILE);

    sync_print("init", "");
    MyServerSocket *server = new MyServerSocket(PORT);
    MySocket *client;

    services.push_back(new FileService(BASEDIR));

    createThreadPool();

    // dthread_mutex_lock(&socketBufferLock);
    while (true)
    {
        sync_print("waiting_to_accept", "");
        client = server->accept();
        sync_print("client_accepted", "");
        producer(client);
    }
    // dthread_mutex_unlock(&socketBufferLock);
}

void createThreadPool()
{
    pthread_t *thread;
    for (unsigned int i = 0; i < THREAD_POOL_SIZE; ++i)
    {
        thread = new pthread_t;
        if (0 != dthread_create(thread, NULL, consumer, NULL)) //The last NULL is args if needed eventually
        {
            cerr << "Error Creating Threads" << endl;
            exit(1);
        }
        threadPool.push_back(thread);
        dthread_detach(*thread);
    }
}

void producer(MySocket *client)
{
    dthread_mutex_lock(&socketBufferLock);
    while (socketBuffer.size() == BUFFER_SIZE)
    {
        dthread_cond_wait(&socketBufferHasRoom, &socketBufferLock);
    }
    socketBuffer.push_back(client);
    dthread_cond_signal(&socketBufferHasSocket);
    dthread_mutex_unlock(&socketBufferLock);
}

void *consumer(void *arg)
{
    MySocket *client;
    while (1)
    {
        dthread_mutex_lock(&socketBufferLock);
        while (socketBuffer.size() == 0)
        {
            dthread_cond_wait(&socketBufferHasSocket, &socketBufferLock);
        }
        client = socketBuffer.front();
        socketBuffer.pop_front();
        dthread_cond_signal(&socketBufferHasRoom);
        dthread_mutex_unlock(&socketBufferLock);
        handle_request(client);
    }
}