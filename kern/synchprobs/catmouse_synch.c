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
struct lock **bowl_locks;
struct lock *arr_lock;
struct cv *arr_cv;
volatile int catcount = 0;
volatile int mousecount = 0;

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

  bowl_locks = kmalloc((bowls+1)*sizeof(struct lock *));
  if(bowl_locks == NULL){
    panic("initialize_bowl_locks: unable to allocate space for %d bowl_locks\n",bowls);
  }
  for (int i = 1; i <= bowls; i++){
    bowl_locks[i-1] = lock_create("bowl");
  }

  arr_cv = cv_create("cv arr");
  arr_lock = lock_create("arr lock");  

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

  for (int i = 0; i < bowls; ++i){
    lock_destroy(bowl_locks[i]);
  }
  cv_destroy(arr_cv);
  lock_destroy(arr_lock);
  kfree(bowl_locks);
  
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

  lock_acquire(bowl_locks[bowl-1]);
  lock_acquire(arr_lock);
  while(mousecount > 0){
    cv_wait(arr_cv, arr_lock);
  }
  catcount++;
  lock_release(arr_lock);
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

  lock_acquire(arr_lock);
  catcount--;
  if(catcount == 0){
    cv_broadcast(arr_cv, arr_lock);
  }
  lock_release(arr_lock);
  lock_release(bowl_locks[bowl-1]);
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

  lock_acquire(bowl_locks[bowl-1]);
  lock_acquire(arr_lock);
  while(catcount > 0){
    cv_wait(arr_cv, arr_lock);
  }
  mousecount++;
  lock_release(arr_lock);
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

  lock_acquire(arr_lock);
  mousecount--;
  if(mousecount == 0){
    cv_broadcast(arr_cv, arr_lock);
  }
  lock_release(arr_lock);
  lock_release(bowl_locks[bowl-1]);
}
