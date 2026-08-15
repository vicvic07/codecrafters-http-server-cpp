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
#include <map>
#include <netdb.h>
#include <zlib.h>
#include <sys/wait.h>
const std::string website_dir="../website/";
std::map <std::string, std::string> mime_type;
std::string nextWord(int& ptr, std::string input, const char* sep)
{
    std::string ret = "";
    while (ptr < input.size() && strchr(sep, input[ptr]))
        ++ptr;
    while (ptr < input.size() && !strchr(sep, input[ptr]))
        ret += input[ptr++];
    return ret;
}

std::string getBody(char* buff)
{
    std::string ret = "";
    int n = strlen(buff);
    for (int i = n - 1; i >= 3; i--)
    {
        std::string is_CRLF;
        for (int j = i - 3; j <= i; j++)
        {
            is_CRLF += buff[j];
        }
        if (is_CRLF == "\r\n\r\n")
            break;
        ret += buff[i];
    }
    std::reverse(ret.begin(), ret.end());
    return ret;
}

std::vector<Bytef> encodeGzip(std::string input)
{
    uLong source_len = input.size();
    z_stream stream{};
    int result = deflateInit2(&stream, Z_DEFAULT_COMPRESSION, Z_DEFLATED, 15+16, 8, Z_DEFAULT_STRATEGY);
    if (result == Z_OK)
    {
        uLong dest_len = deflateBound(&stream, source_len);
        std::vector<Bytef> compressed(dest_len);
        stream.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(input.data()));
        stream.avail_in = input.size();
        stream.avail_out = compressed.size();
        stream.next_out = compressed.data();
        int ret = deflate(&stream, Z_FINISH);
        if (ret == Z_STREAM_END)
        {
            compressed.resize(stream.total_out);
            deflateEnd(&stream);
            return compressed;
        }
    }
    std::cerr << "Compression failed";
    exit(1);
}
void getFile (std::string rel_path, std::string &status, std::string &header, std::string &body)
{
    std::cout << rel_path << "\n";
    status = "HTTP/1.1 200 OK\r\n";
    FILE *f;
    if ((f=fopen (std::string(website_dir+rel_path).c_str(), "r")))
    {
        char buff[4096];
        while (fread (&buff, sizeof(buff[0]), sizeof(buff), f))
            body+=std::string(buff);
        fclose (f);
        std::string file_ext="";
        int ptr=rel_path.size()-1;
        while (ptr>=0 && rel_path[ptr]!='.')
            file_ext+=rel_path[ptr--];
        file_ext+=".";
        std::reverse (file_ext.begin(), file_ext.end());
        header="Content-Type: " + mime_type[file_ext] + "\r\nContent-Length: " + std::to_string(body.size()) + "\r\n" + header;
        std::cout << "OK\n";
    }
    else
    {
        status = "HTTP/1.1 404 Not Found\r\n";
        std::cout << "File " << (website_dir+rel_path) << " not found!";
    }
}
void mapMimeTypes()
{
    std::ifstream f ("../file-extension-to-mime-types.json");
    std::string line;
    while (getline (f, line))
    {
        if (line=="{" || line=="}")
            continue;
        int ptr=0;
        std::string ext=nextWord (ptr, line, "\"\n\t\r: ,"), mime=nextWord (ptr, line, "\"\n\t\r: ,");
        if (ext.empty() || mime.empty())
            continue;
        mime_type[ext]=mime;
    }
}
int main(int argc, char** argv)
{
    mapMimeTypes();
    // Flush after every std::cout / std::cerr
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
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
    int cnt=-1;
    while (true)
    {
        int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, (socklen_t*)&client_addr_len);
        pid_t pid = fork();
        ++cnt;
        if (pid == 0)
        {
            int crt=cnt;
            std::cout << "Opened connection #" << crt << "\n";
            bool keep_alive=1;
            while (keep_alive)
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
                bool encoded = 0;
                std::vector<Bytef> enc_bytes;
                if (method == "GET")
                {
                    path = nextWord(ptr, std::string(buff), " ");
                    if (path=="/")
                    {
                        getFile ("index.html", status, header, body);
                    }
                    else
                    {
                        int ptr2 = 0;
                        std::string arg = nextWord(ptr2, path, "/");
                        if (arg == "echo")
                        {
                            std::string word = nextWord(ptr2, path, "/ ");
                            status = "HTTP/1.1 200 OK\r\n";
                            std::string req_header = "";
                            while (ptr < nr_bytes)
                            {
                                req_header = nextWord(ptr, std::string(buff), " :\r\n");
                                if (req_header == "Accept-Encoding")
                                {
                                    std::string encodings = nextWord(ptr, std::string(buff), ":\n");
                                    int ptr3 = 0;
                                    while (ptr3 < encodings.size())
                                    {
                                        std::string encoding = nextWord(ptr3, encodings, ", \n\r");
                                        if (encoding == "gzip")
                                            header = "Content-Encoding: gzip\r\n" + header, encoded = 1;
                                    }
                                }
                            }
                            if (!encoded)
                            {
                                body = word;
                                header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(word.size()) +
                                    "\r\n" +
                                    header;
                            }
                            else
                            {
                                enc_bytes = encodeGzip(word);
                                header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(
                                        enc_bytes.size()) + "\r\n" +
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
                            header = "Content-Type: text/plain\r\nContent-Length: " + std::to_string(agent.size()) +
                                "\r\n"
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
                        {
                            getFile (path, status, header, body);
                        }
                    }
                }
                else if (method == "POST")
                {
                    std::cout << "Posting...";
                    std::string path = nextWord(ptr, std::string(buff), " ");
                    int ptr2 = 0;
                    std::string file = nextWord(ptr2, path, "/");
                    file = nextWord(ptr2, path, "/");
                    std::fstream f((std::string(argv[2]) + file).c_str(), std::ios::out);
                    f << getBody(buff);
                    std::cout << getBody(buff) << "\n";
                    status = "HTTP/1.1 201 Created\r\n";
                }
                ptr=0;
                while (ptr<nr_bytes)
                {
                    std::string word = nextWord (ptr, std::string(buff), " :/\r\n");
                    if (word=="Connection")
                    {
                        std::string arg = nextWord(ptr, std::string(buff), " :\r\n");
                        if (arg == "close")
                        {
                            header = "Connection: close\r\n"+header;
                            keep_alive=0;
                            break;
                        }
                    }
                }
                if (path=="/")
                    status="HTTP/1.1 200 OK\r\n";
                response = status + header + body;
                write(client_fd, response.c_str(), response.size());
                if (encoded)
                    write(client_fd, enc_bytes.data(), enc_bytes.size());
            }
            close (client_fd);
            std::cout << "Closed connection #" << crt << "\n";
            _exit (0);
        }
        else
            close (client_fd);
    }
    close(server_fd);
    return 0;
}
