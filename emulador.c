
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINEAS 250
#define MAX_TEXTO 160
#define MAX_PASOS 10000

// Estructura que representa la CPU
typedef struct {
    uint32_t r[4];           // EAX, EBX, ECX, EDX
    bool ZF, SF, CF, OF;      // Banderas
} CPU;

// Estructura para guardar las etiquetas
typedef struct {
    char nombre[40];
    int destino;
} Etiqueta;

// Variables globales
static const char *registros[] = {"EAX", "EBX", "ECX", "EDX"};
static CPU cpu = {0};
static char lineas[MAX_LINEAS][MAX_TEXTO];
static Etiqueta etiquetas[MAX_LINEAS];
static int total_lineas = 0;
static int total_etiquetas = 0;

// Eliminar espacios innecesarios
static void limpiar(char *s) {
    char *p = s;

    while (isspace((unsigned char)*p))
        p++;

    if (p != s)
        memmove(s, p, strlen(p) + 1);

    size_t n = strlen(s);

    while (n && isspace((unsigned char)s[n - 1]))
        s[--n] = '\0';
}

// Normalizar las instrucciones
static void normalizar(char *s) {
    char *comentario = strchr(s, ';');

    if (comentario)
        *comentario = '\0';

    limpiar(s);

    for (char *p = s; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
}

// Identificar registros
static int indice_registro(const char *nombre) {
    for (int i = 0; i < 4; ++i) {
        if (strcmp(nombre, registros[i]) == 0)
            return i;
    }

    return -1;
}

// Obtener el valor de un registro o numero
static bool obtener_valor(const char *texto, uint32_t *valor) {
    int reg = indice_registro(texto);

    if (reg >= 0) {
        *valor = cpu.r[reg];
        return true;
    }

    if (!*texto)
        return false;

    errno = 0;

    char *fin;
    long long numero = strtoll(texto, &fin, 0);

    if (errno || *fin || fin == texto ||
        numero < INT32_MIN ||
        numero > (long long)UINT32_MAX)
        return false;

    *valor = (uint32_t)numero;

    return true;
}

// Actualizar las banderas basicas
static void banderas_basicas(uint32_t resultado) {
    cpu.ZF = resultado == 0;
    cpu.SF = (resultado >> 31) != 0;
}

// Operacion ADD
static void sumar(uint32_t a, uint32_t b,
                  uint32_t *resultado) {

    uint64_t operacion = (uint64_t)a + b;

    *resultado = (uint32_t)operacion;

    banderas_basicas(*resultado);

    cpu.CF = operacion > UINT32_MAX;

    cpu.OF = ((~(a ^ b) & (a ^ *resultado)) >> 31) != 0;
}

// Operacion SUB y CMP
static void restar(uint32_t a, uint32_t b,
                   uint32_t *resultado) {

    *resultado = a - b;

    banderas_basicas(*resultado);

    cpu.CF = a < b;

    cpu.OF = (((a ^ b) & (a ^ *resultado)) >> 31) != 0;
}

// Buscar una etiqueta
static int buscar_etiqueta(const char *nombre) {
    for (int i = 0; i < total_etiquetas; ++i) {
        if (strcmp(nombre, etiquetas[i].nombre) == 0)
            return etiquetas[i].destino;
    }

    return -1;
}

// Mostrar los registros y las banderas
static void imprimir_estado(void) {
    printf(
        "  EAX=%lld  EBX=%lld  ECX=%lld  EDX=%lld"
        "  | ZF=%d SF=%d CF=%d OF=%d\n",

        (long long)(cpu.r[0] <= INT32_MAX ?
            (int64_t)cpu.r[0] :
            (int64_t)cpu.r[0] - 4294967296LL),

        (long long)(cpu.r[1] <= INT32_MAX ?
            (int64_t)cpu.r[1] :
            (int64_t)cpu.r[1] - 4294967296LL),

        (long long)(cpu.r[2] <= INT32_MAX ?
            (int64_t)cpu.r[2] :
            (int64_t)cpu.r[2] - 4294967296LL),

        (long long)(cpu.r[3] <= INT32_MAX ?
            (int64_t)cpu.r[3] :
            (int64_t)cpu.r[3] - 4294967296LL),

        cpu.ZF, cpu.SF, cpu.CF, cpu.OF
    );
}

// Leer el programa ensamblador
static bool cargar_programa(void) {
    char entrada[MAX_TEXTO];

    puts("Escribi el programa (una instruccion por linea).");
    puts("Para terminar de introducirlo, escribi FIN.\n");

    while (total_lineas < MAX_LINEAS &&
           fgets(entrada, sizeof entrada, stdin)) {

        if (!strchr(entrada, '\n') && !feof(stdin)) {
            int c;

            while ((c = getchar()) != '\n' && c != EOF) {}
        }

        normalizar(entrada);

        if (strcmp(entrada, "FIN") == 0)
            break;

        if (!*entrada)
            continue;

        size_t n = strlen(entrada);

        if (entrada[n - 1] == ':') {
            entrada[n - 1] = '\0';

            limpiar(entrada);

            if (!*entrada ||
                strlen(entrada) >= sizeof etiquetas[0].nombre ||
                buscar_etiqueta(entrada) >= 0) {

                fprintf(stderr,
                        "Etiqueta invalida o repetida: %s\n",
                        entrada);
                return false;
            }

            strcpy(etiquetas[total_etiquetas].nombre, entrada);

            etiquetas[total_etiquetas].destino = total_lineas;

            total_etiquetas++;

        } else {
            strcpy(lineas[total_lineas++], entrada);
        }
    }

    return true;
}

// Evaluar las condiciones de salto
static bool condicion_salto(const char *op) {
    if (strcmp(op, "JMP") == 0)
        return true;

    if (strcmp(op, "JC") == 0 ||
        strcmp(op, "JB") == 0)
        return cpu.CF;

    if (strcmp(op, "JNC") == 0)
        return !cpu.CF;

    if (strcmp(op, "JE") == 0 ||
        strcmp(op, "JZ") == 0)
        return cpu.ZF;

    if (strcmp(op, "JNE") == 0 ||
        strcmp(op, "JNZ") == 0)
        return !cpu.ZF;

    if (strcmp(op, "JG") == 0)
        return !cpu.ZF && cpu.SF == cpu.OF;

    if (strcmp(op, "JGE") == 0)
        return cpu.SF == cpu.OF;

    if (strcmp(op, "JL") == 0)
        return cpu.SF != cpu.OF;

    if (strcmp(op, "JLE") == 0)
        return cpu.ZF || cpu.SF != cpu.OF;

    if (strcmp(op, "JA") == 0)
        return !cpu.CF && !cpu.ZF;

    return false;
}

// Identificar si una instruccion es un salto
static bool es_salto(const char *op) {
    const char *saltos[] = {
        "JMP", "JC", "JNC", "JB",
        "JE", "JZ", "JNE", "JNZ",
        "JG", "JGE", "JL", "JLE", "JA"
    };

    for (size_t i = 0;
         i < sizeof saltos / sizeof saltos[0];
         ++i) {

        if (strcmp(op, saltos[i]) == 0)
            return true;
    }

    return false;
}

// Funcion principal del emulador
static bool ejecutar(void) {
    int pc = 0;
    int pasos = 0;

    while (pc < total_lineas) {

        if (++pasos > MAX_PASOS) {
            fputs(
                "Error: posible ciclo infinito.\n",
                stderr
            );
            return false;
        }

        char texto[MAX_TEXTO];
        char op[20] = "";
        char a[40] = "";
        char b[40] = "";
        char extra[2] = "";

        strcpy(texto, lineas[pc]);

        // Reemplazar las comas por espacios
        for (char *p = texto; *p; ++p) {
            if (*p == ',')
                *p = ' ';
        }

        // Separar instruccion y operandos
        int argumentos = sscanf(
            texto,
            "%19s %39s %39s %1s",
            op, a, b, extra
        );

        if (argumentos < 1 || argumentos > 3) {
            fprintf(stderr,
                    "Error de sintaxis en linea %d.\n",
                    pc + 1);
            return false;
        }

        printf("Paso %d: %s\n", pasos, lineas[pc]);

        // Finalizar ejecucion
        if (strcmp(op, "RET") == 0 ||
            strcmp(op, "HLT") == 0) {

            if (argumentos != 1)
                return false;

            imprimir_estado();
            return true;
        }

        // Procesar saltos
        if (es_salto(op)) {

            if (argumentos != 2)
                return false;

            if (condicion_salto(op)) {

                int destino = buscar_etiqueta(a);

                if (destino < 0) {
                    fprintf(stderr,
                            "No existe la etiqueta %s.\n",
                            a);
                    return false;
                }

                pc = destino;

            } else {
                pc++;
            }

        } else {

            // Procesar instrucciones normales
            int destino = indice_registro(a);

            uint32_t valor = 0;
            uint32_t resultado = 0;

            if (strcmp(op, "MOV") == 0 ||
                strcmp(op, "ADD") == 0 ||
                strcmp(op, "SUB") == 0 ||
                strcmp(op, "CMP") == 0) {

                if (argumentos != 3 ||
                    destino < 0 ||
                    !obtener_valor(b, &valor)) {

                    fprintf(stderr,
                            "Operandos invalidos en: %s\n",
                            lineas[pc]);
                    return false;
                }

                // MOV
                if (strcmp(op, "MOV") == 0) {
                    cpu.r[destino] = valor;
                }

                // ADD
                else if (strcmp(op, "ADD") == 0) {

                    sumar(cpu.r[destino],
                          valor,
                          &resultado);

                    cpu.r[destino] = resultado;
                }

                // SUB y CMP
                else {

                    restar(cpu.r[destino],
                           valor,
                           &resultado);

                    // SUB guarda la resta
                    if (strcmp(op, "SUB") == 0)
                        cpu.r[destino] = resultado;

                    // CMP solo actualiza banderas
                }

            }

            // INC y DEC
            else if (strcmp(op, "INC") == 0 ||
                     strcmp(op, "DEC") == 0) {

                if (argumentos != 2 || destino < 0)
                    return false;

                bool cf_anterior = cpu.CF;

                if (strcmp(op, "INC") == 0) {

                    sumar(cpu.r[destino],
                          1,
                          &resultado);

                } else {

                    restar(cpu.r[destino],
                           1,
                           &resultado);
                }

                cpu.r[destino] = resultado;

                // INC y DEC conservan CF
                cpu.CF = cf_anterior;

            } else {

                fprintf(stderr,
                        "Instruccion desconocida: %s\n",
                        op);
                return false;
            }

            pc++;
        }

        imprimir_estado();
    }

    return true;
}

// Inicio del programa
int main(void) {

    // Recibir instrucciones
    if (!cargar_programa())
        return EXIT_FAILURE;

    // Ejecutar instrucciones
    if (!ejecutar())
        return EXIT_FAILURE;

    // Mostrar resultados
    puts("\nREGISTROS FINALES:");

    imprimir_estado();

    return EXIT_SUCCESS;
}