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
#include "filesys/file.h"
#include "threads/synch.h"
#include "devices/input.h"

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
/*
    Espera un proceso pid secundario y recupera el estado de salida del subproceso.
*/
int sys_wait(tid_t pid);
/*
    Lee size bytes del archivo abierto fd en el búfer. Devuelve el número de bytes realmente 
    leídos (0 al final del archivo), o -1 si el archivo no se pudo leer (debido a una condición 
    distinta al final del archivo). Fd 0 lee desde el teclado usando input_getc ().  
*/
int sys_read(int fd, void *buffer, unsigned size);
/*
    Cambia el siguiente byte a leer o escribir en el archivo abierto fd a la posición, expresada
    en bytes desde el principio del archivo.
*/
void sys_seek(int fd, unsigned position);
/*
    Devuelve la posición del siguiente byte a leer o escribir en el archivo abierto fd, expresado
    en bytes desde el principio del archivo.
*/
unsigned sys_tell(int fd);
/* Escribe BYTE en la dirección de usuario UDST.
   UDST debe estar por debajo de PHYS_BASE.
   Devuelve verdadero si tiene éxito, falso si ocurrió una falla de segmento */
static bool put_user(uint8_t *udst, uint8_t byte);

struct lock archivos;

void
syscall_init (void) 
{
  lock_init(&archivos);
  intr_register_int (0x30, 3, INTR_ON, syscall_handler, "syscall");
}

static void
syscall_handler (struct intr_frame *f UNUSED) 
{
  int sys_code;
  ASSERT( sizeof(sys_code) == 4 );
  // validar que sea un puntero valido
  if (get_user_bytes(f->esp, &sys_code, sizeof(sys_code)) == -1) {
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }    
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

        if(get_user_bytes(f->esp + 4, &status, sizeof(status)) == -1){
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        };

        sys_exit(status);
        
        break;
      }
    case SYS_WRITE:
      {
        int fd;
        const void* buffer;
        unsigned size;

        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &buffer, sizeof(buffer)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }
        
        if (get_user_bytes(f->esp + 12, &size, sizeof(size)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        int retorno = sys_write(fd, buffer, size);
        f->eax = (uint32_t)retorno;
        break; 
      }
    case SYS_EXEC:
      {
        void* cmd_line;

        int retorno = get_user_bytes(f->esp + 4, &cmd_line, sizeof(cmd_line));
        if(retorno == -1){
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        retorno = sys_exec((const char*)cmd_line);
        f->eax = (uint32_t)retorno;
        break;
      }
    case SYS_CREATE:
      {
        const char* filename;
        unsigned initial_size;
        
        if (get_user_bytes(f->esp + 4, &filename, sizeof(filename)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &initial_size, sizeof(initial_size)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
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
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
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
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
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
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        sys_close(fd);
        break;
      }
    case SYS_FILESIZE:
      {
        int fd;
        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        int retorno = sys_filesize(fd);
        f->eax = retorno;
        break;
      }
    case SYS_WAIT:
      {
        tid_t pid;
        if (get_user_bytes(f->esp + 4, &pid, sizeof(tid_t)) == -1){
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        int retorno = sys_wait(pid);
        f->eax = retorno;
        break;
      }
    case SYS_READ:
      {
        int fd;
        void* buffer;
        unsigned size;

        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &buffer, sizeof(buffer)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }
        
        if (get_user_bytes(f->esp + 12, &size, sizeof(size)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        int retorno = sys_read(fd, buffer, size);
        f->eax = (uint32_t)retorno;
        break; 
      }
    case SYS_SEEK:
      {
        int fd;
        unsigned position;

        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }

        if (get_user_bytes(f->esp + 8, &position, sizeof(position)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }
        
        sys_seek(fd, position);
        break; 
      }
    case SYS_TELL:
      {
        int fd;

        if (get_user_bytes(f->esp + 4, &fd, sizeof(fd)) == -1) {
          if (lock_held_by_current_thread(&archivos)){
            lock_release (&archivos);
          }  
          sys_exit(-1);
        }
        
        unsigned retorno = sys_tell(fd);
        f->eax = (uint32_t)retorno;
        break;
      }
    default:
      printf("[ERROR] system call %d is unimplemented!\n", sys_code);
      sys_exit(-1);
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

  struct process_control_block *pcb = thread_current()->pcb;
  if(pcb != NULL){
    pcb->terminado = true;
    pcb->exit_code = status;
  } 
  thread_exit();
}

static int get_user (const uint8_t *uaddr)
{
  // obtuvimos esta funcion de https://web.stanford.edu/~ouster/cgi-bin/cs140-spring20/pintos/pintos_3.html#SEC44
  // Accessing User Memory
  if(((void*)uaddr < PHYS_BASE)){
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
  return (int)bytes;
};
 
int sys_write(int fd, const void* buffer, unsigned size){
  // validamos que no este accesando a memoria que no debe
  if(get_user((const uint8_t*)buffer) == -1){
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }
  // final del archivo
  if(get_user((const uint8_t*)(buffer + size -1)) == -1){
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }
  lock_acquire(&archivos);
  int retorno = 0;
  //Todos nuestros programas de prueba escriben en la consola
  if(fd == 1){
    putbuf(buffer, size);
    retorno = size;
  } else {
    // para escribir en un archivo
    struct descriptor* descriptor = obtener_descriptor(fd);
    if(descriptor && descriptor->file){
      retorno = file_write(descriptor->file, buffer, size);
    } else {
      retorno = -1;
    }
  }
  lock_release(&archivos);
  return retorno;
}

tid_t sys_exec(const char* cmd_line){
  
  // cmd_line es un puntero a donde esta el argumento, debemos verificar que sea
  // valida la direccion
  if(get_user((const uint8_t*)cmd_line) == -1) {
    sys_exit(-1);
  }
  lock_acquire(&archivos);
  tid_t pid = process_execute(cmd_line);
  lock_release(&archivos);

  return pid;
}

bool sys_create(const char *file, unsigned initial_size){

  /* Para las llamadas del sistema que requieran manejo de archivos vamos a usar filesys/filesys.h */

  if(get_user((const uint8_t*)file) == -1){
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }
  lock_acquire(&archivos);
  bool retorno = filesys_create(file, initial_size);
  lock_release(&archivos);
  return retorno;
}

bool sys_remove(const char *file){

  if(get_user((const uint8_t*)file) == -1){
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }
  lock_acquire(&archivos);
  bool retorno = filesys_remove(file);
  lock_release(&archivos);
  return retorno;
} 

int sys_open(const char* file) {
  if (get_user((const uint8_t*)file) == -1) {
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }
  struct file* file_opened;
  struct descriptor* fd = palloc_get_page(0);
  if(!fd){
    return -1;
  }

  lock_acquire(&archivos);
  file_opened = filesys_open(file);
  if (!file_opened) {
    palloc_free_page(fd);
    lock_release(&archivos);
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
  lock_release(&archivos);
  return fd->id;
}

void sys_close(int fd) {
  lock_acquire(&archivos);

  struct descriptor* descriptor = obtener_descriptor(fd);

  if(descriptor && descriptor->file) {
    // llamamos a la funcion del file system
    file_close(descriptor->file);
    // quitamos el archivo del proceso
    list_remove(&(descriptor->elem));
    // liberamos recursos
    palloc_free_page(descriptor);
  }
  lock_release(&archivos);
}

static struct descriptor* obtener_descriptor(int fd){
  
  struct thread *t = thread_current();

  ASSERT(t!=NULL);

  if(fd<3){
    return NULL;
  }

  struct list *descriptores = &t->descriptores;
  struct list_elem *e;
  if(!list_empty(descriptores)){
    for (e = list_begin (descriptores); e != list_end (descriptores); e = list_next (e))
    {
      struct descriptor *descriptor = list_entry(e,struct descriptor, elem);
      if(descriptor->id == fd){
        return descriptor;
      }
    }
  }  
  
  // si llega hasta aqui entonces no hay un elemento relacionado
  return NULL;
}

int sys_filesize(int fd) {
  struct descriptor* descriptor;

  if (get_user((const uint8_t*)fd) == -1) {
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }

  lock_acquire(&archivos);

  descriptor = obtener_descriptor(fd);

  if(descriptor == NULL) {
    lock_release(&archivos);
    return -1;
  }

  int retorno = file_length(descriptor->file);

  lock_release(&archivos);

  return retorno;
}

int sys_wait(tid_t pid){
  return process_wait(pid);
}

int sys_read(int fd, void *buffer, unsigned size) {

  if (get_user((const uint8_t*) buffer) == -1) {
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  }

  if (get_user((const uint8_t*) (buffer + size -1)) == -1) {
    if (lock_held_by_current_thread(&archivos)){
      lock_release (&archivos);
    }  
    sys_exit(-1);
  } 

  lock_acquire(&archivos);
  int retorno = 0;
  if(fd == 1){
    retorno -1;
  } else if(fd == 0) { 
    // fd 0 lee desde el teclado usando input_getc ()
    uint8_t c;
    unsigned counter = size;
    uint8_t *buf = buffer;
    while (counter > 1 && (c = input_getc()) != 0)
      {
        *buf = c;
        buffer++;
        counter--; 
      }
    *buf = 0;
    retorno = size - counter;
  } else {
    // leer desde un archivo 
    struct descriptor* descriptor = obtener_descriptor(fd);

    if(descriptor && descriptor->file) {
      retorno = file_read(descriptor->file, buffer, size);
    } else {
      retorno = -1;
    }
  }
  lock_release(&archivos);
  return retorno;
}

void sys_seek (int fd, unsigned position){
  lock_acquire(&archivos);
  struct descriptor *descriptor = obtener_descriptor(fd);

  if(descriptor && descriptor->file){
    file_seek(descriptor->file, position);
  } else {
    return;
  }
  lock_release(&archivos);
}

unsigned sys_tell(int fd){
  lock_acquire(&archivos);
  struct descriptor *descriptor = obtener_descriptor(fd);
  int retorno;
  if(descriptor && descriptor->file) {
    retorno = file_tell(descriptor->file);
  } else {
    retorno = -1;
  }
  lock_release(&archivos);
  return retorno;
}

static bool put_user(uint8_t *udst, uint8_t byte)
{
  if(((void*)udst < PHYS_BASE)){
    int error_code;
    asm ("movl $1f, %0; movb %b2, %1; 1:"
       : "=&a" (error_code), "=m" (*udst) : "q" (byte));
    return error_code != -1;
  } else {
    return false;
  }  
}