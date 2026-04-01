#define _POSIX_C_SOURCE 200809L  /* activa funciones POSIX avanzadas */

#include "smart_copy.h"  /* nuestro propio header */

/* Estos headers nos dan las syscalls POSIX */
#include <stdio.h>       /* fprintf — solo para mensajes, NO para I/O  */
#include <string.h>      /* strerror — convierte errno a texto          */
#include <errno.h>       /* errno, ENOENT, EACCES, ENOSPC              */
#include <time.h>        /* clock_gettime, struct timespec              */
#include <syslog.h>      /* openlog, syslog, closelog                  */
#include <fcntl.h>       /* open(), O_RDONLY, O_WRONLY, O_CREAT        */
#include <unistd.h>      /* read(), write(), close(), access()         */
#include <sys/stat.h>    /* fstat(), struct stat                       */
#include <sys/types.h>   /* off_t, ssize_t                             */

/*timespec_diff() — calcula la diferencia entre dos momentos*/
static double
timespec_diff(const struct timespec *inicio,
              const struct timespec *fin)
{
    /*
     * (fin->tv_sec - inicio->tv_sec)      → diferencia en segundos
     * (fin->tv_nsec - inicio->tv_nsec)    → diferencia en nanosegundos
     * × 1e-9                              → convierte ns a segundos
     */
    return (double)(fin->tv_sec  - inicio->tv_sec) +
           (double)(fin->tv_nsec - inicio->tv_nsec) * 1e-9;
}

/* ─────────────────────────────────────────────────────────
 *   ENOENT → el archivo no existe en esa ruta
 *   EACCES → el proceso no tiene permiso de lectura/escritura
 *   default → cualquier otro error inesperado
 * 
 * perror() imprime un mensaje genérico.
 *
 * @param path   ruta del archivo que falló
 * @param is_src 1 si era el origen, 0 si era el destino
 * @return       código SC_ERR_* correspondiente
 * ───────────────────────────────────────────────────────── */
static int
handle_open_error(const char *path, int is_src)
{
    switch (errno) {
 
        case ENOENT:
            /*
             * El archivo o algún directorio en la ruta no existe.
             * Ejemplo: open("/ruta/que/no/existe.txt", O_RDONLY)
             * errno queda en ENOENT (valor 2 en Linux)
             */
            fprintf(stderr,
                "[ERROR] El archivo no existe: '%s'\n", path);
            syslog(LOG_WARNING,
                "Archivo no encontrado: %s", path);
            return SC_ERR_NOENT;
 
        case EACCES:
            /*
             * El proceso no tiene los permisos necesarios.
             * Ejemplo: intentar leer un archivo con chmod 000
             * errno queda en EACCES (valor 13 en Linux)
             */
            fprintf(stderr,
                "[ERROR] Permiso denegado para %s: '%s'\n",
                is_src ? "leer" : "escribir en", path);
            syslog(LOG_WARNING,
                "Permiso denegado: %s", path);
            return SC_ERR_PERM;
 
        default:
            /*
             * Cualquier otro error: dispositivo no disponible,
             * demasiados archivos abiertos, etc.
             * strerror(errno) convierte el número a texto legible.
             */
            fprintf(stderr,
                "[ERROR] No se pudo abrir '%s': %s\n",
                path, strerror(errno));
            return is_src ? SC_ERR_OPEN_SRC : SC_ERR_OPEN_DST;
    }
}


int
sys_smart_copy(const char *src_path,
               const char *dst_path,
               sc_result_t *result)
{
    /* Código de retorno — empieza en "sin error" */
    int rc = SC_OK;
 
    /* Descriptores de archivo
     * Empiezan en -1 para indicar "todavía no abierto".
     */
    int src_fd = -1;
    int dst_fd = -1;
 
    /*
     * Buffer de trabajo — PAGE_SIZE bytes (4096)
     * Está en el STACK (memoria local de la función).
     * Cuando sys_smart_copy() termina, este buffer desaparece.
     */
    char    buffer[PAGE_SIZE];
 
    /* Cuántos bytes leyó el último read() */
    ssize_t bytes_read;
 
    /* Contadores para sc_result_t */
    off_t    bytes_copied = 0;  /* total transferido              */
    uint32_t read_ops     = 0;  /* veces que llamamos read()      */
    uint32_t write_ops    = 0;  /* veces que llamamos write()     */

    struct timespec t_start, t_end;
 
  /* ── 1. Inicializar syslog ───────────────────────────
     * openlog() registra el nombre del programa en syslog.
     * LOG_PID agrega el ID del proceso a cada mensaje.
     * Puedes ver los mensajes con: cat /var/log/syslog
     * En Mac: log show --predicate 'process == "backup"'
     */
    openlog("backup_engine", LOG_PID | LOG_CONS, LOG_USER);
    syslog(LOG_INFO,
        "Iniciando copia: '%s' -> '%s'", src_path, dst_path);
 
    /* ── 2. Verificar permisos ANTES de abrir ───────────
     * access() pregunta al kernel si podemos leer el archivo.
     * Lo hacemos ANTES de open() para dar un error claro.
     *
     * R_OK = verificar permiso de lectura
     *
     * Si falla, errno ya tiene el motivo (ENOENT o EACCES).
     * goto cleanup salta al final donde se cierra todo.
     */
    if (access(src_path, R_OK) != 0) {
        rc = handle_open_error(src_path, 1);
        goto cleanup;
    }
 
    /* ── 3. Abrir origen — solo lectura ────────────────
     * O_RDONLY = abrir en modo solo lectura.
     * open() retorna un int (el fd) o -1 si falla.
     * Si src_fd < 0 → algo salió mal → saltar a cleanup.
     *
     * CONTEXT SWITCH #1 ocurre aquí:
     * tu programa → kernel → tu programa
     */
    src_fd = open(src_path, O_RDONLY);
    if (src_fd < 0) {
        rc = handle_open_error(src_path, 1);
        goto cleanup;
    }
 
    /* ── 3. Abrir/crear destino ────────────────────────
     * O_WRONLY = solo escritura
     * O_CREAT  = crear si no existe
     * O_TRUNC  = si existe, vaciarlo primero
     * 0644     = permisos del nuevo archivo en octal:
     *            dueño: leer+escribir (6)
     *            grupo: solo leer (4)
     *            otros: solo leer (4)
     *
     * CONTEXT SWITCH #2 ocurre aquí.
     */
    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd < 0) {
        rc = handle_open_error(dst_path, 0);
        goto cleanup;
    }
 
    /* ── 4. Iniciar medición de tiempo ──────────────────
     * Tomamos la "foto" del reloj justo antes del bucle.
     * CLOCK_MONOTONIC = reloj que nunca retrocede,
     * aunque el usuario cambie la hora del sistema.
     */
    clock_gettime(CLOCK_MONOTONIC, &t_start);
 
    /* ── 5. Bucle read/write — viene en el paso 4 ───── */
    (void)buffer;       /* temporal: evita warning de unused */
    (void)bytes_read;   /* temporal: evita warning de unused */
 
    /* Éxito provisional */
    syslog(LOG_INFO, "Archivos abiertos correctamente.");
    printf("[OK] Archivos abiertos: src_fd=%d  dst_fd=%d\n",
           src_fd, dst_fd);
 
/* ── CLEANUP — siempre se ejecuta ──────────────────────
 * goto cleanup salta aquí desde cualquier punto.
 * Esto garantiza que SIEMPRE cerramos los descriptores,
 * aunque haya habido un error a mitad del proceso.
 *
 * Si no cerramos los fds, el kernel los mantiene ocupados
 * hasta que el proceso termine — resource leak.
 *
 * if (fd >= 0) evita llamar close(-1) que daría error.
 */
cleanup:
    clock_gettime(CLOCK_MONOTONIC, &t_end);
 
    if (src_fd >= 0) close(src_fd);
    if (dst_fd >= 0) close(dst_fd);
 
    if (rc != SC_OK)
        syslog(LOG_ERR, "Error %d al copiar '%s'", rc, src_path);
 
    closelog();
 
    /* Rellenar sc_result_t si el llamador la proporcionó */
    if (result) {
        result->status          = rc;
        result->bytes_copied    = bytes_copied;
        result->read_ops        = read_ops;
        result->write_ops       = write_ops;
        result->elapsed_sec     = timespec_diff(&t_start, &t_end);
        result->throughput_mbps = 0.0; /* se calcula en paso 4 */
    }
 
    return rc;
}
