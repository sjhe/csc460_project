#include "os.h"
#include "kernel.h"

#include "led_test.h"

#define Disable_Interrupt()   asm volatile ("cli"::)
#define Enable_Interrupt()    asm volatile ("sei"::)

#include <string.h>
#include <stdio.h>

/** @brief a_main function provided by user application. The first task to run. */
extern void a_main();

static queue_t system_queue;
static queue_t rr_queue;

/* Kernel functions */
static void Kernel_Handle_Request(void);
static void Kernel_Main_Loop(void); 
static void Kernel_Dispatch(void);

/* Context Switching*/
extern void Exit_Kernel();    /* this is the same as CSwitch() */
extern void CSwitch();
extern void Enter_Kernel();


/* Task management  */
static void Kernel_Create_Task(void);
void Task_Terminate();
void Task_Create( voidfuncptr f, int arg, unsigned int level);

/* Queue management */
static void Enqueue(queue_t* queue_ptr, PD* p);
static PD*  Dequeue(queue_t* queue_ptr);
/*
 * FUNCTIONS
 */
/**
 *  @brief The Idle task does nothing but busy loop.
 */
static void Idle (void)
{
  for(;;);
}

/**
 * When creating a new task, it is important to initialize its stack just like
 * it has called "Enter_Kernel()"; so that when we switch to it later, we
 * can just restore its execution context on its stack.
 * (See file "cswitch.S" for details.)
 */
void Kernel_Create_Task_At( PD *p ) 
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
  *(unsigned char *)sp-- = ((unsigned int)Cp->code) & 0xff;
  *(unsigned char *)sp-- = (((unsigned int)Cp->code) >> 8) & 0xff;

  *(unsigned char *)sp-- = (uint8_t) 0;
    //Place stack pointer at top of stack
  sp = sp - 34;      
  p->sp = sp;    /* stack pointer into the "workSpace" */


  p->code = Cp->code;		/* function to be executed as a task */
  p->request = NONE;
  /*----END of NEW CODE----*/
  p->state = READY;
  Cp->state = READY;

  /* ---- Need to add switch statement for handling ---- 
   * ---- PERIODIC | SYSTEM | RR                    ----
   */
  switch(Cp->level){
    case SYSTEM :
      Enqueue(&system_queue , p);
      break;
  }
}


/**
  *  Create a new task
  */
static void Kernel_Create_Task() 
{
  int x;
  if (Tasks == MAXTHREAD) return;  /* Too many task! */
   /* find a DEAD PD that we can use  */
  for (x = 0; x < MAXTHREAD; x++) {
    if (Process[x].state == DEAD) 
      break;
  }
  ++Tasks;
  Kernel_Create_Task_At( &(Process[x]));
}

/**
  * This internal Kernel function is a part of the "scheduler". It chooses the 
  * next task to run, i.e., Cp.
  */

static void Kernel_Dispatch()
{
  /* find the next READY task
   * Note: if there is no READY task, then this will loop forever!.
   */

  // if(Cp->state != RUNNING )
  // {
    if(system_queue.head != NULL)
    {
      Cp = Dequeue(&system_queue);
      CurrentSp = Cp->sp;
      Cp->state = RUNNING;
    }
  // }
  // 
  // while(Process[NextP].state != READY) {
  //   NextP = (NextP + 1) % MAXTHREAD;
  // }

   // Cp = &(Process[NextP]);  // Current Process Descriptor
   // CurrentSp = Cp->sp;
   // Cp->state = RUNNING;

   // NextP = (NextP + 1) % MAXTHREAD;
}

/**
  * This internal Kernel function is the "main" driving loop of this full-served
  * model architecture. Basically, on OS_Start(), the Kernel repeatedly
  * requests the next user task's next system call and then invokes the
  * corresponding Kernel function on its behalf.
  *
  * This is the main loop of our Kernel, called by OS_Start().
  */
static void Kernel_Main_Loop() 
{
  Kernel_Dispatch();  /* select a new task to run */

  while(1) {
    Cp->request = NONE; /* clear its request */

     /* activate this newly selected task */
    CurrentSp = Cp->sp;

    Exit_Kernel();    /* or CSwitch() */

    /* if this task makes a system call, it will return to here! */

    /* save the Cp's stack pointer */
    Cp->sp = CurrentSp;

    Kernel_Handle_Request();
  } 
}

/** el*
 * Also the first part of the scheduler
 * Determines whether current process should be
 * in the ready or waiting queue
 */
static void Kernel_Handle_Request(void)
{
  switch(Cp->request){
    case CREATE:
      Kernel_Create_Task();
      break;
    case NEXT:
    case NONE:
     /* NONE could be caused by a timer interrupt */
      Cp->state = READY;
      Enqueue(&system_queue, Cp);
      Kernel_Dispatch();
      break;
    case TERMINATE:
      /* deallocate all resources used by this task */
      Cp->state = DEAD;
      // Cp = Dequeue(&system_queue);
      Kernel_Dispatch();
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
  //Init Kernel data structures
  for (x = 0; x < MAXTHREAD; x++) {
    memset(&(Process[x]),0,sizeof(PD));
    Process[x].state = DEAD;    
  }
  // Task_Create_System(Idle,0);
  Task_Create_System(a_main,1);

  // Dequeue(&system_queue);
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
    Kernel_Main_Loop();   /* main loop of the Kernel*/
  }
}


/*
 * Task management.
 */

PID Task_Create_System(void (*f)(void), int arg){
  Task_Create(f,arg,SYSTEM);
};

PID Task_Create_RR(void (*f)(void), int arg){
  Task_Create(f,arg,RR);
};

PID Task_Create_Period(void (*f)(void), int arg, TICK period, TICK wcet, TICK offset){
  Task_Create(f,arg,PERIODIC);
};

/**
  * For this example, we only support cooperatively multitasking, i.e.,
  * each task gives up its share of the processor voluntarily by calling
  * Task_Next().
  */
void Task_Create( voidfuncptr f, int arg, unsigned int level)
{
  if (KernelActive ) {
    Disable_Interrupt();
    Cp ->code = f;
    Cp ->arg  = arg;
    Cp ->level = (uint8_t)level;    
    
    Cp ->request = CREATE;
    Enter_Kernel();
  } else{
    Cp ->code = f;
    Cp ->arg  = arg;
    Cp ->level = (uint8_t)level; 

    Kernel_Create_Task();
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



/*
 * Queue manipulation.
 */

/**
 * @brief Add a task the head of the queue
 *
 * @param queue_ptr the queue to insert in
 * @param task_to_add the task descriptor to add
 */
static void Enqueue(queue_t* queue_ptr, PD* p)
{
    p->next = NULL;

    if(queue_ptr->head == NULL)
    {
        /* empty queue */
        queue_ptr->head = p;
        queue_ptr->tail = p;
    }
    else
    {
        /* put task at the back of the queue */
        queue_ptr->tail->next = p;
        queue_ptr->tail = p;
    }
}


/**
 * @brief Pops head of queue and returns it.
 *
 * @param queue_ptr the queue to pop
 * @return the popped task descriptor
 */
static PD* Dequeue(queue_t* queue_ptr)
{
    PD* p = queue_ptr->head;

    if(queue_ptr->head != NULL)
    {
        queue_ptr->head = queue_ptr->head->next;
        p->next = NULL;
    }

    return p;
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
  OS_Start();
}

