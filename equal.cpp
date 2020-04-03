//equal.cpp

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

