#include <iostream>
#include <fstream>
#include <vector>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <zlib.h>

std::string nextWord(int& ptr, std::string input, const char* sep)
{
    std::string ret = "";
    while (ptr < input.size() && strchr(sep, input[ptr]))
        ++ptr;
    while (ptr < input.size() && !strchr(sep, input[ptr]))
        ret += input[ptr++];
    return ret;
}
std::string getBody (char *buff)
{
    std::string ret="";
    int n=strlen(buff);
    for (int i=n-1;i>=3;i--)
    {
        std::string is_CRLF;
        for (int j=i-3;j<=i;j++)
        {
            is_CRLF+=buff[j];
        }
        if (is_CRLF=="\r\n\r\n")
            break;
        ret+=buff[i];
    }
    std::reverse (ret.begin(), ret.end());
    return ret;
}
std::vector<Bytef> encodeGzip (std::string input)
{
    uLong source_len = input.size();
    z_stream stream{};
    uLong dest_len = deflateBound(&stream, source_len);
    int result = deflateInit2 (&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15+16, 8, Z_DEFAULT_STRATEGY);
    if (result==Z_OK)
    {
        std::vector<Bytef> compressed(dest_len);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        stream.avail_in = input.size();
        stream.avail_out = compressed.size();
        stream.next_out = compressed.data();
        int ret = deflate(&stream, Z_FINISH);
        if (ret==Z_STREAM_END)
        {
            compressed.resize (stream.total_out);
            deflateEnd(&stream);
            return compressed;
        }
    }
    std::cerr << "Compression failed";
    exit(1);
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
    while (true)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        pid_t pid = fork();
        if (pid == 0)
        {
            std::cout << "Client connected\n";
            char buff[4096] = {0};
            ssize_t nr_bytes = read(client_fd, buff, sizeof(buff));
            if (nr_bytes == -1)
            {
                std::cerr << "Read failed\n";
                return 1;
            }
            int ptr = 0;
            std::string path = "";
            std::string method = "";
            method = nextWord(ptr, std::string(buff), " ");
            std::string status = "", header = "\r\n", body = "", response = "";
            bool encoded=0;
            std::vector <Bytef> enc_bytes;
            if (method == "GET")
            {
                for (int i = 1; i <= 1; i++)
                    path = nextWord(ptr, std::string(buff), " ");
                if (path.size() == 1)
                    status = "HTTP/1.1 200 OK\r\n\r\n";
                else
                {
                    int ptr2 = 0;
                    std::string arg = nextWord(ptr2, path, "/");
                    if (arg == "echo")
                    {
                        std::string word = nextWord(ptr2, path, "/ ");
                        status = "HTTP/1.1 200 OK\r\n";
                        std::string req_header = "";
                        while (ptr<nr_bytes)
                        {
                            req_header = nextWord (ptr, std::string(buff), " :\r\n");
                            if (req_header=="Accept-Encoding")
                            {
                                std::string encodings = nextWord (ptr, std::string(buff), ":\n");
                                int ptr3=0;
                                while (ptr3<encodings.size())
                                {
                                    std::string encoding = nextWord (ptr3, encodings, ", \n\r");
                                    if (encoding=="gzip")
                                        header="Content-Encoding: gzip\r\n"+header, encoded=1;
                                }
                            }
                        }
                        if (!encoded)
                        {
                            body=word;
                            header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(word.size()) + "\r\n" +
                                header;
                        }
                        else
                        {
                            enc_bytes=encodeGzip(word);
                            header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(enc_bytes.size()) + "\r\n" +
                                header;
                        }
                    }
                    else if (arg == "user-agent")
                    {
                        std::string word = "", agent = "";
                        while (word != "User-Agent")
                            word = nextWord(ptr, std::string(buff), " \r\n:");
                        agent = nextWord(ptr, std::string(buff), " \r\n:");
                        status = "HTTP/1.1 200 OK\r\n";
                        header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(agent.size()) + "\r\n"
                            + header;
                        body = agent;
                    }
                    else if (arg == "files")
                    {
                        std::string file = nextWord(ptr2, path, " \r\n:/");
                        FILE* fptr;
                        if ((fptr = fopen((std::string(argv[2]) + file).c_str(), "r")) != NULL)
                        {
                            char buff[4096];
                            while (fgets(buff, sizeof(buff), fptr))
                                body += std::string(buff);
                            fclose(fptr);
                            status = "HTTP/1.1 200 OK\r\n";
                            header = "Content-Type: application/octet-stream\r\nContent-Length: " +
                                std::to_string(body.size()) + "\r\n" + header;
                        }
                        else
                            status = "HTTP/1.1 404 Not Found\r\n";
                    }
                    else
                        status = "HTTP/1.1 404 Not Found\r\n";
                }
            }
            else if (method=="POST")
            {
                std::cout << "Posting...";
                std::string path = nextWord(ptr, std::string(buff), " ");
                int ptr2=0;
                std::string file = nextWord(ptr2, path, "/");
                file = nextWord(ptr2, path, "/");
                std::fstream f ((std::string(argv[2])+file).c_str(), std::ios::out);
                f << getBody (buff);
                std::cout << getBody (buff) << "\n";
                status = "HTTP/1.1 201 Created\r\n";
            }
            response += status + header + body;
            write(client_fd, response.c_str(), response.size());
            if (encoded)
                write(client_fd, enc_bytes.data(), enc_bytes.size());
        }
        close(client_fd);
    }
    close(server_fd);
    return 0;
}
