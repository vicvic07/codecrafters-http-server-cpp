#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
std::string nextWord (int &ptr, std::string input, const char *sep)
{
    std::string ret="";
    while (ptr<input.size() && strchr (sep, input[ptr]))
        ++ptr;
    while (ptr<input.size() && !strchr (sep, input[ptr]))
        ret+=input[ptr++];
    return ret;
}
int main(int argc, char** argv)
{
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;

    // You can use print statements as follows for debugging, they'll be visible when running tests.
    std::cout << "Logs from your program will appear here!\n";
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0)
    {
        std::cerr << "Failed to create server socket\n";
        return 1;
    }
    int reuse = 1;
    if (setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) < 0)
    {
        std::cerr << "setsockopt failed\n";
        return 1;
    }
    struct sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(4221);
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0)
    {
        std::cerr << "Failed to bind to port 4221\n";
        return 1;
    }
    int connection_backlog = 5;
    if (listen(server_fd, connection_backlog) != 0)
    {
        std::cerr << "listen failed\n";
        return 1;
    }
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    std::cout << "Waiting for a client to connect...\n";
    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
    std::cout << "Client connected\n";
    char buff[4096]={0};
    ssize_t nr_bytes = read (client_fd, buff, sizeof(buff));
    if (nr_bytes==-1)
    {
        std::cerr << "Read failed\n";
        return 1;
    }
    int ptr=0;
    std::string path="";
    for (int i=1;i<=2;i++)
        path=nextWord (ptr, std::string(buff), " ");
    std::string status="", header="\r\n", body="", response="";
    if (path.size()==1)
        status = "HTTP/1.1 200 OK\r\n\r\n";
    else
    {
        int ptr2=0;
        std::string arg=nextWord (ptr2, std::string(path), "/");
        if (arg=="echo")
        {
            std::string word=nextWord (ptr2, std::string(path), "/ ");
            status="HTTP/1.1 200 OK\r\n";
            header="Content-Type: text/plain\r\nContent-Length: " + std::to_string(word.size()) + "\r\n"+header;
            body=word;
        }
        else
            status = "HTTP/1.1 404 Not Found\r\n";
    }

    response+=status+header+body;
    write(client_fd, response.c_str(), response.size());
    close(client_fd);
    close(server_fd);

    return 0;
}
