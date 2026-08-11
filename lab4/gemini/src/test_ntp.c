#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <math.h>
#include "ntp_time.h"
#include "ntp_packet.h"

int main(void) {
    printf("======================================================================\n");
    printf("            PRUEBAS UNITARIAS - CLIENTE NTP (VECTOR DE TEST)          \n");
    printf("======================================================================\n\n");

    int failed = 0;

    /* ------------------------------------------------------------------ */
    /* PRUEBA 1: Conversión de Fecha Guatemala -> Timestamp NTP           */
    /* ------------------------------------------------------------------ */
    printf("[TEST 1] Conversión de fecha Guatemala '2020-09-01 21:00:17.123'\n");
    const char *test_date_str = "2020-09-01 21:00:17.123";
    ntp_time_t parsed_ntp;

    if (!parse_guatemala_time(test_date_str, &parsed_ntp)) {
        printf("  ❌ FAIL: No se pudo parsear la fecha '%s'\n", test_date_str);
        failed++;
    } else {
        uint32_t expected_sec = 3808004417U;
        uint32_t expected_frac = 528280977U;

        printf("  • Obtenido : Segundos = %u (0x%08X), Fracción = %u (0x%08X)\n",
               parsed_ntp.seconds, parsed_ntp.seconds, parsed_ntp.fraction, parsed_ntp.fraction);
        printf("  • Esperado : Segundos = %u (0x%08X), Fracción = %u (0x%08X)\n",
               expected_sec, expected_sec, expected_frac, expected_frac);

        if (parsed_ntp.seconds == expected_sec && abs((int)parsed_ntp.fraction - (int)expected_frac) <= 1) {
            printf("  ✅ PASS: Conversión de fecha coincidente con el vector del PDF.\n\n");
        } else {
            printf("  ❌ FAIL: Diferencia detectada en segundos o fracción.\n\n");
            failed++;
        }
    }

    /* ------------------------------------------------------------------ */
    /* PRUEBA 2: Cálculo de Delay y Offset con Vector del PDF Página 4    */
    /* ------------------------------------------------------------------ */
    printf("[TEST 2] Algoritmo de Sincronización (Vector del PDF Página 4)\n");
    ntp_time_t t1 = { .seconds = 0xE33A1691U, .fraction = 0x1F9843A4U }; /* 3808004417 - 529998964 */
    ntp_time_t t2 = { .seconds = 0xE33A18CBU, .fraction = 0xBE25E755U }; /* 3808005003 - 3189781941 */
    ntp_time_t t3 = { .seconds = 0xE33A18CCU, .fraction = 0x05A4D480U }; /* 3808005004 - 94489280 */
    ntp_time_t t4 = { .seconds = 0xE33A1692U, .fraction = 0x00C3F6D6U }; /* 3808004418 - 12884902 */

    double delay_ms  = ntp_calc_delay_ms(t1, t2, t3, t4);
    double offset_ms = ntp_calc_offset_ms(t1, t2, t3, t4);

    printf("  • T1 Hex: 0x%08X%08X\n", t1.seconds, t1.fraction);
    printf("  • T2 Hex: 0x%08X%08X\n", t2.seconds, t2.fraction);
    printf("  • T3 Hex: 0x%08X%08X\n", t3.seconds, t3.fraction);
    printf("  • T4 Hex: 0x%08X%08X\n", t4.seconds, t4.fraction);
    printf("  • Delay Calculado  : %.3f ms (Esperado ≈ 601 ms / 600.28 ms)\n", delay_ms);
    printf("  • Offset Calculado : %.3f ms (%.6f s)\n", offset_ms, offset_ms / 1000.0);

    /* Verify Delay ~ 600-601 ms */
    if (fabs(delay_ms - 600.28) < 2.0) {
        printf("  ✅ PASS: Cálculo de Delay correcto (~600.28 ms).\n");
    } else {
        printf("  ❌ FAIL: Delay difiere del valor esperado.\n");
        failed++;
    }

    if (failed == 0) {
        printf("\n======================================================================\n");
        printf("🎉 TODAS LAS PRUEBAS UNITARIAS PASARON EXITOSAMENTE (0 ERRORES)\n");
        printf("======================================================================\n");
        return 0;
    } else {
        printf("\n======================================================================\n");
        printf("❌ ERRORES DETECTADOS EN PRUEBAS UNITARIAS (%d fallos)\n", failed);
        printf("======================================================================\n");
        return 1;
    }
}
