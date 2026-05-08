#include <getopt.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "../include/chttp.h"
#include "logger.h"

static HttpServer* g_server = NULL;

static void handle_signal(int sig) {
    (void)sig;
    if (g_server) {
        LOG_INFO("Received shutdown signal, shutting down...");
        exit(0);
    }
}

static void print_usage(const char* prog_name) {
    printf("Usage: %s [options]\n", prog_name);
    printf("Options:\n");
    printf("  --port <port>       Port to listen on (default: 8080)\n");
    printf("  --root <path>       Root directory for static files (default: ./www)\n");
    printf("  --max-conn <count>  Maximum simultaneous connections (default: 10000)\n");
    printf("  --debug             Enable debug logging\n");
    printf("  --live-reload       Enable automatic page reloads on file changes\n");
    printf("  --help              Show this help message\n");
}

int main(int argc, char** argv) {
    HttpServerConfig config = {
        .port = 8080, .root_dir = "./www", .max_connections = 10000, .io_ops = NULL};

    LogLevel log_level = LOG_INFO;

    struct option long_options[] = {{"port", required_argument, 0, 'p'},
                                    {"root", required_argument, 0, 'r'},
                                    {"max-conn", required_argument, 0, 'm'},
                                    {"debug", no_argument, 0, 'd'},
                                    {"live-reload", no_argument, 0, 'l'},
                                    {"help", no_argument, 0, 'h'},
                                    {0, 0, 0, 0}};

    int opt;
    int option_index = 0;
    while ((opt = getopt_long(argc, argv, "p:r:m:dlh", long_options, &option_index)) != -1) {
        if (opt == 'p') {
            config.port = (uint16_t)strtol(optarg, NULL, 10);
        } else if (opt == 'r') {
            config.root_dir = optarg;
        } else if (opt == 'm') {
            config.max_connections = (int)strtol(optarg, NULL, 10);
        } else if (opt == 'd') {
            log_level = LOG_DEBUG;
        } else if (opt == 'l') {
            config.live_reload = true;
        } else if (opt == 'h') {
            print_usage(argv[0]);
            return 0;
        } else {
            print_usage(argv[0]);
            return 1;
        }
    }

    logger_set_level(log_level);

    struct sigaction sa = {0};
    sa.sa_handler = handle_signal;
    sigemptyset(&sa.sa_mask);
    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);

    (void)signal(SIGPIPE, SIG_IGN);

    if (chttp_server_create(&config, &g_server) != 0) {
        LOG_ERROR("Failed to initialize server");
        return 1;
    }

    LOG_INFO("Starting chttp server on port %d, root: %s", config.port, config.root_dir);
    printf("\n  >> Local:   http://localhost:%d/\n\n", config.port);

    if (chttp_server_run(g_server) != 0) {
        LOG_ERROR("Server run loop exited with error");
    }

    chttp_server_destroy(g_server);
    g_server = NULL;

    return 0;
}
