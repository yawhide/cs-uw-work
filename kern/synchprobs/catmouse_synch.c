#include <types.h>
#include <lib.h>
#include <synchprobs.h>
#include <synch.h>

/* 
 * This simple default synchronization mechanism allows only creature at a time to
 * eat.   The globalCatMouseSem is used as a a lock.   We use a semaphore
 * rather than a lock so that this code will work even before locks are implemented.
 */

/* 
 * Replace this default synchronization mechanism with your own (better) mechanism
 * needed for your solution.   Your mechanism may use any of the available synchronzation
 * primitives, e.g., semaphores, locks, condition variables.   You are also free to 
 * declare other global variables if your solution requires them.
 */

/*
 * replace this with declarations of any synchronization and other variables you need here
 */
//static struct semaphore *globalCatMouseSem;
volatile int *bowl_arr;
struct lock **bowl_locks;
struct cv **bowl_cvs;
struct lock *arr_lock;
struct cv *arr_cv;
volatile int catcount = 0;
volatile int mousecount = 0;

bool miceStillEating(struct lock *lk);
bool catsStillEating(struct lock *lk);


bool miceStillEating(struct lock *lk){
  lock_acquire(lk);
  return mousecount > 0;
}
bool catsStillEating(struct lock *lk){
  lock_acquire(lk);
  return catcount > 0;
}

/* 
 * The CatMouse simulation will call this function once before any cat or
 * mouse tries to each.
 *
 * You can use it to initialize synchronization and other variables.
 * 
 * parameters: the number of bowls
 */
void
catmouse_sync_init(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_init */

  bowl_arr = kmalloc(bowls*sizeof(int));
  if (bowl_arr == NULL) {
    panic("initialize_bowl_arr: unable to allocate space for %d bowl_arr\n",bowls);
  }
  for(int i=0;i<bowls;i++) {
    bowl_arr[i] = -1;
  }

  bowl_locks = kmalloc(bowls*sizeof(struct lock *));
  if(bowl_locks == NULL){
    panic("initialize_bowl_locks: unable to allocate space for %d bowl_locks\n",bowls);
  }
  for (int i = 0; i < bowls; i++){
    bowl_locks[i] = lock_create("bowl");
  }

  bowl_cvs = kmalloc(bowls*sizeof(struct cv *));
  if(bowl_cvs == NULL){
    panic("initialize_bowl_cvs: unable to allocate space for %d bowl_cvs\n",bowls);
  }
  for (int i = 0; i < bowls; i++){
    bowl_cvs[i] = cv_create("bowl");
  }
  arr_cv = cv_create("cv arr");
  arr_lock = lock_create("arr lock");  

  //(void)bowls;
  // globalCatMouseSem = sem_create("globalCatMouseSem",1);
  // if (globalCatMouseSem == NULL) {
  //   panic("could not create global CatMouse synchronization semaphore");
  // }
  kprintf("Finished the catmouse sync init\n");
  return;
}

/* 
 * The CatMouse simulation will call this function once after all cat
 * and mouse simulations are finished.
 *
 * You can use it to clean up any synchronization and other variables.
 *
 * parameters: the number of bowls
 */
void
catmouse_sync_cleanup(int bowls)
{
  /* replace this default implementation with your own implementation of catmouse_sync_cleanup */
  //(void)bowls;
  //KASSERT(globalCatMouseSem != NULL);
  //sem_destroy(globalCatMouseSem);

  if(bowl_arr != NULL)
    kfree( (void *) bowl_arr);

  for (int i = 0; i < bowls; ++i){
    lock_destroy(bowl_locks[i]);
  }
  for (int i = 0; i < bowls; ++i){
    cv_destroy(bowl_cvs[i]);
  }
  cv_destroy(arr_cv);
  lock_destroy(arr_lock);
  kfree(bowl_locks);
  kfree(bowl_cvs);
  
  //cv_destroy(cat_eating_cv);
}


/*
 * The CatMouse simulation will call this function each time a cat wants
 * to eat, before it eats.
 * This function should cause the calling thread (a cat simulation thread)
 * to block until it is OK for a cat to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the cat is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_before_eating */
  //(void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //P(globalCatMouseSem);
  lock_acquire(bowl_locks[bowl]);
  kprintf("Cat got lock: %d\n", bowl);
  while(miceStillEating(arr_lock)){
    cv_wait(arr_cv, arr_lock);
  }
  catcount++;
  lock_release(arr_lock);
  kprintf("Cat eating bowl: %d\n", bowl);
}

/*
 * The CatMouse simulation will call this function each time a cat finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this cat finished.
 *
 * parameter: the number of the bowl at which the cat is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
cat_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of cat_after_eating */
  //(void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //V(globalCatMouseSem);
  lock_acquire(arr_lock);
  catcount--;
  if(catcount == 0){
    cv_broadcast(arr_cv, arr_lock);
  }
  lock_release(arr_lock);
  lock_release(bowl_locks[bowl]);
  kprintf("Cat finished bowl: %d\n", bowl);
}

/*
 * The CatMouse simulation will call this function each time a mouse wants
 * to eat, before it eats.
 * This function should cause the calling thread (a mouse simulation thread)
 * to block until it is OK for a mouse to eat at the specified bowl.
 *
 * parameter: the number of the bowl at which the mouse is trying to eat
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_before_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_before_eating */
  //(void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //P(globalCatMouseSem);
  lock_acquire(bowl_locks[bowl]);
  kprintf("Mouse got lock: %d\n", bowl);
  while(catsStillEating(arr_lock)){
    cv_wait(arr_cv, arr_lock);
  }
  mousecount++;
  lock_release(arr_lock);
  kprintf("Mouse eating bowl: %d\n", bowl);
}

/*
 * The CatMouse simulation will call this function each time a mouse finishes
 * eating.
 *
 * You can use this function to wake up other creatures that may have been
 * waiting to eat until this mouse finished.
 *
 * parameter: the number of the bowl at which the mouse is finishing eating.
 *             legal bowl numbers are 1..NumBowls
 *
 * return value: none
 */

void
mouse_after_eating(unsigned int bowl) 
{
  /* replace this default implementation with your own implementation of mouse_after_eating */
  //(void)bowl;  /* keep the compiler from complaining about an unused parameter */
  //KASSERT(globalCatMouseSem != NULL);
  //V(globalCatMouseSem);
  lock_acquire(arr_lock);
  mousecount--;
  if(mousecount == 0){
    cv_broadcast(arr_cv, arr_lock);
  }
  lock_release(arr_lock);
  lock_release(bowl_locks[bowl]);
  kprintf("Mouse finish bowl: %d\n", bowl);
}
