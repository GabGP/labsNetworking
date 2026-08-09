#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <math.h>
#include <sys/time.h>

#include "ntp_time.h"
#include "ntp_packet.h"
#include "ntp_net.h"
#include "sync.h"
#include "log.h"

static volatile bool g_running = true;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = false;
    printf("\n\n[INFO] Señal de interrupción (Ctrl+C) recibida. Finalizando cliente NTP...\n");
}

static void print_usage(const char *prog) {
    printf("Uso: %s [OPCIONES]\n", prog);
    printf("Opciones:\n");
    printf("  --fecha \"YYYY-MM-DD HH:MM:SS.mmm\"  Fecha/Hora local inicial en Guatemala (UTC-6)\n");
    printf("                                       [Default: Hora actual del sistema]\n");
    printf("  --umbral-ms NUM                      Umbral de offset en milisegundos para considerar\n");
    printf("                                       sincronizado el reloj [Default: 50.0 ms]\n");
    printf("  --intervalo NUM                      Intervalo de verificación en segundos tras\n");
    printf("                                       la sincronización [Default: 60 s]\n");
    printf("  --help                               Muestra este mensaje de ayuda\n\n");
    printf("Ejemplo de uso:\n");
    printf("  %s --fecha \"2020-09-01 21:00:17.123\" --umbral-ms 50 --intervalo 60\n\n", prog);
}

int main(int argc, char *argv[]) {
    signal(SIGINT, handle_sigint);

    char *fecha_str = NULL;
    double threshold_ms = 50.0;
    int interval_sec = 60;

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--fecha") == 0 && i + 1 < argc) {
            fecha_str = argv[++i];
        } else if (strcmp(argv[i], "--umbral-ms") == 0 && i + 1 < argc) {
            threshold_ms = atof(argv[++i]);
        } else if (strcmp(argv[i], "--intervalo") == 0 && i + 1 < argc) {
            interval_sec = atoi(argv[++i]);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            print_usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Opción no reconocida: %s\n", argv[i]);
            print_usage(argv[0]);
            return 1;
        }
    }

    ntp_time_t initial_ntp;
    if (fecha_str) {
        if (!parse_guatemala_time(fecha_str, &initial_ntp)) {
            fprintf(stderr, "Error: Formato de fecha/hora no válido: '%s'\n", fecha_str);
            fprintf(stderr, "Debe ser \"YYYY-MM-DD HH:MM:SS.mmm\" (ej. \"2020-09-01 21:00:17.123\")\n");
            return 1;
        }
    } else {
        /* Default: current system time */
        struct timeval tv;
        gettimeofday(&tv, NULL);
        double frac = (double)tv.tv_usec / 1000000.0;
        initial_ntp = unix_to_ntp_time(tv.tv_sec, frac);
    }

    sim_clock_init(initial_ntp);

    log_banner();
    log_config(fecha_str, threshold_ms, interval_sec, DEFAULT_NTP_SERVER_COUNT, DEFAULT_NTP_SERVERS);

    int iteration = 1;
    bool synchronized = false;

    /* Fase 1: Bucle de Sincronización Inicial */
    while (g_running && !synchronized) {
        ntp_time_t sim_before = sim_clock_get_time();
        log_iteration_start(iteration, sim_before);

        ntp_server_result_t results[MAX_SERVERS];
        size_t valid_count = ntp_query_all_servers(DEFAULT_NTP_SERVERS,
                                                  DEFAULT_NTP_SERVER_COUNT,
                                                  DEFAULT_TIMEOUT_SEC,
                                                  sim_clock_get_time,
                                                  results);
        (void)valid_count;

        for (size_t i = 0; i < DEFAULT_NTP_SERVER_COUNT; i++) {
            log_server_result(i, &results[i]);
        }

        int best_idx = sync_find_best_server(results, DEFAULT_NTP_SERVER_COUNT);
        ntp_time_t sim_after = sim_before;

        if (best_idx >= 0) {
            double chosen_offset = results[best_idx].offset_ms;
            sim_clock_adjust_ms(chosen_offset);
            sim_after = sim_clock_get_time();

            if (fabs(chosen_offset) <= threshold_ms) {
                synchronized = true;
            }

            log_iteration_summary(results, DEFAULT_NTP_SERVER_COUNT, best_idx,
                                  sim_before, sim_after, threshold_ms, synchronized);
        } else {
            log_iteration_summary(results, DEFAULT_NTP_SERVER_COUNT, -1,
                                  sim_before, sim_after, threshold_ms, false);
        }

        iteration++;

        if (!synchronized && g_running) {
            /* Sleep 2 seconds between sync iterations */
            for (int s = 0; s < 2 && g_running; s++) {
                sleep(1);
            }
        }
    }

    /* Fase 2: Bucle de Verificación Periódica */
    while (g_running) {
        printf("[INFO] Esperando %d segundos para la siguiente verificación periódica...\n\n", interval_sec);

        for (int s = 0; s < interval_sec && g_running; s++) {
            sleep(1);
        }

        if (!g_running) break;

        ntp_time_t sim_before = sim_clock_get_time();
        log_verification_start(sim_before);

        ntp_server_result_t results[MAX_SERVERS];
        ntp_query_all_servers(DEFAULT_NTP_SERVERS,
                             DEFAULT_NTP_SERVER_COUNT,
                             DEFAULT_TIMEOUT_SEC,
                             sim_clock_get_time,
                             results);

        for (size_t i = 0; i < DEFAULT_NTP_SERVER_COUNT; i++) {
            log_server_result(i, &results[i]);
        }

        int best_idx = sync_find_best_server(results, DEFAULT_NTP_SERVER_COUNT);
        ntp_time_t sim_after = sim_before;

        if (best_idx >= 0) {
            double chosen_offset = results[best_idx].offset_ms;
            sim_clock_adjust_ms(chosen_offset);
            sim_after = sim_clock_get_time();

            log_iteration_summary(results, DEFAULT_NTP_SERVER_COUNT, best_idx,
                                  sim_before, sim_after, threshold_ms, true);
        } else {
            log_iteration_summary(results, DEFAULT_NTP_SERVER_COUNT, -1,
                                  sim_before, sim_after, threshold_ms, true);
        }
    }

    printf("[INFO] Cliente NTP finalizado correctamente.\n");
    return 0;
}
