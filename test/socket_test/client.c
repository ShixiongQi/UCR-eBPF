#include <sys/socket.h>
#include <sys/types.h>
#include <netinet/in.h>
#include <netdb.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <arpa/inet.h>
 
int main(int argc,char *argv[])
{
  int connfd=0;
  int sockfd = 0,n = 0;
  char recvBuff[1024];
  struct hostent *hen;
  if(argc<2)
  {
	  fprintf(stderr,"Error: Host name and port is not given \n");
	  exit(1);
  }
  struct sockaddr_in serv_addr;
  char mes[1024];
  memset(recvBuff, '0' ,sizeof(recvBuff));
  if((sockfd = socket(AF_INET, SOCK_STREAM, 0))< 0)
    {
      printf("\n Error : Could not create socket \n");
      return 1;
    }
 
  hen = gethostbyname(argv[1]);
  if(hen == NULL)
  {
    fprintf(stdout,"Host not found");
    exit(1);
  }
  serv_addr.sin_family = AF_INET;
  serv_addr.sin_port = htons(5000);
  bcopy((char *)hen->h_addr,(char *)&serv_addr.sin_addr.s_addr,hen->h_length);
 
  if(connect(sockfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr))<0)
    {
      printf("\n Error : Connect Failed \n");
      return 1;
    }

  printf("rec: %s\n", mes);

  if(scanf("%s",mes)<=0){
    printf("Error: scanf failed \n");
  }else{
    if(write(sockfd,mes,strlen(mes))>0)
      {}
  }
  
  /*
  while((n = read(sockfd, recvBuff, sizeof(recvBuff)-1)) > 0)
    {
      recvBuff[n] = 0;
      if(fputs(recvBuff, stdout) == EOF)
    {
      printf("\n Error : Fputs error");
    }
      printf("\n");
    }
  connfd=accept(sockfd, (struct sockaddr*)NULL,NULL);
   strcpy(mes, "Hi");
   write(connfd, mes,strlen(mes));

  if( n < 0)
    {
      printf("\n Read Error \n");
      }*/
 
 
  return 0;
}
