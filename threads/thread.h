#ifndef THREADS_THREAD_H
#define THREADS_THREAD_H

#include <debug.h>
#include <list.h>
#include <stdint.h>

/* States in a thread's life cycle. */
enum thread_status
  {
    THREAD_RUNNING,     /* Running thread. */
    THREAD_READY,       /* Not running but ready to run. */
    THREAD_BLOCKED,     /* Waiting for an event to trigger. */
    THREAD_DYING        /* About to be destroyed. */
  };

/* Thread identifier type.
   You can redefine this to whatever type you like. */
typedef int tid_t;
#define TID_ERROR ((tid_t) -1)          /* Error value for tid_t. */

/* Thread priorities. */
#define PRI_MIN 0                       /* Lowest priority. */
#define PRI_DEFAULT 31                  /* Default priority. */
#define PRI_MAX 63                      /* Highest priority. */

#define CORRIMIENTO 12
#define ADD_X_N(X,N) ((X) + (N<<CORRIMIENTO))          // Add x and n:	x + n * f  
#define MUL_X_N(X,N) ((X) * N)                         // Multiply x by n:	x * n
#define DIV_X_N(X,N) ((X) / N)                         // Divide x by n:	x / n
#define DIV_X_Y(X,Y) ((((int64_t)X)<<CORRIMIENTO)/(Y)) // Divide x by y:	((int64_t) x) * f / y
#define MUL_X_Y(X,Y) (((int64_t)X)*(Y)>>CORRIMIENTO)   // Multiply x by y:	((int64_t) x) * y / f
#define CONV_N(N) ((N)<<CORRIMIENTO)                   // Convert n to fixed point:	n * f
#define SUB_X_Y(X,Y) ((X) - (Y))                       // Subtract y from x:	x - y
                           /*  (x + f / 2) / f if x >= 0, (x - f / 2) / f if x <= 0 */
#define ROUND_X(X) ((X)>=0 ? (((X)+(1<<(CORRIMIENTO-1)))>>CORRIMIENTO): (((X)-(1<<(CORRIMIENTO-1)))>>CORRIMIENTO) )  

/* A kernel thread or user process.

   Each thread structure is stored in its own 4 kB page.  The
   thread structure itself sits at the very bottom of the page
   (at offset 0).  The rest of the page is reserved for the
   thread's kernel stack, which grows downward from the top of
   the page (at offset 4 kB).  Here's an illustration:

        4 kB +---------------------------------+
             |          kernel stack           |
             |                |                |
             |                |                |
             |                V                |
             |         grows downward          |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             |                                 |
             +---------------------------------+
             |              magic              |
             |                :                |
             |                :                |
             |               name              |
             |              status             |
        0 kB +---------------------------------+

   The upshot of this is twofold:

      1. First, `struct thread' must not be allowed to grow too
         big.  If it does, then there will not be enough room for
         the kernel stack.  Our base `struct thread' is only a
         few bytes in size.  It probably should stay well under 1
         kB.

      2. Second, kernel stacks must not be allowed to grow too
         large.  If a stack overflows, it will corrupt the thread
         state.  Thus, kernel functions should not allocate large
         structures or arrays as non-static local variables.  Use
         dynamic allocation with malloc() or palloc_get_page()
         instead.

   The first symptom of either of these problems will probably be
   an assertion failure in thread_current(), which checks that
   the `magic' member of the running thread's `struct thread' is
   set to THREAD_MAGIC.  Stack overflow will normally change this
   value, triggering the assertion. */
/* The `elem' member has a dual purpose.  It can be an element in
   the run queue (thread.c), or it can be an element in a
   semaphore wait list (synch.c).  It can be used these two ways
   only because they are mutually exclusive: only a thread in the
   ready state is on the run queue, whereas only a thread in the
   blocked state is on a semaphore wait list. */
struct thread
  {
    /* Owned by thread.c. */
    tid_t tid;                          /* Thread identifier. */
    enum thread_status status;          /* Thread state. */
    char name[16];                      /* Name (for debugging purposes). */
    uint8_t *stack;                     /* Saved stack pointer. */
    int priority;                       /* Priority. */
    int priorityOriginal;                // Prioridad Original, para Priority Donation
    struct list_elem allelem;           /* List element for all threads list. */

    /* Shared between thread.c and synch.c. */
    struct list_elem elem;              /* List element. */


   uint64_t TIEMPO_DORMIDO;   //entero que represente el tiempo que un thread debe permanecer dormido
   struct lock *waiting_for_lock;     // El lock por el cual espera este thread
   struct list holding_lock;          // Los bloqueos que tiene este thread 
   
   int64_t recent_cpu;                // Para advanced scheduller
   int nice;                          // Para advanced scheduller

#ifdef USERPROG
    /* Owned by userprog/process.c. */
    uint32_t *pagedir;                  /* Page directory. */
    /*Cada proceso tiene un conjunto independiente de descriptores de archivo.*/
    struct list descriptores;
    struct process_control_block *pcb;
    struct list procesos;
    struct file *ejecutable;           //El archivo ejecutable de asociado
#endif

    /* Owned by thread.c. */
    unsigned magic;                     /* Detects stack overflow. */
  };

/* If false (default), use round-robin scheduler.
   If true, use multi-level feedback queue scheduler.
   Controlled by kernel command-line option "-o mlfqs". */
extern bool thread_mlfqs;

void thread_init (void);
void thread_start (void);

void thread_tick (void);
void thread_print_stats (void);

typedef void thread_func (void *aux);
tid_t thread_create (const char *name, int priority, thread_func *, void *);

void thread_block (void);
void thread_unblock (struct thread *);

struct thread *thread_current (void);
tid_t thread_tid (void);
const char *thread_name (void);

void thread_exit (void) NO_RETURN;
void thread_yield (void);

/* Performs some operation on thread t, given auxiliary data AUX. */
typedef void thread_action_func (struct thread *t, void *aux);
void thread_foreach (thread_action_func *, void *); // Esta funcion aplica thread_action_func en todos los threads

int thread_get_priority (void);
void thread_set_priority (int);

int thread_get_nice (void);
void thread_set_nice (int);
int thread_get_recent_cpu (void);
int thread_get_load_avg (void);

void insertar_en_lista_espera(int64_t ticks);
void remover_thread_durmiente(int64_t ticks);

/* Funcion auxiliar para ordenar la lista, comparara si la prioridad del thread 
a es mayor a la de b, mayor prioridad en head */
static bool ordenarMayorMenor(const struct list_elem *a,
                             const struct list_elem *b,
                             void *aux);

void verificar(struct thread *t, int p);

void actualizar_current_recent_cpu();
void actualizar_load_avg();
void actualizar_thread_recent_cpu(struct thread *t, void *aux);
void actualizar_thread_priority(struct thread *t, void *aux);
void ordernar_ready_list();
#endif /* threads/thread.h */
