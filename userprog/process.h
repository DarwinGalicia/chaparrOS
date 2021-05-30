#ifndef USERPROG_PROCESS_H
#define USERPROG_PROCESS_H

#include "threads/thread.h"
#include "threads/synch.h"

/* elemento de descriptores */
struct descriptor {
  int id;
  struct list_elem elem;
  struct file* file;
};

/* PCB */
struct process_control_block {
  tid_t pid;                
  const char* cmdline;
  struct list_elem elem;
  bool esperando;     // esta bandera, indica si el proceso padre, va ha esperar
  bool terminado;     // indica si el proceso ya esta terminado
  int exit_code;      // indica el codigo con el termino el proceso
  struct semaphore inicializacion; 
  struct semaphore esperar; 
  // en el caso que el padre no tenga que esperar por sus hijos, entonces el padre no 
  // debe liberar los recursos de los hijos, en este caso el hijo se encargara de liberarlo
  bool deboliberar;
};

tid_t process_execute (const char *file_name);
int process_wait (tid_t);
void process_exit (void);
void process_activate (void);

#endif /* userprog/process.h */
