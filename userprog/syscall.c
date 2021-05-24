#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"

static void syscall_handler (struct intr_frame *);

/* 
    Lee un byte en la dirección virtual del usuario UADDR.
    UADDR debe estar por debajo de PHYS_BASE.
    Devuelve el valor del byte si tiene éxito, -1 si ocurrió una falla de segmento. 
*/
static int get_user (const uint8_t *uaddr);
/*
    Lee bytes (bytes) consecutivos, tomando como base la direccion virtual del usuario
    UADDR y lo pone en dst, devuelve el numero de bytes leidos o -1 en caso exista page fault
*/
static int get_user_bytes (void *uaddr, void *dst, size_t bytes);
/*
    Termina Pintos llamando a shutdown_power_off () (declarado en devices / shutdown.h). 
    Esto debería usarse pocas veces, porque pierde información sobre posibles situaciones de interbloqueo, etc.
*/
void sys_halt(void);
/*
    Termina el programa de usuario actual, devolviendo el estado al kernel. Si el padre del proceso 
    lo espera (ver más abajo), este es el estado que se devolverá. Convencionalmente, un estado de 0
    indica éxito y los valores distintos de cero indican errores.
*/
void sys_exit(int);
/*
    Escribe size bytes desde el búfer al archivo abierto fd.
*/
int sys_write(int fd, const void* buffer, unsigned size);
/*
    Ejecuta el ejecutable cuyo nombre se da en cmd_line, pasando los argumentos dados y
    devuelve el ID de programa (pid) del nuevo proceso, devuelve pid -1 de otro modo
*/
tid_t sys_exec(const char* cmd_line);

void
syscall_init (void) 
{
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_code;
  // validar que sea un puntero valido
  if (get_user_bytes(f->esp, &sys_code, sizeof(sys_code)) == -1) {
    sys_exit(-1);
  }

  switch (sys_code)
  {
    case SYS_HALT:
      {
        sys_halt();
        break;
      }
    case SYS_EXIT:
      {
        int status;
      
        // para leer un argumento en la documentacion
        // int fd = *((int*)f->esp + 1); <--- esto es sin validar y como parsea a int +1 es 
        // equivalente a +4 sin parsear 4 bytes

        int retorno = get_user_bytes(f->esp + 4, &status, sizeof(status));

        if (retorno == -1) {
          sys_exit(-1);
        } else {
          sys_exit(status);
        }
        break;
      }
    case SYS_WRITE:
      {
        int fd;
        const void* buffer;
        unsigned size;

        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &buffer, 4) == -1) {
          sys_exit(-1);
        }
        
        if (get_user_bytes(f->esp + 12, &size, 4) == -1) {
          sys_exit(-1);
        }

        int retorno = sys_write(fd, buffer, size);
        if(retorno == 0) {
          thread_exit();
        } else {
          f->eax = (uint32_t)retorno; // indicamos cuantos bytes se escribieron
        }
        break; 
      }
    case SYS_EXEC:
      {
        void* cmd_line;

        int retorno = get_user_bytes(f->esp + 4, &cmd_line, sizeof(cmd_line));
        if(retorno == -1){
          sys_exit(-1);
        }

        tid_t pid = sys_exec((const char*)cmd_line);
        f->eax = (uint32_t)pid;
        break;
      }
    default:
      thread_exit();
      break;
  }
}

void sys_halt(void){
  shutdown_power_off();
}

void sys_exit(int status){
  /*
    Siempre que un proceso de usuario finaliza, porque llamó a exit o por cualquier otra razón, 
    imprima el nombre del proceso y el código de salida, formateado como si estuviera impreso
    por printf ("%s: exit(%d)\n", ...);
  */
  printf("%s: exit(%d)\n", thread_current()->name, status);
  thread_exit();

}

static int get_user (const uint8_t *uaddr)
{
  // obtuvimos esta funcion de https://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/pintos/pintos_3.html#SEC44
  // Accessing User Memory
  if((void*)uaddr < PHYS_BASE){
    int result;
    asm ("movl $1f, %0; movzbl %1, %0; 1:"
        : "=&a" (result) : "m" (*uaddr));
    return result;
  } else {
    return -1;
  }  
}

static int get_user_bytes (void *uaddr, void *dst, size_t bytes){
  int32_t valor;
  size_t i; // de acuerdo al size de la estrucutra asi nos vamos moviendo en la memoria
  for( i = 0; i < bytes; i++){
    valor = get_user(uaddr + i); // usamos get user para leer el byte
    if(valor == -1){
      return -1;
    } else {
      *(char*)(dst + i) = valor & 0xff; // solo dejamos pasar el byte hacia dst
    }
  }
};
 
int sys_write(int fd, const void* buffer, unsigned size){
  // validamos que no este accesando a memoria que no debe
  if(get_user((const uint8_t*)buffer) == -1){
    thread_exit();
    return 0;
  }
  //Todos nuestros programas de prueba escriben en la consola
  if(fd == 1){
    putbuf(buffer, size);
    return size;
  } 
  return 0;
};

tid_t sys_exec(const char* cmd_line){
  
  // cmd_line es un puntero a donde esta el argumento, debemos verificar que sea
  // valida la direccion
  if(get_user((const uint8_t*)cmd_line) == -1) {
    thread_exit();
    return -1;
  }

  return process_execute(cmd_line);
}