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



/* ─────────────────────────────────────────────────────────
 * log_write() — escribe una línea en output/backup.log
 *
 * Formato de cada línea:
 *   [2024-11-15 10:32:45] syscall | origen → destino | 1048576 bytes | 0.003100s | 321.42 MB/s | OK
 *
 * Abre el archivo en modo "a" (append) para no borrar
 * entradas anteriores — cada copia agrega una línea nueva.
 *
 * Si la carpeta output/ no existe, intenta crearla.
 * ───────────────────────────────────────────────────────── */
void
log_write(const char *method,
          const char *src,
          const char *dst,
          const sc_result_t *result)
{
    if (!result) return;
 
    /* Crear output/ si no existe */
    mkdir("output", 0755);
 
    /* Abrir log en modo append — agrega al final sin borrar */
    FILE *log = fopen(LOG_PATH, "a");
    if (!log) return; /* si no se puede abrir, continuar sin log */
 
    /* Obtener fecha y hora actual */
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    char timestamp[32];
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", t);
 
    /* Escribir línea de log */
    fprintf(log,
        "[%s] %-7s | %s → %s | %lld bytes | %.6fs | %.2f MB/s | %s\n",
        timestamp,
        method,
        src,
        dst,
        (long long)result->bytes_copied,
        result->elapsed_sec,
        result->throughput_mbps,
        result->status == SC_OK ? "OK" : sc_strerror(result->status));
 
    fclose(log);
}
 
/* ─────────────────────────────────────────────────────────
 * log_check() — verifica si un archivo ya tuvo backup
 *
 * Lee output/backup.log línea por línea buscando src_path.
 * Si lo encuentra imprime cada entrada con su historial.
 *
 * Formato de búsqueda — cada línea del log tiene esta forma:
 *   [2024-11-15 10:32:45] syscall | /etc/hosts → dst | ...
 *   El origen está entre "| " y " →"
 *
 * @param src_path  ruta del archivo a consultar
 * @return          1 si tiene al menos un backup, 0 si no
 * ───────────────────────────────────────────────────────── */
int
log_check(const char *src_path)
{
    FILE *log = fopen(LOG_PATH, "r");
    if (!log) {
        /* El log no existe todavía — nunca se hizo un backup */
        printf("[INFO] No existe historial de backups todavía.\n");
        printf("       Ejecuta './backup -b %s <destino>' primero.\n",
               src_path);
        return 0;
    }
 
    char    line[512];
    int     found    = 0;
    int     count    = 0;
 
    printf("\n  Historial de backups para: %s\n", src_path);
    printf("  %-19s  %-7s  %-30s  %-8s  %s\n",
           "Fecha/hora", "Método", "Destino", "Bytes", "Estado");
    printf("  %-19s  %-7s  %-30s  %-8s  %s\n",
           "───────────────────", "───────",
           "──────────────────────────────", "────────", "──────");
 
    /*
     * Leer el log línea por línea.
     * strstr() busca src_path como substring de cada línea.
     * Si lo encuentra, parsea y muestra los campos relevantes.
     */
    while (fgets(line, sizeof(line), log)) {
 
        /* Verificar si esta línea contiene el archivo buscado */
        if (strstr(line, src_path) == NULL)
            continue;
 
        found = 1;
        count++;
 
        /*
         * Parsear la línea para extraer campos.
         * Formato: [timestamp] metodo | src → dst | bytes bytes | tiempo | throughput | estado
         *
         * Usamos sscanf con %*s para saltar campos que no necesitamos.
         */
        char timestamp[32]  = "";
        char method[16]     = "";
        char dst[256]       = "";
        long long bytes     = 0;
        char estado[32]     = "";
 
        /* Extraer timestamp — está entre [ y ] */
        char *ts_start = strchr(line, '[');
        char *ts_end   = strchr(line, ']');
        if (ts_start && ts_end && ts_end > ts_start) {
            int len = (int)(ts_end - ts_start - 1);
            if (len > 0 && len < (int)sizeof(timestamp)) {
                strncpy(timestamp, ts_start + 1, (size_t)len);
                timestamp[len] = '\0';
            }
        }
 
        /* Extraer método — primer token después de "] " */
        char *after_bracket = ts_end ? ts_end + 2 : line;
        sscanf(after_bracket, "%15s", method);
 
        /* Extraer destino — está entre "→ " y " |" */
        char *arrow = strstr(line, "→ ");
        if (arrow) {
            char *pipe = strstr(arrow, " |");
            if (pipe) {
                int len = (int)(pipe - arrow - 2);
                if (len > 0 && len < (int)sizeof(dst)) {
                    strncpy(dst, arrow + 2, (size_t)len);
                    dst[len] = '\0';
                }
            }
        }
 
        /* Extraer bytes */
        char *bytes_ptr = strstr(line, "| ");
        if (bytes_ptr) bytes_ptr = strstr(bytes_ptr + 2, "| ");
        if (bytes_ptr) sscanf(bytes_ptr + 2, "%lld", &bytes);
 
        /* Extraer estado — último campo de la línea */
        char *last_pipe = strrchr(line, '|');
        if (last_pipe) {
            sscanf(last_pipe + 2, "%31s", estado);
            /* quitar \n si lo hay */
            char *nl = strchr(estado, '\n');
            if (nl) *nl = '\0';
        }
 
        printf("  %-19s  %-7s  %-30s  %-8lld  %s\n",
               timestamp, method, dst, bytes, estado);
    }
 
    fclose(log);
 
    if (!found) {
        printf("  (sin registros para este archivo)\n");
        printf("\n  Nunca se hizo backup de '%s'\n\n", src_path);
        return 0;
    }
 
    printf("\n  Total: %d backup(s) encontrado(s) para '%s'\n\n",
           count, src_path);
    return 1;
}
 

/* sys_smart_copy() — pseudofunción de sistema*/

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

    /* tiempo */
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
 
    /* tiempo
    * CLOCK_MONOTONIC = reloj que nunca retrocede,
     * aunque el usuario cambie la hora del sistema.
     */
    clock_gettime(CLOCK_MONOTONIC, &t_start);
 
    
    /*
     * El externo itera sobre los chunks del archivo.
     * El interno maneja short-writes — write() puede
     * escribir MENOS bytes de los pedidos sin ser error.
     * Sin el bucle interno, esos bytes se perderían.
     *
     * Cada llamada a read() y write() es un CONTEXT SWITCH:
     * tu programa → kernel → tu programa
     * ───────────────────────────────────────────────────── */
    while ((bytes_read = read(src_fd, buffer, PAGE_SIZE)) > 0) {
 
        /*
         * read() retornó bytes_read bytes leídos.
         * Contamos la operación de lectura.
         */
        read_ops++;
 
        /*
         * ptr apunta al inicio del buffer.
         * remaining es cuánto falta escribir.
         * Ambos se actualizan en cada iteración del while interno.
         */
        char    *ptr       = buffer;
        ssize_t  remaining = bytes_read;
 
        /* ── Bucle interno: short-write handler ─────────
         *
         * Ejemplo de short-write:
         *   bytes_read = 4096
         *   write() escribe solo 2000 → n_written = 2000
         *   ptr += 2000    (avanza el puntero)
         *   remaining = 2096 (queda esto por escribir)
         *   write() escribe 2096 → n_written = 2096
         *   remaining = 0  → sale del while
         *
         * Sin este bucle, los 2096 bytes se perderían.
         * ─────────────────────────────────────────────── */
        while (remaining > 0) {
 
            ssize_t n_written = write(dst_fd, ptr,
                                      (size_t)remaining);
 
            if (n_written < 0) {
 
                if (errno == EINTR) {
                    /*
                     * EINTR: una señal del sistema operativo
                     * interrumpió write() a mitad.
                     * No es un error real — reintentar.
                     */
                    continue;
                }
 
                if (errno == ENOSPC) {
                    /*
                     * ENOSPC: el disco está lleno.
                     * No hay forma de recuperarse — salir.
                     */
                    fprintf(stderr,
                        "[ERROR] Disco lleno escribiendo '%s'\n",
                        dst_path);
                    syslog(LOG_ERR,
                        "Disco lleno: %s", dst_path);
                    rc = SC_ERR_NOSPC;
                } else {
                    fprintf(stderr,
                        "[ERROR] Error de escritura: %s\n",
                        strerror(errno));
                    rc = SC_ERR_WRITE;
                }
                goto cleanup;
            }
 
            /* Avanzar puntero y reducir lo que falta */
            ptr          += n_written;
            remaining    -= n_written;
            bytes_copied += n_written;
            write_ops++;
        }
    }
 
    /*
     * El while externo termina cuando read() retorna 0 (EOF)
     * o negativo (error de lectura).
     * Verificamos cuál fue el caso.
     */
    if (bytes_read < 0) {
        fprintf(stderr,
            "[ERROR] Error de lectura en '%s': %s\n",
            src_path, strerror(errno));
        syslog(LOG_ERR, "Error de lectura: %s", src_path);
        rc = SC_ERR_READ;
        goto cleanup;
    }
 
    syslog(LOG_INFO, "Copia exitosa: %lld bytes",
           (long long)bytes_copied);

/* Esto garantiza que SIEMPRE cerramos los descriptores,
 * aunque haya habido un error a mitad del proceso.
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
        result->throughput_mbps = (result->elapsed_sec > 0.0)
            ? (double)bytes_copied
              / result->elapsed_sec
              / (1024.0 * 1024.0)
            : 0.0;
    }

    if (result)
        log_write("syscall", src_path, dst_path, result);

    return rc;
}


/* ─────────────────────────────────────────────────────────
 * sc_strerror() — convierte código SC_ERR_* a texto
 *
 * Igual que strerror() de la libc pero para nuestros
 * códigos propios. La usa syslog y el benchmark.
 *
 * Retorna un puntero a string estático — no necesita
 * free() porque vive en el segmento de datos del programa.
 * ───────────────────────────────────────────────────────── */
const char *
sc_strerror(int err_code)
{
    switch (err_code) {
        case SC_OK:           return "Exito";
        case SC_ERR_OPEN_SRC: return "No se pudo abrir el origen";
        case SC_ERR_OPEN_DST: return "No se pudo crear el destino";
        case SC_ERR_READ:     return "Error de lectura";
        case SC_ERR_WRITE:    return "Error de escritura";
        case SC_ERR_PERM:     return "Permiso denegado";
        case SC_ERR_NOENT:    return "Archivo no existe";
        case SC_ERR_NOSPC:    return "Disco lleno";
        case SC_ERR_STAT:     return "Error al leer metadatos";
        default:              return "Error desconocido";
    }
}
 
/* ─────────────────────────────────────────────────────────
 * sc_print_result() — imprime resumen de una sc_result_t
 *
 * La usa run_backup() en main.c para mostrar los
 * resultados de ambos métodos en formato de tabla.
 *
 * @param label  nombre del método ("sys_smart_copy", etc.)
 * @param result puntero a la estructura con estadísticas
 * ───────────────────────────────────────────────────────── */
void
sc_print_result(const char *label, const sc_result_t *result)
{
    if (!result) return;
 
    printf("  %-28s %8.6f s   %8.2f MB/s   %u ops\n",
           label,
           result->elapsed_sec,
           result->throughput_mbps,
           result->read_ops + result->write_ops);
}
 