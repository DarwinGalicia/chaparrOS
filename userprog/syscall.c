#include "userprog/syscall.h"
#include <stdio.h>
#include <syscall-nr.h>
#include "threads/interrupt.h"
#include "threads/thread.h"
#include "threads/vaddr.h"
#include "userprog/process.h"
#include "devices/shutdown.h"
#include "filesys/filesys.h"
#include "lib/kernel/list.h"

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
/*
    Crea un nuevo archivo llamado file inicialmente initial_size bytes de tamaño. 
    Devuelve verdadero si tiene éxito, falso en caso contrario. La creación de un nuevo
    archivo no lo abre: abrir el nuevo archivo es una operación separada que requeriría 
    una llamada al sistema sys_open.
*/
bool sys_create(const char *file, unsigned initial_size);
/*
    Elimina el archivo llamado file . Devuelve verdadero si tiene éxito, falso en caso 
    contrario. Un archivo puede eliminarse independientemente de si está abierto o cerrado, 
    y la eliminación de un archivo abierto no lo cierra.
*/
bool sys_remove(const char *file);
/*
    Abre el archivo llamado file. Devuelve un identificador de entero no negativo llamado
    "descriptor de archivo" (fd), o -1 si el archivo no se pudo abrir. Los descriptores de 
    archivo numerados 0 y 1 están reservados para la consola: fd 0 (STDIN_FILENO) es la entrada 
    estándar, fd 1 ( STDOUT_FILENO) es una salida estándar.
*/
int sys_open (const char *file);
/*
    Cierra el descriptor de archivo fd. Salir o terminar un proceso cierra implícitamente todos sus 
    descriptores de archivos abiertos, como si llamara a esta función para cada uno.
*/
void sys_close(int fd);
/*
    Devuelve un descriptor de archivo dado su id, del thread actual
*/
static struct descriptor* obtener_descriptor(int fd);
/*
    Devuelve el tamaño, en bytes, del archivo abierto como fd.
*/
int sys_filesize (int fd);

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

        if (get_user_bytes(f->esp + 8, &buffer, sizeof(buffer)) == -1) {
          sys_exit(-1);
        }
        
        if (get_user_bytes(f->esp + 12, &size, sizeof(size)) == -1) {
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
    case SYS_CREATE:
      {
        const char* filename;
        unsigned initial_size;
        
        if (get_user_bytes(f->esp + 4, &filename, sizeof(filename)) == -1) {
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &initial_size, sizeof(initial_size)) == -1) {
          sys_exit(-1);
        }

        bool retorno = sys_create(filename, initial_size);
        f->eax = retorno;

        break;
      }
    case SYS_REMOVE:
      {
        const char* filename;
            
        if (get_user_bytes(f->esp + 4, &filename, sizeof(filename)) == -1) {
          sys_exit(-1);
        }

        bool retorno = sys_remove(filename);
        f->eax = retorno;
        break;
      }
    case SYS_OPEN:
      {
        const char* filename;
            
        if (get_user_bytes(f->esp + 4, &filename, sizeof(filename)) == -1) {
          sys_exit(-1);
        }

        int retorno = sys_open(filename);
        f->eax = retorno;
        break;
      }
    case SYS_CLOSE:
      {
        int fd;
        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          sys_exit(-1);
        }

        sys_close(fd);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd;
        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          sys_exit(-1);
        }

        int retorno = sys_filesize(fd);
        f->eax = retorno;
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

bool sys_create(const char *file, unsigned initial_size){

  /* Para las llamadas del sistema que requieran manejo de archivos vamos a usar filesys/filesys.h */

  if(get_user((const uint8_t*)file) == -1){
    sys_exit(-1);
    return -1;
  }

  return filesys_create(file, initial_size);
}

bool sys_remove(const char *file){

  if(get_user((const uint8_t*)file) == -1){
    sys_exit(-1);
    return -1;
  }

  return filesys_remove(file);
} 

int sys_open(const char* file) {
  struct file* file_opened;
  struct descriptor* fd = palloc_get_page(0);

  if (get_user((const uint8_t*)file) == -1) {
    sys_exit(-1);
    return -1;
  }

  file_opened = filesys_open(file);
  if (!file_opened) {
    return -1;
  }

  fd->file = file_opened;

  // asignar id
  // si la lista de descriptores esta vacia, el pimer id debe ser 3, pues 0,1 y 2 estan reservados
  struct list* descriptores = &thread_current()->descriptores;
  if (list_empty(descriptores)) {
    fd->id = 3;
  } else {
    fd->id = (list_entry(list_back(descriptores), struct descriptor, elem)->id) + 1;
  }
  // insertar en la lista de descriptores
  list_push_back(descriptores, &(fd->elem));

  // regresar el id del descriptor
  return fd->id;
}

void sys_close(int fd) {
  struct descriptor* descriptor = obtener_descriptor(fd);

  if (get_user((const uint8_t*)fd) == -1) {
    sys_exit(-1);
    return -1;
  }

  if(descriptor && descriptor->file) {
    // llamamos a la funcion del file system
    file_close(descriptor->file);
    // quitamos el archivo del proceso
    list_remove(&(descriptor->elem));
    // liberamos recursos
    palloc_free_page(descriptor);
  }
}

static struct descriptor* obtener_descriptor(int fd){
  if(fd<3){
    return NULL;
  }
  struct thread* thread_actual = thread_current();

  struct list_elem *e = list_begin(&thread_actual->descriptores); 
  int i = 3; // la lista empieza con id desde 3
  while(i<fd){
    if(e == NULL){
      // Si el primer elemento esta NULL, entonces no hay elementos
      return NULL;
    } else {
      e = list_next (e);
    }
  }
  // con la referencia al elemento obtenemos nuestra estructura
  return list_entry(e, struct descriptor, elem);
};

int sys_filesize(int fd) {
  struct descriptor* descriptor;

  if (get_user((const uint8_t*)fd) == -1) {
    sys_exit(-1);
    return -1;
  }

  descriptor = obtener_descriptor(fd);

  if(descriptor == NULL) {
    return -1;
  }

  return file_length(descriptor->file);
}