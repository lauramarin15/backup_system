#define _POSIX_C_SOURCE 200809L

#include "smart_copy.h"  /* nuestro header */

/* Librería estándar C — aquí SÍ usamos stdio para I/O (stdio_copy) */
#include <stdio.h>       /* fopen, fread, fwrite, fclose, printf      */
#include <stdlib.h>      /* exit, EXIT_SUCCESS, EXIT_FAILURE           */
#include <string.h>      /* strcmp, memset                             */
#include <errno.h>       /* errno, strerror                            */
#include <time.h>        /* clock_gettime                              */
#include <unistd.h>      /* unlink                                     */
#include <sys/stat.h>    /* stat                                       */

/* Tamaños para el benchmark */
#define SIZE_1KB   (1024L)
#define SIZE_1MB   (1024L * 1024L)
#define SIZE_1GB   (1024L * 1024L * 1024L)

/* Rutas temporales */
#define TMP_SRC    "/tmp/bk_src.bin"
#define TMP_SMART  "/tmp/bk_dst_smart.bin"
#define TMP_STDIO  "/tmp/bk_dst_stdio.bin"

static int
stdio_copy(const char *src_path,
           const char *dst_path,
           sc_result_t *result)
{
    int rc = SC_OK;
 
    FILE *src_f = NULL;
    FILE *dst_f = NULL;
 
    /*Buffer del mismo tamaño que sys_smart_copy.*/
    char buffer[PAGE_SIZE];
 
    /* Mismos contadores que en backup_engine.c */
    off_t    bytes_copied = 0;
    uint32_t read_ops     = 0;
    uint32_t write_ops    = 0;
 
    /* Mismo sistema de temporización */
    struct timespec t_start, t_end;
 
 
    clock_gettime(CLOCK_MONOTONIC, &t_start);
 
    /* ── 1. Abrir origen con fopen ───────────────────────
     * "rb" = read binary
     *   r = abrir para lectura
     *   b = modo binario (sin conversiones de \n)
     *
     * fopen() internamente llama a open() del kernel,
     * pero envuelve el resultado en una estructura FILE*
     * con el buffer interno de la libc.
     *
     * Si falla retorna NULL (no -1 como open()).
     */
    src_f = fopen(src_path, "rb");
    if (!src_f) {
        /*
         * Mismo switch(errno) que handle_open_error()
         * en backup_engine.c.
         * errno lo llenó fopen() internamente cuando
         * su open() interno falló.
         */
        switch (errno) {
            case ENOENT:
                fprintf(stderr,
                    "[ERROR] stdio: archivo no existe: '%s'\n",
                    src_path);
                rc = SC_ERR_NOENT;
                break;
            case EACCES:
                fprintf(stderr,
                    "[ERROR] stdio: sin permisos para leer: '%s'\n",
                    src_path);
                rc = SC_ERR_PERM;
                break;
            default:
                fprintf(stderr,
                    "[ERROR] stdio: no se pudo abrir '%s': %s\n",
                    src_path, strerror(errno));
                rc = SC_ERR_OPEN_SRC;
        }
        goto cleanup;
    }
 
    /* ── 2. Crear destino con fopen ─────────────────────
     * "wb" = write binary
     *   w = crear/vaciar y abrir para escritura
     *   b = modo binario
     *
     * Si el archivo no existe, fopen lo crea.
     * Si existe, lo vacía (equivale a O_TRUNC).
     */
    dst_f = fopen(dst_path, "wb");
    if (!dst_f) {
        switch (errno) {
            case ENOSPC:
                fprintf(stderr,
                    "[ERROR] stdio: disco lleno: '%s'\n",
                    dst_path);
                rc = SC_ERR_NOSPC;
                break;
            case EACCES:
                fprintf(stderr,
                    "[ERROR] stdio: sin permisos para escribir: '%s'\n",
                    dst_path);
                rc = SC_ERR_PERM;
                break;
            default:
                fprintf(stderr,
                    "[ERROR] stdio: no se pudo crear '%s': %s\n",
                    dst_path, strerror(errno));
                rc = SC_ERR_OPEN_DST;
        }
        goto cleanup;
    }

    /* fread(buffer, 1, PAGE_SIZE, src_f)
     *   parámetros: dónde guardar, tamaño elemento,
     *               cuántos elementos, de qué FILE*
     *   retorna: cuántos elementos leyó (0 = EOF o error)
     *
     * fwrite() retorna cuántos elementos escribió.
     * Si es distinto de n_read → error.
     */
    size_t n_read;
    while ((n_read = fread(buffer, 1, PAGE_SIZE, src_f)) > 0) {
 
        read_ops++;
 
        size_t n_written = fwrite(buffer, 1, n_read, dst_f);
 
        if (n_written != n_read) {
            /*
             * fwrite falló — verificar si el disco está lleno.
             * errno lo llenó fwrite() internamente.
             */
            if (errno == ENOSPC) {
                fprintf(stderr,
                    "[ERROR] stdio: disco lleno escribiendo\n");
                rc = SC_ERR_NOSPC;
            } else {
                fprintf(stderr,
                    "[ERROR] stdio: error de escritura: %s\n",
                    strerror(errno));
                rc = SC_ERR_WRITE;
            }
            goto cleanup;
        }
 
        write_ops++;
        bytes_copied += (off_t)n_written;
    }
 
    /*
     * ferror() distingue si el while terminó por:
     *   EOF normal → feof(src_f) es verdadero → ok
     *   error real → ferror(src_f) es verdadero → error
     */
    if (ferror(src_f)) {
        fprintf(stderr,
            "[ERROR] stdio: error de lectura en '%s'\n", src_path);
        rc = SC_ERR_READ;
        goto cleanup;
    }
 
    printf("[OK] stdio_copy: %lld bytes copiados\n",
           (long long)bytes_copied);
 
cleanup:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
 
    /* fclose() vacía el buffer interno antes de cerrar */
    if (src_f) fclose(src_f);
    if (dst_f) fclose(dst_f);
 
    if (result) {
        result->status      = rc;
        result->bytes_copied = bytes_copied;
        result->read_ops    = read_ops;
        result->write_ops   = write_ops;
        result->elapsed_sec = (double)(t_end.tv_sec - t_start.tv_sec)
                            + (double)(t_end.tv_nsec - t_start.tv_nsec)
                            * 1e-9;
        result->throughput_mbps = (result->elapsed_sec > 0.0)
            ? (double)bytes_copied
              / result->elapsed_sec
              / (1024.0 * 1024.0)
            : 0.0;
    }
 
    return rc;
}

static int
generate_file(const char *path, long size)
{
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "[ERROR] No se pudo generar '%s': %s\n",
                path, strerror(errno));
        return -1;
    }
 
    char block[PAGE_SIZE];
    srand(12345); /* semilla fija = mismo archivo siempre */
 
    long written = 0;
    while (written < size) {
 
        /* Calcular chunk: puede ser menor en el último bloque */
        long chunk = PAGE_SIZE;
        if (written + chunk > size)
            chunk = size - written;
 
        /* Llenar con bytes aleatorios 0-255 */
        for (long i = 0; i < chunk; i++)
            block[i] = (char)(rand() & 0xFF);
 
        if ((long)fwrite(block, 1, (size_t)chunk, f) != chunk) {
            fclose(f);
            return -1;
        }
 
        written += chunk;
    }
 
    fclose(f);
    return 0;
}

static void
print_header(void)
{
    printf("\n" );
    printf("╔══════════════════════════════════════════════════════╗\n");
    printf("║     BENCHMARK: sys_smart_copy vs stdio_copy          ║\n");
    printf("║     buffer = %d bytes (PAGE_SIZE)                  ║\n",
           PAGE_SIZE);
    printf("╚══════════════════════════════════════════════════════╝\n");
    printf( "\n");
    printf( "%-6s  %-24s  %-24s  %-10s\n",
           "Tamaño", "sys_smart_copy", "stdio_copy", "Ganador");
    printf("  %-6s  %-24s  %-24s  %-10s\n",
           "──────", "────────────────────────",
           "────────────────────────", "──────────");
}

static void
print_row(const char *label,
          const sc_result_t *smart,
          const sc_result_t *stdio_r)
{
    int smart_wins = (smart->elapsed_sec <= stdio_r->elapsed_sec);
 
    printf("  %-6s  %8.6fs %8.2f MB/s    "
           "%8.6fs %8.2f MB/s    %s\n",
           label,
           smart->elapsed_sec,
           smart->throughput_mbps,
           stdio_r->elapsed_sec,
           stdio_r->throughput_mbps,
           smart_wins
               ?  "syscall ✓" 
               :  "fread  ✓" );
}
 
 /* ─────────────────────────────────────────────────────────
 * run_benchmark() — el benchmark completo
 *
 * Para cada tamaño (1KB, 1MB, 1GB):
 *   1. genera el archivo de prueba
 *   2. corre sys_smart_copy → mide tiempo
 *   3. corre stdio_copy     → mide tiempo
 *   4. imprime fila de la tabla
 *   5. elimina archivos temporales
 * ───────────────────────────────────────────────────────── */
static void
run_benchmark(void)
{
    /* Array de structs con los 3 tamaños a probar */
    struct {
        const char *label;
        long        size;
    } tests[] = {
        { "1 KB",  SIZE_1KB },
        { "1 MB",  SIZE_1MB },
        { "1 GB",  SIZE_1GB },
    };
 
    /*
     * sizeof(tests) / sizeof(tests[0]) = cantidad de elementos.
     * Si agregas un 4to test no tienes que cambiar este número.
     */
    int n = (int)(sizeof(tests) / sizeof(tests[0]));
 
    print_header();
 
    for (int i = 0; i < n; i++) {
 
        /* Paso 1: generar archivo de prueba */
        printf("  Generando %s...\r", tests[i].label);
        fflush(stdout);
 
        if (generate_file(TMP_SRC, tests[i].size) != 0) {
            fprintf(stderr, "Error generando archivo de %s\n",
                    tests[i].label);
            continue;
        }
 
        /* Paso 2: sys_smart_copy */
        sc_result_t res_smart;
        memset(&res_smart, 0, sizeof(res_smart));
        unlink(TMP_SMART); /* eliminar si existe del run anterior */
        sys_smart_copy(TMP_SRC, TMP_SMART, &res_smart);
 
        /* Paso 3: stdio_copy */
        sc_result_t res_stdio;
        memset(&res_stdio, 0, sizeof(res_stdio));
        unlink(TMP_STDIO);
        stdio_copy(TMP_SRC, TMP_STDIO, &res_stdio);
 
        /* Paso 4: imprimir fila */
        print_row(tests[i].label, &res_smart, &res_stdio);
 
        /* Paso 5: limpiar */
        unlink(TMP_SRC);
        unlink(TMP_SMART);
        unlink(TMP_STDIO);
    }
 
    /* Análisis explicativo */
    printf("\n" "  Análisis:\n");
    printf("  1 KB → fread gana: buffer interno libc absorbe el\n");
    printf("         archivo en 1 solo context switch al kernel.\n");
    printf("  1 GB → syscall gana: fread hace copia extra en RAM\n");
    printf("         (disco→buf_libc→tu_buf) en cada iteración.\n\n");
}
/* ─────────────────────────────────────────────────────────
 * run_backup() — modo -b: respalda un archivo real
 *
 * Corre ambos métodos sobre el mismo archivo y muestra
 * una tabla comparativa con los tiempos reales.
 *
 * Antes de copiar, verifica que el origen existe con stat().
 * Si no existe, switch(errno) da el mensaje correcto.
 * ───────────────────────────────────────────────────────── */
static void
run_backup(const char *src, const char *dst)
{
    struct stat st;
 
    /* Verificar que el origen existe antes de todo */
    if (stat(src, &st) != 0) {
        switch (errno) {
            case ENOENT:
                fprintf(stderr,
                    "[ERROR] El origen no existe: '%s'\n", src);
                break;
            case EACCES:
                fprintf(stderr,
                    "[ERROR] Sin permisos para acceder: '%s'\n", src);
                break;
            default:
                fprintf(stderr,
                    "[ERROR] stat() falló en '%s': %s\n",
                    src, strerror(errno));
        }
        exit(EXIT_FAILURE);
    }
 
    printf("\n--- Respaldando '%s' ---\n\n", src);
 
    /*
     * Generamos dos destinos con sufijo para no pisar
     * el archivo original:
     *   archivo.txt → archivo.txt.smart
     *   archivo.txt → archivo.txt.stdio
     */
    char dst_smart[512], dst_stdio[512];
    snprintf(dst_smart, sizeof(dst_smart), "%s.smart", dst);
    snprintf(dst_stdio,  sizeof(dst_stdio),  "%s.stdio",  dst);
 
    /* Ejecutar ambos métodos */
    sc_result_t res_smart, res_stdio;
    memset(&res_smart, 0, sizeof(res_smart));
    memset(&res_stdio,  0, sizeof(res_stdio));
 
    sys_smart_copy(src, dst_smart, &res_smart);
    stdio_copy(src, dst_stdio, &res_stdio);
 
    /* Mostrar resultados comparativos */
    printf("\n" "  Resultados:\n");
    printf("  %-28s %-10s  %-12s  %s\n",
           "Método", "Tiempo", "Throughput", "Ops");
    printf("  %-28s %-10s  %-12s  %s\n",
           "────────────────────────────",
           "──────────", "────────────", "────");
 
    sc_print_result("sys_smart_copy (syscall)", &res_smart);
    sc_print_result("stdio_copy     (fread)  ", &res_stdio);
 
    int smart_wins =
        (res_smart.elapsed_sec <= res_stdio.elapsed_sec);
 
    printf("\n  "  "Ganador: %s"  "\n\n",
           smart_wins ? "sys_smart_copy" : "stdio_copy");
}
 
/* ─────────────────────────────────────────────────────────
 * print_help() — muestra cómo usar el programa
 * ───────────────────────────────────────────────────────── */
static void
print_help(const char *prog)
{
    printf("\n" 
           "  Sistema de Backup — Syscalls vs stdio\n");
    printf("\n  Uso:\n");
    printf("    %s -b <origen> <destino>   respalda un archivo\n",
           prog);
    printf("    %s --benchmark             tabla 1KB · 1MB · 1GB\n",
           prog);
    printf("    %s -h                      esta ayuda\n\n", prog);
    printf("  Ejemplos:\n");
    printf("    %s -b /etc/hosts /tmp/hosts_copia\n", prog);
    printf("    %s --benchmark\n\n", prog);
}
 
/* ─────────────────────────────────────────────────────────
 * main() — FINAL
 *
 * Flujo de decisiones:
 *   argc < 2        → print_help()
 *   argv[1] == -h   → print_help()
 *   argv[1] == -b   → run_backup(argv[2], argv[3])
 *   argv[1] == --benchmark → run_benchmark()
 *   otro            → error + print_help()
 *
 * strcmp() retorna 0 cuando dos strings son iguales.
 * argc == 4 verifica que haya exactamente: prog -b src dst
 * ───────────────────────────────────────────────────────── */
int
main(int argc, char *argv[])
{
    /* Sin argumentos → mostrar ayuda */
    if (argc < 2) {
        print_help(argv[0]);
        return EXIT_FAILURE;
    }
 
    /* -h o --help */
    if (strcmp(argv[1], "-h") == 0 ||
        strcmp(argv[1], "--help") == 0) {
        print_help(argv[0]);
        return EXIT_SUCCESS;
    }
 
    /* --benchmark */
    if (strcmp(argv[1], "--benchmark") == 0) {
        run_benchmark();
        return EXIT_SUCCESS;
    }
 
    /* -b o --backup */
    if (strcmp(argv[1], "-b") == 0 ||
        strcmp(argv[1], "--backup") == 0) {
 
        /* Verificar que vienen los dos argumentos */
        if (argc != 4) {
            fprintf(stderr,
                "[ERROR] Uso: %s -b <origen> <destino>\n",
                argv[0]);
            return EXIT_FAILURE;
        }
 
        run_backup(argv[2], argv[3]);
        return EXIT_SUCCESS;
    }
 
    /* Opción no reconocida */
    fprintf(stderr,
        "[ERROR] Opción no reconocida: '%s'\n", argv[1]);
    print_help(argv[0]);
    return EXIT_FAILURE;
}
 