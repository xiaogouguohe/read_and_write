//readFirst.c

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
