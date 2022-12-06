#pragma once
#include <common/list.h>
// pid manager's node
typedef struct s_pid_node {
    int pid;
    ListNode node;
} PIDNode;

// pid manager
typedef struct s_pid_manager {
    int max; // max_pid
    PIDNode freep; // free list of pid
} PIDManager;