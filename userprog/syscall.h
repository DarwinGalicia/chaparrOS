#ifndef USERPROG_SYSCALL_H
#define USERPROG_SYSCALL_H

void syscall_init (void);
/*
    Termina el programa de usuario actual, devolviendo el estado al kernel. Si el padre del proceso 
    lo espera (ver más abajo), este es el estado que se devolverá. Convencionalmente, un estado de 0
    indica éxito y los valores distintos de cero indican errores.
*/
void sys_exit(int);
#endif /* userprog/syscall.h */
