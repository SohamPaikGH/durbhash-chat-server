/*
 * benchmark_client.c: Durbhasha Server Benchmark Tool
 *
 * Author: Soham Paik
 * Usage:
 *   ./benchmark throughput <num_clients>
 *   ./benchmark latency
 *   ./benchmark maxconn
 */

#include <arpa/inet.h>
#include <errno.h>
#include <netdb.h>
#include <netinet/in.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

#define HOST               "localhost"
#define PORT               "50000"
#define BUF_SIZE           1024
#define MESSAGES_PER_CLIENT 100
#define LATENCY_SAMPLES    100
#define MAX_CONNECTIONS    10000

/* ── Shared throughput counter ───────────────────────────────────────── */
static int      total_sent = 0;
static pthread_mutex_t total_lock = PTHREAD_MUTEX_INITIALIZER;

/* ── connect_to_server ───────────────────────────────────────────────── */

int connect_to_server(const char *host, const char *port) {
    struct addrinfo hints, *res, *res0;
    memset(&hints, 0, sizeof(hints));
    hints.ai_family   = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    if (getaddrinfo(host, port, &hints, &res) != 0)
        return -1;

    int sockfd = -1;
    for (res0 = res; res0 != NULL; res0 = res0->ai_next) {
        sockfd = socket(res0->ai_family, res0->ai_socktype, res0->ai_protocol);
        if (sockfd == -1) continue;
        if (connect(sockfd, res0->ai_addr, res0->ai_addrlen) == -1) {
            close(sockfd);
            sockfd = -1;
            continue;
        }
        break;
    }
    freeaddrinfo(res);
    return sockfd;
}

/* ── Handshake: drain prompt, send name, drain welcome ──────────────── */

int connect_and_register(const char *name) {
    int sockfd = connect_to_server(HOST, PORT);
    if (sockfd == -1) return -1;

    char c;
    /* Drain "Enter your name:\n" */
    while (recv(sockfd, &c, 1, 0) > 0)
        if (c == '\n') break;

    /* Send name */
    char msg[64];
    snprintf(msg, sizeof(msg), "%s\n", name);
    send(sockfd, msg, strlen(msg), 0);

    /* Drain "Welcome, <name>!\n" */
    while (recv(sockfd, &c, 1, 0) > 0)
        if (c == '\n') break;

    return sockfd;
}

/* ── Read one complete newline-terminated message ────────────────────── */

ssize_t recv_line(int sockfd, char *buf, int maxlen) {
    int i = 0;
    char c;
    ssize_t n;
    while ((n = recv(sockfd, &c, 1, 0)) > 0) {
        if (i < maxlen - 1) buf[i++] = c;
        if (c == '\n') { buf[i] = '\0'; return i; }
    }
    return n;
}

/* ── Drain thread: silently consumes incoming broadcasts ─────────────── */

void *drain_thread(void *arg) {
    int sockfd = (intptr_t)arg;
    char buf[BUF_SIZE];
    while (recv(sockfd, buf, sizeof(buf), 0) > 0);
    return NULL;
}

/* ── compare_doubles: for qsort ─────────────────────────────────────── */

int compare_doubles(const void *a, const void *b) {
    double da = *(const double *)a;
    double db = *(const double *)b;
    return (da > db) - (da < db);
}

/* ── Throughput worker ───────────────────────────────────────────────── */

void *throughput_worker(void *arg) {
    int id = *(int *)arg;
    free(arg);

    char name[32];
    snprintf(name, sizeof(name), "bench-t-%d", id);

    int sockfd = connect_and_register(name);
    if (sockfd == -1) {
        fprintf(stderr, "Worker %d: failed to connect\n", id);
        return NULL;
    }

    /* Drain incoming broadcasts so socket buffer doesn't fill */
    pthread_t drain;
    pthread_create(&drain, NULL, drain_thread, (void *)(intptr_t)sockfd);
    pthread_detach(drain);

    char test[64] = "Hello World!\n";
    send(sockfd, test, strlen(test), 0);

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    int sent = 0;
    for (int i = 0; i < MESSAGES_PER_CLIENT; i++) {
        char msg[64];
        snprintf(msg, sizeof(msg), "msg-%d\n", i);
        if (send(sockfd, msg, strlen(msg), 0) < 0) break;
        sent++;
    }

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec  - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("  Worker %2d: %d messages in %.3fs = %.1f msg/sec\n",
           id, sent, elapsed, sent / elapsed);

    /* Update shared total */
    pthread_mutex_lock(&total_lock);
    total_sent += sent;
    pthread_mutex_unlock(&total_lock);

    pthread_cancel(drain);
    close(sockfd);
    return NULL;
}

/* ── Latency benchmark ───────────────────────────────────────────────── */

void run_latency_benchmark(void) {
    printf("\n── Latency Benchmark ─────────────────────────────────────\n");

    int sockfd = connect_and_register("bench-latency");
    if (sockfd == -1) {
        fprintf(stderr, "Failed to connect for latency benchmark\n");
        return;
    }

    double latencies[LATENCY_SAMPLES];

    for (int i = 0; i < LATENCY_SAMPLES; i++) {
        struct timespec t1, t2;
        char msg[64];
        snprintf(msg, sizeof(msg), "ping-%d\n", i);

        clock_gettime(CLOCK_MONOTONIC, &t1);
        send(sockfd, msg, strlen(msg), 0);

        /* Read one complete message back (our own broadcast) */
        char buf[BUF_SIZE];
        recv_line(sockfd, buf, sizeof(buf));

        clock_gettime(CLOCK_MONOTONIC, &t2);

        latencies[i] = (t2.tv_sec  - t1.tv_sec)  * 1000.0 +
                       (t2.tv_nsec - t1.tv_nsec) / 1e6;

        usleep(10000);   /* 10ms gap between samples */
    }

    close(sockfd);

    /* Calculate statistics */
    qsort(latencies, LATENCY_SAMPLES, sizeof(double), compare_doubles);

    double total = 0;
    for (int i = 0; i < LATENCY_SAMPLES; i++) total += latencies[i];

    printf("  Samples : %d\n",        LATENCY_SAMPLES);
    printf("  Average : %.2f ms\n",   total / LATENCY_SAMPLES);
    printf("  Min     : %.2f ms\n",   latencies[0]);
    printf("  p50     : %.2f ms\n",   latencies[LATENCY_SAMPLES / 2]);
    printf("  p95     : %.2f ms\n",   latencies[(int)(LATENCY_SAMPLES * 0.95)]);
    printf("  p99     : %.2f ms\n",   latencies[(int)(LATENCY_SAMPLES * 0.99)]);
    printf("  Max     : %.2f ms\n",   latencies[LATENCY_SAMPLES - 1]);
}

/* ── Throughput benchmark ────────────────────────────────────────────── */

void run_throughput_benchmark(int num_clients) {
    printf("\n── Throughput Benchmark ──────────────────────────────────\n");
    printf("  Clients : %d\n", num_clients);
    printf("  Messages per client: %d\n\n", MESSAGES_PER_CLIENT);

    pthread_t *threads = malloc(sizeof(pthread_t) * num_clients);
    if (!threads) { perror("malloc"); return; }

    total_sent = 0;

    struct timespec start, end;
    clock_gettime(CLOCK_MONOTONIC, &start);

    for (int i = 0; i < num_clients; i++) {
        int *id = malloc(sizeof(int));
        *id = i;
        if (pthread_create(&threads[i], NULL, throughput_worker, id) != 0) {
            fprintf(stderr, "Failed to create thread %d\n", i);
            free(id);
        }
    }

    for (int i = 0; i < num_clients; i++)
        pthread_join(threads[i], NULL);

    clock_gettime(CLOCK_MONOTONIC, &end);

    double elapsed = (end.tv_sec  - start.tv_sec) +
                     (end.tv_nsec - start.tv_nsec) / 1e9;

    printf("\n  Total messages sent : %d\n",      total_sent);
    printf("  Total time          : %.3f s\n",    elapsed);
    printf("  Overall throughput  : %.1f msg/sec\n",
           total_sent / elapsed);

    free(threads);
}

/* ── Max connections benchmark ───────────────────────────────────────── */

void run_maxconn_benchmark(void) {
    printf("\n── Max Connections Benchmark ─────────────────────────────\n");

    int *sockfds = malloc(sizeof(int) * MAX_CONNECTIONS);
    if (!sockfds) { perror("malloc"); return; }

    int successful = 0;
    for (int i = 0; i < MAX_CONNECTIONS; i++) {
        char name[32];
        snprintf(name, sizeof(name), "conn-%d", i);
        int fd = connect_and_register(name);
        if (fd < 0) {
            printf("  Failed at connection #%d (errno: %s)\n",
                   i, strerror(errno));
            break;
        }
        sockfds[i] = fd;
        successful++;

        /* Print progress every 100 connections */
        if (successful % 100 == 0)
            printf("  %d connections open...\n", successful);
    }

    printf("\n  Max concurrent connections: %d\n", successful);

    for (int i = 0; i < successful; i++) close(sockfds[i]);
    free(sockfds);
}

/* ── main ────────────────────────────────────────────────────────────── */

int main(int argc, char **argv) {
    if (argc < 2) {
        printf("Usage: %s [throughput|latency|maxconn] [num_clients]\n",
               argv[0]);
        printf("  throughput <N>   Spawn N clients each sending %d messages\n",
               MESSAGES_PER_CLIENT);
        printf("  latency          Measure round-trip latency (%d samples)\n",
               LATENCY_SAMPLES);
        printf("  maxconn          Find max concurrent connections\n");
        return 1;
    }

    if (!strcmp(argv[1], "throughput")) {
        if (argc < 3) {
            fprintf(stderr, "throughput requires num_clients argument\n");
            return 1;
        }
        int num_clients = atoi(argv[2]);
        if (num_clients <= 0) {
            fprintf(stderr, "num_clients must be a positive integer\n");
            return 1;
        }
        run_throughput_benchmark(num_clients);
    }
    else if (!strcmp(argv[1], "latency")) {
        run_latency_benchmark();
    }
    else if (!strcmp(argv[1], "maxconn")) {
        run_maxconn_benchmark();
    }
    else {
        fprintf(stderr, "Unknown mode: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
