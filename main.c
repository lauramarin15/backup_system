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
 
    /* ── 3. Bucle fread/fwrite */
    (void)buffer;     /* temporal: evita warning */
    (void)read_ops;   /* temporal: evita warning */
    (void)write_ops;  /* temporal: evita warning */
 
    printf("[OK] stdio: archivos abiertos correctamente\n");
 
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
        result->throughput_mbps = 0.0; /* se calcula en paso 4 */
    }
 
    return rc;
}
int
main(int argc, char *argv[])
{
    /* Crear un archivo de prueba pequeño */
    FILE *f = fopen("/tmp/prueba_paso3.txt", "wb");
    if (f) {
        fprintf(f, "Hola desde el paso 3\n");
        fclose(f);
    }
 
    printf("\n=== Probando apertura de archivos ===\n\n");
 
    /* Prueba 1: archivo que existe */
    printf("Prueba 1 — archivo válido:\n");
    sc_result_t r1;
    sys_smart_copy("/tmp/prueba_paso3.txt",
                   "/tmp/prueba_dst_smart.txt", &r1);
    stdio_copy("/tmp/prueba_paso3.txt",
               "/tmp/prueba_dst_stdio.txt", &r1);
 
    /* Prueba 2: archivo que NO existe */
    printf("\nPrueba 2 — archivo inexistente:\n");
    sc_result_t r2;
    sys_smart_copy("/tmp/no_existe.txt",
                   "/tmp/dst.txt", &r2);
    stdio_copy("/tmp/no_existe.txt",
               "/tmp/dst.txt", &r2);
 
    /* Prueba 3: sin permisos */
    printf("\nPrueba 3 — sin permisos (solo en Linux):\n");
    system("touch /tmp/sin_permisos.txt && "
           "chmod 000 /tmp/sin_permisos.txt");
    sc_result_t r3;
    sys_smart_copy("/tmp/sin_permisos.txt",
                   "/tmp/dst.txt", &r3);
 
    /* Limpieza */
    unlink("/tmp/prueba_paso3.txt");
    unlink("/tmp/prueba_dst_smart.txt");
    unlink("/tmp/prueba_dst_stdio.txt");
    unlink("/tmp/sin_permisos.txt");
 
    printf("\nPaso 3 completo\n");
    return EXIT_SUCCESS;
}
 