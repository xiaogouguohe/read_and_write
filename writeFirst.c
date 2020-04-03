//writeFirst.c

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

