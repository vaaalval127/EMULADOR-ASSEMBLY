
/*
 * EMULADOR DIDACTICO DE ENSAMBLADOR x86
 * Registros de 32 bits
 *
 * Registros: EAX, EBX, ECX, EDX, EDI, ESI
 * Aritmetica: MOV, ADD, SUB, INC, DEC, CMP, NEG
 * Logica: AND, OR, XOR, NOT, TEST
 * Saltos: JMP, JC, JNC, JB, JAE, JA, JBE,
 *         JE, JZ, JNE, JNZ, JG, JGE, JL, JLE,
 *         JS, JNS, JO, JNO
 *
 * Finalizar ejecucion: RET o HLT
 * Finalizar entrada: FIN
 */

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
#define NUM_REGISTROS 6

// 1. Estado de la CPU simulada

typedef struct {
    uint32_t r[NUM_REGISTROS];
    bool ZF, SF, CF, OF;
} CPU;

typedef struct {
    char nombre[40];
    int destino;
} Etiqueta;

static const char *registros[NUM_REGISTROS] = {
    "EAX", "EBX", "ECX", "EDX", "EDI", "ESI"
};

static CPU cpu = {0};
static char lineas[MAX_LINEAS][MAX_TEXTO];
static Etiqueta etiquetas[MAX_LINEAS];

static int total_lineas = 0;
static int total_etiquetas = 0;


// 2. Preparar las instrucciones

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

static void normalizar(char *s) {
    char *comentario = strchr(s, ';');

    if (comentario)
        *comentario = '\0';

    limpiar(s);

    for (char *p = s; *p; ++p)
        *p = (char)toupper((unsigned char)*p);
}


// 3. Identificar registros y obtener operandos

static int indice_registro(const char *nombre) {
    for (int i = 0; i < NUM_REGISTROS; i++) {
        if (strcmp(nombre, registros[i]) == 0)
            return i;
    }

    return -1;
}

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

    if (errno || fin == texto || *fin ||
        numero < INT32_MIN ||
        numero > (long long)UINT32_MAX) {
        return false;
    }

    *valor = (uint32_t)numero;

    return true;
}


// 4. Banderas y funciones aritmeticas

static void banderas_basicas(uint32_t resultado) {
    cpu.ZF = (resultado == 0);
    cpu.SF = ((resultado >> 31) != 0);
}

static void sumar(uint32_t a, uint32_t b,
                  uint32_t *resultado) {

    uint64_t operacion = (uint64_t)a + b;

    *resultado = (uint32_t)operacion;

    banderas_basicas(*resultado);

    cpu.CF = (operacion > UINT32_MAX);

    cpu.OF = ((~(a ^ b) & (a ^ *resultado))
              >> 31) != 0;
}

static void restar(uint32_t a, uint32_t b,
                   uint32_t *resultado) {

    *resultado = a - b;

    banderas_basicas(*resultado);

    cpu.CF = (a < b);

    cpu.OF = (((a ^ b) & (a ^ *resultado))
              >> 31) != 0;
}


// 5. Banderas para operaciones logicas

static void banderas_logicas(uint32_t resultado) {
    banderas_basicas(resultado);

    cpu.CF = false;
    cpu.OF = false;
}


// 6. Buscar etiquetas

static int buscar_etiqueta(const char *nombre) {
    for (int i = 0; i < total_etiquetas; i++) {
        if (strcmp(nombre, etiquetas[i].nombre) == 0)
            return etiquetas[i].destino;
    }

    return -1;
}


// 7. Cargar el programa ensamblador

static bool cargar_programa(void) {
    char entrada[MAX_TEXTO];

    puts("Escribi el programa ensamblador, "
         "una instruccion por linea.");

    puts("Escribi FIN para comenzar a ejecutarlo.\n");

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
                strlen(entrada) >=
                sizeof etiquetas[0].nombre ||
                buscar_etiqueta(entrada) >= 0) {

                fprintf(stderr,
                        "Etiqueta invalida o repetida: %s\n",
                        entrada);
                return false;
            }

            strcpy(
                etiquetas[total_etiquetas].nombre,
                entrada
            );

            etiquetas[total_etiquetas].destino =
                total_lineas;

            total_etiquetas++;

        } else {
            strcpy(lineas[total_lineas], entrada);
            total_lineas++;
        }
    }

    return true;
}


// 8. Reconocer instrucciones de salto

static bool es_salto(const char *op) {
    static const char *saltos[] = {
        "JMP", "JC", "JNC", "JB", "JAE",
        "JA", "JBE", "JE", "JZ", "JNE",
        "JNZ", "JG", "JGE", "JL", "JLE",
        "JS", "JNS", "JO", "JNO"
    };

    for (size_t i = 0;
         i < sizeof(saltos) / sizeof(saltos[0]);
         i++) {

        if (strcmp(op, saltos[i]) == 0)
            return true;
    }

    return false;
}


// 9. Evaluar condiciones de salto

static bool condicion_salto(const char *op) {

    if (strcmp(op, "JMP") == 0)
        return true;

    if (strcmp(op, "JC") == 0 ||
        strcmp(op, "JB") == 0)
        return cpu.CF;

    if (strcmp(op, "JNC") == 0 ||
        strcmp(op, "JAE") == 0)
        return !cpu.CF;

    if (strcmp(op, "JA") == 0)
        return !cpu.CF && !cpu.ZF;

    if (strcmp(op, "JBE") == 0)
        return cpu.CF || cpu.ZF;

    if (strcmp(op, "JE") == 0 ||
        strcmp(op, "JZ") == 0)
        return cpu.ZF;

    if (strcmp(op, "JNE") == 0 ||
        strcmp(op, "JNZ") == 0)
        return !cpu.ZF;

    if (strcmp(op, "JG") == 0)
        return !cpu.ZF && (cpu.SF == cpu.OF);

    if (strcmp(op, "JGE") == 0)
        return cpu.SF == cpu.OF;

    if (strcmp(op, "JL") == 0)
        return cpu.SF != cpu.OF;

    if (strcmp(op, "JLE") == 0)
        return cpu.ZF || (cpu.SF != cpu.OF);

    if (strcmp(op, "JS") == 0)
        return cpu.SF;

    if (strcmp(op, "JNS") == 0)
        return !cpu.SF;

    if (strcmp(op, "JO") == 0)
        return cpu.OF;

    if (strcmp(op, "JNO") == 0)
        return !cpu.OF;

    return false;
}


// 10. Mostrar los registros y las banderas

static int64_t valor_con_signo(uint32_t valor) {
    return valor <= INT32_MAX
        ? (int64_t)valor
        : (int64_t)valor - 4294967296LL;
}

static void imprimir_estado(void) {
    printf("  ");

    for (int i = 0; i < NUM_REGISTROS; i++) {
        printf(
            "%s=%lld  ",
            registros[i],
            (long long)valor_con_signo(cpu.r[i])
        );
    }

    printf(
        "| ZF=%d SF=%d CF=%d OF=%d\n",
        cpu.ZF, cpu.SF, cpu.CF, cpu.OF
    );
}


// 11. Interpretar y ejecutar instrucciones

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

        // Reemplazar comas por espacios
        for (char *p = texto; *p; ++p) {
            if (*p == ',')
                *p = ' ';
        }

        // Separar la instruccion y operandos
        int argumentos = sscanf(
            texto,
            "%19s %39s %39s %1s",
            op, a, b, extra
        );

        if (argumentos < 1 || argumentos > 3) {
            fprintf(
                stderr,
                "Error de sintaxis en: %s\n",
                lineas[pc]
            );
            return false;
        }

        printf(
            "Paso %d: %s\n",
            pasos,
            lineas[pc]
        );

        // Finalizar la ejecucion
        if (strcmp(op, "RET") == 0 ||
            strcmp(op, "HLT") == 0) {

            if (argumentos != 1)
                return false;

            imprimir_estado();
            return true;
        }

        // Instruccion NOP
        if (strcmp(op, "NOP") == 0) {

            if (argumentos != 1)
                return false;

            pc++;
        }

        // Procesar saltos
        else if (es_salto(op)) {

            if (argumentos != 2) {
                fprintf(
                    stderr,
                    "Salto invalido: %s\n",
                    lineas[pc]
                );
                return false;
            }

            if (condicion_salto(op)) {

                int destino = buscar_etiqueta(a);

                if (destino < 0) {
                    fprintf(
                        stderr,
                        "No existe la etiqueta: %s\n",
                        a
                    );
                    return false;
                }

                pc = destino;

            } else {
                pc++;
            }
        }

        // Procesar operaciones normales
        else {

            int destino = indice_registro(a);

            uint32_t valor = 0;
            uint32_t resultado = 0;

            if (destino < 0) {
                fprintf(
                    stderr,
                    "Registro invalido en: %s\n",
                    lineas[pc]
                );
                return false;
            }

            // Operaciones con dos operandos
            if (strcmp(op, "MOV") == 0 ||
                strcmp(op, "ADD") == 0 ||
                strcmp(op, "SUB") == 0 ||
                strcmp(op, "CMP") == 0 ||
                strcmp(op, "AND") == 0 ||
                strcmp(op, "OR") == 0 ||
                strcmp(op, "XOR") == 0 ||
                strcmp(op, "TEST") == 0) {

                if (argumentos != 3 ||
                    !obtener_valor(b, &valor)) {

                    fprintf(
                        stderr,
                        "Operandos invalidos en: %s\n",
                        lineas[pc]
                    );
                    return false;
                }

                // MOV: mover un valor
                if (strcmp(op, "MOV") == 0) {

                    cpu.r[destino] = valor;
                }

                // ADD: sumar
                else if (strcmp(op, "ADD") == 0) {

                    sumar(
                        cpu.r[destino],
                        valor,
                        &resultado
                    );

                    cpu.r[destino] = resultado;
                }

                // SUB y CMP: restar o comparar
                else if (strcmp(op, "SUB") == 0 ||
                         strcmp(op, "CMP") == 0) {

                    restar(
                        cpu.r[destino],
                        valor,
                        &resultado
                    );

                    // CMP no modifica el registro
                    if (strcmp(op, "SUB") == 0)
                        cpu.r[destino] = resultado;
                }

                // Operaciones logicas
                else {

                    // AND y TEST
                    if (strcmp(op, "AND") == 0 ||
                        strcmp(op, "TEST") == 0) {

                        resultado =
                            cpu.r[destino] & valor;
                    }

                    // OR
                    else if (strcmp(op, "OR") == 0) {

                        resultado =
                            cpu.r[destino] | valor;
                    }

                    // XOR
                    else {

                        resultado =
                            cpu.r[destino] ^ valor;
                    }

                    banderas_logicas(resultado);

                    // TEST no modifica el registro
                    if (strcmp(op, "TEST") != 0)
                        cpu.r[destino] = resultado;
                }
            }

            // Operaciones con un operando
            else if (strcmp(op, "INC") == 0 ||
                     strcmp(op, "DEC") == 0 ||
                     strcmp(op, "NEG") == 0 ||
                     strcmp(op, "NOT") == 0) {

                if (argumentos != 2) {
                    fprintf(
                        stderr,
                        "Operandos invalidos en: %s\n",
                        lineas[pc]
                    );
                    return false;
                }

                // NOT: invertir los bits
                if (strcmp(op, "NOT") == 0) {

                    cpu.r[destino] =
                        ~cpu.r[destino];
                }

                // NEG: cambiar el signo
                else if (strcmp(op, "NEG") == 0) {

                    restar(
                        0,
                        cpu.r[destino],
                        &resultado
                    );

                    cpu.r[destino] = resultado;
                }

                // INC y DEC
                else {

                    bool cf_anterior = cpu.CF;

                    if (strcmp(op, "INC") == 0) {

                        sumar(
                            cpu.r[destino],
                            1,
                            &resultado
                        );

                    } else {

                        restar(
                            cpu.r[destino],
                            1,
                            &resultado
                        );
                    }

                    cpu.r[destino] = resultado;

                    // INC y DEC conservan CF
                    cpu.CF = cf_anterior;
                }
            }

            // Instruccion no reconocida
            else {

                fprintf(
                    stderr,
                    "Instruccion desconocida: %s\n",
                    op
                );
                return false;
            }

            pc++;
        }

        // Mostrar el estado despues de cada paso
        imprimir_estado();
    }

    return true;
}


// 12. Funcion principal

int main(void) {

    // Cargar el codigo ensamblador
    if (!cargar_programa())
        return EXIT_FAILURE;

    // Ejecutar las instrucciones
    if (!ejecutar())
        return EXIT_FAILURE;

    // Mostrar los resultados finales
    puts("\nREGISTROS FINALES:");

    imprimir_estado();

    return EXIT_SUCCESS;
}
