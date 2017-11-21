# MLFQ & Scheduling
## Stride Scheduler
The notion, "stride", is a beautiful way to overcome inaccuracy of lottery ticketing.
It picks up the process who has the lowest pass so far and advances the pass by stride. I use a linear method for this popping operation, although it can be popped from in priority queue in *O(1)* and rearranged in *O(lg N)*, because the number of processes (NPROC) is not so large and it might be slower in terms of assembly execution (memory caching or accessing).

### Dynamic Allocation
To deal with a problem caused by the dynamic participation of the processes, I will use a variable *ss_pass* to maintain current pass for schedule. It advances *unit strider(stride1)* / ss_tickets* per every quantum.

### Dynamic Ticket Modification
*Ticket*, which is inversely proportional to stride, can be modified. An important thing is that ptable.ss_tickets should be less or equal to 80.


<br>
## MLFQ
As the professor required, 3-level feedback queue is used, time quantum will be proportional to 1:2:4, each level of the queue will adopt RR, and priority boost will be done periodically (100t) to prevent starvation. As doing priority boost, all the processes in every queue of MLFQ would be moved to the highest-level queue. *yield()* function is always called after ending execution in MLFQ, so that whether the process has fully consumed its given quantum should be checked before.
I didn't any linear ADTs but new features in *struct proc* because neither most ADTs nor dynamic allocation is avoided in kernel implementation.

## Boost Up
It is boosted up for every 100 quantum(=timer interrupt) and checked by a formula *ticks % BOOSTUP_PERIOD == 0*. This is not a lottery because it is continuously checked as *ticks* increases.


<br>
## Combine SS with MLFQ
### 20% vs 80%
Stride Scheduling can be used recursively by considering ticket currencies. Therefore, I will use it for adjusting the proportion between MLFQ and the child stride-scheduler which the project requires. The term *"at most" 80%* would be kept by following concepts.


ptable.mlfq_stride, ptable.ss_stride - Root SS (competition between schedulers at least 20% vs at most 80%)
ptable.ss_pass - expected_pass in Sub-SS (competition between processes), any processes whose *ss_ticket* is smaller than *ss_pass* would be chosen by Sub-SS
proc.ss_ticket - 1 ticket guarantees 1% of CPU



### How to determine the amount of the ticket in SS
Increased y the parameter provided by system call
Reduced by exit(), kill()

### How to penalize the process who game the MLFQ
Advised NOT to do in this project.
<br>
## Loss of precision
We can barely do divisions precisely in the computer. It is encouraged to use a large value for *unit_stride* to reduce the amount of error, however, it requires complex implementation not related to this class or to install some libraries, which may be not allowed. Therefore, I just used integer, relatively smaller than recommended. It is recommended to set (unsigned) long long.

### How if there are no processes?
The professor said that there will be some processes related to kernel. I thought it is also a good concept of having an idle process like Windows but turned out to be needless.


# LWP
## Definitions
process - initproc or its page directory is not the same as its parent's  
thread - its page directory is the same as its parent's 

## Memory structure

———————  
heap  
———————   
thread stack  
..  
..  
NTHREAD(=16)  
———————  
process stack  
———————  
code  
———————  


### Why should we place thread stacks there?
The heap area is allocated by users. For the convenience of managing the heap, we already have *malloc()* function in *umalloc.c*. Since *umalloc.c* is not a part of kernel (you may see *Makefile*), you cannot allocate the stack of *thread* once user process gets a control and allocate the heap memory. Therefore you have to reserve area for thread stacks, but it does not mean that you would allocate physical memory for that area.

### Now, memory is  cheap.
For the memory efficiency, I implemented to allocate and deallocate for every creation or termination of threads, however, it required much more lines and locks. It would make a considerable difference in which threads are frequently made or destroyed, for example, a database system or a webserver. To make it **light**, I decided to allocate all thread stacks as making process with extra costs of NTHREAD(16) * PGSIZE(4KB) =  64KB and few cycles in a procedure of making *process*.

### We stores related information in .. 
The information - which stack has been used or not, virtual memory base of each thread stack - is stored in *struct proc* only of *process*.

## Functions
### fork()
In *process*, copy all threads. You were enough to check the return value of allocproc() without LWP to know that there are UNUSED proc structure in *ptable*. But now, you have to check there are as many as its threads, which means you have to store the number of threads of a process and remaining *proc* structure in *ptable*.  
In *thread*, copy the calling thread only. It means we do CoW but make only the calling thread active.

### exec() </h2>
In *process*, terminate all threads and clear resources.
In *thread*, it becomes child process.

### pthread_create() </h2>
We now have a concurrency issue with this function.
This function can be called by both *process* and *thread*.

### wait() </h2>
If it is called by a process, do as usual.
If it is called by a thread, the thread pretends to be process and clean up the other threads and the process.


## Changes in proc.c </h1>
### void exit(void) </h2>
It is inevitable not to change* exit() *at all since, in the *start_routine*, we have to do the callback procedure even though users write not only *thread_exit()* but "*return [retval]*" as well. It means that after escaping out from the *start_routine*, *eip*(*PC*) should go to exit().
<br>

### thread_create() </h2>
This function should be alike *fork()* and *exec()*.  
Also, it should allocate separated user stack for a thread. I got it from heap area by using *allocuvm()*  

After allocating the pages, the user stack should be filled with arguments to be immediately executed after switching.  
First of all, you have to know that this function will not go to other functions but do return. Thus, the highest word should be filled with *arg*, and the second highest word should be filled with meaningless value but you should move *esp* by 2 words ( 8bytes ). After that, simply set ebp as *esp*, and *eip* as *start_routine* like we intended.  

The other lines are needed for the same reason as you fork or exec process.  

### thread_exit() </h2>
You should put return value into *eax* due to xv6 convention.
<br>

### thread_join() </h2>
The process we would like to choose is the one whose tid is the same as *thread*. However, an adversary can put *thread* which he does not owe, you should check that the *pgdir* is not different.
The other things are almost same as wait().

### Following properties are consistent only within a process, not in a thread. </h2>
pgdir, killed, thread, stride_tickets, stride_pass, ofile, cwd, sz.  
Thus, I changed all the references to them in sysproc, trap, proc, exec, etc to refresh every changes in a process.

# File System
## Note that 
1. There exist inode for memory and dinode for disk. inode may have extra information but if you change one you should consider the other to be fit into your system. In this project, a change of the block structure would affect.  
2. The number of blocks per inode, the maximum file size are increased. You may have to change some constant to scale those things.

## Things to change
Assuming xv6 codes has good readability and writability, it is clear that the lines I should change would contain *NDIRECT*. I found the lines containing *NDIRECT* by cscope and could see a line in *file.h* and many lines in *fs.c*, *fs.h*, and *mkfs.c*.

### size of inode
There are two graceful important assertions that  
`assert((BSIZE % sizeof(struct dinode)) == 0)`  
`assert((BSIZE % sizeof(struct dirent)) == 0)`  
To maximize the utility of the given disk space(to reduce extra fragmentation), xv6 forces the size of struct dinode(and inode) to be a power of 2, which means I have to replace an existing direct pointer with a double indirect pointer, or create another 63 pointers to respect the xv6 file system. I thought the former would be much better so changed *NDIRECT* (=12) to 11 and "uint addrs[NDIRECT+1]" to "uint addrs[NDIRECT+1+1]" in *struct inode* (in *file.h*) and *struct dinode* (in *fs.h*).  

In *fs.h*, there is only one more thing to be discussed, *MAXFILE*, which is declared as (NDIRECT + NINDIRECT). I could see that this variable (formally macro) denotes the maximum blocks in *inode* and added by *NINDIRECT* * *NINDIRECT* due to the double indirection inode.
  
Now we have only *fs.c* and *mkfs.c*

There are three functions that refers to *NDIRECT* variable.
### bmap, itrunc, iappend
Thankfully, there already all exist the codes for single indirection. I just copied that and write the code to search one more depth to reach the block.

## Panic!
The test program seems to have been working but stoped with panic "out of block". Tracing the frames I found that it is related to sb.size (the size of superblock) set to FSSIZE.
The existing file system could endure the large size up to 512000 which is slightly less than 512KB.
I calculated maximum block size as below (in Performance) but TA said that a single file would be up to 512KB in the test and recommended to set FSSIZE as 2000 in "3. Tips" . Therefore I set it to 2000 and could pass whole test

## Performance
|   	| Single Indirection  	|   Double Indirection	|  Improved |
|---	|---	|---	|--- |
|  # of blocks  	|   12(direct) + 512/4(single indirect) = 140 (~70KB)  | 11 (direct) + 512 / 4 (single indirect) + (512/4) * (512/4) = 16523 (~8.07MB) | 1143%|
|   Access time(when full) 	|   (12 + 2 * 128)/140 = 1.91 |  ( 11 + 2 * 128+ 3* 128 * 128) / (11 + 128 + 128 * 128) = 2.99	|  -56.45% |
|   Space efficiency - Inode proportion( when full ) 	|   (140 * 512) /(64+512) = 0.803% |   (64 + 512 + 512 + 512*128) / ( 11 + 128+ 128 * 128) = 0.787%	| 1.99%, 0.016%p |

Note that access time and Inode proportion for double indirection would be gradually increased/decreased from 1.91 to 2.99 and from 0.803% to 0.787% as the blocks are filled.
