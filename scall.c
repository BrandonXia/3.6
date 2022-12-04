/* process.c - Functions to initializing page
 * 
 */

#include "lib.h"
#include "syscall.h"
#include "RTC.h"
#include "paging.h"
#include "terminal.h"
#include "x86_desc.h"
#include "scall.h"
#include "filesystem.h"
#include "fileoptable.h"
#include "keyboard.h"

/* 
 * execute
 *   DESCRIPTION: initializing the page, initialize all PTE and PDE in page directory
                  and page tables
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */

#define SUCCESS 0
#define FAIL -1
// global varible for use
uint8_t pid_check[MAX_PROCESS_NUMBER]= {0,0,0,0,0,0};
int32_t new_pid = -1;
fdarray* filed_array = NULL ;
int32_t cur_pid = -1;
PCB_t* cur_process_ptr = NULL;

/*
 *  int32_t execute(const uint8_t* command)
 *  DESCRIPTION: create a new process        
 *  INPUTS: a command from keyborad buffer
 *  OUTPUTS: whether it succed
 *  RETURN VALUE: halt return val or failure.
 */
int32_t execute(const uint8_t* command)
{
    int i=0; // loop variable
    int cmd_length;
    int cmd_curindex = 0;
    int filename_count = 0;
    int arg_count = 0;
    int empty_check = 0;
    uint32_t pd_addr = 0;
    uint32_t pd_index = 0;
    uint8_t file_command[FILE_NAME_LENGTH+1];
    uint8_t arg_command[FILE_NAME_LENGTH+1];

    dentry_t check_dentry;
    uint8_t data_buf[sizeof(int32_t)];
    /*store info defined in x86_desc.h into the prepared varible for later use*/
    uint8_t* EIPP =  (uint8_t*) (USER_PADDR + 24) ;  // 24 is the offset for EIP

    cli();
    // 1 - parse argument
     cmd_length = strlen((int8_t*)command);

    // chech command validity
     if(command == NULL)
    {
        printf("No command now \n");
        return FAIL;
    }
    // step for the space
    while(command[cmd_curindex] == ' ')
    {   cmd_curindex ++; }

    // fill file_command[]
    while (command[cmd_curindex] != ' ' && cmd_curindex < cmd_length)
    {
        file_command[filename_count] = command[cmd_curindex];
        filename_count++;
        cmd_curindex++;
        if (filename_count > FILE_NAME_LENGTH+1) {return FAIL;}
    }
    file_command[filename_count] = '\0' ;

    // step for space
    while (command[cmd_curindex] == ' '){cmd_curindex++;}

    // fill file_command[]
    while ( cmd_curindex < cmd_length )
    {
        arg_command[arg_count] = command[cmd_curindex];
        cmd_curindex++; arg_count++;
        if (arg_count > FILE_NAME_LENGTH+1) {return FAIL;}
    }
    arg_command[arg_count] = '\0';
 
    // 2 - check file validity
    if(read_dentry_by_name(file_command, &check_dentry)==-1)
    {
        return -1; 
    } 

    if(read_data(check_dentry.inode_index, 0, data_buf, sizeof(int32_t)) == -1) //need check
    {
        return -1;
    }

    if((data_buf[0] != EXE_CHECK1) || (data_buf[1] != EXE_CHECK2) || (data_buf[2] != EXE_CHECK3) || (data_buf[3] != EXE_CHECK4))
    {
        return -1;
    }

    // find current pit
    // Process allocate
    for(i=0;i<MAX_PROCESS_NUMBER;i++)
    {
        if(pid_check[i] == 0)
        {
            pid_check[i] = 1;
            new_pid = i;
            empty_check = 1;
            break;
        }
    }
    // check process number at max size
    if(empty_check == 0)
    {
        printf("No more process avaliable now !\n");
        return FAIL;
    }

    // 3 - set up paging
    pd_index = USER_MEM >> 22; // 22 is to get the offset
    pd_addr = 2 + new_pid ; // start from 8MB + current pid
    DT[pd_index] = 0x00000000;
    DT[pd_index] = DT[pd_index] | PD_MASK;
    DT[pd_index] = DT[pd_index] | (pd_addr << 22);
    tlb_flash();

    // 4 - load file into memory
    read_data(check_dentry.inode_index, 0, (uint8_t*)USER_PADDR, USER_STACK_SIZE);

    // 5 - create PCBs  
    if(initialize_PCB((int8_t*) arg_command) == FAIL)
        return FAIL;
    // initialized arg
    // We don't know why we put it in the PCB initialize function, it occurs page fault
    int8_t* argg = (int8_t*) arg_command;
    strncpy(cur_process_ptr->arg, argg, FILE_NAME_LENGTH);
    // uint32_t stack_esp = 0;
    // uint32_t stack_ebp = 0;
    // int ebpp = *((int32_t*)cur_process_ptr->scheduled_ebp);
    // printf("ebpp is %x\n", ebpp);
    //     asm volatile(
	//         "movl %%esp, %%eax;"
	//         "movl %%ebp, %%ebx;"
	//         :"=a"(stack_esp), "=b"(stack_ebp)
	//         :											
	//     );
    //     /*store it*/
    //     cur_process_ptr->scheduled_esp = stack_esp;
    //     cur_process_ptr->scheduled_ebp = stack_ebp;
    // printf("cur_pid is %d\n",cur_pid);
    // printf("cur_process_ptr->scheduled_esp is %d\n",cur_process_ptr->scheduled_esp);
    // printf("cur_process_ptr->scheduled_ebp is %d\n",cur_process_ptr->scheduled_ebp);
    // printf("cur_process_ptr->tss_esp0 is %d\n",cur_process_ptr->tss_esp0);
    
    // 6 - prepare for context switch
    //if it's not the first process, then store esp and ebp to the old process for halt to use

    if(cur_process_ptr->parent_pid != -1){
        int32_t old_pid = cur_process_ptr->parent_pid;
        PCB_t* old_process_ptr = (PCB_t*)(END_OF_KERNEL - KERNEL_STACK_SIZE * (old_pid + 1));
        asm volatile(
            "movl %%esp, %0 ;"
            "movl %%ebp, %1 ;"
            : "=r" (old_process_ptr->scheduled_esp) ,"=r" (old_process_ptr->scheduled_ebp)
        );
    }
    //prepare for context switch
    tss.esp0 = cur_process_ptr -> tss_esp0 ;
    tss.ss0 = KERNEL_DS ;

    /*store info defined in x86_desc.h into the prepared varible for later use*/
    uint32_t EIP = *(uint32_t*) EIPP;
    sti();
    // context switch
    /*some knowledge from https://zichengma.notion.site/   ECE391-MP3-d1de1e2d815c45c88bc4a093aa1ea143#fff473a16c2f4197b6f6df796c101b6a*/
    
    asm volatile(
        "movl 4(%%esp), %%ebx;" // 4 is for shift 1 memory location
        "xorl %%eax, %%eax;"
        "movw $0x2B, %%ax;"
        "pushl %%eax;" // which is USER_DS
        "movl $0x83ffffc, %%ebx;" 
        "pushl %%ebx;" // which is user_stack = USER_MEM + USER_STACK_SIZE - 4, 4 is the stack_fence, USER_STACK_SIZE is 8kb
        "pushfl;"
        "xorl %%ecx, %%ecx;"
        "movw $0x23, %%cx;" // which is USER_DS
        "pushl %%ecx;"
        "pushl %%edx;"
        "IRET"
        :
        : "d"(EIP)
        : "cc", "memory"
    );
    // actually never here
    return SUCCESS;
}


/* 
 * initialize_PCB
 *   DESCRIPTION: initializing new process control block
 *   INPUTS: arg_command -- the arguments read from file
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
int8_t initialize_PCB(int8_t* arg_command)
{
    /* allocate space in kernel space for the new process control block */
    PCB_t* cur_prc = (PCB_t*)(END_OF_KERNEL - KERNEL_STACK_SIZE * (new_pid + 1));
    cur_prc->pid = new_pid ;
    /* If this process is the first process of the terminal,*/
    if (terminal_array[current_terminal_id].current_process_id == -1)
        cur_prc->parent_pid = -1;
    else
        cur_prc->parent_pid = cur_pid;

    // init filed_array in PCB
    filed_array = cur_prc->fd_array ;
    if (cur_prc->fd_array == NULL)
    {
        printf("Wrong filed_array pointer when initialize PCB!\n");
        return FAIL;

    }
    filed_array[0].fileot_pointer = &file_operation_table[3];
    filed_array[0].inode = NULL;
    filed_array[0].flags = 1;
    filed_array[0].position = 0;
    filed_array[1].fileot_pointer = &file_operation_table[3];
    filed_array[1].inode = NULL;
    filed_array[1].flags = 1;
    filed_array[1].position = 0;
    int count;
    for ( count =2 ; count<8 ; count++ ) /* 8 stands for max fdarray size*/
    {
        filed_array[count].flags = 0;        
    }

    // initialize tss_esp0
    cur_prc->tss_esp0 = (uint32_t)cur_prc + KERNEL_STACK_SIZE - 4 ;

    // cur_prc->mapped_video_addr = 0 ;
    // update current pointer and pid in the global variable
    cur_process_ptr = cur_prc;
    cur_pid = cur_prc->pid ;
    terminal_array[current_terminal_id].current_process_id = cur_pid;


    /* initialization for file descriptor array*/
    return SUCCESS;
} 


/* 
 * halt
 *   DESCRIPTION: halt the whole process
 *   INPUTS: status -- the current status of the process
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
int32_t halt(uint8_t status)
{
    int i;
     /* free current pid */
    pid_check[cur_pid] = 0;
    /*get parent pid and parent process ptr*/
    uint32_t parentpid ;
    parentpid = cur_process_ptr->parent_pid ;
    PCB_t* parent_process_ptr = (PCB_t*)(END_OF_KERNEL - KERNEL_STACK_SIZE * (parentpid+1));

    /* check for shell process */
    if( cur_process_ptr->parent_pid == -1 ){
        printf("can't halt from this process\n");
        cur_pid = -1;
        execute((uint8_t*)"shell");
    }

    /* set tss ss0 and esp0 by parent process */
    tss.ss0 = KERNEL_DS;
    tss.esp0 = parent_process_ptr->tss_esp0 ;

    /* set up user paging for parent process */
    int pd_index = USER_MEM >> 22;
    int pd_addr = 2 + parentpid ;
    DT[pd_index] = 0x00000000;
    DT[pd_index] = DT[pd_index] | PD_MASK;
    DT[pd_index] = DT[pd_index] | (pd_addr << 22);
    tlb_flash();
    
   /* free the file descriptor array here */
    for (i=2;i<8;i++)
    {
        close(i);
    }
    filed_array[0].flags = 0;
    close(0);
    filed_array[1].flags = 0;
    close(1);

    /* set file descriptor array to the parent filed_array*/
    filed_array = parent_process_ptr->fd_array ;
    
    /* reset the current pid and current process pointer*/
    cur_pid = parentpid ;
    cur_process_ptr = parent_process_ptr ;

    /* deal with the return value to execute */
    uint16_t result;
    if ( status == EXCEPTION_STATUS )
        result = 256 ; /* as the exception return value */
    else
        result = (uint16_t)status;

    asm volatile(
        "movl %0, %%esp ;"
        "movl %1, %%ebp ;"
        "xorl %%eax,%%eax;"
        "movw %2, %%ax ;"
        "leave;"
        "ret;"
        : 
        : "r" (cur_process_ptr->scheduled_esp), "r" (cur_process_ptr->scheduled_ebp), "r"(result)
        : "esp", "ebp", "eax"
    );

    return SUCCESS; 
}

/* 
 * getargs
 *   DESCRIPTION: get n byte arguments from buf and copy it to PCB
 *   INPUTS: buf -- the buf can get args
 *           n -- the length of arguments
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
int32_t getargs(uint8_t* buf, int32_t n){     
    int start = (int)buf;
    int end = (int)(buf+n);
    /*check if the buf is out of the user memory*/
    if( start < USER_MEM ) { return -1;}
    if( end > USER_END ){ return -1;}
    int8_t first_arg;
    first_arg = cur_process_ptr->arg[0];
    if(first_arg == '\n'){ return -1;}
    int8_t* buff = (int8_t*) buf;
    strncpy(buff, cur_process_ptr->arg, n);
    return 0;
}

/* 
 * set handler
 *   DESCRIPTION: not required in this checkpoint now
 *   INPUTS: signum -- the number of signals
 *           address -- where to set
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
int32_t set_handler(int32_t signum, void* address){
    return -1;
}

/* 
 * sigreturn
 *   DESCRIPTION: not required in this checkpoint now
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
int32_t sigreturn(void){
    return -1;
}

/* 
 * vidmap
 *   DESCRIPTION: allocate a 4KB space for process image in video memory
 *   INPUTS: screen_start -- address of virtual memory
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none 
 */
int32_t vidmap(uint8_t** screen_start){
    // check validity
    if(screen_start == NULL)
    {
        return FAIL;
    }
    *screen_start = (uint8_t*)USER_END - FENCE_NUM;
    // check boundary condition
    if((uint32_t)screen_start < USER_MEM)
    {
        return FAIL;
    }
    if((uint32_t)screen_start > USER_END - FENCE_NUM)
    {
        return FAIL;
    }
    // paging map
    int pd_index = VIDEO_INDEX;
    DT[pd_index] = 0;
    PT_VIDMEM[0] = 0;
    DT[pd_index] = DT[pd_index] | (uint32_t)(&PT_VIDMEM[0]);
    DT[pd_index] = DT[pd_index] | PDE_P;
    DT[pd_index] = DT[pd_index] | PDE_RW;
    DT[pd_index] = DT[pd_index] | PDE_US;
    PT_VIDMEM[0] = PT_VIDMEM[0] | PTE_P;
    PT_VIDMEM[0] = PT_VIDMEM[0] | PTE_RW;
    PT_VIDMEM[0] = PT_VIDMEM[0] | PTE_US;
    PT_VIDMEM[0] = PT_VIDMEM[0] | VIDEO_MEM;
    tlb_flash();
    *screen_start = (uint8_t*)USER_VIDEO_MEM;
    return VIDEO_MEM;
}

/*
 *  int32_t open (const uint8_t* filename)
 *  DESCRIPTION: insert one new file          
 *  INPUTS: the file name
 *  OUTPUTS: file array filled with entry
 *  RETURN VALUE: -1 for failure.
 */
int32_t open (const uint8_t* filename){
    int counter = -1;
    int loop_through_fd;
    dentry_t dentry;
    dentry_t *current = &dentry;

    // check validity
    if(filename == NULL || filed_array == NULL || -1 == read_dentry_by_name(filename, current)) {return -1;}

    // check where should we insert the new file
    for(loop_through_fd = 0; loop_through_fd < FDARRAY_MAX; loop_through_fd++)
    {
        if(filed_array[loop_through_fd].flags == 0)
        {   
            counter = loop_through_fd;
            filed_array[counter].fileot_pointer = &file_operation_table[current->file_type];
            filed_array[counter].position = 0;
            // set -1 for rtc
            if (current->file_type == 0)
            {
                filed_array[counter].inode = -1;
            }
            else
            {
                filed_array[counter].inode = current->inode_index;
            }
            if(filed_array[counter].fileot_pointer->open((char *)filename)==-1) {return -1;}
            filed_array[counter].flags = 1;
            return counter;
        }
    }
    return -1;
}

/*
 *  int32_t close (int i)
 *  DESCRIPTION: close files      
 *  INPUTS: i -- size of file array
 *  OUTPUTS: file array filled with entry
 *  RETURN VALUE: -1 for failure.
 */
int32_t close(int i)
{
    int loop_through_fd;
    int loop_through_dentry;
    dentry_t dentry;
    dentry_t *current = &dentry;
    char *filename;
    // check validity
    if(i >= 7 || i < 0){ return -1; } // 7 is the size of file array
    if(filed_array == NULL) {return -1;}
    if (i == 0 || i == 1 || filed_array[i].flags == 0)
    {
        return -1;
    }
    for(loop_through_fd = 0; loop_through_fd < FDARRAY_MAX; loop_through_fd++)
    {
        for(loop_through_dentry = 0; loop_through_dentry < 63; loop_through_dentry++)
        {
            read_dentry_by_index(loop_through_dentry, current);
            if (filed_array[loop_through_fd].inode == current->inode_index)
            {
                filename = (char*) &current->file_name;
            }
        }
    }
    filed_array[i].flags = 0;
    return filed_array[i].fileot_pointer->close(filename);
}

/*
 *  int32_t read(int32_t offset, void* buf, int32_t n)
 *  DESCRIPTION: call the read function        
 *  INPUTS: int32_t offset  - the index for the file array
 *          void* buf  - the buffer to store the data read
 *          int32_t n - the number of how many bytes need to read
 *  OUTPUTS: read according  to the file
 *  RETURN VALUE: -1 if fail, the corresponding offset if success
 */
int32_t read(uint32_t offset, void* buf, int32_t n){
    /* check if the arguments is valid*/
    int buf_address_start = (int) buf ;
    int buf_address_end = buf_address_start + n;
    if(buf_address_start < USER_MEM){  return -1; }
    if(buf_address_end > USER_END){ return -1; } // #define USER_END 0x8400000
    if(offset >= 7 || offset < 0 || offset == 1){ return -1; } // 7 is the size of file array
    if(filed_array[offset].flags == 0){ return -1; }

    /* now we can read file by the position into the buf*/
    if (offset == 0 || offset == 1)
    {
        return filed_array[offset].fileot_pointer->read("terminal", filed_array[offset].position, buf, n);
    }

    // 63 is the max number of the file list
    return filed_array[offset].fileot_pointer->read("file", offset, buf, n);
}

/*
 *  int32_t write(uint32_t offset, void* buf, int32_t n)
 *  DESCRIPTION: call the write function        
 *  INPUTS: int32_t offset  - the index for the file array
 *          void* buf  - the buffer to store the data read
 *          int32_t n - the number of how many bytes need to read
 *  OUTPUTS: write according from the file
 *  RETURN VALUE: -1 if fail, the corresponding offset if success
 */ 
int32_t write(uint32_t offset, void* buf, int32_t n){
    /* check if the arguments is valid*/
    /* almost same thing as the read function*/
    int buf_address_start = (int) buf ;
    int buf_address_end = buf_address_start + n;
    if(buf_address_start < USER_MEM){  return -1; }
    if(buf_address_end > USER_END){ return -1; } // #define USER_END 0x8400000
    if(offset >= 7 || offset < 0 || offset == 0){ return -1; } // 7 is the size of file array
    if(filed_array[offset].flags == 0){ return -1; }
    /* check if the ptr supports write*/
    int32_t ifwrite;
    ifwrite = (int32_t)filed_array[offset].fileot_pointer->write;
    if(ifwrite == 0){ return -1; } //doesn't support write function
    /* now we can read file by the position into the buf*/
    return filed_array[offset].fileot_pointer->write(offset,buf,n); // file system here
}

/* 
 * getfdarray
 *   DESCRIPTION: get fdarray
 *   INPUTS: none
 *   OUTPUTS: none
 *   RETURN VALUE: void 
 *   SIDE EFFECTS: none
 */
fdarray *getfdarray()
{
    return filed_array;
}


int32_t get_cur_pid()
{
    return cur_pid;
}

void set_cur_pid(int32_t pid)
{
    cur_pid = pid;
    return;
}

void set_cur_process_ptr(PCB_t* process_ptr)
{
    cur_process_ptr = process_ptr;
    return;
}
