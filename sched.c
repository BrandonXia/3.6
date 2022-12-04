#include "types.h"
#include "i8259.h"
#include "lib.h"
#include "scall.h"
#include "terminal.h"
#include "paging.h"
#include "sched.h"
int espp, ebpp;
int counter = 0;
int schdule_tid = -1;
int32_t n_terminal = 0;
/*reference from https://wiki.osdev.org/Programmable_Interval_Timer*/
int ack;
void pit_init(){
    
    outb(PIT_MODE,PIT_CMD);
    
    uint8_t data_port =  (uint8_t)PIT_MODE && 0xFF;
    outb(data_port, PIT_DATA);
    
    data_port = (uint8_t)FREQ_DIV;
    outb(data_port,PIT_DATA);
    
    enable_irq(IDT_PIT);
}
void pit_handler()
{
    send_eoi(IDT_PIT);
    if(terminal_array[1].current_process_id != -1 || terminal_array[2].current_process_id != -1){
        if(cur_pid != -1){
        process_switch();
        }
    }else{
        asm volatile(
	        "movl %%esp, %%eax;"
	        "movl %%ebp, %%ebx;"
	        :"=a"(espp), "=b"(ebpp)
	        :											
	    );
        /*store it*/
        if(cur_process_ptr != NULL){
        cur_process_ptr->scheduled_esp = espp;
        cur_process_ptr->scheduled_ebp = ebpp;
        cur_process_ptr->tss_esp0 = tss.esp0;
        }
    }
    return;
}

void process_switch()
{
    send_eoi(IDT_PIT);
    counter ++;
    if(schdule_tid == -1){
        schdule_tid = current_terminal_id;
    }
    n_terminal = schdule_tid;
    int i;
    // check if any terminal running
    for(i =0; i < MAX_TERMINAL_SIZE; ++i){
        n_terminal = (n_terminal+1)%MAX_TERMINAL_SIZE;
        if(terminal_array[n_terminal].current_process_id != -1){
            break;
        }
    }

    /*video mem switch*/
    uint8_t temp = 0;
    uint8_t* new_screen = &temp;
    uint8_t** NSS = &new_screen;
    vidmap(NSS);
    /*check if the terminal matched, if not remap the video memeory*/
    if(n_terminal != current_terminal_id){
    int32_t NS = (int32_t) new_screen;
	int32_t PDE =  NS/ 0x400000; // 4MB
    int32_t P_ADD = (int32_t)terminal_array[n_terminal].video_buffer_addr;
	DT[PDE] = 0;
	DT[PDE] = (uint32_t)PT;	/* get the first page_base_address in table 12*/
    //DT[PDE].kb.p = 1;			/* set present */
	//DT[PDE].kb.rw = 1;		/* read or write */
	//DT[PDE].kb.us = 1;		/* assign the user privilege level */
    DT[PDE] = DT[PDE] | PDE_P;
    DT[PDE] = DT[PDE] | PDE_RW;
    DT[PDE] = DT[PDE] | PDE_US;
	//map video memory
    PT[0] = P_ADD; // the first PT, 0xB8 entry --table
	PT[0] = PT[0] | PTE_P;
    PT[0] = PT[0] | PTE_RW;
    PT[0] = PT[0] | PTE_US;
    tlb_flash();
    }
    // cli();
    
    /*get the new process*/
    uint32_t stack_esp = 0;
    uint32_t stack_ebp = 0;
    int32_t next_tid = 0;
    uint32_t pd_addr = 0;
    uint32_t pd_index = 0;
    term_info[current_terminal_id].pid = cur_pid;
    term_info[current_terminal_id].pid_t = cur_process_ptr;
    terminal_array[current_terminal_id].current_process_id = cur_pid;
    /*check if any shell exists, if not, create one*/

    if(cur_pid == -1){
        next_tid = forward(current_terminal_id);
        execute((uint8_t*)"shell");
    }else{
        /*get the current stack info*/
        asm volatile(
	        "movl %%esp, %%eax;"
	        "movl %%ebp, %%ebx;"
	        :"=a"(stack_esp), "=b"(stack_ebp)
	        :											
	    );
        /*store it*/
        cur_process_ptr->scheduled_esp = stack_esp;
        cur_process_ptr->scheduled_ebp = stack_ebp;
        cur_process_ptr->tss_esp0 = tss.esp0;
    // printf("cur_pid is %d\n",cur_pid);
    // printf("cur_process_ptr->scheduled_esp is %d\n",cur_process_ptr->scheduled_esp);
    // printf("cur_process_ptr->scheduled_ebp is %d\n",cur_process_ptr->scheduled_ebp);
    // printf("cur_process_ptr->tss_esp0 is %d\n",cur_process_ptr->tss_esp0);
        /*switch the terminal*/
        next_tid = forward(current_terminal_id);
        /*set user page*/
        pd_index = USER_MEM >> 22; // 22 is to get the offset
        pd_addr = 2 + cur_pid ; // start from 8MB + current pid
        DT[pd_index] = 0x00000000;
        DT[pd_index] = DT[pd_index] | PD_MASK;
        DT[pd_index] = DT[pd_index] | (pd_addr << 22);
        // tlb_flash();
            asm volatile(
        "movl %%cr3,%%eax     ;"
        "movl %%eax,%%cr3     ;"

        : : : "eax", "cc" 
        );
        /*store stack infomation*/
    // printf("cur_pid is %d\n",cur_pid);
    // printf("cur_process_ptr->scheduled_esp is %d\n",cur_process_ptr->scheduled_esp);
    // printf("cur_process_ptr->scheduled_ebp is %d\n",cur_process_ptr->scheduled_ebp);
    // printf("cur_process_ptr->tss_esp0 is %d\n",cur_process_ptr->tss_esp0);
        stack_esp = cur_process_ptr->scheduled_esp;
        stack_ebp = cur_process_ptr->scheduled_ebp;
        tss.esp0 = cur_process_ptr->tss_esp0;
        tss.ss0 = KERNEL_DS;
    // printf("cur_process_ptr->scheduled_esp is %d\n",cur_process_ptr->scheduled_esp);
    // printf("cur_process_ptr->scheduled_ebp is %d\n",cur_process_ptr->scheduled_ebp);
    // printf("cur_process_ptr->tss_esp0 is %d\n",cur_process_ptr->tss_esp0);
        asm volatile(
            "movl %%eax, %%esp;"
	        "movl %%ebx, %%ebp;"
            :
            :"a"(stack_esp), "b"(stack_ebp)
        );
    }
    /*set user page and the file array*/
    pd_index = USER_MEM >> 22; // 22 is to get the offset
    pd_addr = 2 + cur_pid ; // start from 8MB + current pid
    DT[pd_index] = 0x00000000;
    DT[pd_index] = DT[pd_index] | PD_MASK;
    DT[pd_index] = DT[pd_index] | (pd_addr << 22);
    tlb_flash();
    filed_array = cur_process_ptr->fd_array;
    // sti();

    /*return*/
    asm volatile(
        "leave ;"
        "ret ;"
    );
    return;
}


int32_t forward( int32_t tid)
{
    schdule_tid = n_terminal;
    cur_pid = terminal_array[n_terminal].current_process_id;
    cur_process_ptr = (PCB_t*)(END_OF_KERNEL - KERNEL_STACK_SIZE * (cur_pid + 1));
    return n_terminal;
}

int32_t get_n_terminal(void){
    return n_terminal;
}

