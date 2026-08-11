#define _POSIX_C_SOURCE 200809L
#include "log.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

void log_banner(void) {
    printf("======================================================================\n");
    printf("        CLIENTE NTP EN C - RFC 5905 (Sincronización Multiseridor)      \n");
    printf("        Curso: Ciencias de la Computación VIII                        \n");
    printf("======================================================================\n\n");
}

void log_config(const char *initial_date_str,
                double threshold_ms,
                int interval_sec,
                size_t server_count,
                const char **servers) {
    printf("[CONFIGURACIÓN INICIAL]\n");
    printf("  • Fecha/Hora Reloj Simulado: %s\n", initial_date_str ? initial_date_str : "Hora Actual del Sistema");
    printf("  • Umbral de Sincronización:  %.2f ms\n", threshold_ms);
    printf("  • Intervalo de Verificación: %d segundos\n", interval_sec);
    printf("  • Servidores NTP (%zu configurados):\n", server_count);
    for (size_t i = 0; i < server_count; i++) {
        printf("      [%2zu] %s\n", i + 1, servers[i]);
    }
    printf("----------------------------------------------------------------------\n\n");
}

void log_iteration_start(int iteration, ntp_time_t current_sim_time) {
    char guat_str[64], utc_str[64];
    ntp_time_to_guatemala_str(current_sim_time, guat_str, sizeof(guat_str));
    ntp_time_to_utc_str(current_sim_time, utc_str, sizeof(utc_str));

    printf("======================================================================\n");
    printf(">>> ITERACIÓN DE SINCRONIZACIÓN N° %d <<<\n", iteration);
    printf("  • Hora Reloj Simulado Actual (Guatemala): %s\n", guat_str);
    printf("  • Hora Reloj Simulado Actual (UTC):       %s\n", utc_str);
    printf("  • Consultando %d servidores NTP en paralelo...\n", MAX_SERVERS);
    printf("----------------------------------------------------------------------\n");
}

void log_server_result(size_t index, const ntp_server_result_t *res) {
    printf("\n[%2zu] Servidor: %s (IP: %s)\n", index + 1, res->hostname,
           res->ip_str[0] ? res->ip_str : "N/A");

    if (!res->responded) {
        printf("     ESTADO: [ ERROR / NO RESPONDIÓ ] -> %s\n", res->error_msg);
        return;
    }

    printf("     ESTADO: [ RESPUESTA EXITOSA ] (Stratum %u)\n", res->stratum);

    char t1_utc[64], t2_utc[64], t3_utc[64], t4_utc[64];
    ntp_time_to_utc_str(res->t1, t1_utc, sizeof(t1_utc));
    ntp_time_to_utc_str(res->t2, t2_utc, sizeof(t2_utc));
    ntp_time_to_utc_str(res->t3, t3_utc, sizeof(t3_utc));
    ntp_time_to_utc_str(res->t4, t4_utc, sizeof(t4_utc));

    printf("     TIMESTAMPS (T1 - T4):\n");
    printf("       T1 (Cliente Envía)   : 0x%08X%08X | Sec: %10u | Frac: %10u | %s\n",
           res->t1.seconds, res->t1.fraction, res->t1.seconds, res->t1.fraction, t1_utc);
    printf("       T2 (Servidor Recibe) : 0x%08X%08X | Sec: %10u | Frac: %10u | %s\n",
           res->t2.seconds, res->t2.fraction, res->t2.seconds, res->t2.fraction, t2_utc);
    printf("       T3 (Servidor Responde):0x%08X%08X | Sec: %10u | Frac: %10u | %s\n",
           res->t3.seconds, res->t3.fraction, res->t3.seconds, res->t3.fraction, t3_utc);
    printf("       T4 (Cliente Recibe)  : 0x%08X%08X | Sec: %10u | Frac: %10u | %s\n",
           res->t4.seconds, res->t4.fraction, res->t4.seconds, res->t4.fraction, t4_utc);

    printf("     CÁLCULOS:\n");
    printf("       Delay (δ) = (T4 - T1) - (T3 - T2) = %11.3f ms (%.6f s)\n",
           res->delay_ms, res->delay_ms / 1000.0);
    printf("       Offset (θ) = ((T2 - T1) + (T3 - T4)) / 2 = %11.3f ms (%.6f s)\n",
           res->offset_ms, res->offset_ms / 1000.0);
}

void log_iteration_summary(const ntp_server_result_t *results,
                           size_t count,
                           int best_idx,
                           ntp_time_t sim_before,
                           ntp_time_t sim_after,
                           double threshold_ms,
                           bool is_sync) {
    (void)count;
    printf("\n----------------------------------------------------------------------\n");
    printf("[RESUMEN DE ITERACIÓN]\n");

    if (best_idx < 0) {
        printf("  ❌ Ningún servidor NTP entregó una respuesta válida.\n");
        printf("  • No se pudo realizar ajuste en esta iteración.\n");
        printf("======================================================================\n\n");
        return;
    }

    const ntp_server_result_t *best = &results[best_idx];

    printf("  ★ Servidor Seleccionado (MENOR DELAY): %s (%s)\n",
           best->hostname, best->ip_str);
    printf("      • Delay Seleccionado (δ):  %.3f ms\n", best->delay_ms);
    printf("      • Offset Aplicado (θ):     %.3f ms (%.6f s)\n",
           best->offset_ms, best->offset_ms / 1000.0);

    char guat_before[64], guat_after[64];
    ntp_time_to_guatemala_str(sim_before, guat_before, sizeof(guat_before));
    ntp_time_to_guatemala_str(sim_after, guat_after, sizeof(guat_after));

    printf("\n  • Reloj Simulado Antes del Ajuste:  %s\n", guat_before);
    printf("  • Reloj Simulado Después del Ajuste: %s\n", guat_after);

    double abs_offset = fabs(best->offset_ms);
    printf("  • Offset Restante (|θ|):            %.3f ms (Umbral: %.2f ms)\n",
           abs_offset, threshold_ms);

    if (is_sync) {
        printf("\n======================================================================\n");
        printf("🎉 ¡ESTADO: RELOJ SINCRONIZADO EXITOSAMENTE! (|θ| <= %.2f ms)\n", threshold_ms);
        printf("======================================================================\n\n");
    } else {
        printf("  ➜ El offset aún supera el umbral. Reinstalando siguiente iteración...\n");
        printf("======================================================================\n\n");
    }
}

void log_verification_start(ntp_time_t current_sim_time) {
    char guat_str[64];
    ntp_time_to_guatemala_str(current_sim_time, guat_str, sizeof(guat_str));

    printf("----------------------------------------------------------------------\n");
    printf("[VERIFICACIÓN PERIÓDICA POST-SINCRONIZACIÓN]\n");
    printf("  • Hora Reloj Simulado Actual: %s\n", guat_str);
    printf("  • Ejecutando sondeo de control...\n");
    printf("----------------------------------------------------------------------\n");
}
