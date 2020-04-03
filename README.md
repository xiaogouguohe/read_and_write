# 读者写者问题的三种打开方式

今天遇到了操作系统的世 界 名 题：读者写者问题。经过我一两天的不懈努力，我终于没能想出来，再经过一两天的不懈努力，我终于勉勉强强地看懂了别人写的是什么。在这里，我会把我自己写的版本放上来，然后尽可能地解释清楚，希望我的解释能说服我自己。

总所周知，读者写者问题有三种打开方式：读者优先，公平竞争和写者优先。我们一个一个往下看，先看一下最简单的读者优先。

## 1 读者优先
```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#define M_SIZE 1024    //共享内存的大小
#define SLEEP_TIME 1    //睡眠时间
#define R_SIZE 10    //读者进程的个数
#define W_SIZE 10    //写者进程的个数
#define RW_SIZE 20    //进程总个数

struct SHM
{
	sem_t rc_sem;    //rc的互斥锁
	sem_t db_sem;    //内存的互斥锁，负责读写互斥和写写互斥
	int rc;    //正在访问内存的读者进程的个数
	int value;    //内存里的值，写进程改变的就是这个值
};

void shmInit(struct SHM* shm_p)    //初始化结构体SHM
{
	sem_init(&shm_p->rc_sem, 1, 1);
	sem_init(&shm_p->db_sem, 1, 1);
	shm_p->rc = 0;
	shm_p->value = 0;
}

void shmDestroy(struct SHM* shm_p)    //养成好的编程习惯，垃圾回收
{
	sem_destroy(&shm_p->rc_sem);
	sem_destroy(&shm_p->db_sem);
}

void readerFunc(struct SHM* shm_p)    //读者
{
	while(1)    //每个读者都是不断地读
	{
		sem_wait(&shm_p->rc_sem);    
			//这个读者进程想要访问内存，所以要修改rc，上互斥锁
		if (++shm_p->rc == 1)    	
			//若这是第一个读者，内存的锁，禁止写进程访问内存
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);

		printf("%s, %ld, %s, %d\n", "process", getpid(), "read", 
			shm_p->value);	//表示成功读取
		sleep(SLEEP_TIME);    //强制延长读进程的时间，方便观察输出

		sem_wait(&shm_p->rc_sem);    //这个读者进程访问完内存，所以要
									//修改rc，上互斥锁
		if (--shm_p->rc == 0)	
			//若这是最后一个读者，打开内存的锁，某个动作快的写进程可以访
			//问内存，然后给内存上锁，禁止其它写进程访问。
		{
			sem_post(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);
	}
}

void writerFunc(struct SHM* shm_p)
{
	while(1)
	{	
		sem_wait(&shm_p->db_sem);    //写进程访问内存，上锁
		shm_p->value = shm_p->value ^ 1;    //异或
		printf("write successfully\n");
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);
	}
}

int main()
{
	int shm_id = shmget(IPC_PRIVATE, M_SIZE, IPC_CREAT | 0600);    
		//申请共享内存    
	if (shm_id == -1)    //申请失败    
	{
		perror("shmget error");
		return -1;
	}
	
	struct SHM *shm1_p = shmat(shm_id, NULL, 0);    
		//得到访问共享内存的权限
	if (shm1_p == (void*) -1)
	{
		perror("shmat error");
		return -1;
	}

	shmInit(shm1_p);    //初始化shm1_p
	
	pid_t pid;
	int i;
	for (i = 0; i < RW_SIZE; ++i)	
	{
		pid = fork();
		if (pid == -1)    //创建子进程失败
		{
			perror("fork fail!");
			exit(1);
		}

		if (pid == 0) { break; }    //防止子进程也创建自己的子进程

	}
	
	if (i < R_SIZE)    //读者进程
	{
		readerFunc(shm1_p);
	}
	else if (i < RW_SIZE)    //写者进程
	{
		writerFunc(shm1_p);
	}

	shmDestroy(shm1_p);
	return 0;
}
```

以上是读者优先的代码。这个其实不算很难想到，值得注意的地方是我们怎么去实现可以多个读者一起读，但是读和写进程不能同时访问这个内存呢？如果我们能够检测某个读者进程是不是当前唯一一个访问共享内存的读者进程，问题就变得很简单了。因为如果它是唯一一个当前访问共享内存的读者进程，就有两种可能：

第一，它是第一个访问共享内存的读者进程。在此之前，共享内存没有上锁，因此这个进程需要给共享内存上锁，通过以下代码段实现：

```c
sem_wait(&shm_p->rc_sem);    
	//这个读者进程想要访问内存，所以要修改rc，上互斥锁
	if (++shm_p->rc == 1)    	
		//若这是第一个读者，内存的锁，禁止写进程访问内存
	{
		sem_wait(&shm_p->db_sem);
	}
sem_post(&shm_p->rc_sem);

```

这个设计是很巧妙的，一方面，在某个读进程给共享内存上锁后，后来的读进程是不会被锁住的，因为它们访问内存的时候，rc的值是大于1的，因此不满足if (++shm_p->rc == 1)，也就不会被阻塞在sem_wait(&shm_p->db_sem)这里；另一方面，写进程的的确确是被阻塞了。还记得写进程的代码吗？

```c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{	
		sem_wait(&shm_p->db_sem);    //写进程访问内存，上锁
		shm_p->value = shm_p->value ^ 1;    //异或
		printf("write successfully\n");
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);
	}
}
```

写进程被阻塞在了sem_wait(&shm_p->db_sem)这里。

第二种情况，它是最后一个访问共享内存的读者进程，因此它要给共享内存解锁，也就是这段代码：

```c
sem_wait(&shm_p->rc_sem);    
	//这个读者进程访问完内存，所以要修改rc，上互斥锁
if (--shm_p->rc == 0)	
	//若这是最后一个读者，打开内存的锁，某个动作快的写进程可以访问内存，
	//然后给内存上锁，禁止其它写进程访问。
{
	sem_post(&shm_p->db_sem);
}
sem_post(&shm_p->rc_sem);
```

以上就是如果当前只有一个读者进程访问内存的两种情况。

读者优先实现了读读共享，读写互斥，写写互斥，但是这个算法其实是存在很大问题的。想象一下，我写的这段代码里有十个读者进程和十个写者进程，然后某个时候，有个读者进程访问了内存，而且它是第一个访问内存的读者进程，之前没有读者进程访问过内存，或者说，就算有，现在也退出了。那么这个第一个访问内存的读者进程会给内存上锁，写进程就都进不来了，但是读进程还能进来。于是，过了一段时间以后，读进程都进来访问内存了，这个时候第一个读进程还在访问着内存。再过一段时间，第一个进程访问完内存，退出去了，然后......还记得我们表示一个读者的函数是怎么写的吗？

```c
void readerFunc(struct SHM* shm_p)    //读者
{
	while(1)    //每个读者都是不断地读
	sem_wait(&shm_p->rc_sem);    
		//这个读者进程想要访问内存，所以要修改rc，上互斥锁
	if (++shm_p->rc == 1)    	
		//若这是第一个读者，内存的锁，禁止写进程访问内存
	{
		sem_wait(&shm_p->db_sem);
	}
	sem_post(&shm_p->rc_sem);
	......
```
这个读者退出去以后，在while(1)的作用下，又去访问内存了，这个时候另外的进程都还在上一个循环中访问内存。因此读进程霸占了内存，写进程阻塞在sem_wait(&shm_p->db_sem)这条语句这里苦苦等待。也就是说，一旦读进程访问了内存，就没写进程什么事了。这显然不是我们想看到的。

怎么解决这个问题呢？接下来，我们会看到公平竞争和写者优先的策略。

## 2 公平竞争

在上一部分中，我们实现了读者写者问题中的读者优先策略，也提及了读者优先存在的问题，即写者饥饿。在这一部分中，我们会实现公平竞争策略。先看代码：

### 2.1 代码

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#define M_SIZE 1024
#define SLEEP_TIME 1
#define R_SIZE 10
#define W_SIZE 10
#define RW_SIZE 20

struct SHM
{
	sem_t rc_sem;    
	sem_t db_sem;    
	sem_t r1_or_w1_sem;    //
	int rc;
	int value;
};

void shmInit(struct SHM* shm_p)
{
	sem_init(&shm_p->rc_sem, 1, 1);
	sem_init(&shm_p->db_sem, 1, 1);
	sem_init(&shm_p->r1_or_w1_sem, 1, 1);    //比读者优先多一个信号量
	shm_p->rc = 0;
	shm_p->value = 0;
}

void shmDestroy(struct SHM* shm_p)
{
	sem_destroy(&shm_p->rc_sem);
	sem_destroy(&shm_p->db_sem);
	sem_destroy(&shm_p->r1_or_w1_sem);
}

void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);    
		sem_wait(&shm_p->rc_sem);
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);
		sem_post(&shm_p->r1_or_w1_sem);

		printf("%d, %ld\n", shm_p->value, getpid());
		sleep(SLEEP_TIME);
		sem_wait(&shm_p->rc_sem);
		--shm_p->rc;
		if (shm_p->rc == 0)
		{
			sem_post(&shm_p->db_sem);
		}	
		sem_post(&shm_p->rc_sem);

	}
}

void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);	
		sem_wait(&shm_p->db_sem);
		shm_p->value = shm_p->value ^ 1;
		printf("%s, %ld\n", "write", getpid());
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);
		sem_post(&shm_p->r1_or_w1_sem);
	}
}



int main()
{
	int shm_id = shmget(IPC_PRIVATE, M_SIZE, IPC_CREAT | 0600);    
	if (shm_id == -1)    
	{
		perror("shmget error");
		return -1;
	}
	
	struct SHM *shm1_p = shmat(shm_id, NULL, 0);
	if (shm1_p == (void*) -1)
	{
		perror("shmat error");
		return -1;
	}
	shmInit(shm1_p);
	
	pid_t pid;
	int i;
	for (i = 0; i < RW_SIZE; ++i)	
	{
		pid = fork();
		if (pid == -1)
		{
			perror("fork fail!");
			exit(1);
		}

		if (pid == 0) { break; }

	}
	
	if (i < R_SIZE)
	{
		readerFunc(shm1_p);
	}
	else if (i < RW_SIZE)
	{
		writerFunc(shm1_p);
	}
	else
	{
		for (int i = 0; i < RW_SIZE; ++i)
		{
			wait(NULL);
		}
		shmDestroy(shm1_p);
	}
	return 0;
}

```

### 2.2 比起读者优先策略，改动在哪里？
和读者优先的代码对比一下，公平竞争做了哪些改动呢？

首先，公平竞争多了一个信号量：

```c
void shmInit(struct SHM* shm_p)
{
	sem_init(&shm_p->rc_sem, 1, 1);
	sem_init(&shm_p->db_sem, 1, 1);
	sem_init(&shm_p->r1_or_w1_sem, 1, 1);    //比读者优先多一个信号量
	shm_p->rc = 0;
	shm_p->value = 0;
}
```

这个信号量r1_or_w1_sem的作用是什么？我们看一下这个信号量在哪些地方被用到了：

```c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);     //   
		sem_wait(&shm_p->rc_sem);
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);
		sem_post(&shm_p->r1_or_w1_sem);    //
		......
	}
}
```

```c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);	    //
		sem_wait(&shm_p->db_sem);
		shm_p->value = shm_p->value ^ 1;
		printf("%s, %ld\n", "write", getpid());
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);
		sem_post(&shm_p->r1_or_w1_sem);    //

		sleep(SLEEP_TIME);
	}
}
```

可以看到，r1_or_w1_sem这个信号量，在表示读者和写者的函数readerFunc和writerFunc中都发挥了作用。还记得在读者优先策略中，写者为什么会被饥饿吗？因为一旦有一个读者进程访问了共享内存，写者进程就会被阻塞，而其它读者进程不会被阻塞。而某个读者进程访问完内存后（也可以说退出临界区后，不知道临界区这个概念的话不会对理解有任何的影响，直接忽略掉这个括号内的内容就可以了），在其它读者进程还没有结束访问内存时，它又开始访问内存。也就是说，这段共享内存一直有很多个读者进程在访问，因此写者进程就一直被阻塞。

### 2.3 读者进程访问内存以后，其他进程是否机会均等？
而加上re_or_w1_sem这个信号量之后，能不能解决这个问题？有人说，**大脑就是最好的编译器（这个真不是我说的，事实上我不是完全认同这个说法）**，不管怎么样，我们先在大脑里过一遍我们的代码。假设我们刚开始运行我们的代码，这个时候就是读者进程和写者进程赛跑了：

```c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);    //赛跑
		sem_wait(&shm_p->db_sem); 
		......
```
```c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);    //赛跑
		sem_wait(&shm_p->db_sem);
		......
```

十个读者和十个写者进程，谁先拿到r1_or_w1_sem这个信号量，谁就可以访问内存（因为开始的时候谁都没有访问过共享内存，所以db_sem是肯定没锁的，阻塞不住任何进程）。现在假设读者进程跑得更快（事实上，谁跑得更快这个不是用户能决定的，而且每次结果都是不可复现的），于是它改变rc的值，并且对写进程锁上数据库，然后放出r1_or_w1_sem这个信号量，实现的代码段如下：

```c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);     //   
		sem_wait(&shm_p->rc_sem);
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);
		sem_post(&shm_p->r1_or_w1_sem);    //
		......
	}
}
```

等等，为什么不在这个进程读完了内存的时候再释放r1_or_w1_sem这个信号量，而是在改变了rc值之后并且在读内存之前就释放这个信号量？想象一下，如果我们在读完内存以后再释放r1_or_w1_sem这个信号量，那么在这个读进程读完内存以前，其它的读进程都被阻塞在了sem_wait(&shm_p->r1_or_w1_sem)这条语句这里，也就是说违反了读读共享原则，这显然违背了我们的本意。

那么，r1_or_w1_sem这个信号量的释放，放在读内存之前，会不会出现问题？我们看一下在这个读者进程释放这个信号量的前一刻，其它进程都在干嘛。其余的读进程和写进程都被阻塞在了sem_wait(&shm_p->r1_or_w1_sem)这里，所以这又是和之前一样的赛跑，每个进程都有均等的机会获得这个r1_or_w1_sem这个信号量。如果这次还是某个读者进程获得这个信号量，那情况和我们上面分析的一样；而如果是一个写者进程获得了这个信号量，会发生什么事？看看下面的代码段：

```c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r1_or_w1_sem);        
		sem_wait(&shm_p->rc_sem);
		......
	}
}
```

```c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		......
		if (shm_p->rc == 0)
		{
			sem_post(&shm_p->db_sem);
		}	
		......
	}
}
```

写者进程获得r1_or_w1_sem这个信号量之后，其它的读者写者进程依旧被阻塞在sem_wait(&shm_p->r1_or_w1_sem)这里，只有这个写者进程比它们快一步。如果这个时候还有读者进程在访问内存，那么写者进程就会被阻塞在sem_wait(&shm_p->rc_sem)这里，等待最后一个访问内存的读者进程都退出后，最后一个读者进程会放出db_sem这个信号量，而这个时候因为其它进程都被阻塞在sem_wait(&shm_p->r1_or_w1_sem)这里，只有这个写者进程拿到db_sem这个信号量，开始访问内存。

所以说，每一次某个读者进程访问完内存之后，其余进程，无论是读还是写，都有均等的机会访问内存，也就不会出现读者优先策略中，写者进程被饥饿的现象。

### 2.4 写者进程访问内存以后，其他进程是否机会均等？
我们刚才已经验证过，每个读者进程访问内存以后，其余进程访问内存的机会都是均等的。那么，某个写者进程访问完内存以后，其余进程是否有均等的机会访问内存呢?我们看一下某个写者进程访问完内存之后会发生什么。

某个写者进程访问完内存之后，就会放出信号量db_sem和r1_or_w1_sem，代码如下：

```c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		......
		printf("%s, %ld\n", "write", getpid());
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);
		sem_post(&shm_p->r1_or_w1_sem);
	}
}
```

释放这些信号量的前一刻，其余进程在干什么？事实上，它们都被阻塞在sem_wait(&shm_p->r1_or_w1_sem)这里，这又是一次公平竞争，所有进程访问内存的机会均等。

说了这么多，其实无非想说明一个问题：**无论是哪个进程访问完了内存，其余的进程访问内存的机会都是均等的**（当然，要是是某个写进程得到访问内存的机会，那么这个写进程必须等待所有还在访问内存的读进程访问完之后才轮到它访问。但是后面的进程，无论是读是写，都不会插到它前面去的）。

还有一个问题就是，如果我们想要写者进程的优先级比读者进程更高怎么办？这个问题，留到下一篇在做解答。而写者优先，难度又比现在这个公平竞争策略的难度还要大许多。但**千里之行始于足下，难的东西，我们一步步来，也就不难了。**

## 3 读者写者问题的三种打开方式——写者优先

上两篇我们分别实现了读者优先和公平竞争两种策略，如果没有特殊情况的话，这将会是这个系列的最后一篇，实现写者优先策略。老规矩，先上代码。

### 3.1 代码

```c
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/ipc.h>
#include <sys/shm.h>
#include <sys/wait.h>
#include <semaphore.h>
#include <time.h>

#define M_SIZE 1024
#define SLEEP_TIME 1
#define R_SIZE 10
#define W_SIZE 10
#define RW_SIZE 20

struct SHM
{
	sem_t rc_sem;    //rc的互斥锁
	sem_t wc_sem;    //wc的互斥锁
	sem_t db_sem;    //内存的互斥锁，负责读写互斥和写写互斥
	sem_t w_sem;    
	sem_t r_sem;
	int rc;    //正在访问内存的读者进程的个数
	int wc;    //正在访问内存的写者进程的个数
	int value;    //内存里的值，写进程改变的就是这个值
};

void shmInit(struct SHM* shm_p)    //初始化
{
	sem_init(&shm_p->rc_sem, 1, 1);
	sem_init(&shm_p->wc_sem, 1, 1);
	sem_init(&shm_p->db_sem, 1, 1);
	sem_init(&shm_p->r_sem, 1, 1);
	sem_init(&shm_p->w_sem, 1, 1);
	shm_p->rc = 0;
	shm_p->wc = 0;
	shm_p->value = 0;
}

void shmDestroy(struct SHM* shm_p)    //养成良好的编程习惯，垃圾回收
{
	sem_destroy(&shm_p->rc_sem);
	sem_destroy(&shm_p->wc_sem);
	sem_destroy(&shm_p->db_sem);
	sem_destroy(&shm_p->r_sem);
	sem_destroy(&shm_p->w_sem);
}

void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r_sem);
		sem_wait(&shm_p->w_sem);
		
		sem_wait(&shm_p->rc_sem);    //想改动rc的值，要给rc上锁
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);

		sem_post(&shm_p->w_sem);
		sem_post(&shm_p->r_sem);

		printf("%d, %ld\n", shm_p->value, getpid());
		sleep(SLEEP_TIME);

		sem_wait(&shm_p->rc_sem);
		--shm_p->rc;
		if (shm_p->rc == 0)
		{
			sem_post(&shm_p->db_sem);
		}	
		sem_post(&shm_p->rc_sem);

		//sleep(SLEEP_TIME);
	}
}

void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->wc_sem);    //想改动rc的值，要给rc上锁
		++shm_p->wc;
		if (shm_p->wc == 1)
		{
			sem_wait(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);
			
		sem_wait(&shm_p->db_sem);
		shm_p->value = shm_p->value ^ 1;
		printf("%s, %ld\n", "write", getpid());
		sleep(SLEEP_TIME);
		sem_post(&shm_p->db_sem);

		sem_wait(&shm_p->wc_sem);
		--shm_p->wc;
		if (shm_p->wc == 0)
		{
			sem_post(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);

		//sleep(SLEEP_TIME);
	}
}

int main()
{
	int shm_id = shmget(IPC_PRIVATE, M_SIZE, IPC_CREAT | 0600);    
	if (shm_id == -1)    
	{
		perror("shmget error");
		return -1;
	}
	
	struct SHM *shm1_p = shmat(shm_id, NULL, 0);
	if (shm1_p == (void*) -1)
	{
		perror("shmat error");
		return -1;
	}
	shmInit(shm1_p);
	
	pid_t pid;
	int i;
	for (i = 0; i < RW_SIZE; ++i)	
	{
		pid = fork();
		if (pid == -1)
		{
			perror("fork fail!");
			exit(1);
		}

		if (pid == 0) { break; }

	}
	
	if (i < R_SIZE)
	{
		readerFunc(shm1_p);
	}
	else if (i < RW_SIZE)
	{
		writerFunc(shm1_p);
	}
	else
	{
		for (int i = 0; i < RW_SIZE; ++i)
		{
			wait(NULL);
		}
		shmDestroy(shm1_p);
	}
	return 0;
}

```

### 3.2 试图用代码解决问题的三种结果
以上就是写者优先的实现代码。在这里，其实我还想说一下我们遇到一个需要用代码去解决的问题的时候，应该怎么去应对。从我自己的经历来看，我们遇到一个需要coding的问题，想去解决，最后无非三种结果：

第一，想出来解决办法，并且成功用代码变现。这自然是最好的，这个时候要做的事情，就是去看看别人的实现代码，看看和自己的有什么差别，自己当时是怎么想的，现在回想起来还有没有哪里可以改进的地方；

第二，想出来一个大概可行的办法，但是没能实现。这种情况也不少见的，比如有一次的OJ实验，就遇到过一道题，难度也不算特别大，我也想到了一个解决办法，而且也比较确定是可行的，但是因为当时不知道有线段树这种数据结构，所以后来去写了个线段树，问题就迎刃而解。还有其它种种原因导致没有通过代码实现的情况，每个人可能都会遇到不同的情况，这种时候就要好好想想自己没能用代码实现的原因，而且最后也要自己重新写一次，而不是看了别人的代码就完事；

第三，毫无头绪，完全不知道怎么入局，就像这次的情况一样。这种时候就只能去翻别人现成的了。但是除了翻完别人的以后，自己去实现一遍，其实还有能做的，那就是尝试着去还原一下，别人当时拿到这个问题的时候，是怎么一步一步想出来这个解决办法的。就是说，不仅要知道这个题怎么做，还要知道别人为什么能这样做出来。这么说可能很抽象，我们就以这次的问题为例。事先声明，对于这个问题，我是一点办法都没有的，完全是看别人的东西，下面我会试图还原别人的思路。

### 3.3 三种策略的结构体SHM的比较
我们把以前的东西也翻出来，和今天的比较一下：

```c
struct SHM    //读者优先
{
	sem_t rc_sem;    //rc的互斥锁
	sem_t db_sem;    //内存的互斥锁，负责读写互斥和写写互斥
	int rc;    //正在访问内存的读者进程的个数
	int value;    //内存里的值，写进程改变的就是这个值
};
```

```c
struct SHM    //公平竞争
{
	sem_t rc_sem;    
	sem_t db_sem;    
	sem_t r1_or_w1_sem;    
	int rc;
	int value;
};
```

先比较读者优先和公平竞争。我们要实现读写互斥和写写互斥，因此读者优先的结构体SHM有信号量db_sem；读和写进程对内存的访问体现在值value上，到时读和写都是对value操作；我们还要记录当前有几个读进程在读内存，因为第一个读进程要禁止写进程访问内存，最后一个读进程要开放写进程访问内存的权限，所以SHM还有一个变量rc；自然而然地就会有rc的信号量rc_sem，因为这个进程在改动rc的时候别的进程不能改动rc。这就是为什么读者优先策略里的结构体SHM会有这些东西。

然后，怎样做到公平竞争呢？在读者优先中，读进程一旦访问内存，写进程就被阻塞了，但是因为我们要保证读读共享，所以其余读进程还是可以访问内存。因此，我们可以说，在读者优先策略中， 读进程的优先级比写进程高，所以在公平竞争中，我们就要把它们拉回到同一优先级。那就还需要一个信号量r1_or_w1_sem，把读进程和写进程拉回同一地位。什么意思呢？r1_or_w1_sem的作用就是，在某个读进程放出这个信号量之前，其余所有进程都要等着，然后这个读进程放出来r1_or_w1_sem之后，其余所有进程公平竞争，拿到这个信号量的进程就可以访问内存（并不一定是马上可以访问内存，比如写进程拿到这个信号量的话就要等还在访问的读进程都访问完以后才轮到它，但反正它前面不再会有进程插队了）。因此我们可以说，读进程和写进程，在公平竞争策略里是平等的。

轮到写者优先了，这个策略想要实现在同时有读进程和写进程等待访问内存的情况下，写进程优先级更高，需要在公平竞争的基础上做出哪些改动呢？我们看一下这两者的结构体SHM：

```c
struct SHM    //公平竞争
{
	sem_t rc_sem;    
	sem_t db_sem;    
	sem_t r1_or_w1_sem;    
	int rc;
	int value;
};
```

```c
struct SHM
{
	sem_t rc_sem;    //rc的互斥锁
	sem_t wc_sem;    //wc的互斥锁
	sem_t db_sem;    //内存的互斥锁，负责读写互斥和写写互斥
	sem_t w_sem;    
	sem_t r_sem;
	int rc;    //正在访问内存的读者进程的个数
	int wc;    //正在访问内存的写者进程的个数
	int value;    //内存里的值，写进程改变的就是这个值
};

```

首先，在前面两种策略中，我们记录了当前有多少个读进程正在访问内存，但是我们是没有记录当前有多少个写进程正在等待访问内存的，因为我们并不关心这一点。而在写者优先策略中，如果我们想要写进程的优先级比读进程更高，我们就要引入一个信号量，这个信号量能发挥如下的作用：**对于一些正在等待访问内存的读进程和写进程来说，它能够使得这些读进程阻塞，同时不会阻塞写进程。也就是说，这些写进程以后肯定是会比这些读进程先访问内存的。** 回想一下公平竞争策略，这些读进程和写进程是有均等的机会访问内存的。这就是写者优先想要达到的目的。那么这个信号量，我们在这里就叫它w_sem。

然后，这个信号量什么时候会用到呢？回想一下读者优先策略，为了保证等待访问内存的读进程和写进程中，读进程的优先级更高，我们在还有读进程访问内存的时候拿走db_sem这个信号量，直到没有读进程访问的时候才会放出db_sem这个信号量，给某个写进程拿到。在写者优先策略中，我们是不是可以借鉴这个思路，在还有写者进程等待访问内存的时候（这里和读者优先略有不同，因为读读共享，同一时刻可能有多个读进程在访问内存；而写写互斥，所以某个时刻最多只可能有一个写进程在访问内存，而其余的在等待访问，但原理是一样的），一直霸占着w_sem这个信号量，直到没有写进程等待着访问内存了，才放出w_sem这个信号量呢？如果是的话，那么我们还需要一个变量wc，记录当前正在或等待访问内存的写进程的个数；然后和读者优先类似，需要一个信号量wc_sem，防止多个进程同时修改wc。

这就是结构体SHM里的所有内容。等等，是不是还少了什么？好像在SHM里面还有r_sem这个结构体没提到？没错，这就是我当时的想法，按照上面的分析过程，r_sem这个信号量是不需要的，但是实际上它又是存在的。到底它有什么用呢？我们试着在我们的代码中去掉这个信号量，看看会发生什么。这个信号量只在readerFunc中出现了两次，我们把这两行代码注释掉。

~~~c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		//sem_wait(&shm_p->r_sem);
		sem_wait(&shm_p->w_sem);
		
		sem_wait(&shm_p->rc_sem);    //想改动rc的值，要给rc上锁
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);

		sem_post(&shm_p->w_sem);
		//sem_post(&shm_p->r_sem);
		......
	}
}
~~~

~~~c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->wc_sem);    //想改动wc的值，要给wc上锁
		++shm_p->wc;
		if (shm_p->wc == 1)
		{
			sem_wait(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);
		......
		sem_wait(&shm_p->wc_sem);
		--shm_p->wc;
		if (shm_p->wc == 0)
		{
			sem_post(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);

		//sleep(SLEEP_TIME);
	}
}
~~~

接下来，我们分析一下代码运行的情况。最开始的时候，没有任何进程访问过内存，因此读写进程的竞争会发生在readerFunc和writerFunc这两个函数的sem_wait(&shm_p->w_sem)这条语句上，而在此之前，所有进程相安无事并发运行。

假设现在是个写进程竞争到这个信号量，那么它就拿走w_sem这个信号量，那么所有的读进程就被阻塞在sem_wait(&shm_p->w_sem)这条语句上了，而写进程没有被阻塞在这条语句上，因为一旦写进程访问内存，rc的值就大于0，那么以后再有写进程等待访问内存，rc的值就会大于1，也就不满足if (shm_p->wc == 1)这条语句了，所以就不存在阻塞一说（事实上它们被阻塞在了sem_wait(&shm_p->db_sem) 这里，因为写写互斥）。然后假设经过了一段时间以后，所有的写进程都访问完内存了，也没有写进程等待访问内存了，那么就满足if (shm_p->wc == 0)， 放出信号量w_sem，某个读进程接到这个信号量，开始读内存。这就回到了最开始没有任何进程访问过内存的状态。

到这里，一切都很正常，是我们想要达到的效果。接着往下看会发生什么。假如这个拿到信号量w_sem的读进程在放出这个信号量之前，又有几个写进程等待访问内存了，也就是说被阻塞在sem_wait(&shm_p->w_sem)（其中一个，进入if (shm_p->wc == 1) 的这个）或者sem_wait(&shm_p->db_sem)（其余的）上了，而之前还有几个读进程也被阻塞在这条语句上，那么等那个拿到w_sem这个信号量的读进程把这个信号量放出来的时候，会发生什么？被阻塞在sem_wait(&shm_p->w_sem) 的读进程和写进程有均等的机会拿到这个信号量！而这是公平竞争的效果，显然不是这里想要的（但事实上，因为被阻塞在sem_wait(&shm_p->w_sem) 的写进程只有一个，其余的是被阻塞在em_wait(&shm_p->db_sem) 这里的，所以事实上最可能被落在后面的是这一个写进程而非所有写进程，所以如果是看运行结果的话其实是很难看出什么问题的，看到的可能还是写进程的执行频率远远大于读进程，**而在并发当中过于纠结看到的现象和理论不符，不一定是很理智的选择**）。

也就是说，如果当前访问内存的是个写进程，它的确能保证在等待访存的所有进程中，写进程的优先级比读进程高；但是如果当前访问内存的是个读进程，那么接下来被阻塞在sem_wait(&shm_p->w_sem) 的所有进程具有同样的优先级，这就违背了写者优先策略。

那么，如果加上信号量r_sem呢？代码如下：

~~~c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r_sem);    //加上的代码
		sem_wait(&shm_p->w_sem);
		
		sem_wait(&shm_p->rc_sem);    //想改动rc的值，要给rc上锁
		++shm_p->rc;
		if (shm_p->rc == 1)
		{
			sem_wait(&shm_p->db_sem);
		}
		sem_post(&shm_p->rc_sem);

		sem_post(&shm_p->w_sem);
		sem_post(&shm_p->r_sem);    //加上的代码
		......
	}
}
~~~

~~~c
void writerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->wc_sem);    //想改动wc的值，要给wc上锁
		++shm_p->wc;
		if (shm_p->wc == 1)
		{
			sem_wait(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);
		......
		sem_wait(&shm_p->wc_sem);
		--shm_p->wc;
		if (shm_p->wc == 0)
		{
			sem_post(&shm_p->w_sem);
		}
		sem_post(&shm_p->wc_sem);

		//sleep(SLEEP_TIME);
	}
}
~~~

和之前的假设一样，假设最开始的时候，某个写进程拿到w_sem这个信号量，这个时候其它写进程的情况和之前一样没被阻塞，而读进程的情况不太一样，有一个读进程拿到了r_sem这个信号量而没拿到w_sem这个信号量，因此被阻塞在了sem_wait(&shm_p->w_sem)这里，而另外的读进程更慢一步，没拿到r_sem这个信号量，被阻塞在了sem_wait(&shm_p->r_sem)这里。不过无论怎样，它们都被阻塞在了sem_wait(&shm_p->w_sem)之前，这和之前的情况是一样的。等到没有任何写进程等待或者正在访存后，最后一个写进程会放出w_sem这个信号量，那个比其它读进程快一步的读进程就拿到w_sem这个信号量，开始访存。

如果在这个读进程放出w_sem之前又有几个写进程等待访存了，也就是说等待访存的既有读进程也有写进程，那么在这个读进程放出w_sem以后，谁会拿到w_sem呢？我们看一下readerFunc放出w_sem的那几行代码：

~~~c
void readerFunc(struct SHM* shm_p)
{
	while(1)
	{
		sem_wait(&shm_p->r_sem);    //加上的代码
		sem_wait(&shm_p->w_sem);
		......
		sem_post(&shm_p->w_sem);    //放出w_sem
		sem_post(&shm_p->r_sem);    //加上的代码
		......
	}
}
~~~

readerFunc放出w_sem后，其余的读进程没有任何动静，因为它们连r_sem这个信号量都没拿到，也就是说还被阻塞在sem_wait(&shm_p->r_sem)这里；然后readerFunc再放出r_sem，这个时候剩余的读进程会有一个拿到这个信号量，而那个满足if (shm_p->wc == 1)的写进程呢？它被阻塞在sem_wait(&shm_p->w_sem)，在readerFunc放出w_sem后就可以去抢w_sem了，而读进程还要等r_sem，之后才能去抢w_sem，这就慢了一拍，因此**写进程获得w_sem的概率大大增加（当然也不是绝对的，不要试图猜测并发式的运行顺序）**，优先级也就提高了，这个写进程得以赶上其它写进程，一起等db_sem。然后，等这个读进程访问完内存，放出db_sem后，就轮到这些写进程逐个拿到db_sem了，而读进程还被阻塞在sem_wait(&shm_p->r_sem)。这就实现了等待的读写进程中写进程的优先度更高。

这就是SHM中所有的内容和它们的用途。说实话，我其实并不清楚这种写法到底会不会有死锁或者饥饿的现象出现**。因为并发的结果是不可复现的，可能你自己测试一万次都不会有什么问题，拿去给上司看，运行一次就出了问题，这些都是有可能的。我也不打算去证明这些，我也证明不了，我甚至不能保证我以上的分析过程没有漏洞。**而且我也不是要找到一种解法能够使得读进程和写进程能够和谐共处，都能用到资源。事实上，这个写者优先策略，读进程饥饿的现象很严重。

但是，这些都不重要了。写这么多，只是想重复一下整个思路，下次遇到类似的问题不至于束手无策。**虽然现在开设的很多课程都不尽如人意，但是这不意味着这些东西就不重要，也不是我们不去好好学这些的借口。既然从上课获得的东西那么少，我们就更加要利用作业，实验这些机会，认真对待这些任务，学到东西，而不是敷衍了事。**

这个系列到这里就结束了，不管是这篇还是之前的还是以后的文章，如果发现了什么没写对的地方，或者有什么想法的，都可以给我评论留言，或者联系我。讨论多了，事情也就更明白了。

2019.10.20
