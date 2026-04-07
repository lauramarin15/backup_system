# Sistema de Backup en C — Syscalls vs stdio

Sistema de respaldo de archivos implementado en C que compara el rendimiento de dos métodos de copia: syscalls POSIX directas (`read`/`write`) versus la biblioteca estándar C (`fread`/`fwrite`).

---

## Estructura del proyecto

```
backup/
├── smart_copy.h        # Interfaz pública: tipos, constantes, prototipos
├── backup_engine.c     # Motor de copia con syscalls POSIX directas
├── main.c              # Benchmark, stdio_copy y punto de entrada
├── backup              # Ejecutable compilado
└── output/             # Archivos generados (se crea automáticamente)
    ├── backup.log      # Log de todas las operaciones
    ├── *.smart         # Copias hechas con sys_smart_copy
    └── *.stdio         # Copias hechas con stdio_copy
```

---

## Compilación

```bash
gcc -Wall -O2 -o backup main.c backup_engine.c
```

---

## Uso

```bash
./backup -h                        # ayuda
./backup -b <origen> <destino>     # respaldar un archivo
./backup --check <archivo>         # ver historial de backups
./backup --benchmark               # tabla comparativa 1KB · 1MB · 1GB
```

---

## Ejemplos

### Respaldar un archivo

```bash
./backup -b test.txt test_backup.txt
```

Genera dos archivos en `output/`:
- `output/test_backup.txt.smart` — copia por syscalls
- `output/test_backup.txt.stdio` — copia por stdio

### Ver historial de backups

```bash
./backup --check test_backup.txt
```

```
  Historial de backups para: test.txt
  Fecha/hora           Método   Destino                Bytes     Estado
  ───────────────────  ───────  ─────────────────────  ────────  ──────
  2026-04-06 07:45:25  syscall  � output/test_backup.txt.smart  12        OK
  2026-04-06 07:45:25  stdio    � output/test_backup.txt.stdio  12        OK
  2026-04-06 19:25:16  syscall  � output/test_backup.txt.smart  12        OK
  2026-04-06 19:25:16  stdio    � output/test_backup.txt.stdio  12        OK

  Total: 4 backup(s) encontrado(s) para 'test_backup.txt'

```

### Warning de archivo no modificado

Si intentas hacer backup de un archivo que no fue modificado desde el último backup:

```
[WARNING] El archivo 'test.txt' no fue modificado
          desde el último backup registrado.
          ¿Deseas hacer el backup de todos modos? [s/N]:
```

### Correr el benchmark

```bash
./backup --benchmark
```

```
| Tamaño | sys_smart_copy | Velocidad | stdio_copy | Velocidad | Ganador |
|--------|---------------|-----------|------------|-----------|---------|
| 1 KB   | 0.000075 s    | 13.02 MB/s | 0.000147 s | 6.64 MB/s | syscall ✓ |
| 1 MB   | 0.002489 s    | 401.77 MB/s | 0.002370 s | 421.94 MB/s | fread ✓ |
| 1 GB   | 1.488406 s    | 687.98 MB/s | 1.424912 s | 718.64 MB/s | fread ✓ |
 
```

---
### Análisis
 
**1 KB — syscall gana:** con archivos tan pequeños el overhead del buffer interno de `fread()` supera su ventaja. `sys_smart_copy` va directo al kernel y termina antes.
 
**1 MB — fread gana por poco:** zona de equilibrio entre los dos métodos. El caché del sistema operativo nivela la diferencia.
 
**1 GB — fread gana:** en archivos grandes el buffer interno de `fread()` ayuda a reducir las interrupciones al kernel. En un disco duro convencional la ventaja de syscalls directas sería más visible.
 
> Los resultados varían entre ejecuciones por el caché del sistema operativo. Para resultados más estables se recomienda correr el benchmark varias veces y promediar.

---

## ¿Por qué dos métodos?

```
sys_smart_copy:   disco → buffer[4096] → destino                    (1 copia)
stdio_copy:       disco → buf_libc(~8KB) → buffer[4096] → destino   (2 copias)

```

| Aspecto | sys_smart_copy (syscalls) | stdio_copy (fread/fwrite) |
|---------|--------------------------|--------------------------|
| Apertura | `open()` → retorna `int fd` | `fopen()` → retorna `FILE*` |
| Lectura | `read()` → directo al kernel | `fread()` → buffer interno libc |
| Escritura | `write()` → directo al kernel | `fwrite()` → buffer interno libc |
| Copias en RAM | 1 (disco → buffer) | 2 (disco → buf_libc → buffer) |
| Context switches (1GB) | ~16.384 | ~131.072 |
| Gana en | archivos grandes | archivos pequeños |

### Por qué fread gana en archivos pequeños

`fread()` tiene un buffer interno de ~8KB gestionado por la libc. Si el archivo cabe en ese buffer, el kernel se invoca **una sola vez** independientemente del tamaño del archivo. `read()` hace un context switch por cada llamada de 4KB.

### Por qué syscalls ganan en archivos grandes

Cada `fread()` hace **dos copias en RAM**: disco → buffer libc → tu buffer. `read()` hace una sola: disco → tu buffer. En un archivo de 1GB esa copia extra se repite 16.000 veces y acumula tiempo significativo.

---

## Archivos generados

### `output/backup.log`

Registro de todas las operaciones de copia:

```
[2026-04-06 19:17:05] syscall | bk_src.bin → bk_dst_smart.bin | 1024 bytes | 0.000075s | 13.02 MB/s | OK
[2026-04-06 19:17:05] stdio   | bk_src.bin → bk_dst_stdio.bin | 1024 bytes | 0.000147s |  6.64 MB/s | OK
```

---

## Manejo de errores

El sistema detecta y reporta tres tipos de error con mensajes específicos:

| Error | Código | Mensaje |
|-------|--------|---------|
| Archivo no existe | `SC_ERR_NOENT` | `[ERROR] El archivo no existe: 'ruta'` |
| Sin permisos | `SC_ERR_PERM` | `[ERROR] Permiso denegado para leer: 'ruta'` |
| Disco lleno | `SC_ERR_NOSPC` | `[ERROR] Disco lleno escribiendo 'ruta'` |

---

## Conceptos clave

**Context switch** — cada llamada a `read()` o `write()` provoca un cambio de modo usuario → kernel → usuario. Minimizar estas llamadas usando buffers grandes es la clave del rendimiento.

**PAGE_SIZE (4096 bytes)** — el buffer se alinea con el tamaño de página de la MMU del procesador, permitiendo transferencias eficientes con el hardware.

**Short-write** — `write()` puede escribir menos bytes de los pedidos sin ser error. El motor maneja esto con un bucle interno que reintenta hasta escribir todo.