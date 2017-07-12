<h1> Definitions </h1>
process - initproc or its page directory is not the same as its parent's  
thread - its page directory is the same as its parent's 

<h1> Memory structure </h1>

???????  
heap  
???????   
thread stack  
..  
..  
NTHREAD(=16)  
???????  
process stack  
???????  
code  
???????  


<h2> Why should we place thread stacks there? </h2>
The heap area is allocated by users. For the convenience of managing the heap, we already have *malloc()* function in *umalloc.c*. Since *umalloc.c* is not a part of kernel (you may see *Makefile*), you cannot allocate the stack of *thread* once user process gets a control and allocate the heap memory. Therefore you have to reserve area for thread stacks, but it does not mean that you would allocate physical memory for that area.

<h2> Now, memory is  cheap. </h2>
For the memory efficiency, I implemented to allocate and deallocate for every creation or termination of threads, however, it required much more lines and locks. It would make a considerable difference in which threads are frequently made or destroyed, for example, a database system or a webserver. To make it **light**, I decided to allocate all thread stacks as making process with extra costs of NTHREAD(16) * PGSIZE(4KB) =  64KB and few cycles in a procedure of making *process*.

<h2> We stores related information in .. </h2>
The information - which stack has been used or not, virtual memory base of each thread stack - is stored in *struct proc* only of *process*.

<h1> Functions </h1>
<h2> fork() </h2>
In *process*, copy all threads. You were enough to check the return value of allocproc() without LWP to know that there are UNUSED proc structure in *ptable*. But now, you have to check there are as many as its threads, which means you have to store the number of threads of a process and remaining *proc* structure in *ptable*.  
In *thread*, copy the calling thread only. It means we do CoW but make only the calling thread active.

<h2> exec() </h2>
In *process*, terminate all threads and clear resources.
In *thread*, it becomes child process.

<h2> pthread_create() </h2>
We now have a concurrency issue with this function.
This function can be called by both *process* and *thread*.

<h2> wait() </h2>
If it is called by a process, do as usual.
If it is called by a thread, the thread pretends to be process and clean up the other threads and the process.


<h1> Changes in proc.c </h1>
<h2> void exit(void) </h2>
It is inevitable not to change* exit() *at all since, in the *start_routine*, we have to do the callback procedure even though users write not only *thread_exit()* but "*return [retval]*" as well. It means that after escaping out from the *start_routine*, *eip*(*PC*) should go to exit().
<br>

<h2> thread_create() </h2>
This function should be alike *fork()* and *exec()*.
Also, it should allocate separated user stack for a thread. I got it from heap area by using *allocuvm()*
<br>
<br>
After allocating the pages, the user stack should be filled with arguments to be immediately executed after switching.
First of all, you have to know that this function will not go to other functions but do return. Thus, the highest word should be filled with *arg*, and the second highest word should be filled with meaningless value but you should move *esp* by 2 words ( 8bytes ). After that, simply set ebp as *esp*, and *eip* as *start_routine* like we intended.
<br>
The other lines are needed for the same reason as you fork or exec process.
<br>
<h2> thread_exit() </h2>
You should put return value into *eax* due to xv6 convention.
<br>
<h2> thread_join() </h2>
The process we would like to choose is the one whose tid is the same as *thread*. However, an adversary can put *thread* which he does not owe, you should check that the *pgdir* is not different.
The other things are almost same as wait().

<h2> Following properties are consistent only within a process, not in a thread. </h2>
pgdir, killed, thread, stride_tickets, stride_pass, ofile, cwd, sz.  
Thus, I changed all the references to them in sysproc, trap, proc, exec, etc to refresh every changes in a process.