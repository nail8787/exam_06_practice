#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>

typedef struct s_client {
    int fd;
    int id;
    struct s_client* next;
}              t_client;

char msg[64];
char msg_client[1024*5];
char msg_final[4096*5];
char tmp[1024*5];
t_client *g_clients = NULL;

void fatal_error()
{
    write(2, "Fatal error\n", strlen("Fatal error\n"));
    exit(1);
}

int  count_clients()
{
    int i = 1;
    t_client *current_client;

    if (g_clients == NULL)
        return 0;
    current_client = g_clients;
    while (current_client->next != NULL)
    {
        i++;
        current_client = current_client->next;
    }
    return i;
}

t_client *create_client(int client_fd, int client_id){
    t_client *new_client;
    new_client = malloc(sizeof(t_client));
    if (new_client == NULL)
        fatal_error();
    new_client->fd = client_fd;
    new_client->id = client_id;
    new_client->next = NULL;
    return new_client;
}

int get_last_id(){
    t_client *temp = g_clients;
    while (temp->next != NULL)
        temp = temp->next;
    return temp->id;
}

int add_client(int client_fd)
{
    int count = count_clients();
    t_client *new_client;
    t_client *temp = g_clients;
    if (count == 0)
    {
        g_clients = create_client(client_fd, 0);
        return g_clients->id;
    }
    else
    {
        new_client = create_client(client_fd, get_last_id() + 1);
        while (temp->next != NULL)
            temp = temp->next;
        temp->next = new_client;
        return temp->next->id;
    }
}

void send_all(char *str, int fromfd, fd_set writeset)
{
    t_client *temp = g_clients;

    while (temp)
    {
        if (temp->fd != fromfd && FD_ISSET(temp->fd, &writeset))
        {
            if (send(temp->fd, str, strlen(str), 0) < 0)
                fatal_error();
        }
        temp = temp->next;
    }
    bzero(str, strlen(str));
}

int remove_client(int clientfd)
{
    t_client *temp = g_clients;
    int remove_id;
    if (temp->fd == clientfd)
    {
        g_clients = temp->next;
        temp->next = NULL;
        remove_id = temp->id;
        free(temp);
    }
    else
    {
        while (temp->next != NULL && temp->next->fd != clientfd)
            temp = temp->next;
        t_client *remove = temp->next;
        temp->next = remove->next;
        remove_id = remove->id;
        remove->next = NULL;
        free(remove);
    }
    return remove_id;
}

int get_client_id(int clientfd){
    t_client *temp = g_clients;
    while (temp != NULL && temp->fd != clientfd)
        temp = temp->next;
    return temp->id;
}

void send_message(int fromfd, fd_set writeset){
    int i = 0;
    int j = 0;

    while (msg_client[i])
    {
        tmp[j] = msg_client[i];
        j++;
        if (msg_client[i] == '\n')
        {
            sprintf(msg_final, "client %d %s", get_client_id(fromfd), tmp);
            send_all(msg_final, fromfd, writeset);
            j = 0;
            bzero(msg_final, sizeof(msg_final));
            bzero(tmp, sizeof(tmp));
        }
        i++;
    }
    bzero(msg_client, sizeof(msg_client));
}

int main(int argc, char **argv)
{
    int sockfd, fd_max, new_connection_fd;
    struct sockaddr_in serveraddr, clientaddr;
    fd_set master, readset, writeset;

    if (argc != 2)
    {
        write(2, "Wrong number of arguments\n", strlen("Wrong number of arguments"));
        exit(1);
    }
    uint16_t port = atoi(argv[1]);
    bzero(&serveraddr, sizeof(serveraddr));
    bzero(&clientaddr, sizeof(clientaddr));
    serveraddr.sin_family = AF_INET;
    serveraddr.sin_addr.s_addr = 127 | 1 << 24;
    serveraddr.sin_port = port >> 8 | port << 8;

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd == -1)
        fatal_error();
    if (bind(sockfd, (const struct sockaddr*)&serveraddr, sizeof(serveraddr)) != 0)
        fatal_error();
    if (listen(sockfd, 100) != 0)
        fatal_error();
    FD_ZERO(&master);
    FD_ZERO(&readset);
    FD_ZERO(&writeset);
    FD_SET(sockfd, &master);
    fd_max = sockfd;
    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 0;

    while (1){
        readset = master;
        writeset = master;
        if (select(fd_max + 1, &readset, &writeset, NULL, &timeout) < 0)
            fatal_error();
        for (int current_fd = 0; current_fd <= fd_max; current_fd++)
        {
            if (FD_ISSET(current_fd, &readset))
            {
                if (current_fd == sockfd)
                {
                    int id;
                    socklen_t len = sizeof(clientaddr);
                    new_connection_fd = accept(sockfd, (struct sockaddr*)&clientaddr, &len);
                    if (new_connection_fd < 0)
                        fatal_error();
                    else
                    {
                        FD_SET(new_connection_fd, &master);
                        fd_max = new_connection_fd;
                        id = add_client(new_connection_fd);
                        sprintf(msg, "server: client %d just arrived\n", id);
                        send_all(msg, new_connection_fd, writeset);
                    }
                }
                else {
                    int bytes_received = recv(current_fd, &msg_client, sizeof(msg_client), 0);
                    if (bytes_received < 0)
                        fatal_error();
                    else if (bytes_received == 0)
                    {
                        int id;
                        id = remove_client(current_fd);
                        FD_CLR(current_fd, &master);
                        sprintf(msg, "server: client %d just left\n", id);
                        send_all(msg, current_fd, writeset);
                        close(current_fd);
                    }
                    else
                    {
                        send_message(current_fd, writeset);
                    }
                }
            }
        }
    }
    return 0;
}
