# Stride Scheduler
The notion, "stride", is a beautiful way to overcome inaccuracy of lottery ticketing.
It picks up the process who has the lowest pass so far and advances the pass by stride. I use a linear method for this popping operation, although it can be popped from in priority queue in *O(1)* and rearranged in *O(lg N)*, because the number of processes (NPROC) is not so large and it might be slower in terms of assembly execution (memory caching or accessing).

## Dynamic Allocation
To deal with a problem caused by the dynamic participation of the processes, I will use a variable *ss_pass* to maintain current pass for schedule. It advances *unit strider(stride1)* / ss_tickets* per every quantum.

## Dynamic Ticket Modification
*Ticket*, which is inversely proportional to stride, can be modified. An important thing is that ptable.ss_tickets should be less or equal to 80.


<br>
# MLFQ
As the professor required, 3-level feedback queue is used, time quantum will be proportional to 1:2:4, each level of the queue will adopt RR, and priority boost will be done periodically (100t) to prevent starvation. As doing priority boost, all the processes in every queue of MLFQ would be moved to the highest-level queue. *yield()* function is always called after ending execution in MLFQ, so that whether the process has fully consumed its given quantum should be checked before.
I didn't any linear ADTs but new features in *struct proc* because neither most ADTs nor dynamic allocation is avoided in kernel implementation.

# Boost Up
It is boosted up for every 100 quantum(=timer interrupt) and checked by a formula *ticks % BOOSTUP_PERIOD == 0*. This is not a lottery because it is continuously checked as *ticks* increases.


<br>
# Combine SS with MLFQ
## 20% vs 80%
Stride Scheduling can be used recursively by considering ticket currencies. Therefore, I will use it for adjusting the proportion between MLFQ and the child stride-scheduler which the project requires. The term *"at most" 80%* would be kept by following concepts.


ptable.mlfq_stride, ptable.ss_stride - Root SS (competition between schedulers at least 20% vs at most 80%)
ptable.ss_pass - expected_pass in Sub-SS (competition between processes), any processes whose *ss_ticket* is smaller than *ss_pass* would be chosen by Sub-SS
proc.ss_ticket - 1 ticket guarantees 1% of CPU



## How to determine the amount of the ticket in SS
Increased y the parameter provided by system call
Reduced by exit(), kill()

## How to penalize the process who game the MLFQ
Advised NOT to do in this project.
<br>
# Loss of precision
We can barely do divisions precisely in the computer. It is encouraged to use a large value for *unit_stride* to reduce the amount of error, however, it requires complex implementation not related to this class or to install some libraries, which may be not allowed. Therefore, I just used integer, relatively smaller than recommended. It is recommended to set (unsigned) long long.

# How if there are no processes?
The professor said that there will be some processes related to kernel. I thought it is also a good concept of having an idle process like Windows but turned out to be needless.