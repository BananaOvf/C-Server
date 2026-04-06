#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>

#include <netinet/in.h>

#include <stdint.h>
#include <sys/stat.h>
#include <dirent.h>

// ------------ SOCKET WRAPPERS ------------
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

// ------------ WORKING WITH STATISTICS -----------
static volatile int accepting_polls = 1;

void create_storage_dir() {
	struct stat st = {0};
	if(stat("storage", &st) == -1)
		mkdir("storage", 0755);	
}

void get_filename(uint8_t facultyId, uint8_t eduForm, char *out, size_t out_size) {
	int idx = (facultyId << 1) | eduForm;
	snprintf(out, out_size, "storage/%d.bin", idx);
}

int load_stats(const char *fname, uint32_t stats[32]) {
	FILE *f = fopen(fname, "rb");
	if(!f) {
		memset(stats, 0, 32 * sizeof(uint32_t));
		f = fopen(fname, "wb");
		if(!f) return -1;
		fwrite(stats, sizeof(uint32_t), 32, f);
		fclose(f);
		return 0;
	}
	size_t n = fread(stats, sizeof(uint32_t), 32, f);
	fclose(f);
	if(n != 32) {
		memset(stats, 0, 32 * sizeof(uint32_t));
		f = fopen(fname, "wb");
		if(!f) return -1;
		fwrite(stats, sizeof(uint32_t), 32, f);
		fclose(f);
		return -1;
	}
	return 0;
}

int save_stats(const char *fname, const uint32_t stats[32]) {
	FILE *f = fopen(fname, "wb");
	if(!f) return -1;
	fwrite(stats, sizeof(uint32_t), 32, f);
	fclose(f);
	return 0;
}

int main() {
	int server = Socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in addr = {0};
	addr.sin_family = AF_INET;
	addr.sin_port = htons(34543);	
	Bind(server, (struct sockaddr*) &addr, sizeof addr);	
	
	char ip_str[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &addr.sin_addr, ip_str, sizeof(ip_str));
	printf("Server started on %s:%d\n", ip_str, ntohs(addr.sin_port));
	printf("Waiting for connections...\n\n");
	
	Listen(server, 100);
	
	create_storage_dir();

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
		
		if(operationId != 0) {
			const char *err_msg = "Invalid operationID";
			write(fd, err_msg, strlen(err_msg));
			close(fd);
			continue;
		}

		printf("\n============= NEW CONNECTION ===============\n");
		printf("3 bytes received: ");
		for(int i = 0; i < 3; i++) printf("0x%02X ", (unsigned char)buf[i]);
		printf("\n");
		printf("Operation ID: %u\n", operationId);
		printf("Faculty ID: %u\n", facultyId);
		printf("EduForm: %s\n", eduForm ? "part-time" : "full-time");
		printf("Answers:\n");
		for(int i = 0; i < 8; i++) {
			printf(" Answer %d: %u\n", i + 1, answers[i]);
		}
		
		const char *response = "OK";
		write(fd, response, strlen(response));
			
		//sleep(1);	
		
		close(fd);
	}
	close(server);

	return 0;
}









