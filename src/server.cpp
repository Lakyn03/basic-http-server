#include <iostream>
#include <cstdlib>
#include <string>
#include <cstring>
#include <unistd.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <thread>
#include <filesystem>
#include <fstream>
#include <sstream>

#define OK_RESPONSE "HTTP/1.1 200 OK\r\n\r\n"

std::string receive_request(int client_fd)
{
  // buffer for storing the request
  char buffer[1024];
  // receive
  int received_bytes = recv(client_fd, buffer, sizeof(buffer) - 1, 0);
  if (received_bytes < 0)
  {
    std::cerr << "Error while receiving\n";
  }
  // manually add \0 to be able to work with a string
  buffer[received_bytes] = '\0';
  std::cout << received_bytes << " bytes received" << std::endl;

  std::string request(buffer);
  return std::move(request);
}

std::string parse_path(std::string &request)
{
  std::string path;
  // now parse the path
  size_t first_space = request.find(' ');
  size_t second_space = request.find(' ', first_space + 1);
  if (first_space != std::string::npos && second_space != std::string::npos)
  {
    path = request.substr(first_space + 1, second_space - first_space - 1);
  }
  return std::move(path);
}

std::string find_user_agent(std::string &request)
{
  size_t start = request.find("User-Agent: ") + 12;
  size_t end = request.find('\r', start);
  return request.substr(start, end - start);
}

std::string get_file_content(std::filesystem::path& file)
{
  std::ifstream file_stream(file);
  if (!file_stream.is_open())
  {
    std::cerr << "Error opening file\n";
  }

  std::stringstream buffer;
  buffer << file_stream.rdbuf();
  std::string content = buffer.str();
  return std::move(content);
}

void handle_request(int client_fd, std::string files)
{
  std::string request = receive_request(client_fd);
  std::string path = parse_path(request);
  std::string response;

  // check all cases with the path
  if (path == "/")
  {
    response = OK_RESPONSE;
  }
  else if (path.compare(0, 6, "/echo/") == 0)
  {
    std::string content = path.substr(6);
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
  }
  else if (path == "/user-agent")
  {
    std::string content = find_user_agent(request);
    response = "HTTP/1.1 200 OK\r\nContent-Type: text/plain\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
  }
  else if (path.compare(0, 7, "/files/") == 0)
  {
    std::filesystem::path file = std::filesystem::path(files) / path.substr(7);
    if (std::filesystem::exists(file))
    {
      std::string content = get_file_content(file);
      response = "HTTP/1.1 200 OK\r\nContent-Type: application/octet-stream\r\nContent-Length: " + std::to_string(content.length()) + "\r\n\r\n" + content;
    }
    else
    {
      response = "HTTP/1.1 404 Not Found\r\n\r\n";
    }
  }
  else
  {
    response = "HTTP/1.1 404 Not Found\r\n\r\n";
  }

  // .c_str to make a const pointer to content
  send(client_fd, response.c_str(), response.length(), 0);
  close(client_fd);
}

int main(int argc, char **argv)
{
  // check the arguments
  std::string files;
  if (argc >= 3 && std::string(argv[1]) == "--directory")
  {
    files = argv[2];
  }

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

  if (bind(server_fd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0)
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

  std::cout << "Waiting for a client to connect...\n";
  while (true)
  {
    // struct for storing client address
    struct sockaddr_in client_addr;
    int client_addr_len = sizeof(client_addr);
    // establish a connection, store file descriptor
    int client_fd = accept(server_fd, (struct sockaddr *)&client_addr, (socklen_t *)&client_addr_len);
    if (client_fd < 0)
    {
      std::cerr << "Error while accepting\n";
      return 1;
    }

    std::thread client_thread(handle_request, client_fd, files);
    // makes it run on its own, no need to join later
    client_thread.detach();
  }

  close(server_fd);
  return 0;
}
