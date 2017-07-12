<h1> Note that </h1>
1. There exist inode for memory and dinode for disk. inode may have extra information but if you change one you should consider the other to be fit into your system. In this project, a change of the block structure would affect.  
2. The number of blocks per inode, the maximum file size are increased. You may have to change some constant to scale those things.

<h1> What should I change? </h1>
Assuming xv6 codes has good readability and writability, it is clear that the lines I should change would contain *NDIRECT*. I found the lines containing *NDIRECT* by cscope and could see a line in *file.h* and many lines in *fs.c*, *fs.h*, and *mkfs.c*.

<h2> size of inode </h2>
There are two graceful important assertions that  
`assert((BSIZE % sizeof(struct dinode)) == 0)`  
`assert((BSIZE % sizeof(struct dirent)) == 0)`  
To maximize the utility of the given disk space(to reduce extra fragmentation), xv6 forces the size of struct dinode(and inode) to be a power of 2, which means I have to replace an existing direct pointer with a double indirect pointer, or create another 63 pointers to respect the xv6 file system. I thought the former would be much better so changed *NDIRECT* (=12) to 11 and "uint addrs[NDIRECT+1]" to "uint addrs[NDIRECT+1+1]" in *struct inode* (in *file.h*) and *struct dinode* (in *fs.h*).  

In *fs.h*, there is only one more thing to be discussed, *MAXFILE*, which is declared as (NDIRECT + NINDIRECT). I could see that this variable (formally macro) denotes the maximum blocks in *inode* and added by *NINDIRECT* * *NINDIRECT* due to the double indirection inode.
  
Now we have only *fs.c* and *mkfs.c*

There are three functions that refers to *NDIRECT* variable.
<h2> bmap, itrunc, iappend </h2>
Thankfully, there already all exist the codes for single indirection. I just copied that and write the code to search one more depth to reach the block.

<h1> Panic! </h1>
The test program seems to have been working but stoped with panic "out of block". Tracing the frames I found that it is related to sb.size (the size of superblock) set to FSSIZE.
The existing file system could endure the large size up to 512000 which is slightly less than 512KB.
I calculated maximum block size as below (in Performance) but TA said that a single file would be up to 512KB in the test and recommended to set FSSIZE as 2000 in "3. Tips" . Therefore I set it to 2000 and could pass whole test

<h1> Performance </h1>
|   	| Single Indirection  	|   Double Indirection	|  Improved |
|---	|---	|---	|--- |
|  # of blocks  	|   12(direct) + 512/4(single indirect) = 140 (~70KB)  | 11 (direct) + 512 / 4 (single indirect) + (512/4) * (512/4) = 16523 (~8.07MB) | 1143%|
|   Access time(when full) 	|   (12 + 2 * 128)/140 = 1.91 |  ( 11 + 2 * 128+ 3* 128 * 128) / (11 + 128 + 128 * 128) = 2.99	|  -56.45% |
|   Space efficiency - Inode proportion( when full ) 	|   (140 * 512) /(64+512) = 0.803% |   (64 + 512 + 512 + 512*128) / ( 11 + 128+ 128 * 128) = 0.787%	| 1.99%, 0.016%p |

Note that access time and Inode proportion for double indirection would be gradually increased/decreased from 1.91 to 2.99 and from 0.803% to 0.787% as the blocks are filled.


