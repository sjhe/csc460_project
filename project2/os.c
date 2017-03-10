#include "os.h"
#include "kernel.h"

#include "led_test.h"

#include <string.h>
#include <stdio.h>


/** @brief a_main function provided by user application. The first task to run. */
extern void a_main();

static queue_t system_queue;
static queue_t rr_queue;


/* Kernel functions */
static void kernel_handle_request(void);
static void Next_Kernel_Request(void); 
static void Dispatch(void);

/* Context Switching*/
extern void Exit_Kernel();    /* this is the same as CSwitch() */
extern void CSwitch();
extern void Enter_Kernel();


/* Task management  */
static void Kernel_Create_Task( voidfuncptr f);

/*
 * FUNCTIONS
 */
/**
 *  @brief The idle task does nothing but busy loop.
 */
static void idle (void)
{
    for(;;)
    {};
}


/**
 * \file active.c
 * \brief A Skeleton Implementation of an RTOS
 * 
 * \mainpage A Skeleton Implementation of a "Full-Served" RTOS Model
 * This is an example of how to implement context-switching based on a 
 * full-served model. That is, the RTOS is implemented by an independent
 * "kernel" task, which has its own stack and calls the appropriate kernel 
 * function on behalf of the user task.
 *
 * \author Dr. Mantis Cheng
 * \date 29 September 2006
 *
 * ChangeLog: Modified by Alexander M. Hoole, October 2006.
 *			  -Rectified errors and enabled context switching.
 *			  -LED Testing code added for development (remove later).
 *
 * \section Implementation Note
 * This example uses the ATMEL AT90USB1287 instruction set as an example
 * for implementing the context switching mechanism. 
 * This code is ready to be loaded onto an AT90USBKey.  Once loaded the 
 * RTOS scheduling code will alternate lighting of the GREEN LED light on
 * LED D2 and D5 whenever the correspoing PING and PONG tasks are running.
 * (See the file "cswitch.S" for details.)
 */


/*===========
  * RTOS Internal
  *===========
  */

/**
  * This internal kernel function is the context switching mechanism.
  * It is done in a "funny" way in that it consists two halves: the top half
  * is called "Exit_Kernel()", and the bottom half is called "Enter_Kernel()".
  * When kernel calls this function, it starts the top half (i.e., exit). Right in
  * the middle, "Cp" is activated; as a result, Cp is running and the kernel is
  * suspended in the middle of this function. When Cp makes a system call,
  * it enters the kernel via the Enter_Kernel() software interrupt into
  * the middle of this function, where the kernel was suspended.
  * After executing the bottom half, the context of Cp is saved and the context
  * of the kernel is restore. Hence, when this function returns, kernel is active
  * again, but Cp is not running any more. 
  * (See file "switch.S" for details.)
  */

/* Prototype */
void Task_Terminate(void);

/** 
  * This external function could be implemented in two ways:
  *  1) as an external function call, which is called by Kernel API call stubs;
  *  2) as an inline macro which maps the call into a "software interrupt";
  *       as for the AVR processor, we could use the external interrupt feature,
  *       i.e., INT0 pin.
  *  Note: Interrupts are assumed to be disabled upon calling Enter_Kernel().
  *     This is the case if it is implemented by software interrupt. However,
  *     as an external function call, it must be done explicitly. When Enter_Kernel()
  *     returns, then interrupts will be re-enabled by Enter_Kernel().
  */ 

#define Disable_Interrupt()		asm volatile ("cli"::)
#define Enable_Interrupt()		asm volatile ("sei"::)
  
/**
 * When creating a new task, it is important to initialize its stack just like
 * it has called "Enter_Kernel()"; so that when we switch to it later, we
 * can just restore its execution context on its stack.
 * (See file "cswitch.S" for details.)
 */
void Kernel_Create_Task_At( PD *p, voidfuncptr f ) 
{   
  unsigned char *sp;
  //Changed -2 to -1 to fix off by one error.
  sp = (unsigned char *) &(p->workSpace[WORKSPACE-1]);

  /*----BEGIN of NEW CODE----*/
  //Initialize the workspace (i.e., stack) and PD here!
  //Clear the contents of the workspace
  memset(&(p->workSpace),0,WORKSPACE);

  //Notice that we are placing the address (16-bit) of the functions
  //onto the stack in reverse byte order (least significant first, followed
  //by most significant).  This is because the "return" assembly instructions 
  //(rtn and rti) pop addresses off in BIG ENDIAN (most sig. first, least sig. 
  //second), even though the AT90 is LITTLE ENDIAN machine.

  //Store terminate at the bottom of stack to protect against stack underrun.
  *(unsigned char *)sp-- = ((unsigned int)Task_Terminate) & 0xff;
  *(unsigned char *)sp-- = (((unsigned int)Task_Terminate) >> 8) & 0xff;

  //Place return address of function at bottom of stack
  *(unsigned char *)sp-- = ((unsigned int)f) & 0xff;
  *(unsigned char *)sp-- = (((unsigned int)f) >> 8) & 0xff;
  *(unsigned char *)sp-- = 0x00;
    //Place stack pointer at top of stack
  sp = sp - 34;      
  p->sp = sp;    /* stack pointer into the "workSpace" */

  p->code = f;		/* function to be executed as a task */
  p->request = NONE;
  /*----END of NEW CODE----*/
  p->state = READY;


  /* ---- Need to add switch statement for handling ---- 
   * ---- PERIODIC | SYSTEM | RR                    ----
   */
}


/**
  *  Create a new task
  */
static void Kernel_Create_Task( voidfuncptr f ) 
{
  int x;
  if (Tasks == MAXTHREAD) return;  /* Too many task! */
   /* find a DEAD PD that we can use  */
  for (x = 0; x < MAXTHREAD; x++) {
    if (Process[x].state == DEAD) 
      break;
  }
  ++Tasks;
  Kernel_Create_Task_At( &(Process[x]), f );
}

/**
  * This internal kernel function is a part of the "scheduler". It chooses the 
  * next task to run, i.e., Cp.
  */

static void Dispatch()
{
  /* find the next READY task
   * Note: if there is no READY task, then this will loop forever!.
   */
   while(Process[NextP].state != READY) {
      NextP = (NextP + 1) % MAXTHREAD;
   }

   Cp = &(Process[NextP]);
   CurrentSp = Cp->sp;
   Cp->state = RUNNING;

   NextP = (NextP + 1) % MAXTHREAD;
}

/**
  * This internal kernel function is the "main" driving loop of this full-served
  * model architecture. Basically, on OS_Start(), the kernel repeatedly
  * requests the next user task's next system call and then invokes the
  * corresponding kernel function on its behalf.
  *
  * This is the main loop of our kernel, called by OS_Start().
  */
static void Next_Kernel_Request() 
{
  Dispatch();  /* select a new task to run */

  while(1) {
    Cp->request = NONE; /* clear its request */

     /* activate this newly selected task */
    CurrentSp = Cp->sp;
    Exit_Kernel();    /* or CSwitch() */

    /* if this task makes a system call, it will return to here! */

    /* save the Cp's stack pointer */
    Cp->sp = CurrentSp;

    kernel_handle_request();
  } 
}

/* 
 * fn kernel_handle_request() 
 * the switch statement below should be the main logic 
 * for handling the different types of kernel_requests 
 *
 * Also the first part of the scheduler
 * Determines whether current process should be
 * in the ready or waiting queue
*/
static void kernel_handle_request(void)
{
  switch(Cp->request){
    case CREATE:
      Kernel_Create_Task( Cp->code );
      break;
    case NEXT:
    case NONE:
     /* NONE could be caused by a timer interrupt */
      Cp->state = READY;
      Dispatch();
      break;
    case TERMINATE:
      /* deallocate all resources used by this task */
      Cp->state = DEAD;
      Dispatch();
      break;
    default:
      /* Houston! we have a problem here! */
    break;
  }
}

/*================
  * RTOS  API  and Stubs
  *================
  */

/**
  * This function initializes the RTOS and must be called before any other
  * system calls.
  */
void OS_Init() 
{
  int x;

  Tasks = 0;
  KernelActive = 0;
  NextP = 0;
	//Reminder: Clear the memory for the task on creation.
  for (x = 0; x < MAXTHREAD; x++) {
    memset(&(Process[x]),0,sizeof(PD));
    Process[x].state = DEAD;
  }
}


/**
  * This function starts the RTOS after creating a few tasks.
  */
void OS_Start() 
{   
  if ( (! KernelActive) && (Tasks > 0)) {
    Disable_Interrupt();
    /* we may have to initialize the interrupt vector for Enter_Kernel() here. */
    /* here we go...  */
    KernelActive = 1;
    Next_Kernel_Request();
    /* NEVER RETURNS!!! */
  }
}


/**
  * For this example, we only support cooperatively multitasking, i.e.,
  * each task gives up its share of the processor voluntarily by calling
  * Task_Next().
  */
void Task_Create( voidfuncptr f)
{
  if (KernelActive ) {
    Disable_Interrupt();
    Cp ->request = CREATE;
    Cp->code = f;
    Enter_Kernel();
  } else { 
    /* call the RTOS function directly */
    Kernel_Create_Task( f );
  }
}

/**
  * The calling task gives up its share of the processor voluntarily.
  */
void Task_Next() 
{
  if (KernelActive) {
    Disable_Interrupt();
    Cp ->request = NEXT;
    Enter_Kernel();
  }
}

/**
  * The calling task terminates itself.
  */
void Task_Terminate() 
{
  if (KernelActive) {
    Disable_Interrupt();
    Cp -> request = TERMINATE;
    Enter_Kernel();
    /* never returns here! */
  }
}

/**
  * Interrupt service routine
  */
// ISR(TIMER3_COMPA_vect)
// {
//   enable_LED(LED_ISR);
//   Task_Next();
//   disable_LEDs();
// }

/**
  * This function creates two cooperative tasks, "Ping" and "Pong". Both
  * will run forever.
  */
void main() 
{
  OS_Init();
  // Task_Create( Pong );
  Task_Create( a_main );
  OS_Start();
}

