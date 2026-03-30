#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <netinet/in.h>

int Socket(int domain, int type, int protocol) {
	int res = socket(domain, type, protocol);
	if(res == -1) {
		perror("socket failed");
		exit(EXIT_FAILURE);	
	}
	
	return res;
}

void Bind(int sockfd, const struct sockaddr *addr, socklen_t addrlen) {
	int res = bind(sockfd, addr, addrlen);
	if(res == -1) {
		perror("bind failed");
		exit(EXIT_FAILURE);
	}
}

void Listen(int sockfd, int backlog) {
	int res = listen(sockfd, backlog);
	if(res == -1){
		perror("listen failed");
		exit(EXIT_FAILURE);
	}
}

int Accept(int sockfd, struct sockaddr *addr, socklen_t *addrlen) {
	int res = accept(sockfd, addr, addrlen);
	if(res == -1) {
		perror("accept failed");
		exit(EXIT_FAILURE);
	}
	return res;
}

int main() {
	int server = Socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(34543);	
	Bind(server, (struct sockaddr*) &addr, sizeof addr);	
	
	Listen(server, 100);
	
	socklen_t addrlen = sizeof addr;
	while(1) {
		int fd = Accept(server, (struct sockaddr*) &addr, &addrlen);	
	
		ssize_t nread = 0;
		unsigned char buf[3];
		while(nread < sizeof(buf)) {
			ssize_t r = read(fd, buf + nread, sizeof(buf) - nread);
			if(r <= 0) {
				perror("read failed");
				close(fd);
				continue;
			}
			nread += r;
		}
		
		uint8_t operationId = (buf[0] >> 6) & 0x03;
		uint8_t facultyId   = (buf[0] >> 1) & 0x1F;
		uint8_t eduForm     = buf[0] & 0x01;

		uint16_t answersBits = (uint16_t)buf[1] | ((uint16_t)buf[2] << 8);
		uint8_t answers[8];
		for(int i = 0; i < 8; i++) {
			answers[i] = (answersBits >> (i * 2)) & 0x03;
		}
		
		if(operationId != 1) {
			const char *err_msg = "Invalid operationID";
			write(fd, err_msg, strlen(err_msg));
			close(fd);
			continue;
		}

		
		write(STDOUT_FILENO, buf, nread);
		write(fd, "принял", 6);
			
		//sleep(1);	
		
		close(fd);
	}
	close(server);

	return 0;
}









