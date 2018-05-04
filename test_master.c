/**
 *  This program runs child test programs concurrently.
 */

#include "types.h"
#include "stat.h"
#include "user.h"

// Number of child programs
#define CNT_CHILD           10

// Name of child test program that tests Stride scheduler
#define NAME_CHILD_STRIDE   "test_stride"
// Name of child test program that tests MLFQ scheduler
#define NAME_CHILD_MLFQ     "test_mlfq"

char *child_argv[CNT_CHILD][3] = {
  {NAME_CHILD_STRIDE, "1", "stride 10%"},
  {NAME_CHILD_STRIDE, "2", "stride 15%"},
  {NAME_CHILD_STRIDE, "3", "stride 20%"},
  {NAME_CHILD_STRIDE, "4", "stride 10%"},
  {NAME_CHILD_STRIDE, "5", "stride 10%"},
  {NAME_CHILD_STRIDE, "6", "stride 10%"},
  {NAME_CHILD_STRIDE, "7", "stride 10%"},
  {NAME_CHILD_STRIDE, "8", "stride 5% 3"},
  {NAME_CHILD_STRIDE, "9", "stride 5% 4"},
  {NAME_CHILD_STRIDE, "10", "stride 5% 3"},
//  {NAME_CHILD_MLFQ, "1", "MLFQ 1 level"},
};

int
main(int argc, char *argv[])
{
  int pid;
  int i;

  for (i = 0; i < CNT_CHILD; i++) {
    pid = fork();
    if (pid > 0) {
      // parent
      continue;
    } else if (pid == 0) {
      // child
      exec(child_argv[i][0], child_argv[i]);
      printf(1, "exec failed!!\n");
      exit();
    } else {
      printf(1, "fork failed!!\n");
      exit();
    }
  }
  
  for (i = 0; i < CNT_CHILD; i++) {
    wait();
  }

  exit();
}
