#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <sys/socket.h>
#include <sys/types.h>
#include <arpa/inet.h>
 
#define MAXLINE (1024 * 1024 * 32) 
char mesg[MAXLINE];
 
void echo(int sockfd, struct sockaddr *client, socklen_t clilen)
{
    int n;
    socklen_t len;
 
    for(; ;)
    {
        len = clilen;
        
        // 接收客户端的消息
        n = recvfrom(sockfd, mesg, MAXLINE, 0, client, &len);
        
        printf("recv: %s\n", mesg);
        
        // 将来自客户端的消息发送给它
        sendto(sockfd, mesg, n, 0, client, len);
    }
}
 
int main(int argc, char** argv)
{
    if (argc < 2)
    {
        printf("Usage:./udpechosvr <port>\n");
        return -1;
    }

    int sockfd;
    struct sockaddr_in servaddr, cliaddr;
 
    uint16_t port = (uint16_t) atoi(argv[1]);

    // 创建网际数据报套接字
    
    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    memset(&servaddr, 0, sizeof(servaddr));
    servaddr.sin_family = AF_INET;
    servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
    servaddr.sin_port = htons(port);
    
    // 绑定指定的套接字地址结构
    bind(sockfd, (struct sockaddr *)&servaddr, sizeof(servaddr));
    
    printf("udpechosvr run...\n");

    // 主echo循环
    echo(sockfd, (struct sockaddr *)&cliaddr, sizeof(cliaddr));
    
    printf("udpechosvr exit...\n");

    return 0;
} 
