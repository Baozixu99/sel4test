#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "uthash.h"

// 定义测试结构体
typedef struct test_item {
    int id;
    char name[32];
    UT_hash_handle hh;
} test_item_t;

typedef struct string_item {
    char key[32];
    int value;
    UT_hash_handle hh;
} string_item_t;

typedef struct ptr_item {
    void *ptr_key;
    int data;
    UT_hash_handle hh;
} ptr_item_t;

// 打印哈希表内容的函数
void print_hash_int(test_item_t *head, const char *description) {
    test_item_t *current;
    printf("%s: [", description);
    int first = 1;
    for (current = head; current != NULL; current = current->hh.next) {
        if (!first) printf(", ");
        printf("(%d:%s)", current->id, current->name);
        first = 0;
    }
    printf("]\n");
}

void print_hash_str(string_item_t *head, const char *description) {
    string_item_t *current;
    printf("%s: [", description);
    int first = 1;
    for (current = head; current != NULL; current = current->hh.next) {
        if (!first) printf(", ");
        printf("(%s:%d)", current->key, current->value);
        first = 0;
    }
    printf("]\n");
}

void print_hash_ptr(ptr_item_t *head, const char *description) {
    ptr_item_t *current;
    printf("%s: [", description);
    int first = 1;
    for (current = head; current != NULL; current = current->hh.next) {
        if (!first) printf(", ");
        printf("(%p:%d)", current->ptr_key, current->data);
        first = 0;
    }
    printf("]\n");
}

// 释放哈希表中所有节点
void clear_hash_int(test_item_t **head) {
    test_item_t *current, *tmp;
    HASH_ITER(hh, *head, current, tmp) {
        HASH_DEL(*head, current);
        free(current);
    }
}

void clear_hash_str(string_item_t **head) {
    string_item_t *current, *tmp;
    HASH_ITER(hh, *head, current, tmp) {
        HASH_DEL(*head, current);
        free(current);
    }
}

void clear_hash_ptr(ptr_item_t **head) {
    ptr_item_t *current, *tmp;
    HASH_ITER(hh, *head, current, tmp) {
        HASH_DEL(*head, current);
        free(current);
    }
}

int test_hash_add_find_int() {
    test_item_t *users = NULL;
    test_item_t *item1, *item2, *item3, *found;
    
    printf("\n=== Testing HASH_ADD_INT and HASH_FIND_INT ===\n");
    print_hash_int(users, "Initial hash");
    
    // 创建测试节点
    item1 = malloc(sizeof(test_item_t));
    item1->id = 1;
    strcpy(item1->name, "Alice");
    
    item2 = malloc(sizeof(test_item_t));
    item2->id = 2;
    strcpy(item2->name, "Bob");
    
    item3 = malloc(sizeof(test_item_t));
    item3->id = 3;
    strcpy(item3->name, "Charlie");
    
    // 添加节点
    HASH_ADD_INT(users, id, item1);
    print_hash_int(users, "After adding first item");
    
    HASH_ADD_INT(users, id, item2);
    print_hash_int(users, "After adding second item");
    
    HASH_ADD_INT(users, id, item3);
    print_hash_int(users, "After adding third item");
    
    // 查找节点
    int search_id = 2;
    HASH_FIND_INT(users, &search_id, found);
    if (found) {
        printf("Found item with id %d: %s\n", found->id, found->name);
    }
    
    // 查找不存在的节点
    search_id = 5;
    HASH_FIND_INT(users, &search_id, found);
    if (!found) {
        printf("Item with id %d not found as expected\n", search_id);
    }
    
    // 清理
    clear_hash_int(&users);
    print_hash_int(users, "After clearing");
    
    return 0;
}

int test_hash_add_find_str() {
    string_item_t *items = NULL;
    string_item_t *item1, *item2, *item3, *found;
    
    printf("\n=== Testing HASH_ADD_STR and HASH_FIND_STR ===\n");
    print_hash_str(items, "Initial hash");
    
    // 创建测试节点
    item1 = malloc(sizeof(string_item_t));
    strcpy(item1->key, "apple");
    item1->value = 10;
    
    item2 = malloc(sizeof(string_item_t));
    strcpy(item2->key, "banana");
    item2->value = 20;
    
    item3 = malloc(sizeof(string_item_t));
    strcpy(item3->key, "cherry");
    item3->value = 30;
    
    // 添加节点
    HASH_ADD_STR(items, key, item1);
    print_hash_str(items, "After adding first item");
    
    HASH_ADD_STR(items, key, item2);
    print_hash_str(items, "After adding second item");
    
    HASH_ADD_STR(items, key, item3);
    print_hash_str(items, "After adding third item");
    
    // 查找节点
    HASH_FIND_STR(items, "banana", found);
    if (found) {
        printf("Found item with key %s: %d\n", found->key, found->value);
    }
    
    // 查找不存在的节点
    HASH_FIND_STR(items, "orange", found);
    if (!found) {
        printf("Item with key 'orange' not found as expected\n");
    }
    
    // 清理
    clear_hash_str(&items);
    print_hash_str(items, "After clearing");
    
    return 0;
}

int test_hash_add_find_ptr() {
    ptr_item_t *items = NULL;
    ptr_item_t *item1, *item2, *item3, *found;
    int dummy1, dummy2, dummy3;
    void *search_ptr;
    
    printf("\n=== Testing HASH_ADD_PTR and HASH_FIND_PTR ===\n");
    print_hash_ptr(items, "Initial hash");
    
    // 创建测试节点
    item1 = malloc(sizeof(ptr_item_t));
    item1->ptr_key = &dummy1;
    item1->data = 100;
    
    item2 = malloc(sizeof(ptr_item_t));
    item2->ptr_key = &dummy2;
    item2->data = 200;
    
    item3 = malloc(sizeof(ptr_item_t));
    item3->ptr_key = &dummy3;
    item3->data = 300;
    
    // 添加节点
    HASH_ADD_PTR(items, ptr_key, item1);
    print_hash_ptr(items, "After adding first item");
    
    HASH_ADD_PTR(items, ptr_key, item2);
    print_hash_ptr(items, "After adding second item");
    
    HASH_ADD_PTR(items, ptr_key, item3);
    print_hash_ptr(items, "After adding third item");
    
    // 查找节点
    search_ptr = &dummy2;
    HASH_FIND_PTR(items, &search_ptr, found);
    if (found) {
        printf("Found item with ptr_key %p: %d\n", found->ptr_key, found->data);
    }
    
    // 查找不存在的节点
    search_ptr = NULL;
    HASH_FIND_PTR(items, &search_ptr, found);
    if (!found) {
        printf("Item with ptr_key NULL not found as expected\n");
    }
    
    // 清理
    clear_hash_ptr(&items);
    print_hash_ptr(items, "After clearing");
    
    return 0;
}

int test_hash_delete() {
    test_item_t *users = NULL;
    test_item_t *item1, *item2, *item3, *found;
    int search_id;
    
    printf("\n=== Testing HASH_DEL ===\n");
    print_hash_int(users, "Initial hash");
    
    // 创建测试节点
    item1 = malloc(sizeof(test_item_t));
    item1->id = 1;
    strcpy(item1->name, "Alice");
    
    item2 = malloc(sizeof(test_item_t));
    item2->id = 2;
    strcpy(item2->name, "Bob");
    
    item3 = malloc(sizeof(test_item_t));
    item3->id = 3;
    strcpy(item3->name, "Charlie");
    
    // 添加所有节点
    HASH_ADD_INT(users, id, item1);
    HASH_ADD_INT(users, id, item2);
    HASH_ADD_INT(users, id, item3);
    print_hash_int(users, "After adding all items");
    
    // 删除中间节点
    search_id = 2;
    HASH_FIND_INT(users, &search_id, found);
    if (found) {
        HASH_DEL(users, found);
        free(found);
        printf("Deleted item with id %d\n", search_id);
    }
    print_hash_int(users, "After deleting middle item");
    
    // 删除第一个节点
    search_id = 1;
    HASH_FIND_INT(users, &search_id, found);
    if (found) {
        HASH_DEL(users, found);
        free(found);
        printf("Deleted item with id %d\n", search_id);
    }
    print_hash_int(users, "After deleting first item");
    
    // 清理剩余节点
    clear_hash_int(&users);
    print_hash_int(users, "After clearing");
    
    return 0;
}

int test_hash_count() {
    test_item_t *users = NULL;
    test_item_t *item1, *item2, *item3;
    int count;
    
    printf("\n=== Testing HASH_COUNT ===\n");
    print_hash_int(users, "Initial hash");
    
    count = HASH_COUNT(users);
    printf("Initial count: %d\n", count);
    
    // 创建测试节点
    item1 = malloc(sizeof(test_item_t));
    item1->id = 1;
    strcpy(item1->name, "Alice");
    
    item2 = malloc(sizeof(test_item_t));
    item2->id = 2;
    strcpy(item2->name, "Bob");
    
    item3 = malloc(sizeof(test_item_t));
    item3->id = 3;
    strcpy(item3->name, "Charlie");
    
    // 添加节点并检查计数
    HASH_ADD_INT(users, id, item1);
    count = HASH_COUNT(users);
    printf("Count after adding first item: %d\n", count);
    print_hash_int(users, "Hash after adding first item");
    
    HASH_ADD_INT(users, id, item2);
    count = HASH_COUNT(users);
    printf("Count after adding second item: %d\n", count);
    print_hash_int(users, "Hash after adding second item");
    
    HASH_ADD_INT(users, id, item3);
    count = HASH_COUNT(users);
    printf("Count after adding third item: %d\n", count);
    print_hash_int(users, "Hash after adding third item");
    
    // 删除节点并检查计数
    HASH_DEL(users, item2);
    free(item2);
    count = HASH_COUNT(users);
    printf("Count after deleting one item: %d\n", count);
    print_hash_int(users, "Hash after deleting one item");
    
    // 清理
    clear_hash_int(&users);
    count = HASH_COUNT(users);
    printf("Count after clearing: %d\n", count);
    print_hash_int(users, "After clearing");
    
    return 0;
}

int test_hash_iter() {
    test_item_t *users = NULL, *current, *tmp;
    test_item_t *item1, *item2, *item3;
    int count;
    
    printf("\n=== Testing HASH_ITER ===\n");
    print_hash_int(users, "Initial hash");
    
    // 创建测试节点
    item1 = malloc(sizeof(test_item_t));
    item1->id = 1;
    strcpy(item1->name, "Alice");
    
    item2 = malloc(sizeof(test_item_t));
    item2->id = 2;
    strcpy(item2->name, "Bob");
    
    item3 = malloc(sizeof(test_item_t));
    item3->id = 3;
    strcpy(item3->name, "Charlie");
    
    // 添加节点
    HASH_ADD_INT(users, id, item1);
    HASH_ADD_INT(users, id, item2);
    HASH_ADD_INT(users, id, item3);
    print_hash_int(users, "After adding items");
    
    // 使用HASH_ITER遍历并计数
    count = 0;
    HASH_ITER(hh, users, current, tmp) {
        count++;
        printf("Visited item: (%d:%s)\n", current->id, current->name);
    }
    printf("Total items visited: %d\n", count);
    
    // 清理
    clear_hash_int(&users);
    print_hash_int(users, "After clearing");
    
    return 0;
}


int test_hash_replace() {
    test_item_t *users = NULL;
    test_item_t *item1, *item2, *replaced;
    int search_id;
    
    printf("\n=== Testing HASH_REPLACE ===\n");
    print_hash_int(users, "Initial hash");
    
    // 创建初始节点
    item1 = malloc(sizeof(test_item_t));
    item1->id = 1;
    strcpy(item1->name, "Alice");
    
    // 添加初始节点
    HASH_ADD_INT(users, id, item1);
    print_hash_int(users, "After adding initial item");
    
    // 创建替换节点
    item2 = malloc(sizeof(test_item_t));
    item2->id = 1;  // 相同的ID
    strcpy(item2->name, "Alicia");  // 不同的名字
    
    // 替换节点
    HASH_REPLACE_INT(users, id, item2, replaced);
    if (replaced) {
        printf("Replaced item: (%d:%s)\n", replaced->id, replaced->name);
        free(replaced);
    }
    print_hash_int(users, "After replacing item");
    
    // 尝试替换不存在的节点
    test_item_t *item3 = malloc(sizeof(test_item_t));
    item3->id = 2;
    strcpy(item3->name, "Bob");
    
    HASH_REPLACE_INT(users, id, item3, replaced);
    if (replaced) {
        printf("Replaced item: (%d:%s)\n", replaced->id, replaced->name);
        free(replaced);
    } else {
        printf("No item replaced, added new item instead\n");
    }
    print_hash_int(users, "After attempting to replace non-existent item");
    
    // 清理
    clear_hash_int(&users);
    print_hash_int(users, "After clearing");
    
    return 0;
}

void uthash_test() {
    printf("Testing UTHASH operations\n");
    
    test_hash_add_find_int();
    test_hash_add_find_str();
    test_hash_add_find_ptr();
    test_hash_delete();
    test_hash_count();
    test_hash_iter();
    test_hash_replace();
    printf("\nAll UTHASH tests completed!\n");
}