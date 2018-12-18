
//Hubert Borkowski 283668

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <sys/shm.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>

//compile with flags -pthread -ltr

#define SIZE BUFFER_SIZE + 1 //buffer[SIZE - 1].value := current number of elements in buffer

typedef struct node
{
  bool readByA;//this node was previously read by consumerA
  bool readByB;//this node was previously read by consumerB
  bool readByC;//this node was previously read by consumerC
  char lastDelete;//consumer who most recently deleted a node from buffer,
                  //X := not read (only used in buffer[SIZE])
  int value;
}node;

sem_t* SEM_A;//blocks consumerA
sem_t* SEM_B;//blocks consumerB
sem_t* SEM_C;//blocks consumerC
sem_t* MUTEX;//when up buffer can be accessed
sem_t* SEM_PROD;//blocks producer
node* buffer;
unsigned int BUFFER_SIZE;
const char* shmName = "buffer";
int productionLimit;

void move();
void enqueue(int);
int dequeue();
void init();

void launchProcesses();
void consumerA();
void consumerB();
void consumerC();
void producer();

//------------------------------------------------------------------------------

void move()
{
  for(int i = 0; i < (buffer[SIZE - 1].value - 1); i++)
  {
    buffer[i].value = buffer[i + 1].value;
    buffer[i].readByA = buffer[i + 1].readByA;
    buffer[i].readByB = buffer[i + 1].readByB;
    buffer[i].readByC = buffer[i + 1].readByC;
  }
}

void enqueue(int value)//adds value at the end of buffer
{
  int index = buffer[SIZE - 1].value;
  buffer[index].value = value;
  buffer[index].readByA = false;
  buffer[index].readByB = false;
  buffer[index].readByC = false;
  buffer[index].lastDelete = 'X';
  buffer[SIZE - 1].value++;

}

int dequeue()//returns first element from buffer and removes it
{
  int result = buffer[0].value;
  move();
  buffer[SIZE - 1].value--;

  return result;
}

void init()
{
  int shm_fd;

  SEM_A = sem_open("semA", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0);
  SEM_B = sem_open("semB", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0);
  SEM_C = sem_open("semC", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0);
  MUTEX = sem_open("mutex", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 1);
  SEM_PROD = sem_open("semProd", O_CREAT, S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP, 0);

  shm_fd = shm_open(shmName, O_CREAT | O_RDWR, 0666);
  if(shm_fd == -1)
  {
    printf("shm_open() failed\n");
    exit(EXIT_FAILURE);
  }

  ftruncate(shm_fd, SIZE * sizeof(node));

  buffer = mmap(NULL, SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd,0);
  if(buffer == MAP_FAILED)
  {
    printf("mmap failed\n");
    exit(EXIT_FAILURE);
  }

  buffer[SIZE - 1].value = 0;
}

void producer()
{
  sleep(2);

  int i = 0;
  int semValue;

  while(i < productionLimit)
  {
    sem_wait(MUTEX);
    if(buffer[SIZE - 1].value < BUFFER_SIZE)
    {
        enqueue(i);
        i++;

        sem_post(MUTEX);

        sem_getvalue(SEM_A, &semValue);
        if(semValue == 0) sem_post(SEM_A);

        sem_getvalue(SEM_B, &semValue);
        if(semValue == 0) sem_post(SEM_B);

        sem_getvalue(SEM_C, &semValue);
        if(semValue == 0) sem_post(SEM_C);
    }
    else
    {
        sem_post(MUTEX);
        sem_wait(SEM_PROD);
    }
  }

    sleep(SIZE);
}

void consumerA()
{
  const char id = 'A';
  int item;
  int semValue;

  while(1)
  {
    sem_wait(MUTEX);

    if(buffer[SIZE - 1].value == 0 || buffer[0].readByA == true || buffer[0].readByC == true)
    {
        sem_post(MUTEX);
        sem_wait(SEM_A);
    }
    else if(buffer[0].readByB == true)
    {
        if(buffer[SIZE - 1].lastDelete == id)
        {
          sem_post(MUTEX);
          sem_wait(SEM_A);
        }
        else
        {
            item = dequeue();
            buffer[SIZE - 1].lastDelete = id;
            printf("Konsument %c: usuwam %d\n",id,item);

            sem_post(MUTEX);

            sem_getvalue(SEM_B, &semValue);
            if(semValue == 0) sem_post(SEM_B);

            sem_getvalue(SEM_C, &semValue);
            if(semValue == 0) sem_post(SEM_C);

            sem_getvalue(SEM_PROD, &semValue);
            if(semValue == 0) sem_post(SEM_PROD);
        }
    }
    else if(buffer[SIZE - 1].lastDelete == 'B')
    {
      sem_post(MUTEX);
      sem_wait(SEM_A);
    }
    else
    {
        item = buffer[0].value;
        buffer[0].readByA = true;

        printf("Konsument %c: czytam %d\n",id,item);

        sem_post(MUTEX);
    }
  }
}

void consumerB()
{
  int item;
  const char id = 'B';
  int semValue;

  while(1)
  {
    sem_wait(MUTEX);

    if(buffer[SIZE - 1].value == 0 || buffer[0].readByB == true)
    {
      sem_post(MUTEX);
      sem_wait(SEM_B);
    }
    else if(buffer[0].readByA == true || buffer[0].readByC == true)
    {
      item = dequeue();
      buffer[SIZE - 1].lastDelete = id;
      printf("Konsument %c: usuwam %d\n",id,item);

      sem_post(MUTEX);

      sem_getvalue(SEM_PROD, &semValue);
      if(semValue == 0) sem_post(SEM_PROD);
    }
    else
    {
      if(buffer[SIZE - 1].lastDelete == id)
      {
        sem_getvalue(SEM_A, &semValue);
        if(semValue == 0) sem_post(SEM_A);

        sem_getvalue(SEM_C, &semValue);
        if(semValue == 0) sem_post(SEM_C);
      }
      item = buffer[0].value;
      buffer[0].readByB = true;

      printf("Konsument %c: czytam %d\n",id,item);

      sem_post(MUTEX);
    }
  }
}

void consumerC()
{
  int item;
  const char id = 'C';
  int semValue;

  while(1)
  {
    sem_wait(MUTEX);

    if(buffer[SIZE - 1].value == 0 || buffer[0].readByA == true || buffer[0].readByC == true)
    {
      sem_post(MUTEX);
      sem_wait(SEM_C);
    }
    else if(buffer[0].readByB == true)
    {
      if(buffer[SIZE - 1].lastDelete == id)
      {
        sem_post(MUTEX);
        sem_wait(SEM_C);
      }
      else
      {
        item = dequeue();
        buffer[SIZE - 1].lastDelete = id;
        printf("Konsument %c: usuwam %d\n",id,item);

        sem_post(MUTEX);

        sem_getvalue(SEM_A, &semValue);
        if(semValue == 0) sem_post(SEM_A);

        sem_getvalue(SEM_B, &semValue);
        if(semValue == 0) sem_post(SEM_B);

        sem_getvalue(SEM_PROD, &semValue);
        if(semValue == 0) sem_post(SEM_PROD);
      }
    }
    else if(buffer[SIZE - 1].lastDelete == 'B')
    {
      sem_post(MUTEX);
      sem_wait(SEM_C);
    }
    else
    {
        item = buffer[0].value;
        buffer[0].readByC = true;

        printf("Konsument %c: czytam %d\n",id,item);

        sem_post(MUTEX);
    }
  }
}

void launchProcesses()
{
  pid_t pid,
        pidA,
        pidB,
        pidC;

  pid = fork();
  pidA = pid;
  if(pid < 0)
  {
    printf("Fork failed\n");
    exit(EXIT_FAILURE);
  }
  if(pid == 0)
  {
    consumerA();

  }

  pid = fork();
  pidB = pid;
  if(pid < 0)
  {
    printf("Fork failed\n");
    exit(EXIT_FAILURE);
  }
  if(pid == 0)
  {
    consumerB();
  }

  pid = fork();
  pidC = pid;
  if(pid < 0)
  {
    printf("Fork failed\n");
    exit(EXIT_FAILURE);
  }
  if(pid == 0)
  {
    consumerC();
  }

  producer();

  kill(pidA,SIGKILL);
  kill(pidB,SIGKILL);
  kill(pidC,SIGKILL);

  munmap(buffer, SIZE * sizeof(node));

  sem_unlink("semA");
  sem_unlink("semB");
  sem_unlink("semC");
  sem_unlink("mutex");
  sem_unlink("semProd");
}

//------------------------------------------------------------------------------

int main(int argc, char* argv[])
{
  if(argc < 2)
  {
    puts("Insufficient data. Closing..\n");
    return(1);
  }
  BUFFER_SIZE = atoi(argv[1]);
  productionLimit = atoi(argv[2]);

  init();
  launchProcesses();

  return 0;
}
