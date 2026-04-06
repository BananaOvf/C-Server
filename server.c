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

int update_stats(uint8_t facultyId, uint8_t eduForm, const uint8_t answers[8]) {
	char fname[64];
	get_filename(facultyId, eduForm, fname, sizeof(fname));
	uint32_t stats[32];
	if(load_stats(fname, stats) != 0) return -1;
	for(int i = 0; i < 8; i++) {
		int idx = answers[i] * 8 + i;
		if(idx >= 0 && idx < 32) stats[idx]++;
	}
	return save_stats(fname, stats);
}

void get_group_stats(uint8_t facultyId, uint8_t eduForm, uint32_t out[32]) {
	char fname[64];
	get_filename(facultyId, eduForm, fname, sizeof(fname));
	FILE *f = fopen(fname, "rb");
	if(!f) {
		memset(out, 0, 32 * sizeof(uint32_t));
		return;
	}
	size_t n = fread(out, sizeof(uint32_t), 32, f);
	fclose(f);
	if(n != 32) memset(out, 0, 32 * sizeof(uint32_t));
}

void get_total_stats(uint32_t out[32]) {
	memset(out, 0, 32 * sizeof(uint32_t));
	DIR *d = opendir("storage");
	if(!d) return;
	struct dirent *entry;
	while((entry = readdir(d)) != NULL) {
		if(entry->d_type != DT_REG) continue;
		char *dot = strrchr(entry->d_name, '.');
		if(!dot || strcmp(dot, ".bin") != 0) continue;
		char fpath[256];
		snprintf(fpath, sizeof(fpath), "storage/%s", entry->d_name);
		uint32_t stats[32];
		if(load_stats(fpath, stats) == 0) {
			for(int i = 0; i < 32; i++) out[i] += stats[i];
		}
	}
	closedir(d);
}

void reset_one_group(uint8_t facultyId, uint8_t eduForm) {
	char fname[64];
	get_filename(facultyId, eduForm, fname, sizeof(fname));
	unlink(fname);
}

void reset_all_stats() {
	DIR *d = opendir("storage");
	if(!d) return;
	struct dirent *entry;
	while((entry = readdir(d)) != NULL) {
		if(entry ->d_type != DT_REG) continue;
		char *dot = strrchr(entry->d_name, '.');
		if(dot && strcmp(dot, ".bin") == 0) {
			char fpath[256];
			snprintf(fpath, sizeof(fpath), "storage/%s", entry->d_name);
			unlink(fpath);
		}
	}
	closedir(d);
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

		unsigned char header;
		ssize_t r = read(fd, &header, 1);
		if(r != 1) {
			perror("read header failed");
			close(fd);
			continue;
		}
	
		uint8_t operationId = (header >> 6) & 0x03;
		uint8_t facultyId   = (header >> 1) & 0x1F;
		uint8_t eduForm     = header & 0x01;
		
		switch(operationId) {
			case 0: // processing survey results
				if(!accepting_polls) {
					const char *msg = "Server paused";
					write(fd, msg, strlen(msg));
					break;
				}
				unsigned char answers_buf[2];
				r = read(fd, answers_buf, 2);
				if(r != 2) {
					const char *err = "Incomplete answers";
					write(fd, err, strlen(err));
					break;
				}
				uint16_t answersBits = (uint16_t)answers_buf[0] | ((uint16_t)answers_buf[1] << 8);
				uint8_t answers[8];
				for(int i = 0; i < 8; i++) {
					answers[i] = (answersBits >> (i * 2)) & 0x03;
				}
				if(update_stats(facultyId, eduForm, answers) == 0)
					write(fd, "OK", 2);
				else write(fd, "Storage error", 13);
				break;				
			case 1: // requesting statistics
				uint32_t stats[32];
				if(facultyId == 31 && eduForm == 0)
					get_total_stats(stats);
				else get_group_stats(facultyId, eduForm, stats);
				write(fd, stats, sizeof(stats));
				break;
			case 2: // resetting statistics
				if(facultyId == 31 && eduForm == 0) {
					reset_all_stats();
					const char *msg = "All stats reset";
					write(fd, msg, strlen(msg));
				} else {
					reset_one_group(facultyId, eduForm);
					const char *msg = "Group reset";
					write(fd, msg, strlen(msg));
				}
				break;
			case 3: // pause switching
				accepting_polls = !accepting_polls;
				const char *msg = accepting_polls ? "Resumed" : "Paused";
				write(fd, msg, strlen(msg));
				break;
			default:
				write(fd, "Unknown operation", 17);
				break;
		}	
		
		close(fd);
	}
	close(server);

	return 0;
}









