#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "queue.h"

typedef struct test_node {
    int id;
    char name[32];
    TAILQ_ENTRY(test_node) entries;
} test_node_t;

// 定义队列头
TAILQ_HEAD(test_queue, test_node);
typedef struct test_queue test_queue_t;

// 打印队列内容的函数
void print_queue(test_queue_t *head, const char *description) {
    test_node_t *np;
    printf("%s: [", description);
    int first = 1;
    TAILQ_FOREACH(np, head, entries) {
        if (!first) printf(", ");
        printf("(%d:%s)", np->id, np->name);
        first = 0;
    }
    printf("]\n");
}

// 释放队列中所有节点
void clear_queue(test_queue_t *head) {
    test_node_t *np;
    while (!TAILQ_EMPTY(head)) {
        np = TAILQ_FIRST(head);
        TAILQ_REMOVE(head, np, entries);
        free(np);
    }
}

int test_tailq_insert_head() {
    test_queue_t head;
    test_node_t *node1, *node2;
    
    printf("\n=== Testing TAILQ_INSERT_HEAD ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    // 插入第一个节点
    TAILQ_INSERT_HEAD(&head, node1, entries);
    print_queue(&head, "After inserting first node");
    
    // 插入第二个节点到头部
    TAILQ_INSERT_HEAD(&head, node2, entries);
    print_queue(&head, "After inserting second node at head");
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_insert_tail() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3;
    
    printf("\n=== Testing TAILQ_INSERT_TAIL ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    // 插入节点到尾部
    TAILQ_INSERT_TAIL(&head, node1, entries);
    print_queue(&head, "After inserting first node at tail");
    
    TAILQ_INSERT_TAIL(&head, node2, entries);
    print_queue(&head, "After inserting second node at tail");
    
    TAILQ_INSERT_TAIL(&head, node3, entries);
    print_queue(&head, "After inserting third node at tail");
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_insert_after() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3;
    
    printf("\n=== Testing TAILQ_INSERT_AFTER ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    // 先插入前两个节点
    TAILQ_INSERT_TAIL(&head, node1, entries);
    TAILQ_INSERT_TAIL(&head, node2, entries);
    print_queue(&head, "After inserting first two nodes");
    
    // 在第一个节点后插入第三个节点
    TAILQ_INSERT_AFTER(&head, node1, node3, entries);
    print_queue(&head, "After inserting third node after first");
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_remove() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3, *node4;
    
    printf("\n=== Testing TAILQ_REMOVE ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    node4 = malloc(sizeof(test_node_t));
    node4->id = 4;
    strcpy(node4->name, "Fourth");
    
    // 插入所有节点
    TAILQ_INSERT_TAIL(&head, node1, entries);
    TAILQ_INSERT_TAIL(&head, node2, entries);
    TAILQ_INSERT_TAIL(&head, node3, entries);
    TAILQ_INSERT_TAIL(&head, node4, entries);
    print_queue(&head, "After inserting all nodes");
    
    // 删除中间节点
    TAILQ_REMOVE(&head, node2, entries);
    print_queue(&head, "After removing second node");
    free(node2);
    
    // 删除头部节点
    TAILQ_REMOVE(&head, node1, entries);
    print_queue(&head, "After removing first node");
    free(node1);
    
    // 删除尾部节点
    TAILQ_REMOVE(&head, node4, entries);
    print_queue(&head, "After removing last node");
    free(node4);
    
    // 清理剩余节点
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_FOREACH() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3, *np;
    int count;
    
    printf("\n=== Testing TAILQ_FOREACH ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    // 插入节点
    TAILQ_INSERT_TAIL(&head, node1, entries);
    TAILQ_INSERT_TAIL(&head, node2, entries);
    TAILQ_INSERT_TAIL(&head, node3, entries);
    print_queue(&head, "After inserting nodes");
    
    // 遍历并计数
    count = 0;
    TAILQ_FOREACH(np, &head, entries) {
        count++;
    }
    printf("Counted %d nodes using TAILQ_FOREACH\n", count);
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_FOREACH_REVERSE() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3, *np;
    int count;
    
    printf("\n=== Testing TAILQ_FOREACH_REVERSE ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    // 插入节点
    TAILQ_INSERT_TAIL(&head, node1, entries);
    TAILQ_INSERT_TAIL(&head, node2, entries);
    TAILQ_INSERT_TAIL(&head, node3, entries);
    print_queue(&head, "After inserting nodes");
    
    // 反向遍历并打印
    printf("Reverse traversal: [");
    count = 0;
    TAILQ_FOREACH_REVERSE(np, &head, test_queue, entries) {
        if (count > 0) printf(", ");
        printf("(%d:%s)", np->id, np->name);
        count++;
    }
    printf("]\n");
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_empty_and_first() {
    test_queue_t head;
    test_node_t *node, *first;
    
    printf("\n=== Testing TAILQ_EMPTY and TAILQ_FIRST ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 检查空队列
    if (TAILQ_EMPTY(&head)) {
        printf("Queue is empty as expected\n");
    }
    
    first = TAILQ_FIRST(&head);
    if (first == NULL) {
        printf("TAILQ_FIRST returned NULL for empty queue as expected\n");
    }
    
    // 添加一个节点
    node = malloc(sizeof(test_node_t));
    node->id = 1;
    strcpy(node->name, "Only");
    TAILQ_INSERT_TAIL(&head, node, entries);
    print_queue(&head, "After inserting one node");
    
    // 检查非空队列
    if (!TAILQ_EMPTY(&head)) {
        printf("Queue is not empty as expected\n");
    }
    
    first = TAILQ_FIRST(&head);
    if (first != NULL) {
        printf("TAILQ_FIRST returned node (%d:%s)\n", first->id, first->name);
    }
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

int test_tailq_next() {
    test_queue_t head;
    test_node_t *node1, *node2, *node3, *np;
    
    printf("\n=== Testing TAILQ_NEXT ===\n");
    TAILQ_INIT(&head);
    
    print_queue(&head, "Initial queue");
    
    // 创建测试节点
    node1 = malloc(sizeof(test_node_t));
    node1->id = 1;
    strcpy(node1->name, "First");
    
    node2 = malloc(sizeof(test_node_t));
    node2->id = 2;
    strcpy(node2->name, "Second");
    
    node3 = malloc(sizeof(test_node_t));
    node3->id = 3;
    strcpy(node3->name, "Third");
    
    // 插入节点
    TAILQ_INSERT_TAIL(&head, node1, entries);
    TAILQ_INSERT_TAIL(&head, node2, entries);
    TAILQ_INSERT_TAIL(&head, node3, entries);
    print_queue(&head, "After inserting nodes");
    
    // 使用TAILQ_NEXT遍历
    printf("Traversal using TAILQ_NEXT: [");
    int count = 0;
    for (np = TAILQ_FIRST(&head); np != NULL; np = TAILQ_NEXT(np, entries)) {
        if (count > 0) printf(", ");
        printf("(%d:%s)", np->id, np->name);
        count++;
    }
    printf("]\n");
    
    // 清理
    clear_queue(&head);
    print_queue(&head, "After clearing");
    
    return 0;
}

void tailq_test(){
    printf("Testing TAILQ operations\n");
    
    test_tailq_insert_head();
    test_tailq_insert_tail();
    test_tailq_insert_after();
    test_tailq_remove();
    test_tailq_FOREACH();
    test_tailq_FOREACH_REVERSE();
    test_tailq_empty_and_first();
    test_tailq_next();
    
    printf("\nAll TAILQ tests completed!\n");
}
    